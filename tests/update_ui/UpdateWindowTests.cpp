#include "ui/MainWindow.h"
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QPointer>
#include <QScopeGuard>
#include <QTest>
#include <iostream>
using namespace compositor;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
QAction* updateCommand(MainWindow& window){for(auto* action:window.findChildren<QAction*>())if(action->property("commandId").toString()=="help.check_updates")return action;throw std::runtime_error("Check for Updates command is missing");}
template<class Predicate>void waitFor(Predicate predicate,const char* message){QElapsedTimer elapsed;elapsed.start();while(!predicate()){if(elapsed.elapsed()>15000)throw std::runtime_error(message);QTest::qWait(5);}}
QPushButton* button(QDialog* panel,const char* name){auto* control=panel->findChild<QPushButton*>(name);require(control,"update control missing");return control;}
}
int main(int argc,char** argv){
    QApplication application(argc,argv);if(argc!=3)return 2;const QString key=QString::fromLocal8Bit(argv[1]),output=QString::fromLocal8Bit(argv[2]);
    QSettings::setDefaultFormat(QSettings::IniFormat);const QString settings=QFileInfo(output).absolutePath()+"/settings";QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings);QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,settings);
    QJsonObject result{{"case",key}};bool passed=false;
    try{MainWindow window(true);window.show();QTest::qWait(30);auto* action=updateCommand(window);require(action->isEnabled(),"update command disabled in welcome session");action->trigger();
        QDialog* panel=nullptr;QElapsedTimer elapsed;elapsed.start();while(elapsed.elapsed()<5000){panel=window.findChild<QDialog*>("updatePanel");if(panel&&!panel->property("updateState").toString().isEmpty())break;QTest::qWait(5);}
        require(panel&&panel->isVisible(),"Help command did not open owned update panel");require(!panel->isModal(),"update panel blocks editor modally");
        if(key=="help_unconfigured")require(panel->property("updateState").toString()=="unconfigured","portable Help check did not explain unconfigured update source");
        else if(key=="restart_save_cancel"||key=="restart_launch"||key=="restart_launch_failure"){
            waitFor([&]{return panel->property("updateState").toString()=="available";},"actual Help did not read signed fixture update");
            button(panel,"updateInstall")->click();waitFor([&]{return panel->property("updateState").toString()=="installed";},"actual Help did not install signed fixture update");
            QDir root(QCoreApplication::applicationDirPath());require(root.cdUp()&&root.cdUp(),"fixture installation root missing");const QString marker=root.filePath("launched.txt");qputenv("COMPOSITOR_UI_LAUNCH_MARKER",marker.toUtf8());
            require(!QFile::exists(marker),"update health process was mistaken for normal launch");application.setQuitOnLastWindowClosed(false);
            if(key=="restart_save_cancel"){
                Document document;document.id=newId();document.width=8;document.height=8;Layer layer;layer.id=newId();layer.name="Unsaved fixture";layer.transform={0,0,8,8};document.layers.push_back(layer);
                auto* project=&window.addProject(document,"Unsaved update fixture");project->history.begin("Fixture edit",project->document,project->active);project->document->layers.front().opacity=.5;project->history.end(project->document,project->active);const auto before=project->document;QPointer<QWidget> page=project->page;
                bool prompted=false;QTimer cancel;cancel.setInterval(5);QObject::connect(&cancel,&QTimer::timeout,&window,[&]{auto* message=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());if(message&&message->parentWidget()==&window&&message->standardButtons().testFlag(QMessageBox::Cancel)){prompted=true;message->button(QMessageBox::Cancel)->click();}});cancel.start();
                button(panel,"updateRestart")->click();cancel.stop();QTest::qWait(100);require(prompted,"restart did not use ordinary unsaved-changes dialog");require(window.isVisible()&&page&&window.findChild<QTabWidget*>()->currentWidget()==page&&project->document==before&&project->history.modified(),"cancelled restart changed editor session or document");require(panel->isVisible()&&!QFile::exists(marker),"cancelled restart closed panel or launched application");
                project->history.markSaved();
            }else if(key=="restart_launch_failure"){
                const QString executable=root.filePath("versions/0.2.0/Compositor.exe"),held=root.filePath("versions/0.2.0/Compositor-held.exe");
                require(QFile::rename(executable,held),"cannot stage task-owned launch failure");const auto restore=qScopeGuard([&]{QFile::rename(held,executable);});
                button(panel,"updateRestart")->click();QTest::qWait(30);require(window.isVisible()&&panel->isVisible(),"launch failure did not retain editor and update panel");require(!QFile::exists(marker),"missing executable unexpectedly launched");
                auto* tabs=window.findChild<QTabWidget*>();require(tabs&&tabs->count()==1,"launch failure did not restore a welcome session");
                QAction* close=nullptr;for(auto* command:window.findChildren<QAction*>())if(command->property("commandId").toString()=="file.close")close=command;
                require(close&&close->isEnabled(),"restored welcome cannot close normally");close->trigger();require(tabs->count()==1,"launch failure left the editor in its closing state");
                QFile active(root.filePath("state/active.json"));require(active.open(QIODevice::ReadOnly),"active update state missing after launch failure");require(QJsonDocument::fromJson(active.readAll()).object()["current"].toString()=="0.2.0","launch failure discarded installed update");
            }else{
                QPointer<QDialog> alive(panel);button(panel,"updateRestart")->click();waitFor([&]{return QFile::exists(marker);},"accepted restart did not launch signed current version");require(!window.isVisible(),"accepted restart did not close old editor");QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);if(!alive)panel=nullptr;
            }
            require(!application.quitOnLastWindowClosed(),"restart did not restore prior quit policy");qunsetenv("COMPOSITOR_UI_LAUNCH_MARKER");
        }else throw std::runtime_error("unknown update window case");
        if(panel)panel->reject();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);passed=true;result["status"]="passed";
    }catch(const std::exception& error){result["status"]="failed";result["error"]=error.what();}
    QFile file(output);if(!file.open(QIODevice::WriteOnly)||file.write(QJsonDocument(result).toJson())<0)return 2;std::cout<<(passed?"PASS ":"FAIL ")<<"update_window."<<key.toStdString()<<'\n';return passed?0:1;
}
