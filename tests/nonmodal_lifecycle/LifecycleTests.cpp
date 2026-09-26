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

void commitPanel(QDialog& p){QPointer<QDialog> guard=&p;apply(p)->click();wait([guard]{return !guard||!guard->isVisible();},"panel Apply completed");}
void renameHistory(Fixture& f){f.project.history.begin("Rename",f.project.document,f.project.active);f.project.document->layers[0].name="Renamed";f.project.history.end(f.project.document,f.project.active);trigger(f.window,"tool.move");}
void removedTargetHistory(Fixture& f){f.project.history.begin("Add target",f.project.document,f.project.active);auto added=f.project.document->layers[0];added.id=newId();added.name="Added";added.raster=Raster::filled(32,24,{180,20,30,255});f.project.document->layers.push_back(added);f.project.active=added.id;f.project.selected={added.id};f.project.history.end(f.project.document,f.project.active);trigger(f.window,"tool.move");}
void filter_undo_metadata_apply(){Fixture f;renameHistory(f);panel(f.window,"filter.gaussian","Gaussian Blur",[&](QDialog& p){field(p,"Radius")->setValue(3);ready(p);f.window.activateWindow();f.project.canvas->setFocus();QApplication::processEvents();trigger(f.window,"edit.undo");require(f.project.document->layers[0].name=="Pixels"&&p.isVisible(),"Undo restores current metadata and keeps filter panel");commitPanel(p);});require(f.project.document->layers[0].name=="Pixels"&&f.project.document->layers[0].raster->width>32&&f.project.history.undoCount()==1,"Filter Apply commits captured pixels onto current metadata");}
void hue_undo_metadata_apply(){Fixture f;renameHistory(f);panel(f.window,"adjust.hue_saturation","Hue/Saturation",[&](QDialog& p){field(p,"Hue")->setValue(30);ready(p);f.window.activateWindow();f.project.canvas->setFocus();QApplication::processEvents();trigger(f.window,"edit.undo");require(f.project.document->layers[0].name=="Pixels"&&p.isVisible(),"Undo keeps Hue panel and restored metadata");commitPanel(p);});require(f.project.document->layers[0].name=="Pixels"&&f.project.document->layers[0].raster!=f.before.layers[0].raster&&f.project.history.undoCount()==1,"Hue Apply merges current metadata");}
void filter_undo_removed_overlay(){Fixture f;removedTargetHistory(f);panel(f.window,"filter.gaussian","Gaussian Blur",[&](QDialog& p){field(p,"Radius")->setValue(3);ready(p);f.window.activateWindow();f.project.canvas->setFocus();QApplication::processEvents();trigger(f.window,"edit.undo");require(f.project.document->layers.size()==1&&p.isVisible(),"Undo removes captured target without dismissing panel");require(f.visible()==Pixel{80,100,120,255},"Filter preview cannot resurrect removed layers");});}
void hue_undo_removed_overlay(){Fixture f;removedTargetHistory(f);panel(f.window,"adjust.hue_saturation","Hue/Saturation",[&](QDialog& p){field(p,"Hue")->setValue(30);ready(p);f.window.activateWindow();f.project.canvas->setFocus();QApplication::processEvents();trigger(f.window,"edit.undo");require(f.project.document->layers.size()==1&&p.isVisible(),"Undo removes Hue target without dismissing panel");require(f.visible()==Pixel{80,100,120,255},"Hue preview cannot resurrect removed layers");});}
void redo_target_restores_preview(){Fixture f;removedTargetHistory(f);panel(f.window,"adjust.exposure","Exposure",[&](QDialog& p){field(p,"Exposure")->setValue(1);ready(p);auto previewPixel=f.visible();f.window.activateWindow();f.project.canvas->setFocus();QApplication::processEvents();trigger(f.window,"edit.undo");f.window.activateWindow();f.project.canvas->setFocus();QApplication::processEvents();trigger(f.window,"edit.redo");require(f.project.document->layers.size()==2&&f.visible()==previewPixel,"Redo restores target and retained preview");});}
void levels_history_blocked(){Fixture f;renameHistory(f);panel(f.window,"adjust.levels","Levels",[&](QDialog& p){ready(p);require(!action(f.window,"edit.undo")->isEnabled()&&!action(f.window,"file.save")->isEnabled(),"Source Levels blocks history and project save");});}
void save_canonical_filter(){Fixture f;QTemporaryDir dir;require(dir.isValid(),"temporary project directory");f.project.path=dir.filePath("canonical.comp");panel(f.window,"filter.gaussian","Gaussian Blur",[&](QDialog& p){field(p,"Radius")->setValue(3);ready(p);const auto shown=f.visible(16,12);trigger(f.window,"file.save");ProjectStore store(makeWicProjectCodec());auto loaded=store.load(std::filesystem::path(f.project.path.toStdWString()));require(loaded.document.layers[0].raster->rgba()==f.before.layers[0].raster->rgba(),"Save serializes canonical original during filter preview");require(p.isVisible()&&f.visible(16,12)==shown,"Save preserves pending preview and panel");commitPanel(p);});require(f.project.history.modified()&&f.project.history.undoCount()==1,"Later Apply marks saved document modified exactly once");}
void close_blocked_filter(){Fixture f;panel(f.window,"filter.gaussian","Gaussian Blur",[&](QDialog& p){ready(p);require(!action(f.window,"file.close")->isEnabled()&&!action(f.window,"app.exit")->isEnabled(),"Source workspace close/quit disabled during Filter");require(!f.window.close()&&p.isVisible(),"Window close preserves active panel");});}
void stale_pixels_discard_apply(){Fixture f;renameHistory(f);panel(f.window,"adjust.exposure","Exposure",[&](QDialog& p){field(p,"Exposure")->setValue(1);ready(p);f.project.document->layers[0].raster=Raster::filled(32,24,{5,6,7,255});commitPanel(p);});require(f.project.document->layers[0].raster->pixel(0,0)==Pixel{5,6,7,255}&&f.project.history.undoCount()==1,"Changed captured input rejects Apply without overwriting current pixels/history");}
void explicit_import_filter(){Fixture f;QTemporaryDir dir;require(dir.isValid(),"temporary import directory");const auto path=dir.filePath("additional.png");QImage image(4,4,QImage::Format_RGBA8888);image.fill(QColor(10,20,220));require(image.save(path),"PNG fixture saved");panel(f.window,"filter.gaussian","Gaussian Blur",[&](QDialog& p){field(p,"Radius")->setValue(3);ready(p);QTimer picker;picker.setInterval(10);QObject::connect(&picker,&QTimer::timeout,&f.window,[&]{if(auto* file=qobject_cast<QFileDialog*>(QApplication::activeModalWidget())){file->selectFile(path);static_cast<QDialog*>(file)->accept();picker.stop();}});picker.start();trigger(f.window,"file.import");picker.stop();wait([&]{return f.project.document->layers.size()==2;},"Explicit import was blocked by existing filter panel");require(p.isVisible()&&f.visible(31,23)==Pixel{10,20,220,255},"Imported layer appears in current stack while captured filter preview remains");commitPanel(p);});require(f.project.document->layers.size()==2&&f.project.document->layers[0].raster->width>32&&f.project.document->layers[1].raster->width==4&&f.project.history.undoCount()==2,"Filter Apply preserves imported layer and separate history");}
}
int main(int argc,char** argv){QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);QApplication app(argc,argv);const std::map<std::string,void(*)()> cases{
{"filter_undo_metadata_apply",filter_undo_metadata_apply},{"hue_undo_metadata_apply",hue_undo_metadata_apply},{"filter_undo_removed_overlay",filter_undo_removed_overlay},{"hue_undo_removed_overlay",hue_undo_removed_overlay},{"redo_target_restores_preview",redo_target_restores_preview},{"levels_history_blocked",levels_history_blocked},{"save_canonical_filter",save_canonical_filter},{"close_blocked_filter",close_blocked_filter},{"stale_pixels_discard_apply",stale_pixels_discard_apply},{"explicit_import_filter",explicit_import_filter}};try{require(argc==2&&cases.contains(argv[1]),"provide named lifecycle case");cases.at(argv[1])();std::printf("PASS %s\n",argv[1]);return 0;}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s: %s\n",argc>1?argv[1]:"argument",e.what());return 1;}}
