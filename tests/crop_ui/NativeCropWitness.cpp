// Windows presentation witness for source TransformOverlay.drawCrop.
// Geometry/alpha predicates are frozen before repair; no Mac raster oracle.
#include "ui/MainWindow.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <cmath>
#include <filesystem>
#include <iostream>
using namespace compositor;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void pump(int duration){QElapsedTimer timer;timer.start();while(timer.elapsed()<duration){QApplication::processEvents();QThread::msleep(5);}}
QAction* command(MainWindow&w,const QString&id){for(auto*a:w.findChildren<QAction*>())if(a->property("commandId").toString()==id)return a;return nullptr;}
QPoint position(const NativeCanvas&c,Point p){const auto v=c.viewMapping().toView(p);return {int(std::floor(v.x*c.devicePixelRatioF())),int(std::floor(v.y*c.devicePixelRatioF()))};}
QColor pixel(const NativeCanvas&c,const QImage&i,Point p){const auto at=position(c,p);require(i.rect().contains(at),"Sample outside framebuffer");return i.pixelColor(at);}
QJsonArray rgba(QColor c){return {c.red(),c.green(),c.blue(),c.alpha()};}
QColor dim(QColor c){return {int(std::lround(c.red()*.4)),int(std::lround(c.green()*.4)),int(std::lround(c.blue()*.4)),255};}
bool equal(QColor a,QColor b){return std::abs(a.red()-b.red())<=1&&std::abs(a.green()-b.green())<=1&&std::abs(a.blue()-b.blue())<=1&&std::abs(a.alpha()-b.alpha())<=1;}
void save(const QImage&i,const std::filesystem::path&p){require(i.save(QString::fromStdWString(p.wstring())),"PNG save failed");}
}
int main(int argc,char**argv){QApplication app(argc,argv);if(argc!=2){std::cerr<<"Usage: native_crop_witness <unique-evidence-directory>\n";return 2;}const std::filesystem::path directory(argv[1]);std::filesystem::create_directories(directory);int failed=0;QJsonArray checks;
try{MainWindow w(true);Document d;d.id="crop-native";d.width=100;d.height=80;Layer l;l.id="base";l.name="Base";l.transform={0,0,100,80};l.raster=Raster::filled(100,80,{80,120,160,255});d.layers={l};auto&p=w.addProject(d);w.resize(1180,880);w.show();w.activateWindow();pump(150);auto& c=*p.canvas;c.setDisplayProfileOverride(directory/"deliberately-missing-profile.icc");c.zoomAt(4*c.devicePixelRatioF(),c.rect().center());c.showPixelGrid=false;const auto original=c.captureRendered();require(!c.presentationConvertsColor()&&!c.presentationDiagnostic().isEmpty(),"sRGB fallback not isolated");save(original,directory/"original.png");
auto record=[&](const char*name,QColor actual,QColor expected){const bool pass=equal(actual,expected);failed+=!pass;checks.append(QJsonObject{{"name",name},{"pass",pass},{"actual",rgba(actual)},{"expected",rgba(expected)}});std::cout<<(pass?"PASS ":"FAIL ")<<name<<'\n';};
auto* crop=command(w,"tool.crop");require(crop&&crop->isEnabled(),"Crop tool unavailable");crop->trigger();pump(30);const auto initial=c.captureRendered();save(initial,directory/"initial-fullframe.png");record("crop_fullframe_exterior_dim",pixel(c,initial,{105,40}),dim(pixel(c,original,{105,40})));record("crop_fullframe_interior",pixel(c,initial,{15,15}),pixel(c,original,{15,15}));
c.pointerDown({20,20},Qt::ControlModifier);c.pointerMove({80,60},Qt::ControlModifier);c.pointerUp({80,60},Qt::ControlModifier);pump(30);const auto live=c.captureRendered();save(live,directory/"pending.png");record("crop_pending_exterior_dim",pixel(c,live,{10,10}),dim(pixel(c,original,{10,10})));record("crop_pending_interior",pixel(c,live,{30,30}),pixel(c,original,{30,30}));record("crop_no_rotation_handle",pixel(c,live,{50,14}),dim(pixel(c,original,{50,14})));
const auto guide=position(c,{40,30});QColor brightest=live.pixelColor(guide);for(int dx=-2;dx<=2;++dx){const auto value=live.pixelColor(guide+QPoint(dx,0));if(value.red()>brightest.red())brightest=value;}const auto base=pixel(c,original,{40,30});const bool thirds=brightest.red()-base.red()>=15&&brightest.green()-base.green()>=15&&brightest.blue()-base.blue()>=15;failed+=!thirds;checks.append(QJsonObject{{"name","crop_thirds_vertical"},{"pass",thirds},{"actual",rgba(brightest)},{"base",rgba(base)},{"minimum_channel_increase",15}});std::cout<<(thirds?"PASS ":"FAIL ")<<"crop_thirds_vertical\n";
const bool immutable=p.document==d&&p.history.undoCount()==0;failed+=!immutable;checks.append(QJsonObject{{"name","crop_draft_immutable"},{"pass",immutable}});std::cout<<(immutable?"PASS ":"FAIL ")<<"crop_draft_immutable\n";
c.pointerCancel();pump(30);const auto cancelled=c.captureRendered();save(cancelled,directory/"cancel-fullframe.png");record("crop_cancel_fullframe_exterior_dim",pixel(c,cancelled,{105,40}),dim(pixel(c,original,{105,40})));record("crop_cancel_canvas_restored",pixel(c,cancelled,{10,10}),pixel(c,original,{10,10}));save(w.grab().toImage(),directory/"window.png");
QFile file(QString::fromStdWString((directory/"results.json").wstring()));require(file.open(QIODevice::WriteOnly),"Report open failed");file.write(QJsonDocument(QJsonObject{{"schema","NATIVE_CROP_V1"},{"dpr",c.devicePixelRatioF()},{"tolerance",1},{"failed",failed},{"checks",checks},{"mac_differential",false},{"presentation","Explicit missing ICC -> sRGB fallback"}}).toJson());
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 2;}return failed?1:0;}
