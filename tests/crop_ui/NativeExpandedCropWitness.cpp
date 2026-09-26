// Source EditorCanvas.renderBounds: crop reveals original.union(pendingCrop).
// Fixed Windows same-renderer controls; this is not a Mac raster differential.
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
void require(bool v,const char*m){if(!v)throw std::runtime_error(m);}
void pump(){QElapsedTimer t;t.start();while(t.elapsed()<40){QApplication::processEvents();QThread::msleep(3);}}
QAction* command(MainWindow&w,const char*id){for(auto*a:w.findChildren<QAction*>())if(a->property("commandId").toString()==id)return a;return nullptr;}
QColor sample(const NativeCanvas& c,const QImage& i,Point p){const auto q=c.viewMapping().toView(p);const QPoint at(int(std::floor(q.x*c.devicePixelRatioF())),int(std::floor(q.y*c.devicePixelRatioF())));require(i.rect().contains(at),"Sample outside framebuffer");return i.pixelColor(at);}
QJsonArray rgba(QColor p){return{p.red(),p.green(),p.blue(),p.alpha()};}
int difference(QColor a,QColor b){return std::max({std::abs(a.red()-b.red()),std::abs(a.green()-b.green()),std::abs(a.blue()-b.blue()),std::abs(a.alpha()-b.alpha())});}
void save(const QImage&i,const std::filesystem::path&p){require(i.save(QString::fromStdWString(p.wstring())),"PNG save failed");}
}
int main(int argc,char**argv){QApplication app(argc,argv);if(argc!=2)return 2;const std::filesystem::path dir(argv[1]);std::filesystem::create_directories(dir);QJsonArray checks;int failed=0;try{
 auto record=[&](const char*name,bool pass,QJsonObject data=QJsonObject{}){data.insert("name",name);data.insert("pass",pass);checks.append(data);failed+=!pass;std::cout<<(pass?"PASS ":"FAIL ")<<name<<'\n';};
 MainWindow w(true);Document d;d.id="expanded-crop";d.width=100;d.height=80;Layer l;l.id="base";l.transform={-30,-20,160,120};l.raster=Raster::filled(160,120,{80,120,160,255});d.layers={l};auto&p=w.addProject(d);w.resize(1180,880);w.show();w.activateWindow();pump();auto&c=*p.canvas;c.setDisplayProfileOverride(dir/"missing.icc");c.zoomAt(4*c.devicePixelRatioF(),c.rect().center());c.showPixelGrid=false;
 const auto original=c.captureRendered();save(original,dir/"original.png");require(!c.presentationConvertsColor()&&!c.presentationDiagnostic().isEmpty(),"sRGB isolation missing");auto* crop=command(w,"tool.crop");require(crop&&crop->isEnabled(),"Crop unavailable");crop->trigger();pump();const auto full=c.captureRendered();const auto originalExterior=sample(c,full,{-10,40});
 c.pointerDown({-20,-10},Qt::ControlModifier);c.pointerMove({120,90},Qt::ControlModifier);c.pointerUp({120,90},Qt::ControlModifier);pump();const auto live=c.captureRendered();save(live,dir/"expanded.png");
 const std::array<Point,5> locations{{{-10,40},{110,40},{50,-5},{50,85},{50,40}}};const std::array<const char*,5> names{{"crop_expanded_left","crop_expanded_right","crop_expanded_top","crop_expanded_bottom","crop_original_interior"}};
 for(size_t i=0;i<locations.size();++i){const auto at=locations[i];auto pixel=SoftwareRenderer().renderScaled(d,at.x,at.y,1,1,1)->pixel(0,0);QColor expected(pixel.r,pixel.g,pixel.b,pixel.a),actual=sample(c,live,at);record(names[i],difference(actual,expected)<=1,{{"actual",rgba(actual)},{"expected",rgba(expected)},{"tolerance",1}});}
 record("crop_expanded_canonical_immutable",p.document==d&&p.history.undoCount()==0);
 const auto patch=c.viewportProvider(-20,-10,140,100,1);record("crop_expanded_provider_bounds",patch.raster&&patch.documentX<=-20&&patch.documentY<=-10&&patch.documentX+patch.raster->width*patch.unitsPerPixel>=120&&patch.documentY+patch.raster->height*patch.unitsPerPixel>=90);
 c.pointerCancel();pump();const auto cancelled=c.captureRendered();save(cancelled,dir/"cancelled.png");record("crop_expanded_cancel",p.document==d&&p.history.undoCount()==0&&difference(sample(c,cancelled,{-10,40}),originalExterior)<=1);
 c.pointerDown({-20,-10},Qt::ControlModifier);c.pointerMove({120,90},Qt::ControlModifier);c.pointerUp({120,90},Qt::ControlModifier);auto* apply=command(w,"crop.apply");require(apply&&apply->isEnabled(),"Apply unavailable");apply->trigger();pump();record("crop_expanded_apply",p.document->width==140&&p.document->height==100&&p.document->layers.front().transform==Transform{-10,-10,160,120}&&p.history.undoCount()==1);save(c.captureRendered(),dir/"applied.png");
 QFile file(QString::fromStdWString((dir/"results.json").wstring()));require(file.open(QIODevice::WriteOnly),"Report failed");file.write(QJsonDocument(QJsonObject{{"schema","NATIVE_EXPANDED_CROP_V1"},{"dpr",c.devicePixelRatioF()},{"checks",checks},{"failed",failed},{"tolerance",1},{"mac_differential",false}}).toJson());
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 2;}return failed?1:0;}
