#include "ui/UpdatePanel.h"
#include "update/Updater.h"
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
#include <iostream>
#include <stdexcept>
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
}
int main(int argc,char** argv){
    QApplication application(argc,argv);if(argc!=4)return 2;
    const QString key=QString::fromLocal8Bit(argv[1]),root=QString::fromLocal8Bit(argv[2]),output=QString::fromLocal8Bit(argv[3]);
    QCoreApplication::setOrganizationName("CompositorFixture");QCoreApplication::setApplicationName("UpdatePanel");
    QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root+"/settings");QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,root+"/settings");
    QJsonObject result{{"case",key}};bool passed=false;
    try{
        application.setProperty("manualUpdatesOnly", key=="preview_ignores_test_feed");
        QWidget owner;owner.resize(300,200);owner.show();
        int restarts=0;ui::UpdatePanelHost host;host.restart=[&](const auto& seen){require(seen==std::filesystem::path(root.toStdWString()),"restart root changed");++restarts;return false;};
        QPointer<QDialog> panel=ui::openUpdatePanel(&owner,host,key=="portable_unconfigured"?root+"/portable":root+"/versions/0.1.0");
        settled(panel);const QString initial=state(panel);result["initial_state"]=initial;
        if(key=="portable_unconfigured"||key=="missing_configuration"||key=="preview_ignores_test_feed"){
            require(initial=="unconfigured","unconfigured build attempted update");require(!button(panel,"updateInstall")->isEnabled(),"unconfigured install enabled");require(text(panel,"updateStatus").contains("not configured"),"unconfigured explanation absent");
            if(key=="preview_ignores_test_feed")require(current(root)=="0.1.0"&&text(panel,"updateStatus").contains("preview"),"preview accepted development update configuration");
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
