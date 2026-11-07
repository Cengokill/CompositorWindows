#include "ui/MainWindow.h"
#include <QApplication>
#include <QKeyEvent>
#include <QSettings>
#include <QTemporaryDir>
#include <QToolButton>
#include <functional>
#include <iostream>
#include <stdexcept>
using namespace compositor;
namespace {
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void closeEnough(double a,double b,const char* message){require(std::abs(a-b)<1e-7,message);}
void key(MainWindow& window,int code,Qt::KeyboardModifiers modifiers=Qt::NoModifier){QKeyEvent event(QEvent::KeyPress,code,modifiers);QApplication::sendEvent(&window,&event);}
template<class T>T* control(MainWindow& window,const char* name){auto* value=window.findChild<T*>(name);require(value!=nullptr,"Brush pointer control missing");return value;}
Document example(){Document document;document.id=newId();document.width=64;document.height=48;Layer layer;layer.id=newId();layer.transform={0,0,64,48};layer.raster=Raster::filled(64,48,{0,0,255,255});document.layers.push_back(layer);return document;}
QList<QDoubleSpinBox*> brushTip(MainWindow& window){auto* bar=control<QToolBar>(window,"brushOptions");auto controls=bar->findChildren<QDoubleSpinBox*>();require(controls.size()==3,"Brush settings controls differ");return controls;}
void callbacks(NativeCanvas& canvas){require(bool(canvas.rightPointerDown)&&bool(canvas.rightPointerMove)&&bool(canvas.pointerHover)&&bool(canvas.pointerLeave)&&bool(canvas.navigationAllowed),"Root brush pointer callback missing");}
}
int main(int argc,char** argv){QApplication app(argc,argv);QTemporaryDir preferences;
    if(!preferences.isValid())return 2;
    QCoreApplication::setOrganizationName("CompositorFixture");
    QCoreApplication::setApplicationName("BrushPointer");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,preferences.path());
    QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,preferences.path());
    int passed=0,failed=0;auto test=[&](const char* name,const std::function<void()>& run){try{run();++passed;std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& error){++failed;std::cout<<"FAIL "<<name<<": "<<error.what()<<'\n';}};
    test("brush_pointer_right_drag_size_hardness_original_baseline",[]{MainWindow window(true);auto& project=window.addProject(example());auto& canvas=*project.canvas;callbacks(canvas);key(window,Qt::Key_B);auto tip=brushTip(window);tip[0]->setValue(40);tip[1]->setValue(25);canvas.zoomAt(canvas.devicePixelRatioF()*2,{200,150});const auto original=project.document;require(canvas.rightPointerDown({100,100},{}),"Brush right drag refused");canvas.rightPointerMove({120,900},{},false);closeEnough(tip[0]->value(),60,"Size did not follow pointer in DIPs");closeEnough(tip[1]->value(),25,"Vertical movement changed hardness");canvas.rightPointerMove({140,100},Qt::ShiftModifier,false);closeEnough(tip[0]->value(),40,"Shift failed to restore press diameter");closeEnough(tip[1]->value(),45,"Hardness not original plus dx/200");canvas.rightPointerMove({140,-900},{},false);closeEnough(tip[0]->value(),80,"Modifier switch accumulated previous diameter");closeEnough(tip[1]->value(),25,"Size mode failed to restore press hardness");canvas.rightPointerMove({150,100},{},true);require(project.document==original&&project.history.undoCount()==0,"Tip gesture changed document/history");});
    test("brush_pointer_limits_and_tool_tip_families",[]{MainWindow window(true);auto& project=window.addProject(example());auto& canvas=*project.canvas;callbacks(canvas);key(window,Qt::Key_B);auto tip=brushTip(window);tip[0]->setValue(40);canvas.zoomAt(canvas.devicePixelRatioF(),{200,150});require(canvas.rightPointerDown({100,100},{}),"Size press refused");canvas.rightPointerMove({-10000,100},{},false);closeEnough(tip[0]->value(),1,"Minimum diameter");canvas.rightPointerMove({10000,100},{},false);closeEnough(tip[0]->value(),2000,"Maximum diameter");canvas.rightPointerMove({10000,100},{},true);control<QAction>(window,"retouchClone")->trigger();control<QDoubleSpinBox>(window,"retouchDiameter")->setValue(18);require(canvas.rightPointerDown({100,100},{}),"Clone tip press refused");canvas.rightPointerMove({108,100},{},false);closeEnough(control<QDoubleSpinBox>(window,"retouchDiameter")->value(),34,"Clone family tip unchanged");canvas.rightPointerMove({108,100},{},true);key(window,Qt::Key_B);closeEnough(tip[0]->value(),2000,"Clone tip contaminated Brush family");});
    test("brush_pointer_hover_leave_space_and_interruption",[]{MainWindow window(true);auto& project=window.addProject(example());auto& canvas=*project.canvas;callbacks(canvas);key(window,Qt::Key_B);canvas.pointerHover({100,100},{});require(canvas.cursor().shape()==Qt::BlankCursor,"Brush native pointer not hidden behind circle");canvas.pointerLeave();require(canvas.cursor().shape()!=Qt::BlankCursor,"Brush cursor survived leave");canvas.pointerHover({100,100},{});key(window,Qt::Key_Space);require(!canvas.rightPointerDown({100,100},{}),"Space allowed a tip edit");canvas.pointerInterrupted();require(canvas.rightPointerDown({100,100},{}),"Interruption did not clear Space");canvas.pointerInterrupted();require(canvas.cursor().shape()!=Qt::BlankCursor,"Interrupted brush cursor remained hidden");});
    test("brush_pointer_busy_raster_and_transform_navigation_guard",[]{MainWindow window(true);auto& project=window.addProject(example());auto& canvas=*project.canvas;callbacks(canvas);key(window,Qt::Key_B);project.canvas->pointerDown({20,20},{});require(!canvas.navigationAllowed(),"Navigation allowed during brush");require(!canvas.rightPointerDown({100,100},{}),"Tip changed during captured brush");project.canvas->pointerCancel();require(canvas.navigationAllowed(),"Brush cancellation retained navigation lock");key(window,Qt::Key_V);canvas.zoomAt(10,{200,150});control<QCheckBox>(window,"transformSnapping")->setChecked(false);canvas.pointerDown({32,24},{});require(!canvas.navigationAllowed(),"Navigation allowed during transform drag");canvas.pointerCancel();require(canvas.navigationAllowed(),"Transform cancel retained navigation lock");});
    test("animated_layer_brush_origin",[]{
        for(bool warp:{false,true}){
            Document d;d.id=newId();d.width=360;d.height=360;
            Layer l;l.id=newId();l.transform={0,0,360,360};l.raster=Raster::filled(360,360,{50,60,70,255});
            l.animation=GifAnimation{{{l.raster,1000},{l.raster,1000}},0};d.layers.push_back(l);
            MainWindow window(warp);auto& project=window.addProject(d);auto& canvas=*project.canvas;key(window,Qt::Key_B);
            require(bool(canvas.viewportProvider),"Missing viewport provider");
            auto checkOrigin=[&]{auto frame=canvas.viewportProvider(0,0,360,360,1);require(frame.raster&&frame.raster->pixel(0,0)==Pixel{50,60,70,255},"Brush preview painted origin");};
            checkOrigin();canvas.pointerDown({180,180},{});checkOrigin();
            for(int i=1;i<=12;++i){canvas.pointerMove({180.+i,180.+i},{});checkOrigin();}
            canvas.pointerUp({192,192},{});checkOrigin();
            require(project.document->layers[0].raster->pixel(0,0)==Pixel{50,60,70,255},"Brush committed a mark at origin");
        }
    });
    test("pen_pressure_controls_history_and_mouse_fallback",[]{
        MainWindow window(true);
        auto& project=window.addProject(example());
        const auto original=project.document;
        key(window,Qt::Key_B);
        auto* size=control<QToolButton>(window,"brushPressureSize");
        auto* opacity=control<QToolButton>(window,"brushPressureOpacity");
        require(!size->isChecked()&&!opacity->isChecked(),"Pressure defaults must be off");
        size->click();
        require(size->isChecked()&&!opacity->isChecked(),"Toggling size changed opacity");
        opacity->click();
        brushTip(window)[0]->setValue(16);
        auto& canvas=*project.canvas;
        const QPointF point(24.5,24.5);
        canvas.pointerDown({point,.25},{});
        require(project.brushPreview&&!size->isEnabled()&&!opacity->isEnabled(),"Pressure controls remained editable during the stroke");
        canvas.pointerCancel();
        require(!project.brushPreview&&project.document==original&&project.history.undoCount()==0,"Cancelling pen stroke changed the project");
        canvas.pointerDown({point,.25},{});
        canvas.pointerUp({point,.25},{});
        require(project.history.undoCount()==1&&!project.brushPreview,"Pen stroke did not create exactly one undo entry");
        const auto painted=project.document;
        const auto raster=painted->layers[0].raster;
        require(raster->pixel(24,24).b==191&&raster->pixel(29,24).b==255,"Pen did not honor both pressure toggles");
        auto command=[&](const QString& id){
            for(auto* action:window.findChildren<QAction*>())if(action->property("commandId").toString()==id){
                require(action->isEnabled(),"History command disabled");
                action->trigger();return;
            }
            throw std::runtime_error("Missing history command");
        };
        command("edit.undo");
        require(project.document==original,"Undo did not restore original pixels");
        command("edit.redo");
        require(project.document==painted,"Redo did not restore pen pixels");
        canvas.pointerDown({42.5,24.5},{});
        canvas.pointerUp({42.5,24.5},{});
        require(project.document->layers[0].raster->pixel(47,24).b==0,"Mouse did not use full size and opacity");
        MainWindow reopened(true);
        reopened.addProject(example());
        require(control<QToolButton>(reopened,"brushPressureSize")->isChecked()&&
                control<QToolButton>(reopened,"brushPressureOpacity")->isChecked(),"Pressure preferences were not restored");
    });
    std::cout<<"{\"suite\":\"brush_pointer_ui\",\"passed\":"<<passed<<",\"failed\":"<<failed<<",\"mac_differential\":false,\"visual_acceptance\":false}\n";return failed?1:0;
}
