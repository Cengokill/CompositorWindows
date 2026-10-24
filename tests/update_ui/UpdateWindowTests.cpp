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
#include <QLabel>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QUrl>
#include <atomic>
#include <map>
#include <memory>
#include "update/ReleaseCheck.h"
using namespace compositor;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
QAction* updateCommand(MainWindow& window){for(auto* action:window.findChildren<QAction*>())if(action->property("commandId").toString()=="help.check_updates")return action;throw std::runtime_error("Check for Updates command is missing");}
template<class Predicate>void waitFor(Predicate predicate,const char* message){QElapsedTimer elapsed;elapsed.start();while(!predicate()){if(elapsed.elapsed()>15000)throw std::runtime_error(message);QTest::qWait(5);}}
QPushButton* button(QDialog* panel,const char* name){auto* control=panel->findChild<QPushButton*>(name);require(control,"update control missing");return control;}
QByteArray releaseJson(const char* tag){return QJsonDocument(QJsonObject{{"tag_name",tag},{"draft",false},{"prerelease",false},{"html_url",QString("https://github.com/Cengokill/CompositorWindows/releases/tag/")+tag}}).toJson();}
struct FakeRelease {
    std::shared_ptr<std::atomic<int>> calls=std::make_shared<std::atomic<int>>(0);
    std::shared_ptr<std::atomic<bool>> hold=std::make_shared<std::atomic<bool>>(false),sawCancel=std::make_shared<std::atomic<bool>>(false);
    std::shared_ptr<QByteArray> body=std::make_shared<QByteArray>(releaseJson("v0.2.0"));
    std::shared_ptr<std::atomic<bool>> fail=std::make_shared<std::atomic<bool>>(false);
    MainWindow::ReleaseFetcher fetcher()const{
        // Copies only shared state: it may outlive the window under test.
        return [calls=calls,hold=hold,sawCancel=sawCancel,body=body,fail=fail](const std::function<bool()>& cancelled){
            ++*calls;QElapsedTimer limit;limit.start();
            while(hold->load()&&limit.elapsed()<15000){if(cancelled())sawCancel->store(true);QThread::msleep(2);}
            if(cancelled())sawCancel->store(true);
            if(fail->load())throw std::runtime_error("synthetic offline");
            return *body;
        };
    }
};
QDialog* notice(MainWindow& window){for(auto* dialog:window.findChildren<QDialog*>("updateAvailableNotice"))if(dialog->isVisible())return dialog;return nullptr;}
QTimer* releaseTimer(MainWindow& window){auto* timer=window.findChild<QTimer*>("automaticReleaseCheck");require(timer,"automatic release timer missing");return timer;}
void waitTicks(MainWindow& window,int count){int ticks=0;QObject::connect(releaseTimer(window),&QTimer::timeout,&window,[&]{++ticks;});waitFor([&]{return ticks>=count;},"automatic timer did not tick");}
void ageLastCheck(){QSettings settings;update::recordReleaseCheck(settings,QDateTime::currentDateTimeUtc().addSecs(-25*3600));}
QString status(MainWindow& window){return window.property("releaseCheckStatus").toString();}
void release_auto_available(){
    FakeRelease fake;MainWindow window(true);window.setReleaseFetcherForTesting(fake.fetcher());window.show();
    Document document;document.id=newId();document.width=8;document.height=8;Layer layer;layer.id=newId();layer.name="Unsaved";layer.transform={0,0,8,8};document.layers.push_back(layer);
    auto* project=&window.addProject(document,"Unsaved");project->history.begin("Fixture edit",project->document,project->active);project->document->layers.front().opacity=.5;project->history.end(project->document,project->active);
    const auto before=project->document;const auto undo=project->history.undoCount();auto* tabs=window.findChild<QTabWidget*>();const auto count=tabs->count();
    window.startAutomaticUpdateChecks(0,20);waitFor([&]{return notice(window)!=nullptr;},"automatic check did not notify");
    auto* dialog=notice(window);require(!dialog->isModal()&&QApplication::activeModalWidget()==nullptr,"notice is not modal");
    const auto text=dialog->findChild<QLabel*>("updateNoticeText")->text();require(text.contains("0.2.0")&&text.contains("0.1.8")&&text.contains("manual"),"notice names both versions and manual installation");
    {QSettings settings;require(!update::releaseCheckDue(settings,QDateTime::currentDateTimeUtc()),"check time recorded");}
    waitTicks(window,3);require(fake.calls->load()==1,"no second request within 24 hours while open");
    require(tabs->count()==count&&project->document==before&&project->history.undoCount()==undo&&project->history.modified(),"open project, document and history untouched");
    ageLastCheck();waitFor([&]{return fake.calls->load()==2;},"daily re-check while open did not run");
}
void release_later_skip(){
    FakeRelease fake;MainWindow window(true);window.setReleaseFetcherForTesting(fake.fetcher());window.show();
    window.startAutomaticUpdateChecks(0,20);waitFor([&]{return notice(window)!=nullptr;},"no notice");
    button(notice(window),"updateNoticeLater")->click();waitFor([&]{return notice(window)==nullptr;},"Later did not close the notice");
    {QSettings settings;require(!update::skippedRelease(settings),"Later does not skip the version");}
    ageLastCheck();waitFor([&]{return notice(window)!=nullptr;},"deferred update not offered again on the next daily check");require(fake.calls->load()==2,"one request per daily check");
    button(notice(window),"updateNoticeSkip")->click();waitFor([&]{return notice(window)==nullptr;},"Skip did not close the notice");
    {QSettings settings;require(update::skippedRelease(settings)==update::Version{0,2,0},"Skip stored the version");}
    ageLastCheck();waitFor([&]{return fake.calls->load()==3;},"no third daily check");waitFor([&]{return status(window).contains("0.2.0 is available");},"third check unfinished");require(notice(window)==nullptr,"skipped version notified automatically");
    *fake.body=releaseJson("v0.3.0");ageLastCheck();waitFor([&]{return notice(window)!=nullptr;},"a newer version than the skipped one was not offered");
    require(notice(window)->property("releaseVersion").toString()=="0.3.0","newer release offered");button(notice(window),"updateNoticeLater")->click();
    *fake.body=releaseJson("v0.2.0");QString reported;window.checkForRelease([&](const QString& text){reported=text;});waitFor([&]{return !reported.isEmpty();},"manual check unreported");
    require(notice(window)&&notice(window)->property("releaseVersion").toString()=="0.2.0","manual check shows a skipped version");
}
void release_download(){
    FakeRelease fake;MainWindow window(true);window.setReleaseFetcherForTesting(fake.fetcher());QList<QUrl> opened;window.setUrlOpenerForTesting([&](const QUrl& url){opened.push_back(url);});window.show();
    window.startAutomaticUpdateChecks(0,20);waitFor([&]{return notice(window)!=nullptr;},"no notice");
    button(notice(window),"updateNoticeDownload")->click();
    require(opened.size()==1&&opened.front()==QUrl("https://github.com/Cengokill/CompositorWindows/releases/tag/v0.2.0"),"Download opens only the release page");
    waitFor([&]{return notice(window)==nullptr;},"Download did not close the notice");
}
void release_disabled(){
    {QSettings settings;update::setAutomaticChecksEnabled(settings,false);}
    FakeRelease fake;MainWindow window(true);window.setReleaseFetcherForTesting(fake.fetcher());window.show();
    window.startAutomaticUpdateChecks(0,20);waitTicks(window,3);require(fake.calls->load()==0&&notice(window)==nullptr,"disabled preference still sent a request");
    QString reported;window.checkForRelease([&](const QString& text){reported=text;});waitFor([&]{return !reported.isEmpty();},"manual check unreported");require(fake.calls->load()==1&&notice(window),"manual check still works when automatic checks are off");
}
void release_quiet_results(){
    FakeRelease fake;*fake.body=releaseJson("v0.1.8");MainWindow window(true);window.setReleaseFetcherForTesting(fake.fetcher());window.show();
    window.startAutomaticUpdateChecks(0,20);waitFor([&]{return status(window).contains("up to date");},"same version not reported up to date");require(notice(window)==nullptr,"same version notified");
    fake.fail->store(true);ageLastCheck();waitFor([&]{return status(window).startsWith("Could not check");},"offline failure not recorded");
    require(notice(window)==nullptr&&QApplication::activeModalWidget()==nullptr&&window.findChildren<QMessageBox*>().isEmpty(),"offline failure is silent");
    {QSettings settings;require(!update::releaseCheckDue(settings,QDateTime::currentDateTimeUtc()),"failed check still spaced by a day");}
    fake.fail->store(false);*fake.body="{\"tag_name\":\"v9.9.9\"}";QString reported;window.checkForRelease([&](const QString& text){reported=text;});waitFor([&]{return !reported.isEmpty();},"manual check unreported");
    require(reported.startsWith("Could not check")&&notice(window)==nullptr,"malformed response rejected without notice");
}
void release_single_flight(){
    FakeRelease fake;fake.hold->store(true);MainWindow window(true);window.setReleaseFetcherForTesting(fake.fetcher());window.show();
    window.startAutomaticUpdateChecks(0,20);waitFor([&]{return fake.calls->load()==1;},"automatic request not started");
    QStringList reports;window.checkForRelease([&](const QString& text){reports.push_back(text);});window.checkForRelease([&](const QString& text){reports.push_back(text);});
    fake.hold->store(false);waitFor([&]{return reports.size()==2;},"waiting callers not all answered");
    require(fake.calls->load()==1&&reports[0].contains("0.2.0 is available")&&notice(window),"one request answers automatic and manual callers");
}
void release_window_closed(){
    FakeRelease fake;fake.hold->store(true);
    {auto window=std::make_unique<MainWindow>(true);window->setReleaseFetcherForTesting(fake.fetcher());window->startAutomaticUpdateChecks(0,20);waitFor([&]{return fake.calls->load()==1;},"request not started");}
    waitFor([&]{return fake.sawCancel->load();},"destroyed window did not cancel its request");fake.hold->store(false);
    require(QThreadPool::globalInstance()->waitForDone(15000),"worker did not finish");QCoreApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
}
void release_help_panel(){
    FakeRelease fake;MainWindow window(true);window.setReleaseFetcherForTesting(fake.fetcher());window.show();
    updateCommand(window)->trigger();QDialog* panel=nullptr;waitFor([&]{panel=window.findChild<QDialog*>("updatePanel");return panel&&!panel->property("updateState").toString().isEmpty();},"Help did not open the update panel");
    button(panel,"updateReleaseCheck")->click();auto* label=panel->findChild<QLabel*>("updateReleaseStatus");
    waitFor([&]{return label->text()=="Compositor 0.2.0 is available.";},"panel did not show the release result");require(notice(window)&&fake.calls->load()==1,"Help check shows the notice");
    panel->reject();
}
}
int main(int argc,char** argv){
    QApplication application(argc,argv);
    if(argc==2){
        QTemporaryDir settingsRoot;QCoreApplication::setOrganizationName("CompositorFixture");QCoreApplication::setApplicationName("ReleaseWindow");QCoreApplication::setApplicationVersion("0.1.8");
        QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settingsRoot.path());
        const std::map<std::string,void(*)()> cases{{"release_auto_available",release_auto_available},{"release_later_skip",release_later_skip},{"release_download",release_download},{"release_disabled",release_disabled},{"release_quiet_results",release_quiet_results},{"release_single_flight",release_single_flight},{"release_window_closed",release_window_closed},{"release_help_panel",release_help_panel}};
        try{if(!cases.contains(argv[1]))throw std::runtime_error("unknown release case");cases.at(argv[1])();std::cout<<"PASS "<<argv[1]<<'\n';return 0;}
        catch(const std::exception& error){std::cerr<<"FAIL "<<argv[1]<<": "<<error.what()<<'\n';return 1;}
    }
    if(argc!=3)return 2;const QString key=QString::fromLocal8Bit(argv[1]),output=QString::fromLocal8Bit(argv[2]);
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
