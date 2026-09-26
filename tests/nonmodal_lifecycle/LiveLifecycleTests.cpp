#include "ui/MainWindow.h"
#include "ui/EditPanelSession.h"
#include "ui/AdjustmentAdvancedControls.h"
#include "effects/Adjustments.h"
#include "filters/PixelFilters.h"
#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QPushButton>
#include <QJsonDocument>
#include <QTest>
#include <QTimer>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <map>
#include <QTemporaryDir>
#include <QFileDialog>
#include <QImage>
#include "persistence/ProjectStore.h"
#include "ui/ImportActions.h"

using namespace compositor;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Fn>void wait(Fn predicate,const char* message){QElapsedTimer elapsed;elapsed.start();while(!predicate()){if(elapsed.elapsed()>6000)throw std::runtime_error(message);QApplication::processEvents(QEventLoop::AllEvents,20);QTest::qWait(1);}}
QAction* action(MainWindow& window,const char* id){for(auto* value:window.findChildren<QAction*>())if(value->property("commandId")==id)return value;throw std::runtime_error(std::string("missing action ")+id);}
void trigger(MainWindow& window,const char* id){auto* value=action(window,id);require(value->isEnabled(),"requested action enabled");value->trigger();}
template<class T>T* control(QObject& owner,const char* name){for(auto* object:owner.findChildren<QObject*>())if(object->objectName()==name)if(auto* result=dynamic_cast<T*>(object))return result;throw std::runtime_error("named panel control");}
QDoubleSpinBox* field(QDialog& panel,const QString& name){for(auto* value:panel.findChildren<QDoubleSpinBox*>())if(value->accessibleName()==name)return value;throw std::runtime_error("named number field");}
QPushButton* apply(QDialog& panel){auto* buttons=panel.findChild<QDialogButtonBox*>();require(buttons,"panel buttons");auto* result=buttons->button(QDialogButtonBox::Apply);require(result,"panel Apply");return result;}
void ready(QDialog& panel){wait([&]{return apply(panel)->isEnabled();},"preview did not become ready");}
Document fixture(Pixel pixel={80,100,120,255}){Document d;d.id=newId();d.width=64;d.height=48;Layer l;l.id=newId();l.name="Pixels";l.transform={16,12,32,24};l.raster=Raster::filled(32,24,pixel);d.layers={l};return d;}
struct Fixture {MainWindow window{true};EditorProject& project;Document before;
    explicit Fixture(Document d=fixture()):project(window.addProject(d)),before(std::move(d)){window.show();QApplication::processEvents();}
    Pixel visible(int x=30,int y=20){auto view=project.canvas->viewportProvider(0,0,64,48,1);require(bool(view.raster),"main canvas raster");return view.raster->pixel(x,y);}
    void unchanged(){require(project.document==before&&project.history.undoCount()==0&&!project.history.modified(),"draft changed canonical document/history");}
    void click(Point point){auto view=project.canvas->viewMapping().toView(point);QTest::mouseClick(project.canvas,Qt::LeftButton,{},QPoint(int(view.x),int(view.y)));QApplication::processEvents();}
};
template<class Fn>void panel(MainWindow& window,const char* command,const QString& title,Fn body){
    QEventLoop loop;QTimer poll;poll.setInterval(10);QElapsedTimer elapsed;elapsed.start();std::exception_ptr failure;bool visited=false;
    QObject::connect(&poll,&QTimer::timeout,&window,[&]{QPointer<QDialog> found;for(auto* candidate:window.findChildren<QDialog*>())if(candidate->isVisible()&&candidate->windowTitle()==title){found=candidate;break;}
        if(!found){if(elapsed.elapsed()<6000)return;poll.stop();failure=std::make_exception_ptr(std::runtime_error("owned editor panel did not open"));if(auto* modal=qobject_cast<QDialog*>(QApplication::activeModalWidget()))modal->reject();loop.quit();return;}
        poll.stop();visited=true;try{body(*found);}catch(...){failure=std::current_exception();}if(found&&found->isVisible())found->reject();loop.quit();});
    poll.start();QTimer::singleShot(0,&window,[&]{try{trigger(window,command);}catch(...){failure=std::current_exception();poll.stop();loop.quit();}});loop.exec();if(failure)std::rethrow_exception(failure);require(visited,"actual editor panel visited");QApplication::processEvents();
}


