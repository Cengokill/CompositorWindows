#include "update/ReleaseCheck.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSettings>
#include <QUrl>
#include <Windows.h>
#include <winhttp.h>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace compositor::update {
namespace {
constexpr auto automaticKey="updates/automatic";
constexpr auto lastCheckKey="updates/lastCheck";
constexpr auto skippedKey="updates/skippedVersion";
[[noreturn]] void fail(const QString& message){throw std::runtime_error(message.toUtf8().constData());}
void require(bool value,const char* message){if(!value)fail(QString::fromUtf8(message));}
struct Internet {
    HINTERNET value{};
    explicit Internet(HINTERNET handle,const char* operation):value(handle){if(!value)fail(QString::fromUtf8(operation)+" failed (Windows error "+QString::number(GetLastError())+")");}
    ~Internet(){if(value)WinHttpCloseHandle(value);}
    Internet(const Internet&)=delete;Internet& operator=(const Internet&)=delete;
};
void call(BOOL result,const char* operation){if(!result)fail(QString::fromUtf8(operation)+" failed (Windows error "+QString::number(GetLastError())+")");}
// Closing a request handle is the documented way to abort a blocking WinHTTP
// call, so the watchdog closes it as soon as the caller cancels.
class CancelWatchdog {
public:
    CancelWatchdog(Internet& request,const std::function<bool()>& cancelled){
        if(cancelled)thread_=std::thread([this,&request,&cancelled]{
            std::unique_lock lock(mutex_);
            while(!done_){
                if(cancelled()){WinHttpCloseHandle(request.value);request.value=nullptr;return;}
                wake_.wait_for(lock,std::chrono::milliseconds(50));
            }
        });
    }
    ~CancelWatchdog(){
        {std::lock_guard lock(mutex_);done_=true;}
        wake_.notify_all();if(thread_.joinable())thread_.join();
    }
    CancelWatchdog(const CancelWatchdog&)=delete;CancelWatchdog& operator=(const CancelWatchdog&)=delete;
private:
    std::mutex mutex_;std::condition_variable wake_;bool done_{};std::thread thread_;
};
std::optional<Version> version(const QString& text){
    try{return Version::parse(text);}catch(const std::exception&){return std::nullopt;}
}
}
std::optional<ReleaseInfo> parseLatestRelease(const QByteArray& json){
    require(json.size()<=releaseResponseLimit,"Release response is too large");
    QJsonParseError error;const auto document=QJsonDocument::fromJson(json,&error);
    require(error.error==QJsonParseError::NoError&&document.isObject(),"Release response is not a JSON object");
    const auto object=document.object();
    const auto tag=object["tag_name"],draft=object["draft"],prerelease=object["prerelease"],page=object["html_url"];
    require(tag.isString()&&draft.isBool()&&prerelease.isBool()&&page.isString(),"Release response is missing tag_name, draft, prerelease or html_url");
    if(draft.toBool()||prerelease.toBool())return std::nullopt;
    // Release tags may omit the patch number (the first stable release is v1.0): vX.Y reads as X.Y.0.
    static const QRegularExpression pattern("^v?([0-9]+\\.[0-9]+)(\\.[0-9]+)?$");
    const auto match=pattern.match(tag.toString());require(match.hasMatch(),"Release tag is not vX.Y or vX.Y.Z");
    const auto parsed=version(match.captured(1)+(match.hasCaptured(2)?match.captured(2):QStringLiteral(".0")));require(parsed.has_value(),"Release tag is not vX.Y or vX.Y.Z");
    const QUrl url(page.toString(),QUrl::StrictMode);
    // Browsers resolve dot segments (also percent-encoded ones), so they could leave the repository prefix.
    const auto segments=url.path(QUrl::FullyDecoded).split(QLatin1Char('/'));
    require(!segments.contains(QStringLiteral("."))&&!segments.contains(QStringLiteral("..")),"Release page is not a CompositorWindows GitHub release");
    require(url.isValid()&&url.scheme()=="https"&&url.host()=="github.com"&&url.port()==-1&&url.userInfo().isEmpty()&&!url.hasQuery()&&!url.hasFragment()&&url.path().startsWith(QString::fromLatin1(releasePagePrefix)),"Release page is not a CompositorWindows GitHub release");
    return ReleaseInfo{*parsed,tag.toString(),url.toString()};
}
QString releaseCheckUrl(){
#ifndef NDEBUG
    const auto override=qEnvironmentVariable("COMPOSITOR_RELEASE_CHECK_URL");
    if(!override.isEmpty())return override;
#endif
    return QString::fromLatin1(stableReleaseUrl);
}
QByteArray fetchReleaseMetadata(const QString& address,const std::function<bool()>& cancelled){
    const QUrl url(address,QUrl::StrictMode);
    const bool secure=url.scheme()=="https",loopback=url.scheme()=="http"&&url.host()=="127.0.0.1";
    require(url.isValid()&&(secure||loopback)&&url.userInfo().isEmpty()&&!url.hasFragment(),"Release check URL must be HTTPS (or plain loopback HTTP)");
    auto check=[&]{if(cancelled&&cancelled())fail("Release check cancelled");};
    Internet session(WinHttpOpen(L"Compositor-Windows-release-check/1",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0),"WinHTTP open");
    call(WinHttpSetTimeouts(session.value,10000,10000,10000,10000),"WinHTTP timeouts");
    const auto host=url.host().toStdWString();
    const auto port=INTERNET_PORT(url.port(secure?INTERNET_DEFAULT_HTTPS_PORT:INTERNET_DEFAULT_HTTP_PORT));
    Internet connection(WinHttpConnect(session.value,host.c_str(),port,0),"WinHTTP connect");
    const auto path=url.path(QUrl::FullyEncoded).toStdWString()+(url.hasQuery()?L"?"+url.query(QUrl::FullyEncoded).toStdWString():std::wstring{});
    Internet request(WinHttpOpenRequest(connection.value,L"GET",path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,secure?WINHTTP_FLAG_SECURE:0),"WinHTTP request");
    DWORD redirects=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    call(WinHttpSetOption(request.value,WINHTTP_OPTION_REDIRECT_POLICY,&redirects,sizeof(redirects)),"WinHTTP redirect policy");
    const wchar_t headers[]=L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n";
    check();
    // The watchdog may close the request at any time, so the worker uses this copy
    // and never reads the field the watchdog clears.
    const HINTERNET handle=request.value;
    CancelWatchdog watchdog(request,cancelled);
    try{
    call(WinHttpSendRequest(handle,headers,DWORD(-1L),WINHTTP_NO_REQUEST_DATA,0,0,0),"Release check request");
    call(WinHttpReceiveResponse(handle,nullptr),"Release check response");
    DWORD status=0,size=sizeof(status);
    call(WinHttpQueryHeaders(handle,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&size,WINHTTP_NO_HEADER_INDEX),"Release check status");
    if(status!=200)fail("Release check returned HTTP "+QString::number(status));
    QByteArray body;std::vector<char> buffer(16384);
    for(;;){
        check();DWORD read=0;
        call(WinHttpReadData(handle,buffer.data(),DWORD(buffer.size()),&read),"Release check read");
        if(!read)break;
        require(body.size()+qsizetype(read)<=releaseResponseLimit,"Release response is too large");
        body.append(buffer.data(),qsizetype(read));
    }
    return body;
    }catch(const std::exception&){check();throw;} // A failure caused by cancelling reports the cancellation.
}
bool automaticChecksEnabled(QSettings& settings){
    const auto value=settings.value(automaticKey);
    if(value.typeId()==QMetaType::Bool)return value.toBool();
    if(value.typeId()==QMetaType::QString){
        const auto text=value.toString().trimmed().toLower();
        if(text=="false")return false;
    }
    return true;
}
void setAutomaticChecksEnabled(QSettings& settings,bool enabled){settings.setValue(automaticKey,enabled);settings.sync();}
bool releaseCheckDue(QSettings& settings,const QDateTime& now){
    const auto last=QDateTime::fromString(settings.value(lastCheckKey).toString(),Qt::ISODate);
    // A missing, malformed or future timestamp (clock moved back) means "check now".
    if(!last.isValid()||last>now)return true;
    return last.secsTo(now)>=releaseCheckIntervalSeconds;
}
void recordReleaseCheck(QSettings& settings,const QDateTime& now){settings.setValue(lastCheckKey,now.toUTC().toString(Qt::ISODate));settings.sync();}
std::optional<Version> skippedRelease(QSettings& settings){return version(settings.value(skippedKey).toString());}
void skipRelease(QSettings& settings,const Version& skipped){settings.setValue(skippedKey,skipped.text());settings.sync();}
}