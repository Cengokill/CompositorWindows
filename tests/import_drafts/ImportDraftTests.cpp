// Prospective source-derived cases. Not configured or executed at creation.
#include "ui/MainWindow.h"
#include "ui/ImportActions.h"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QFocusEvent>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QMouseEvent>
#include <QSaveFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
using namespace compositor;
namespace {
void require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
QAction* command(MainWindow& window,const char* id){
    for(auto* action:window.findChildren<QAction*>())
        if(action->property("commandId").toString()==id&&action->isVisible())return action;
    throw std::runtime_error(std::string("Visible command absent: ")+id);
}
void trigger(MainWindow& window,const char* id){auto* action=command(window,id);require(action->isEnabled(),"Command enabled in this source state");action->trigger();}
Document document(){Document d;d.id=newId();d.width=100;d.height=80;Layer layer;layer.id=newId();layer.name="Original";layer.transform={0,0,100,80};layer.raster=Raster::filled(100,80,{70,100,150,255});d.layers={layer};return d;}
struct Fixture {
    QTemporaryDir directory{QFileInfo(QString::fromUtf8(__FILE__)).dir().filePath("fixtures/import-XXXXXX")};
    std::unique_ptr<MainWindow> window;
    EditorProject* project{};
    QString path;
    int errors{};
    QTimer dismiss;
    Fixture(){
        require(directory.isValid(),"Owned fixture directory");directory.setAutoRemove(false);
        QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,directory.path());
        path=directory.filePath("Imported.png");QImage image(8,6,QImage::Format_RGBA8888);image.fill(QColor(220,40,10));require(image.save(path,"PNG"),"Valid import PNG");
        window=std::make_unique<MainWindow>(true);project=&window->addProject(document(),"Import draft fixture");window->resize(1100,800);window->show();window->activateWindow();project->canvas->setFocus();QTest::qWait(20);
        dismiss.setInterval(5);QObject::connect(&dismiss,&QTimer::timeout,window.get(),[this]{for(auto* box:window->findChildren<QMessageBox*>())if(box->isVisible()){++errors;box->reject();}});dismiss.start();
        std::cout<<"fixture="<<directory.path().toStdString()<<'\n';
    }
    ~Fixture(){dismiss.stop();window.reset();}
    void send(QEvent::Type type,Point point){auto* canvas=project->canvas;const auto view=canvas->viewMapping().toView(point);const QPointF at(view.x,view.y);const bool move=type==QEvent::MouseMove,up=type==QEvent::MouseButtonRelease;QMouseEvent event(type,at,canvas->mapToGlobal(at.toPoint()),move?Qt::NoButton:Qt::LeftButton,up?Qt::NoButton:Qt::LeftButton,{});QApplication::sendEvent(canvas,&event);}
    void click(Point point){send(QEvent::MouseButtonPress,point);send(QEvent::MouseButtonRelease,point);}
    void drag(Point a,Point b,bool finish=true){send(QEvent::MouseButtonPress,a);send(QEvent::MouseMove,b);if(finish)send(QEvent::MouseButtonRelease,b);}
    void picker(bool accept,const std::function<void()>& whileOpen={}){
        bool seen=false,owned=false;QString failure;QElapsedTimer elapsed;elapsed.start();QTimer driver;driver.setInterval(5);
        QObject::connect(&driver,&QTimer::timeout,window.get(),[&]{
            QPointer<QFileDialog> file;for(auto* candidate:window->findChildren<QFileDialog*>())if(candidate->isVisible()){file=candidate;break;}
            if(!file)return;
            if(elapsed.elapsed()>10000){failure="Owned import picker timed out";file->reject();driver.stop();return;}
            if(seen)return;seen=true;owned=file->parentWidget()==window.get()&&QApplication::activeModalWidget()==file;
            try{file->activateWindow();file->setFocus();QTest::qWait(20);auto* focused=QApplication::focusWidget();require(file&&file->isActiveWindow()&&!project->canvas->hasFocus()&&(focused==file||file->isAncestorOf(focused)),"Owned import picker has keyboard focus before observing draft state");if(whileOpen)whileOpen();if(accept){file->selectFile(path);static_cast<QDialog*>(file.data())->accept();}else file->reject();}
            catch(const std::exception& error){failure=QString::fromUtf8(error.what());if(file)file->reject();}driver.stop();
        });
        driver.start();trigger(*window,"file.import");driver.stop();
        require(seen&&owned,"Actual File > Import used an owned modal QFileDialog");if(!failure.isEmpty())throw std::runtime_error(failure.toStdString());
        elapsed.restart();for(;;){QTest::qWait(5);auto* queue=ui::ImportQueue::find(window.get());if((!queue||queue->idle())&&!project->importing)break;require(elapsed.elapsed()<10000,"Import completed without pending-draft deadlock");}
        require(errors==0,"Import generated no error dialog");
    }
    void imported()const{require(project->document&&project->document->layers.size()==2,"One image appended to captured document");const auto& image=project->document->layers.back();require(image.name=="Imported"&&image.raster&&image.raster->width==8&&image.raster->height==6&&image.transform.x==46&&image.transform.y==37&&project->active==image.id,"Source append, centered placement and active image");}
    void gradient(){trigger(*window,"tool.gradient");drag({10,30},{80,30});require(bool(project->gradientPreview)&&command(*window,"gradient.apply")->isEnabled(),"Pending gradient fixture");}
    void polygon(){trigger(*window,"tool.polygon");click({10,10});click({80,10});click({80,60});require(project->canvas->selectionDraft()&&project->canvas->selectionDraft()->points.size()==3,"Three-point polygon draft");}
    void crop(){trigger(*window,"tool.crop");drag({10,10},{70,50});require(project->canvas->cropOverlay()==editing::Rect{10,10,60,40}&&command(*window,"crop.apply")->isEnabled(),"Pending custom crop fixture");}
    void transform(){trigger(*window,"tool.move");trigger(*window,"transform.free");QDoubleSpinBox* x=nullptr;for(auto* field:window->findChildren<QDoubleSpinBox*>())if(field->accessibleName()=="X")x=field;require(x&&x->isEnabled(),"Transform X field");x->setValue(17);project->canvas->setFocus();require(command(*window,"transform.apply")->isEnabled()&&project->document->layers.front().transform.x==17&&project->history.undoCount()==0,"Persistent affine draft is effective but uncommitted");}
};
void gradient_accept(){Fixture f;f.gradient();const auto original=f.project->document->layers.front();const auto preview=f.project->gradientPreview;f.picker(true,[&]{require(f.project->gradientPreview==preview,"Picker focus preserves gradient");});f.imported();std::cout<<"gradient_after="<<bool(f.project->gradientPreview)<<" undo="<<f.project->history.undoCount()<<'\n';require(f.project->gradientPreview==preview&&f.project->document->layers.front()==original&&f.project->history.undoCount()==1,"Import preserves unapplied gradient and commits only image import");trigger(*f.window,"gradient.cancel");f.imported();require(!f.project->gradientPreview&&f.project->history.undoCount()==1,"Explicit gradient Cancel retains imported image/history");}
void gradient_picker_cancel(){Fixture f;f.gradient();const auto before=f.project->document;const auto preview=f.project->gradientPreview;f.picker(false);require(f.project->document==before&&f.project->gradientPreview==preview&&f.project->history.undoCount()==0,"Picker Cancel preserves gradient without import/history");}
void polygon_accept(){Fixture f;f.polygon();const auto points=f.project->canvas->selectionDraft()->points;f.picker(true,[&]{require(f.project->canvas->selectionDraft()&&f.project->canvas->selectionDraft()->points==points,"Picker focus preserves polygon");});f.imported();require(f.project->canvas->selectionDraft()&&f.project->canvas->selectionDraft()->points==points&&!f.project->document->selection&&f.project->history.undoCount()==1,"Import keeps exact polygon draft and only import history");f.project->canvas->setFocus();QTest::keyClick(f.project->canvas,Qt::Key_Return);require(!f.project->canvas->selectionDraft()&&f.project->document->selection&&f.project->document->selection->coverage->pixel(60,20)>0&&f.project->history.undoCount()==2&&f.project->history.undoName()=="Polygonal Lasso","Retained polygon completes once after import");trigger(*f.window,"edit.undo");f.imported();require(!f.project->document->selection&&f.project->history.undoCount()==1,"Selection Undo retains the separate imported image");}
void polygon_picker_cancel(){Fixture f;f.polygon();const auto points=f.project->canvas->selectionDraft()->points;const auto before=f.project->document;f.picker(false);require(f.project->document==before&&f.project->canvas->selectionDraft()&&f.project->canvas->selectionDraft()->points==points&&f.project->history.undoCount()==0,"Picker Cancel keeps polygon draft without history");}
void crop_accept(){Fixture f;f.crop();const auto before=f.project->document->layers.front();f.picker(true,[&]{require(f.project->canvas->cropOverlay()==editing::Rect{10,10,60,40},"Picker focus stops crop drag but retains rectangle");});f.imported();require(f.project->document->width==100&&f.project->document->height==80&&f.project->document->layers.front()==before&&!command(*f.window,"crop.apply")->isEnabled()&&f.project->history.undoCount()==1,"Import cancels crop without applying crop or extra history");require(f.project->canvas->cropOverlay()==editing::Rect{0,0,100,80},"Crop tool shows source full-canvas fallback after cancellation");}
void crop_picker_cancel(){Fixture f;f.crop();const auto before=f.project->document;f.picker(false);require(f.project->document==before&&f.project->canvas->cropOverlay()==editing::Rect{10,10,60,40}&&command(*f.window,"crop.apply")->isEnabled()&&f.project->history.undoCount()==0,"Picker Cancel does not invoke import crop cancellation");}
void transform_accept(){Fixture f;const auto before=f.project->document;f.transform();f.picker(true,[&]{require(f.project->document->layers.front().transform.x==17&&f.project->history.undoCount()==0,"Picker focus preserves effective uncommitted persistent transform");require(!command(*f.window,"transform.apply")->isEnabled(),"Modal picker blocks the background Apply command");});f.imported();require(f.project->document->layers.front().transform.x==17&&!command(*f.window,"transform.apply")->isEnabled()&&f.project->history.undoCount()==2,"Import commits persistent transform before separate image edit");trigger(*f.window,"edit.undo");require(f.project->document->layers.size()==1&&f.project->document->layers.front().transform.x==17&&f.project->history.undoCount()==1,"First Undo removes import only");trigger(*f.window,"edit.undo");require(f.project->document==before&&f.project->history.undoCount()==0,"Second Undo restores pretransform source");}
void transform_picker_cancel(){Fixture f;const auto before=f.project->document;f.transform();f.picker(false);require(f.project->document->layers.front().transform.x==17&&command(*f.window,"transform.apply")->isEnabled()&&f.project->history.undoCount()==0,"Picker Cancel retains persistent transform draft");trigger(*f.window,"transform.cancel");require(f.project->document==before,"Explicit transform Cancel restores original");}
void brush_focus_cancel(){Fixture f;trigger(*f.window,"tool.brush");f.project->canvas->setFocus();f.drag({20,20},{45,20},false);const auto before=f.project->document->layers.front();require(bool(f.project->brushPreview),"Unfinished brush fixture");f.picker(true,[&]{require(!f.project->brushPreview,"Source picker focus loss cancels unfinished brush");});f.imported();require(f.project->document->layers.front()==before&&f.project->history.undoCount()==1,"Cancelled brush contributes no edit before import");}
void freehand_focus_cancel(){Fixture f;trigger(*f.window,"tool.lasso");f.drag({10,10},{70,50},false);require(f.project->canvas->selectionDraft().has_value(),"Unfinished freehand lasso fixture");f.picker(true,[&]{require(!f.project->canvas->selectionDraft(),"Source picker focus loss cancels nonpolygon lasso");});f.imported();require(!f.project->document->selection&&f.project->history.undoCount()==1,"Cancelled freehand contributes no selection edit");}
}
int main(int argc,char** argv){
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);QApplication app(argc,argv);std::cout<<std::unitbuf;QDir().mkpath(QFileInfo(QString::fromUtf8(__FILE__)).dir().filePath("fixtures"));
    const std::map<std::string,void(*)()> cases{{"gradient_accept",gradient_accept},{"gradient_picker_cancel",gradient_picker_cancel},{"polygon_accept",polygon_accept},{"polygon_picker_cancel",polygon_picker_cancel},{"crop_accept",crop_accept},{"crop_picker_cancel",crop_picker_cancel},{"transform_accept",transform_accept},{"transform_picker_cancel",transform_picker_cancel},{"brush_focus_cancel",brush_focus_cancel},{"freehand_focus_cancel",freehand_focus_cancel}};
    try{require(argc==2&&cases.contains(argv[1]),"Provide named import-draft case");std::cout<<"START "<<argv[1]<<'\n';cases.at(argv[1])();std::cout<<"PASS "<<argv[1]<<'\n';return 0;}catch(const std::exception& error){std::cerr<<"FAIL "<<(argc>1?argv[1]:"argument")<<": "<<error.what()<<'\n';return 1;}
}
