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
void require(bool value,const char*m){if(!value)throw std::runtime_error(m);}
void pump(){QElapsedTimer timer;timer.start();while(timer.elapsed()<30){QApplication::processEvents();QThread::msleep(3);}}
QPoint position(const NativeCanvas&c,Point p){const auto v=c.viewMapping().toView(p);return {int(std::floor(v.x*c.devicePixelRatioF())),int(std::floor(v.y*c.devicePixelRatioF()))};}
int difference(QColor a,QColor b){return std::max({std::abs(a.red()-b.red()),std::abs(a.green()-b.green()),std::abs(a.blue()-b.blue()),std::abs(a.alpha()-b.alpha())});}
void save(const QImage&i,const std::filesystem::path&p){require(i.save(QString::fromStdWString(p.wstring())),"PNG save failed");}
}
int main(int argc,char**argv){QApplication app(argc,argv);if(argc!=2)return 2;const std::filesystem::path dir(argv[1]);std::filesystem::create_directories(dir);QJsonArray checks;int failures=0;try{
MainWindow w(true);Document d;d.id="snap-native";d.width=100;d.height=80;Layer base;base.id="base";base.name="Base";base.transform={0,0,100,80};base.raster=Raster::filled(100,80,{80,120,160,255});Layer moving;moving.id="moving";moving.name="Moving";moving.transform={20,20,20,20};moving.raster=Raster::filled(20,20,{210,70,40,255});d.layers={base,moving};auto&p=w.addProject(d);w.resize(1180,880);w.show();w.activateWindow();pump();auto&c=*p.canvas;c.setDisplayProfileOverride(dir/"missing.icc");c.zoomAt(4*c.devicePixelRatioF(),c.rect().center());c.showPixelGrid=false;const auto before=c.captureRendered();require(!c.presentationConvertsColor()&&!c.presentationDiagnostic().isEmpty(),"sRGB fallback not isolated");save(before,dir/"before.png");const auto vertical=position(c,{50,5}),horizontal=position(c,{5,40});
auto record=[&](const char*name,bool pass,QJsonObject detail=QJsonObject{}){detail.insert("name",name);detail.insert("pass",pass);checks.append(detail);failures+=!pass;std::cout<<(pass?"PASS ":"FAIL ")<<name<<'\n';};
c.pointerDown({30,30},{});c.pointerMove({49,40},{});pump();const auto live=c.captureRendered();save(live,dir/"live.png");record("snap_move_position",p.document->layers.back().transform==Transform{40,30,20,20});
for(int axis=0;axis<2;++axis){const auto center=position(c,axis?Point{5,40}:Point{50,5});const auto reference=axis?horizontal:vertical;int maximum=0;for(int offset=-2;offset<=2;++offset){const auto delta=axis?QPoint(0,offset):QPoint(offset,0);maximum=std::max(maximum,difference(live.pixelColor(center+delta),before.pixelColor(reference+delta)));}record(axis?"snap_horizontal_guide":"snap_vertical_guide",maximum>=15,QJsonObject{{"max_channel_difference",maximum},{"minimum",15},{"before_sample",QJsonArray{reference.x(),reference.y()}},{"live_sample",QJsonArray{center.x(),center.y()}}});}
c.pointerCancel();pump();const auto cancelled=c.captureRendered();save(cancelled,dir/"cancel.png");record("snap_cancel_clears_guides",p.document==d&&p.history.undoCount()==0&&difference(cancelled.pixelColor(position(c,{50,5})),before.pixelColor(vertical))<=1&&difference(cancelled.pixelColor(position(c,{5,40})),before.pixelColor(horizontal))<=1);
c.pointerDown({30,30},Qt::ControlModifier);c.pointerMove({49,40},Qt::ControlModifier);record("snap_control_suppresses",p.document->layers.back().transform==Transform{39,30,20,20});c.pointerCancel();
c.pointerDown({30,30},{});c.pointerMove({49,40},{});c.pointerUp({49,40},{});pump();const auto applied=c.captureRendered();save(applied,dir/"applied.png");record("snap_mouseup_clears_guides",p.history.undoCount()==1&&difference(applied.pixelColor(position(c,{50,5})),before.pixelColor(vertical))<=1&&difference(applied.pixelColor(position(c,{5,40})),before.pixelColor(horizontal))<=1);
QFile file(QString::fromStdWString((dir/"results.json").wstring()));require(file.open(QIODevice::WriteOnly),"Report failed");file.write(QJsonDocument(QJsonObject{{"schema","NATIVE_SNAP_V1"},{"dpr",c.devicePixelRatioF()},{"checks",checks},{"failed",failures},{"restoration_tolerance",1},{"guide_minimum_channel_difference",15},{"mac_differential",false}}).toJson());
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 2;}return failures?1:0;}