bool neutralExposure(const std::string& json){const auto settings=QJsonDocument::fromJson(QByteArray::fromStdString(json)).object()["exposureSettings"].toObject();return settings.value("exposure").toDouble(0)==0&&settings.value("offset").toDouble(0)==0&&settings.value("gamma").toDouble(1)==1;}
void created_layer_survives_cancel(){Fixture f;panel(f.window,"adjust.new.exposure","Exposure",[&](QDialog& p){ready(p);require(f.project.document->layers.size()==2&&f.project.history.undoCount()==1,"LayerAdjustment87-109 creates the new layer as one edit before opening controls");field(p,"Exposure")->setValue(1);ready(p);p.reject();});require(f.project.document->layers.size()==2&&f.project.history.undoCount()==1,"Cancel retains newly created layer and its creation edit");require(neutralExposure(f.project.document->layers.back().adjustmentJson),"Cancel restores new layer default settings");}
void created_layer_apply_separate_edit(){Fixture f;panel(f.window,"adjust.new.exposure","Exposure",[&](QDialog& p){field(p,"Exposure")->setValue(1);ready(p);QPointer<QDialog> guard=&p;apply(p)->click();wait([guard]{return !guard||!guard->isVisible();},"live Apply completed");});require(f.project.document->layers.size()==2&&f.project.history.undoCount()==2,"Live layer creation and settings Apply are separate source history edits");f.window.activateWindow();f.project.canvas->setFocus();QApplication::processEvents();trigger(f.window,"edit.undo");require(f.project.document->layers.size()==2&&neutralExposure(f.project.document->layers.back().adjustmentJson),"First Undo restores new-layer defaults without deleting layer");}
void existing_edit_blocks_document_history(){auto d=fixture();Layer l;l.id=newId();l.name="Exposure";l.transform={0,0,64,48};l.adjustmentJson=effects::defaultAdjustmentJson("Exposure");d.layers.push_back(l);Fixture f(d);f.project.history.begin("Rename base",f.project.document,f.project.active);f.project.document->layers[0].name="Renamed";f.project.history.end(f.project.document,f.project.active);trigger(f.window,"tool.move");panel(f.window,"adjust.edit","Exposure",[&](QDialog& p){ready(p);f.window.activateWindow();f.project.canvas->setFocus();QApplication::processEvents();require(!action(f.window,"edit.undo")->isEnabled(),"AdjustmentEditing49 pending transaction blocks document Undo");});}
void imported_layer_survives(bool commit){Fixture f;QTemporaryDir directory;require(directory.isValid(),"live import fixture directory");const auto path=directory.filePath("new.png");QImage image(4,4,QImage::Format_RGBA8888);image.fill(QColor(12,34,220));require(image.save(path),"live import PNG saved");std::string adjustmentId;
    panel(f.window,"adjust.new.exposure","Exposure",[&](QDialog& p){field(p,"Exposure")->setValue(1);ready(p);adjustmentId=f.project.active;
        QTimer picker;picker.setInterval(10);QObject::connect(&picker,&QTimer::timeout,&f.window,[&]{if(auto* file=qobject_cast<QFileDialog*>(QApplication::activeModalWidget())){file->selectFile(path);static_cast<QDialog*>(file)->accept();picker.stop();}});picker.start();trigger(f.window,"file.import");picker.stop();wait([&]{return f.project.document->layers.size()==3;},"explicit import into live adjustment transaction");
        require(p.isVisible()&&!f.project.history.canUndo(),"live transaction remains pending after imported layer");
        if(commit){QPointer<QDialog> guard=&p;apply(p)->click();wait([guard]{return !guard||!guard->isVisible();},"live Apply after import completed");}else p.reject();
    });
    const auto adjustment=std::find_if(f.project.document->layers.begin(),f.project.document->layers.end(),[&](const Layer& layer){return layer.id==adjustmentId;});
    require(f.project.document->layers.size()==3&&adjustment!=f.project.document->layers.end(),"closing live editor preserves accepted imported layer");
    const auto settings=QJsonDocument::fromJson(QByteArray::fromStdString(adjustment->adjustmentJson)).object()["exposureSettings"].toObject();
    require(settings.value("exposure").toDouble()==(commit?1:0),"Apply keeps edited settings; Cancel restores defaults while preserving import");
    require(f.project.history.undoCount()==2,"source pending transaction groups accepted import with editing completion");
    f.window.activateWindow();f.project.canvas->setFocus();QApplication::processEvents();trigger(f.window,"edit.undo");
    require(f.project.document->layers.size()==2&&neutralExposure(f.project.document->layers.back().adjustmentJson),"Undo reverts import/edit transaction and retains created neutral layer");
}
void live_import_cancel(){imported_layer_survives(false);}
void live_import_apply(){imported_layer_survives(true);}
}
int main(int argc,char** argv){QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);QApplication app(argc,argv);const std::map<std::string,void(*)()> cases{{"created_layer_survives_cancel",created_layer_survives_cancel},{"created_layer_apply_separate_edit",created_layer_apply_separate_edit},{"existing_edit_blocks_document_history",existing_edit_blocks_document_history},{"live_import_cancel",live_import_cancel},{"live_import_apply",live_import_apply}};try{require(argc==2&&cases.contains(argv[1]),"provide live lifecycle case");cases.at(argv[1])();std::printf("PASS %s\n",argv[1]);return 0;}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s: %s\n",argc>1?argv[1]:"argument",e.what());return 1;}}
