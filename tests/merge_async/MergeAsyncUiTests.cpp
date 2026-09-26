// Prospective Windows responsiveness/lifetime requirements. Unconfigured at creation.
#include "ui/MainWindow.h"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <iostream>
#include <map>
#include <stdexcept>
using namespace compositor;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
QAction* action(MainWindow& window,const char* id){for(auto* candidate:window.findChildren<QAction*>())if(candidate->property("commandId").toString()==id&&candidate->isVisible())return candidate;throw std::runtime_error(std::string("Visible command absent: ")+id);}
void trigger(MainWindow& window,const char* id){auto* item=action(window,id);require(item->isEnabled(),"Command enabled");item->trigger();}
Document document(int width=256,int height=192){
    Document result;result.id=newId();result.width=width;result.height=height;
    for(int index=0;index<2;++index){Layer layer;layer.id=newId();layer.name=index?"Upper":"Lower";layer.transform={0,0,double(width),double(height)};layer.raster=Raster::filled(16,12,index?Pixel{100,60,20,128}:Pixel{30,50,80,255});result.layers.push_back(std::move(layer));}
    return result;
}
struct Fixture {
    QTemporaryDir directory{QFileInfo(QString::fromUtf8(__FILE__)).dir().filePath("fixtures/merge-XXXXXX")};
    std::unique_ptr<MainWindow> window;
    EditorProject* owner{};
    std::optional<Document> before;
    std::string active;
    std::vector<std::string> selected;
    QTimer dismiss;
    QStringList errors;
    explicit Fixture(bool heavy=false,bool oversized=false){
        require(directory.isValid(),"Owned fixture directory");directory.setAutoRemove(false);
        QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,directory.path());
        window=std::make_unique<MainWindow>(true);owner=&window->addProject(oversized?document(30000,30000):heavy?document(2048,1536):document(),"Merge worker fixture");
        window->resize(1100,800);window->show();QTest::qWait(20);before=owner->document;active=owner->active;selected=owner->selected;
        require(selected.size()==1&&active==before->layers.back().id,"Source Merge Down selection fixture");
        QObject::connect(window->statusBar(),&QStatusBar::messageChanged,window.get(),[this](const QString& message){if(!message.isEmpty())errors.append(message);});
        dismiss.setInterval(5);QObject::connect(&dismiss,&QTimer::timeout,window.get(),[this]{for(auto* box:window->findChildren<QMessageBox*>())if(box->isVisible()){errors.append(box->text());box->reject();}});dismiss.start();
        std::cout<<"fixture="<<directory.path().toStdString()<<'\n';
    }
    ~Fixture(){dismiss.stop();window.reset();}
    QObject* job()const{return window->findChild<QObject*>("layerMergeJob");}
    QProgressDialog* progress()const{return window->findChild<QProgressDialog*>("mergeProgress");}
    void unchanged()const{require(owner->document==before&&owner->active==active&&owner->selected==selected&&!owner->maskSelected&&owner->history.undoCount()==0,"Merge leaves original document, target, selection and history private until commit");}
    void begin(){trigger(*window,"layer.merge");require(owner->projectBusy&&job(),"Merge returns to UI with a task-owned worker and projectBusy");unchanged();}
    void wait(){QElapsedTimer elapsed;elapsed.start();while(owner->projectBusy||job()){QTest::qWait(5);require(elapsed.elapsed()<20000,"Merge worker settles without deadlock");}QTest::qWait(10);}
    void cancel(){QElapsedTimer deadline;deadline.start();while(owner->projectBusy&&(!progress()||!progress()->isVisible())){QTest::qWait(1);require(deadline.elapsed()<10000,"Merge progress becomes visible");}auto* dialog=progress();require(owner->projectBusy&&job(),"Cancel is exercised while merge remains pending");require(dialog&&dialog->windowModality()==Qt::NonModal&&dialog->isVisible(),"Visible nonmodal merge progress exists");QPushButton* button=nullptr;for(auto* candidate:dialog->findChildren<QPushButton*>())if(candidate->text().remove('&')=="Cancel")button=candidate;require(button&&button->isEnabled(),"Merge progress exposes enabled Cancel");QTest::mouseClick(button,Qt::LeftButton);wait();unchanged();require(errors.isEmpty(),"User cancellation emits no error dialog");}
    void completed(){require(errors.isEmpty(),"Successful merge emits no error dialog");require(owner->document&&owner->document->layers.size()==1&&owner->document->layers.front().name=="Lower"&&owner->active==owner->document->layers.front().id&&owner->selected==std::vector<std::string>{owner->active},"Merge Down commits one correctly named and selected image");require(owner->history.undoCount()==1&&owner->history.undoName()=="Merge Down","Merge commits exactly one source-named history entry");}
};
void async_commit(){Fixture fixture;auto expected=SoftwareRenderer().render(*fixture.before,0,0,fixture.before->width,fixture.before->height);fixture.begin();fixture.wait();fixture.completed();auto actual=SoftwareRenderer().render(*fixture.owner->document,0,0,fixture.owner->document->width,fixture.owner->document->height);require(actual->rgba()==expected->rgba(),"Merged appearance exactly matches canonical pre-merge render");const auto merged=fixture.owner->document;trigger(*fixture.window,"edit.undo");fixture.unchanged();trigger(*fixture.window,"edit.redo");require(fixture.owner->document==merged&&fixture.owner->history.undoCount()==1,"Redo restores exact committed immutable merge");}
void cancel_button(){Fixture fixture(true);fixture.begin();fixture.cancel();}
void cancel_retry(){Fixture fixture(true);fixture.begin();fixture.cancel();fixture.begin();fixture.wait();fixture.completed();}
void heartbeat(){Fixture fixture(true);int observations=0;bool privateState=true;QTimer heartbeatTimer;heartbeatTimer.setInterval(1);QObject::connect(&heartbeatTimer,&QTimer::timeout,fixture.window.get(),[&]{if(fixture.owner->projectBusy&&fixture.progress()&&fixture.progress()->isVisible()){++observations;privateState=privateState&&fixture.owner->document==fixture.before&&fixture.owner->history.undoCount()==0;}});heartbeatTimer.start();fixture.begin();fixture.wait();heartbeatTimer.stop();require(observations>0&&privateState,"UI timer dispatches after worker progress is visible while canonical state stays unchanged");fixture.completed();}
void busy_guards(){Fixture fixture(true);auto* tabs=fixture.window->findChild<QTabWidget*>();require(tabs,"Project tabs exist");auto& other=fixture.window->addProject(document(),"Other project");const auto otherBefore=other.document;tabs->setCurrentIndex(0);QTest::qWait(10);fixture.errors.clear();fixture.begin();require(!action(*fixture.window,"file.close")->isEnabled()&&!action(*fixture.window,"layer.merge")->isEnabled()&&!action(*fixture.window,"edit.undo")->isEnabled(),"Close, repeat merge and Undo are disabled during merge");tabs->setCurrentIndex(1);require(tabs->currentIndex()==0&&fixture.window->canvas()==fixture.owner->canvas,"Busy source tab cannot be switched away");fixture.wait();fixture.completed();require(other.document==otherBefore&&other.history.undoCount()==0,"Other project is untouched");}
void stale_document(){Fixture fixture(true);fixture.begin();fixture.owner->document->layers.front().name="Intervening state";const auto intervening=fixture.owner->document;fixture.wait();require(fixture.owner->document==intervening&&fixture.owner->active==fixture.active&&fixture.owner->selected==fixture.selected&&fixture.owner->history.undoCount()==0,"Stale worker result cannot overwrite intervening canonical state or record history");require(fixture.errors.size()==1,"Stale result reports one owned error");}
void destroy_pending(){Fixture fixture(true);fixture.begin();QPointer<QObject> page=fixture.owner->canvas,job=fixture.job();fixture.dismiss.stop();fixture.window.reset();require(!page&&!job,"Window destruction removes merge worker and canvas owner");QTest::qWait(30);}
void destroy_running(){Fixture fixture(true);fixture.begin();QElapsedTimer deadline;deadline.start();while(fixture.owner->projectBusy&&(!fixture.progress()||!fixture.progress()->isVisible())){QTest::qWait(1);require(deadline.elapsed()<10000,"Worker dispatch produces visible progress");}require(fixture.owner->projectBusy&&fixture.job()&&fixture.progress()&&fixture.progress()->isVisible(),"Destroy running fixture has dispatched pending worker");QPointer<QObject> page=fixture.owner->canvas,job=fixture.job();fixture.dismiss.stop();fixture.window.reset();require(!page&&!job,"Window destruction joins dispatched worker and removes canvas owner");QTest::qWait(30);}
void failure_recovers(){Fixture fixture(false,true);fixture.begin();fixture.wait();fixture.unchanged();require(fixture.errors.size()==1,"Working-budget failure reports one owned error");require(action(*fixture.window,"layer.merge")->isEnabled(),"Failure restores editable command state");}
}
int main(int argc,char** argv){QApplication app(argc,argv);std::cout<<std::unitbuf;QDir().mkpath(QFileInfo(QString::fromUtf8(__FILE__)).dir().filePath("fixtures"));const std::map<std::string,void(*)()> cases{{"async_commit",async_commit},{"cancel_button",cancel_button},{"cancel_retry",cancel_retry},{"heartbeat",heartbeat},{"busy_guards",busy_guards},{"stale_document",stale_document},{"destroy_pending",destroy_pending},{"destroy_running",destroy_running},{"failure_recovers",failure_recovers}};try{require(argc==2&&cases.contains(argv[1]),"Provide named async-merge UI case");std::cout<<"START "<<argv[1]<<'\n';cases.at(argv[1])();std::cout<<"PASS "<<argv[1]<<'\n';return 0;}catch(const std::exception& error){std::cerr<<"FAIL "<<(argc>1?argv[1]:"argument")<<": "<<error.what()<<'\n';return 1;}}
