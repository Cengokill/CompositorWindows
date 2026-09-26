// Pinned EditorCanvas.updateBrushCursor/clonePreview and BrushCursorOverlay.
// Uniform interior controls avoid a CoreGraphics interpolation oracle.
#include "ui/MainWindow.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QThread>
#include <cmath>
#include <filesystem>
#include <iostream>
using namespace compositor;
namespace {
void require(bool v,const char*m){if(!v)throw std::runtime_error(m);}
void pump(){QElapsedTimer t;t.start();while(t.elapsed()<30){QApplication::processEvents();QThread::msleep(3);}}
QAction* command(MainWindow&w,const char*id){for(auto*a:w.findChildren<QAction*>())if(a->property("commandId").toString()==id)return a;throw std::runtime_error("Command absent");}
template<class T>T* field(MainWindow&w,const char*name){auto* f=w.findChild<T*>(name);require(f!=nullptr,"Control absent");return f;}
QJsonArray rgba(QColor c){return{c.red(),c.green(),c.blue(),c.alpha()};}
int difference(QColor a,QColor b){return std::max({std::abs(a.red()-b.red()),std::abs(a.green()-b.green()),std::abs(a.blue()-b.blue()),std::abs(a.alpha()-b.alpha())});}
QColor pixel(const NativeCanvas&c,const QImage&i,Point p){auto q=c.viewMapping().toView(p);QPoint at(int(std::floor(q.x*c.devicePixelRatioF())),int(std::floor(q.y*c.devicePixelRatioF())));require(i.rect().contains(at),"Pixel outside canvas");return i.pixelColor(at);}
void hover(NativeCanvas& c,Point p,Qt::KeyboardModifiers modifiers={}){auto q=c.viewMapping().toView(p);c.pointerHover({q.x,q.y},modifiers);pump();}
void key(MainWindow&w,QEvent::Type type,int code){QKeyEvent e(type,code,{});QApplication::sendEvent(&w,&e);pump();}
void save(const QImage&i,const std::filesystem::path&p){require(i.save(QString::fromStdWString(p.wstring())),"PNG save failed");}
}
int main(int argc,char**argv){QApplication app(argc,argv);if(argc!=2)return 2;const std::filesystem::path dir(argv[1]);std::filesystem::create_directories(dir);int failed=0;QJsonArray checks;try{
 MainWindow w(true);Document d;d.id="clone-preview";d.width=128;d.height=64;
 Layer under;under.id="under";under.transform={0,0,128,64};under.raster=Raster::filled(128,64,{30,70,180,255});
 Layer active=under;active.id="active";active.opacity=.25;std::vector<Pixel> pixels(128*64,{30,70,180,255});for(int y=0;y<64;++y)for(int x=0;x<48;++x)pixels[size_t(y)*128+x]={200,50,30,255};active.raster=Raster::fromRgba(128,64,reinterpret_cast<const uint8_t*>(pixels.data()),128*4);
 Layer overlay;overlay.id="overlay";overlay.transform={0,0,48,64};overlay.raster=Raster::filled(48,64,{20,200,80,255});d.layers={under,active,overlay};auto&p=w.addProject(d);p.active=active.id;p.selected={active.id};w.resize(1180,880);w.show();w.activateWindow();command(w,"tool.clone")->trigger();pump();auto&c=*p.canvas;c.setDisplayProfileOverride(dir/"missing.icc");c.zoomAt(4*c.devicePixelRatioF(),c.rect().center());c.showPixelGrid=false;c.setFocus();
 field<QDoubleSpinBox>(w,"retouchDiameter")->setValue(20);field<QDoubleSpinBox>(w,"retouchHardness")->setValue(100);field<QDoubleSpinBox>(w,"retouchOpacity")->setValue(100);
 const Point source{24,32},target{96,32};const QColor currentColor(200,50,30,255),allColor(20,200,80,255);
 auto record=[&](const char*name,bool pass,QJsonObject detail=QJsonObject{}){detail.insert("name",name);detail.insert("pass",pass);checks.append(detail);failed+=!pass;std::cout<<(pass?"PASS ":"FAIL ")<<name<<'\n';};
 hover(c,target);const auto before=c.captureRendered();require(!c.presentationConvertsColor()&&!c.presentationDiagnostic().isEmpty(),"sRGB geometry isolation missing");save(before,dir/"without-source.png");const QColor background=pixel(c,before,target);
 c.pointerDown({source.x,source.y},Qt::AltModifier);c.pointerUp({source.x,source.y},Qt::AltModifier);hover(c,target);const auto current=c.captureRendered();save(current,dir/"current-layer.png");const auto actual=pixel(c,current,target);record("clone_preview_raw_current_layer",difference(actual,currentColor)<=1,{{"actual",rgba(actual)},{"expected",rgba(currentColor)},{"tolerance",1}});
 field<QCheckBox>(w,"retouchAllLayers")->setChecked(true);hover(c,target);const auto all=c.captureRendered();save(all,dir/"all-layers.png");record("clone_preview_all_layers",difference(pixel(c,all,target),allColor)<=1,{{"actual",rgba(pixel(c,all,target))},{"expected",rgba(allColor)},{"tolerance",1}});
 field<QCheckBox>(w,"retouchAllLayers")->setChecked(false);field<QDoubleSpinBox>(w,"retouchOpacity")->setValue(50);hover(c,target);const auto half=c.captureRendered();save(half,dir/"half-opacity.png");const QColor halfColor(int(std::lround((currentColor.red()+background.red())/2.)),int(std::lround((currentColor.green()+background.green())/2.)),int(std::lround((currentColor.blue()+background.blue())/2.)),255);record("clone_preview_tip_opacity",difference(pixel(c,half,target),halfColor)<=1,{{"actual",rgba(pixel(c,half,target))},{"expected",rgba(halfColor)},{"tolerance",1}});
 field<QDoubleSpinBox>(w,"retouchOpacity")->setValue(100);field<QDoubleSpinBox>(w,"retouchHardness")->setValue(0);hover(c,target);const auto soft=c.captureRendered();save(soft,dir/"soft-tip.png");const Point edge{104,32};const int hardShift=difference(pixel(c,current,edge),background),softShift=difference(pixel(c,soft,edge),background);record("clone_preview_soft_tip",hardShift>=30&&softShift>1&&softShift<hardShift-15,{{"hard_difference",hardShift},{"soft_difference",softShift},{"minimum_attenuation",15}});
 hover(c,target,Qt::AltModifier);const auto alt=c.captureRendered();save(alt,dir/"alt.png");record("clone_preview_alt_suppressed",difference(pixel(c,alt,target),background)<=1);
 hover(c,target);c.pointerLeave();pump();const auto leave=c.captureRendered();save(leave,dir/"leave.png");record("clone_preview_leave_suppressed",difference(pixel(c,leave,target),background)<=1);
 hover(c,target);key(w,QEvent::KeyPress,Qt::Key_Space);const auto space=c.captureRendered();save(space,dir/"space.png");record("clone_preview_space_suppressed",difference(pixel(c,space,target),background)<=1);key(w,QEvent::KeyRelease,Qt::Key_Space);
 field<QDoubleSpinBox>(w,"retouchHardness")->setValue(100);field<QDoubleSpinBox>(w,"retouchOpacity")->setValue(50);hover(c,target);c.pointerDown({target.x,target.y},{});pump();const auto activeStroke=c.captureRendered();save(activeStroke,dir/"active-stroke.png");c.pointerLeave();pump();const auto strokeWithoutCursor=c.captureRendered();save(strokeWithoutCursor,dir/"active-stroke-no-cursor.png");record("clone_preview_active_stroke_suppressed",difference(pixel(c,activeStroke,target),pixel(c,strokeWithoutCursor,target))<=1&&p.document==d&&p.history.undoCount()==0);c.pointerCancel();
 record("clone_preview_canonical_immutable",p.document==d&&p.history.undoCount()==0);
 QFile f(QString::fromStdWString((dir/"results.json").wstring()));require(f.open(QIODevice::WriteOnly),"Report failed");f.write(QJsonDocument(QJsonObject{{"schema","NATIVE_CLONE_PREVIEW_V1"},{"checks",checks},{"failed",failed},{"dpr",c.devicePixelRatioF()},{"mac_differential",false}}).toJson());
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 2;}return failed?1:0;}
