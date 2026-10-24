#include "update/ReleaseCheck.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSettings>
#include <QUrl>
#include <Windows.h>
#include <winhttp.h>
#include <stdexcept>
#include <string>
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
    static const QRegularExpression pattern("^v?([0-9.]+)$");
    const auto match=pattern.match(tag.toString());require(match.hasMatch(),"Release tag is not vX.Y.Z");
    const auto parsed=version(match.captured(1));require(parsed.has_value(),"Release tag is not vX.Y.Z");
    const QUrl url(page.toString(),QUrl::StrictMode);
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
    call(WinHttpSendRequest(request.value,headers,DWORD(-1L),WINHTTP_NO_REQUEST_DATA,0,0,0),"Release check request");
    call(WinHttpReceiveResponse(request.value,nullptr),"Release check response");
    DWORD status=0,size=sizeof(status);
    call(WinHttpQueryHeaders(request.value,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&size,WINHTTP_NO_HEADER_INDEX),"Release check status");
    if(status!=200)fail("Release check returned HTTP "+QString::number(status));
    QByteArray body;std::vector<char> buffer(16384);
    for(;;){
        check();DWORD read=0;
        call(WinHttpReadData(request.value,buffer.data(),DWORD(buffer.size()),&read),"Release check read");
        if(!read)break;
        require(body.size()+qsizetype(read)<=releaseResponseLimit,"Release response is too large");
        body.append(buffer.data(),qsizetype(read));
    }
    return body;
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