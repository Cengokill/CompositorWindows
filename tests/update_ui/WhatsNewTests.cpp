#include "ui/MainWindow.h"
#include "ui/WhatsNew.h"
#include "ui/WorkspaceDropQueue.h"
#include <QApplication>
#include <QDialog>
#include <QElapsedTimer>
#include <QImage>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBrowser>
#include <QTimer>
#include <iostream>
#include <functional>
#include <map>
#include <stdexcept>

using namespace compositor;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Predicate>void waitFor(Predicate predicate,const char* message){
    QElapsedTimer elapsed;elapsed.start();
    while(!predicate()){if(elapsed.elapsed()>15000)throw std::runtime_error(message);QTest::qWait(5);}
}
QDialog* whatsNew(MainWindow& window){
    for(auto* dialog:window.findChildren<QDialog*>("whatsNewDialog"))if(dialog->isVisible())return dialog;
    return nullptr;
}
void initializeOlder(){QSettings settings;ui::acknowledgeReleaseNotes(settings,{1,5,5});}
void extraction(){
    const QString markdown=QStringLiteral("# Compositor Windows 1.5.7\n## Improvements\n- Later\n# Compositor Windows 1.5.6\nIntro\n## New features\n- Offline **notes**\n## Empty section\nIgnore this\n## Fixes\n- Repair\n# Compositor Windows 1.2.0\n## Improvements\n- Older\n");
    const auto notes=ui::releaseNotesForVersion(markdown,"1.5.6");
    require(notes.features.contains("Offline")&&notes.fixes.contains("Repair"),"version sections not extracted");
    require(notes.improvements.isEmpty()&&!notes.features.contains("Later")&&!notes.fixes.contains("Older")&&!notes.features.contains("Ignore"),"content escaped its version or section");
    require(ui::releaseNotesForVersion(markdown,"1.5.60").empty(),"partial version matched");
    require(ui::releaseNotesForVersion("# Compositor Windows 1.5.6\n## Fixes\n  \n","1.5.6").empty(),"empty section counted as content");
    require(!ui::installedReleaseNotes().empty(),"running version lacks embedded notes");
}
void settings(){
    QSettings initial;require(ui::releaseNotesDue(initial,{1,5,6}),"fresh history did not show notes");
    require(!initial.contains("updates/whatsNewVersion"),"due check saved an acknowledgement");
    ui::acknowledgeReleaseNotes(initial,{1,5,6});
    require(!ui::releaseNotesDue(initial,{1,5,6})&&!ui::releaseNotesDue(initial,{1,4,9}),"same or older version was due");
    require(ui::releaseNotesDue(initial,{1,10,0}),"numeric version ordering failed");
    require(ui::releaseNotesDue(initial,{1,10,0}),"unacknowledged version was lost");
    ui::acknowledgeReleaseNotes(initial,{1,10,0});
    QSettings reopened;require(!ui::releaseNotesDue(reopened,{1,10,0}),"acknowledgement did not persist");
    ui::acknowledgeReleaseNotes(reopened,{1,5,6});
    require(!ui::releaseNotesDue(reopened,{1,9,9})&&ui::releaseNotesDue(reopened,{1,10,1}),"older acknowledgement changed high-water mark");
    reopened.setValue("updates/whatsNewVersion","invalid");reopened.sync();
    require(ui::releaseNotesDue(reopened,{2,0,0})&&ui::releaseNotesDue(reopened,{2,0,0}),"invalid history suppressed notes");
    require(reopened.value("updates/whatsNewVersion").toString()=="invalid","due check replaced invalid history");
}
void firstLaunch(){
    MainWindow window(true);window.show();bool finished=false;
    window.startWhatsNew([&]{finished=true;});waitFor([&]{return whatsNew(window)!=nullptr||finished;},"first launch did not proceed");
    require(whatsNew(window)&&!finished,"first launch did not show dialog");
    QSettings settings;require(!settings.contains("updates/whatsNewVersion"),"first launch acknowledged before dismissal");
    whatsNew(window)->accept();waitFor([&]{return finished;},"first launch did not finish after dismissal");
    require(settings.value("updates/whatsNewVersion").toString()=="1.5.6","first launch acknowledgement missing");
    MainWindow next(true);next.show();bool again=false;next.startWhatsNew([&]{again=true;});
    waitFor([&]{return again;},"next launch did not finish");require(!whatsNew(next),"first launch notes shown twice");
}
void closeThrough(const std::string& method){
    initializeOlder();MainWindow window(true);
    Document document;document.id=newId();document.width=8;document.height=8;
    Layer layer;layer.id=newId();layer.name="Original";layer.transform={0,0,8,8};document.layers.push_back(layer);
    auto& project=window.addProject(document,"Original");const auto before=project.document;const auto count=project.history.undoCount();
    auto* tabs=window.findChild<QTabWidget*>();const auto tabCount=tabs->count();window.show();
    auto* focusTarget=new QPushButton("Focus target",&window);focusTarget->show();window.activateWindow();focusTarget->setFocus();
    waitFor([&]{return window.focusWidget()==focusTarget;},"focus fixture not active");
    bool finished=false;window.startWhatsNew([&]{finished=true;});
    waitFor([&]{return whatsNew(window)!=nullptr;},"upgrade did not show notes");
    auto* dialog=whatsNew(window);require(dialog->windowModality()==Qt::WindowModal,"dialog is not window modal");
    auto* content=dialog->findChild<QTextBrowser*>("whatsNewContent");
    require(content&&content->toPlainText().contains("Animated GIF")&&!content->toPlainText().contains("Focus Stack Images"),"dialog shows wrong installed version");
    require(dialog->findChild<QLabel*>("whatsNewVersion")->text().contains("1.5.6"),"installed version missing");
    {QSettings pending;require(ui::releaseNotesDue(pending,{1,5,6}),"version acknowledged before dismissal");}
    if(method=="continue")dialog->findChild<QPushButton*>("whatsNewContinue")->click();
    else if(method=="escape")QTest::keyClick(dialog,Qt::Key_Escape);
    else dialog->close();
    waitFor([&]{return finished&&!whatsNew(window);},"dismissal did not finish");
    waitFor([&]{return window.focusWidget()==focusTarget;},"dismissal did not restore focus");
    require(project.document==before&&project.history.undoCount()==count&&tabs->count()==tabCount,"notes changed document or history");
    QSettings persisted;require(!ui::releaseNotesDue(persisted,{1,5,6}),"dismissal not saved");
    MainWindow next(true);next.show();bool again=false;next.startWhatsNew([&]{again=true;});
    waitFor([&]{return again;},"next launch did not finish");require(!whatsNew(next),"same version shown twice");
}
void missing(){
    initializeOlder();QCoreApplication::setApplicationVersion("9.9.9");MainWindow window(true);window.show();
    bool finished=false;window.startWhatsNew([&]{finished=true;});waitFor([&]{return finished;},"missing notes stalled startup");
    require(!whatsNew(window),"missing notes showed empty dialog");
    QSettings settings;require(settings.value("updates/whatsNewVersion").toString()=="1.5.5","missing notes acknowledged a version never shown");
    settings.remove("updates/whatsNewVersion");settings.sync();
    MainWindow fresh(true);fresh.show();bool freshFinished=false;fresh.startWhatsNew([&]{freshFinished=true;});
    waitFor([&]{return freshFinished;},"missing notes on first launch stalled startup");
    require(!whatsNew(fresh)&&!settings.contains("updates/whatsNewVersion"),"missing notes on first launch were acknowledged");
}
void defersStartup(){
    initializeOlder();QTemporaryDir input;require(input.isValid(),"input directory missing");
    const auto imagePath=input.filePath("startup.png");QImage image(8,8,QImage::Format_RGBA8888);image.fill(Qt::red);require(image.save(imagePath),"cannot save input");
    MainWindow window(true);window.show();QDialog blocking(&window);blocking.setWindowModality(Qt::WindowModal);blocking.show();
    waitFor([&]{return QApplication::activeModalWidget()==&blocking;},"startup modal missing");
    window.receiveDropPaths({imagePath});auto* queue=ui::WorkspaceDropQueue::find(&window);
    require(queue&&!queue->idle(),"startup input was not queued");
    bool finished=false;window.startWhatsNew([&]{finished=true;});
    auto* timer=window.findChild<QTimer*>("whatsNewStartup");require(timer,"startup timer missing");
    int ticks=0;QObject::connect(timer,&QTimer::timeout,&window,[&]{++ticks;});
    waitFor([&]{return ticks>=2;},"startup timer did not check blockers");
    require(!whatsNew(window)&&!finished&&!queue->idle(),"notes interrupted startup modal or queue");
    blocking.close();waitFor([&]{return whatsNew(window)!=nullptr;},"notes did not follow startup import");
    require(queue->idle(),"notes opened before startup queue finished");
    require(window.canvas()&&window.findChild<QTabWidget*>()->count()>=1,"startup input did not leave a project");
    whatsNew(window)->accept();waitFor([&]{return finished;},"completion callback missing");
}
void cancelClose(){
    initializeOlder();MainWindow window(true);Document document;document.id=newId();document.width=8;document.height=8;
    Layer layer;layer.id=newId();layer.name="Original";layer.transform={0,0,8,8};document.layers.push_back(layer);
    auto& project=window.addProject(document,"Original");
    project.history.begin("Edit",project.document,project.active);project.document->layers.front().opacity=.5;project.history.end(project.document,project.active);
    const auto before=project.document;window.show();bool finished=false;window.startWhatsNew([&]{finished=true;});
    auto* timer=window.findChild<QTimer*>("whatsNewStartup");require(timer,"startup timer missing");
    int ticks=0;bool cancelled=false;
    QObject::connect(timer,&QTimer::timeout,&window,[&]{
        if(++ticks<2)return;
        if(auto* message=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())){
            cancelled=true;message->button(QMessageBox::Cancel)->click();
        }
    });
    QTimer::singleShot(0,&window,[&]{window.close();});
    waitFor([&]{return whatsNew(window)!=nullptr;},"cancelled close prevented startup notes");
    require(cancelled&&window.isVisible()&&project.document==before&&project.history.undoCount()==1,"cancelled close changed the project");
    whatsNew(window)->accept();waitFor([&]{return finished;},"cancelled close lost completion callback");
}
void longContent(){
    MainWindow window(true);window.resize(520,420);window.show();
    ui::ReleaseNotes notes;for(int i=0;i<80;++i)notes.features+=QString("- Line %1 ").arg(i)+QString(200,QLatin1Char('a'))+"\n";
    QPointer<QDialog> dialog=ui::openWhatsNewDialog(&window,"1.5.6",notes);
    auto* content=dialog->findChild<QTextBrowser*>("whatsNewContent");auto* header=dialog->findChild<QLabel*>("whatsNewHeading");auto* button=dialog->findChild<QPushButton*>("whatsNewContinue");
    waitFor([&]{return content->verticalScrollBar()->maximum()>0;},"long text did not scroll");
    require(content->horizontalScrollBar()->maximum()==0,"long text requires horizontal scroll");
    require(header->isVisible()&&button->isVisible()&&!content->accessibleName().isEmpty(),"header, button or accessibility missing");
    require(dialog->width()<window.width()&&dialog->height()<window.height(),"dialog did not retain margins");
    const auto headingPosition=header->mapTo(dialog,QPoint());const auto buttonPosition=button->mapTo(dialog,QPoint());
    content->verticalScrollBar()->setValue(content->verticalScrollBar()->maximum());
    require(header->mapTo(dialog,QPoint())==headingPosition&&button->mapTo(dialog,QPoint())==buttonPosition,"header or button scrolled");
    require(!content->toPlainText().contains("Improvements")&&!content->toPlainText().contains("Fixes"),"empty sections displayed");
    dialog->reject();
}
}
int main(int argc,char** argv){
    QApplication app(argc,argv);QTemporaryDir settingsRoot;require(settingsRoot.isValid(),"settings directory missing");
    QCoreApplication::setOrganizationName("CompositorWhatsNewFixture");QCoreApplication::setApplicationName("WhatsNewTests");QCoreApplication::setApplicationVersion("1.5.6");
    QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settingsRoot.path());
    if(argc!=2)return 2;
    const std::map<std::string,std::function<void()>> cases{
        {"extraction",extraction},{"settings",settings},{"first_launch",firstLaunch},
        {"continue",[]{closeThrough("continue");}},{"escape",[]{closeThrough("escape");}},{"close",[]{closeThrough("close");}},
        {"missing",missing},{"defers_startup",defersStartup},{"cancel_close",cancelClose},{"long_content",longContent}};
    try{if(!cases.contains(argv[1]))throw std::runtime_error("unknown case");cases.at(argv[1])();std::cout<<"PASS "<<argv[1]<<'\n';return 0;}
    catch(const std::exception& error){std::cerr<<"FAIL "<<argv[1]<<": "<<error.what()<<'\n';return 1;}
}
