#include "CompositorVersion.h"
#include "ui/UpdatePanel.h"
#include "update/ReleaseCheck.h"
#include "update/Updater.h"
#include <QCheckBox>
#include <QTemporaryDir>
#include <QApplication>
#include <QDialog>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QTest>
#include <QDateTime>
#include <atomic>
#include <chrono>
#include <future>
#include <map>
#include <vector>
#include <Windows.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <winsock2.h>
#include <ws2tcpip.h>
using namespace compositor;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Predicate>void waitFor(Predicate predicate,const char* message){QElapsedTimer elapsed;elapsed.start();while(!predicate()){if(elapsed.elapsed()>15000)throw std::runtime_error(message);QTest::qWait(5);}}
QPushButton* button(QDialog* panel,const char* name){auto* result=panel->findChild<QPushButton*>(name);require(result,"missing update control");return result;}
QString state(QDialog* panel){return panel->property("updateState").toString();}
void waitState(QDialog* panel,const QString& wanted){waitFor([&]{return state(panel)==wanted;},qPrintable("update state did not become "+wanted));}
void settled(QDialog* panel){waitFor([&]{auto s=state(panel);return !s.isEmpty()&&s!="checking"&&s!="installing";},"update panel did not settle");}
QString current(const QString& root){return update::Updater(std::filesystem::path(root.toStdWString())).state().current.text();}
QString text(QDialog* panel,const char* name){auto* label=panel->findChild<QLabel*>(name);require(label,"missing update label");return label->text();}
QByteArray release(const char* tag,bool draft=false,bool prerelease=false,const char* page="https://github.com/Cengokill/CompositorWindows/releases/tag/v0.2.0"){
    return QJsonDocument(QJsonObject{{"tag_name",tag},{"draft",draft},{"prerelease",prerelease},{"html_url",page},{"body","notes"}}).toJson();
}
template<class Action>bool throws(Action action){try{action();}catch(const std::exception&){return true;}return false;}
void release_parse(){
    const auto stable=update::parseLatestRelease(release("v0.2.0"));
    require(stable&&stable->version==update::Version{0,2,0}&&stable->pageUrl=="https://github.com/Cengokill/CompositorWindows/releases/tag/v0.2.0","stable vX.Y.Z release accepted");
    require(update::parseLatestRelease(release("1.4.5"))->version==update::Version{1,4,5},"tag without v accepted");
    require(!update::parseLatestRelease(release("v0.3.0",true))&&!update::parseLatestRelease(release("v0.3.0",false,true)),"drafts and prereleases ignored");
    const auto first=update::parseLatestRelease(release("v1.0"));
    require(first&&first->version==update::Version{1,0,0}&&first->tag=="v1.0"&&update::Version{0,1,8}<first->version,"two-part tag v1.0 reads as 1.0.0, newer than 0.1.8");
    require(update::parseLatestRelease(release("2.3"))->version==update::Version{2,3,0},"two-part tag without v accepted");
    for(const char* tag:{"v1","v01.2","v1.02","v1.0-beta","v1.2.","v01.2.3","v1.2.3-beta","latest","v1.2.3.4",""})require(throws([&]{update::parseLatestRelease(release(tag));}),"malformed tag rejected");
    for(const char* page:{"http://github.com/Cengokill/CompositorWindows/releases/tag/v0.2.0","https://github.com/other/CompositorWindows/releases/tag/v0.2.0","https://evil.example/Cengokill/CompositorWindows/releases/x","https://github.com/Cengokill/CompositorWindows/releases/x?download=1","https://user@github.com/Cengokill/CompositorWindows/releases/x","https://github.com/Cengokill/CompositorWindows/releases/../../../other/repo/releases/tag/x","https://github.com/Cengokill/CompositorWindows/releases/%2e%2e/%2E%2E/x","https://github.com/Cengokill/CompositorWindows/releases/./tag/v0.2.0"})
        require(throws([&]{update::parseLatestRelease(release("v0.2.0",false,false,page));}),"foreign or unsafe release page rejected");
    require(throws([]{update::parseLatestRelease("[]");})&&throws([]{update::parseLatestRelease("{");})&&throws([]{update::parseLatestRelease(R"({"tag_name":"v0.2.0","draft":"no","prerelease":false,"html_url":"x"})");}),"non-object, truncated and mistyped JSON rejected");
    QByteArray oversized=release("v0.2.0");oversized.append(QByteArray(update::releaseResponseLimit,' '));
    require(throws([&]{update::parseLatestRelease(oversized);}),"oversized response rejected");
}
void release_settings(){
    QSettings settings;settings.clear();const auto now=QDateTime::currentDateTimeUtc();
    require(update::automaticChecksEnabled(settings),"automatic checks enabled by default");
    settings.setValue("updates/automatic","false");require(!update::automaticChecksEnabled(settings),"stored false honored");
    settings.setValue("updates/automatic","maybe");require(update::automaticChecksEnabled(settings),"invalid preference falls back to default");
    update::setAutomaticChecksEnabled(settings,false);require(!update::automaticChecksEnabled(settings),"preference written");
    require(update::releaseCheckDue(settings,now),"never checked means due");
    update::recordReleaseCheck(settings,now);require(!update::releaseCheckDue(settings,now.addSecs(23*3600)),"checked within a day is not due");
    require(update::releaseCheckDue(settings,now.addSecs(24*3600)),"due after 24 hours");
    require(update::releaseCheckDue(settings,now.addSecs(-3600)),"future timestamp (clock moved back) is due");
    settings.setValue("updates/lastCheck","yesterday");require(update::releaseCheckDue(settings,now),"malformed timestamp is due");
    settings.setValue("updates/skippedVersion","0.2");require(!update::skippedRelease(settings),"malformed skipped version ignored");
    update::skipRelease(settings,{0,2,0});require(update::skippedRelease(settings)==update::Version{0,2,0},"skipped version stored");
}
void release_url(){
    qputenv("COMPOSITOR_RELEASE_CHECK_URL","http://127.0.0.1:1/latest.json");
#ifdef NDEBUG
    require(update::releaseCheckUrl()==QString::fromLatin1(update::stableReleaseUrl),"Release build ignores the URL override");
#else
    require(update::releaseCheckUrl()=="http://127.0.0.1:1/latest.json","Debug build honors the URL override");
#endif
    qunsetenv("COMPOSITOR_RELEASE_CHECK_URL");require(update::releaseCheckUrl()==QString::fromLatin1(update::stableReleaseUrl),"stable GitHub release URL by default");
    for(const char* url:{"http://example.com/latest","file:///C:/latest.json","ftp://127.0.0.1/latest","https://user:pw@example.com/x"})
        require(throws([&]{update::fetchReleaseMetadata(url,{});}),"non-HTTPS, non-loopback or credentialed URL rejected before any request");
    require(throws([]{update::fetchReleaseMetadata(QString::fromLatin1(update::stableReleaseUrl),[]{return true;});}),"cancelled fetch stops before sending");
}
// One-connection loopback HTTP server for the real WinHTTP fetcher. An empty
// reply means "accept and never answer" until the test finishes.
class LoopbackServer {
public:
    explicit LoopbackServer(std::string reply):reply_(std::move(reply)){
        WSADATA data{};require(WSAStartup(MAKEWORD(2,2),&data)==0,"winsock start");
        listener_=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);require(listener_!=INVALID_SOCKET,"loopback socket");
        sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        require(bind(listener_,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0&&listen(listener_,1)==0,"loopback listen");
        int length=sizeof(address);getsockname(listener_,reinterpret_cast<sockaddr*>(&address),&length);port_=ntohs(address.sin_port);
        thread_=std::thread([this]{
            const SOCKET client=accept(listener_,nullptr,nullptr);if(client==INVALID_SOCKET)return;
            std::string request;char buffer[4096];
            while(request.find("\r\n\r\n")==std::string::npos){const int got=recv(client,buffer,sizeof(buffer),0);if(got<=0)break;request.append(buffer,got);}
            if(reply_.empty())while(!stop_)std::this_thread::sleep_for(std::chrono::milliseconds(10));
            else for(size_t sent=0;sent<reply_.size();){const int put=send(client,reply_.data()+sent,int(std::min<size_t>(reply_.size()-sent,8192)),0);if(put<=0)break;sent+=size_t(put);}
            shutdown(client,SD_BOTH);closesocket(client);
        });
    }
    ~LoopbackServer(){stop_=true;closesocket(listener_);thread_.join();WSACleanup();}
    QString url()const{return QString("http://127.0.0.1:%1/latest.json").arg(port_);}
private:
    std::string reply_;SOCKET listener_{INVALID_SOCKET};unsigned short port_{};std::atomic<bool> stop_{};std::thread thread_;
};
std::string http(const char* status,const std::string& body,const char* extra=""){
    return std::string("HTTP/1.1 ")+status+"\r\nContent-Type: application/json\r\nContent-Length: "+std::to_string(body.size())+"\r\n"+extra+"Connection: close\r\n\r\n"+body;
}
QString fetchError(const std::string& reply){
    LoopbackServer server(reply);
    try{update::fetchReleaseMetadata(server.url(),[]{return false;});}catch(const std::exception& error){return QString::fromUtf8(error.what());}
    return {};
}
void release_fetch(){
    const auto json=release("v1.0").toStdString();
    {LoopbackServer server(http("200 OK",json));require(update::fetchReleaseMetadata(server.url(),[]{return false;})==QByteArray::fromStdString(json),"200 body returned whole");}
    require(fetchError(http("301 Moved Permanently","","Location: https://example.com/latest.json\r\n")).contains("HTTP 301"),"redirect is not followed");
    require(fetchError(http("500 Internal Server Error","{}")).contains("HTTP 500"),"server error reported");
    require(fetchError(http("200 OK",std::string(size_t(update::releaseResponseLimit)+1,'x'))).contains("too large"),"oversized body rejected while reading");
    // A server that never answers would hold WinHTTP for its 10 s timeout; cancelling must abort it promptly.
    LoopbackServer stalled("");std::atomic<bool> cancel{};
    auto pending=std::async(std::launch::async,[&]{try{update::fetchReleaseMetadata(stalled.url(),[&]{return cancel.load();});}catch(const std::exception& error){return QString::fromUtf8(error.what());}return QString();});
    require(pending.wait_for(std::chrono::milliseconds(500))==std::future_status::timeout,"stalled request is still waiting");
    QElapsedTimer elapsed;elapsed.start();cancel=true;
    require(pending.wait_for(std::chrono::seconds(3))==std::future_status::ready,"cancel aborts the blocked WinHTTP call");
    require(pending.get().contains("cancelled")&&elapsed.elapsed()<3000,"cancelled request reports cancellation");
}
void panel_release_check(){
    QTemporaryDir root;require(root.isValid(),"panel root");QSettings().clear();
    QWidget owner;owner.show();std::function<void(const QString&)> pending;int calls=0;
    ui::UpdatePanelHost host;host.checkRelease=[&](std::function<void(const QString&)> report){++calls;pending=std::move(report);};
    QPointer<QDialog> panel=ui::openUpdatePanel(&owner,host,root.path());settled(panel);
    auto* automatic=panel->findChild<QCheckBox*>("updateAutomatic");require(automatic&&automatic->isChecked(),"automatic preference shown enabled by default");
    automatic->setChecked(false);{QSettings settings;require(!update::automaticChecksEnabled(settings),"unticking disables automatic checks");}
    auto* check=button(panel,"updateReleaseCheck");require(check->isVisible()&&check->isEnabled(),"release check button available");
    check->click();require(calls==1&&!check->isEnabled()&&text(panel,"updateReleaseStatus").contains("Checking"),"release check started once and shows progress");
    check->click();require(calls==1,"no second request while one is pending");
    pending("Compositor 0.2.0 is available.");require(text(panel,"updateReleaseStatus")=="Compositor 0.2.0 is available."&&check->isEnabled(),"result shown and button re-enabled");
    check->click();panel->reject();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);require(panel.isNull(),"panel closed");
    pending("late");// Must not touch the destroyed panel.
    QPointer<QDialog> plain=ui::openUpdatePanel(&owner,{},root.path());settled(plain);require(!button(plain,"updateReleaseCheck")->isVisible(),"no release button without a host");
}}
// The version comes from CMake project(VERSION) only: the generated header, the value
// CTest passes from PROJECT_VERSION and the Compositor.exe version resource must agree.
void app_version(const QString& projectVersion,const QString& executable){
    require(QString(COMPOSITOR_VERSION_STRING)=="1.1.0","generated version is 1.1.0");
    require(projectVersion==COMPOSITOR_VERSION_STRING,"generated version matches CMake PROJECT_VERSION");
    require(update::Version{1,0,0}<update::Version::parse(COMPOSITOR_VERSION_STRING),"release v1.0 is not offered to this build");
    const auto path=executable.toStdWString();DWORD handle=0;const DWORD size=GetFileVersionInfoSizeW(path.c_str(),&handle);require(size>0,"Compositor.exe has a version resource");
    std::vector<unsigned char> data(size);require(GetFileVersionInfoW(path.c_str(),0,size,data.data()),"version resource is readable");
    VS_FIXEDFILEINFO* fixed=nullptr;UINT length=0;require(VerQueryValueW(data.data(),L"\\",reinterpret_cast<void**>(&fixed),&length)&&fixed,"fixed file info exists");
    require(fixed->dwFileVersionMS==DWORD(MAKELONG(COMPOSITOR_VERSION_MINOR,COMPOSITOR_VERSION_MAJOR))&&fixed->dwFileVersionLS==DWORD(MAKELONG(0,COMPOSITOR_VERSION_PATCH))&&fixed->dwProductVersionMS==fixed->dwFileVersionMS&&fixed->dwProductVersionLS==fixed->dwFileVersionLS,"numeric file and product versions match");
    require(!(fixed->dwFileFlags&fixed->dwFileFlagsMask&VS_FF_PRERELEASE),"executable is not flagged prerelease");
    for(const wchar_t* key:{L"\\StringFileInfo\\040904B0\\FileVersion",L"\\StringFileInfo\\040904B0\\ProductVersion"}){wchar_t* text=nullptr;require(VerQueryValueW(data.data(),key,reinterpret_cast<void**>(&text),&length)&&text&&QString::fromWCharArray(text)==COMPOSITOR_VERSION_STRING,"string file and product versions match");}
}
int main(int argc,char** argv){    QApplication application(argc,argv);
    if(argc==2){
        QTemporaryDir settingsRoot;QCoreApplication::setOrganizationName("CompositorFixture");QCoreApplication::setApplicationName("ReleaseCheck");
        QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settingsRoot.path());
        const std::map<std::string,void(*)()> cases{{"release_parse",release_parse},{"release_settings",release_settings},{"release_url",release_url},{"release_fetch",release_fetch},{"panel_release_check",panel_release_check}};
        try{if(!cases.contains(argv[1]))throw std::runtime_error("unknown release case");cases.at(argv[1])();std::cout<<"PASS "<<argv[1]<<'\n';return 0;}
        catch(const std::exception& error){std::cerr<<"FAIL "<<argv[1]<<": "<<error.what()<<'\n';return 1;}
    }
    if(argc==4&&std::string(argv[1])=="app_version"){
        try{app_version(QString::fromLocal8Bit(argv[2]),QString::fromLocal8Bit(argv[3]));std::cout<<"PASS app_version\n";return 0;}
        catch(const std::exception& error){std::cerr<<"FAIL app_version: "<<error.what()<<'\n';return 1;}
    }
    if(argc!=4)return 2;    const QString key=QString::fromLocal8Bit(argv[1]),root=QString::fromLocal8Bit(argv[2]),output=QString::fromLocal8Bit(argv[3]);
    QCoreApplication::setOrganizationName("CompositorFixture");QCoreApplication::setApplicationName("UpdatePanel");
    QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root+"/settings");QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,root+"/settings");
    QJsonObject result{{"case",key}};bool passed=false;
    try{
        application.setProperty("manualUpdatesOnly", key=="manual_ignores_test_feed");
        QWidget owner;owner.resize(300,200);owner.show();
        int restarts=0;ui::UpdatePanelHost host;host.restart=[&](const auto& seen){require(seen==std::filesystem::path(root.toStdWString()),"restart root changed");++restarts;return false;};
        QPointer<QDialog> panel=ui::openUpdatePanel(&owner,host,key=="portable_unconfigured"?root+"/portable":root+"/versions/0.1.0");
        settled(panel);const QString initial=state(panel);result["initial_state"]=initial;
        if(key=="portable_unconfigured"||key=="missing_configuration"||key=="manual_ignores_test_feed"){
            require(initial=="unconfigured","unconfigured build attempted update");require(!button(panel,"updateInstall")->isEnabled(),"unconfigured install enabled");require(text(panel,"updateStatus").contains("not configured"),"unconfigured explanation absent");
            require(text(panel,"updateSourceInformation").isEmpty()&&!panel->findChild<QLabel*>("updateSourceInformation")->isVisible(),"unconfigured explanation shown twice");
            if(key=="manual_ignores_test_feed")require(current(root)=="0.1.0"&&text(panel,"updateStatus").contains("new stable releases")&&!text(panel,"updateStatus").contains("preview"),"manual-update build accepted development update configuration");
        }else if(key=="missing_optin"||key=="invalid_configuration"||key=="invalid_state"){
            require(initial=="error","invalid configuration/state was accepted");require(!button(panel,"updateInstall")->isEnabled(),"failed configuration permits install");require(!button(panel,"updateRestart")->isVisible(),"failed configuration permits restart");
            if(key=="invalid_state")require(text(panel,"updateVersions").contains("unavailable")&&!text(panel,"updateVersions").contains("0.0.0"),"unknown state fabricated a version");
        }else if(key=="no_update"){
            require(initial=="current","same-version check offered update");require(!button(panel,"updateInstall")->isEnabled(),"same version install enabled");require(current(root)=="0.1.0","check changed active state");
        }else if(key=="signature_error"){
            require(initial=="error","invalid signature accepted");require(current(root)=="0.1.0","signature rejection changed active version");
        }else{
            require(initial=="available","signed update was not available");require(button(panel,"updateInstall")->isEnabled(),"available update cannot install");
            if(key=="signed_check")require(current(root)=="0.1.0","check installed without Apply");
            else if(key=="duplicate_panel"){
                require(ui::openUpdatePanel(&owner,host,root+"/versions/0.1.0")==panel,"second invocation opened duplicate panel");require(owner.findChildren<QDialog*>("updatePanel").size()==1,"duplicate update dialogs exist");
            }else if(key=="payload_retry"){
                button(panel,"updateInstall")->click();waitState(panel,"error");require(current(root)=="0.1.0","tampered payload activated");
                const QString damaged=root+"/feed/payload/Compositor.exe",backup=root+"/original-payload.exe";
                require(QFile::remove(damaged),"cannot remove fixture tampered file");require(QFile::copy(backup,damaged),"cannot restore exact signed fixture bytes");
                button(panel,"updateCheck")->click();waitState(panel,"available");button(panel,"updateInstall")->click();waitState(panel,"installed");require(current(root)=="0.2.0","retry did not activate signed bytes");
            }else if(key=="cancel_health"||key=="close_cancels"){
                const QString marker=root+"/health-started.txt";qputenv("COMPOSITOR_UI_HEALTH_MARKER",marker.toUtf8());
                button(panel,"updateInstall")->click();require(state(panel)=="installing","install did not begin asynchronously");
                waitFor([&]{return QFile::exists(marker);},"real slow health child did not start");
                require(current(root)=="0.2.0","health child did not run after pending activation");
                if(key=="close_cancels"){panel->reject();waitFor([&]{return panel.isNull();},"closing busy panel did not settle/delete");}
                else{button(panel,"updateCancel")->click();waitState(panel,"cancelled");require(text(panel,"updateStatus").contains("0.1.0"),"cancelled status did not report recovered active version");}
                const auto active=update::Updater(std::filesystem::path(root.toStdWString())).state();require(active.current.text()=="0.1.0"&&!active.pending,"cancellation did not roll back active pointer");qunsetenv("COMPOSITOR_UI_HEALTH_MARKER");
            }else if(key=="install_retains_previous"||key=="restart_cancel"){
                button(panel,"updateInstall")->click();waitState(panel,"installed");const auto active=update::Updater(std::filesystem::path(root.toStdWString())).state();
                require(active.current.text()=="0.2.0"&&active.previous=="0.1.0"&&!active.pending,"installation state or previous version incorrect");require(QFile::exists(root+"/versions/0.1.0/Compositor.exe"),"previous payload removed");require(text(panel,"updateRetainedVersion").contains("0.1.0"),"retained version not displayed");
                if(key=="restart_cancel"){button(panel,"updateRestart")->click();require(restarts==1&&panel&&panel->isVisible(),"restart cancellation closed panel or skipped host");require(text(panel,"updateStatus").startsWith("Restart cancelled"),"restart cancellation not explained");require(current(root)=="0.2.0","restart cancellation removed installed update");}
            }else throw std::runtime_error("unknown update UI case");
        }
        if(panel){result["final_state"]=state(panel);result["status_text"]=text(panel,"updateStatus");panel->reject();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);}
        passed=true;result["status"]="passed";
    }catch(const std::exception& error){result["status"]="failed";result["error"]=QString::fromUtf8(error.what());}
    QFile file(output);if(!file.open(QIODevice::WriteOnly)||file.write(QJsonDocument(result).toJson())<0)return 2;
    std::cout<<(passed?"PASS ":"FAIL ")<<"update_ui."<<key.toStdString()<<'\n';return passed?0:1;
}
