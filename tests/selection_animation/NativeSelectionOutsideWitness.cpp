// TransformOverlay.drawSelection uses the complete displayed path, including
// its portion outside the document. Viewport clipping still applies.
#include "ui/NativeCanvas.h"
#include "editing/Selection.h"
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
void pump(){QElapsedTimer time;time.start();while(time.elapsed()<40){QApplication::processEvents();QThread::msleep(2);}}
void save(const QImage& image,const std::filesystem::path& path){require(image.save(QString::fromStdWString(path.wstring())),"Save failed");}
int edgeDifference(const NativeCanvas& canvas,const QImage& before,const QImage& after,double documentX){int maximum=0;for(double y=22;y<38;y+=.25){const auto p=canvas.viewMapping().toView({documentX,y});const QPoint center(int(std::floor(p.x*canvas.devicePixelRatioF())),int(std::floor(p.y*canvas.devicePixelRatioF())));for(int dx=-2;dx<=2;++dx){const QPoint q=center+QPoint(dx,0);require(before.rect().contains(q)&&after.rect().contains(q),"Edge outside viewport");const auto a=before.pixelColor(q),b=after.pixelColor(q);maximum=std::max({maximum,std::abs(a.red()-b.red()),std::abs(a.green()-b.green()),std::abs(a.blue()-b.blue())});}}return maximum;}
}
int main(int argc,char** argv){QApplication app(argc,argv);if(argc!=2)return 2;const std::filesystem::path directory(argv[1]);std::filesystem::create_directories(directory);try{NativeCanvas canvas(true);canvas.resize(600,460);canvas.setRaster(Raster::filled(100,80,{70,120,180,255}));canvas.setDisplayProfileOverride(directory/"missing.icc");canvas.show();pump();canvas.zoomAt(4*canvas.devicePixelRatioF(),canvas.rect().center());canvas.showPixelGrid=false;auto before=canvas.captureRendered();require(!canvas.presentationConvertsColor()&&!canvas.presentationDiagnostic().isEmpty(),"sRGB isolation absent");save(before,directory/"before.png");auto outline=std::make_shared<const editing::SelectionOutline>(editing::SelectionOutline::rectangle({-10,20,20,20}));auto dense=outline->rasterize(100,80);auto sparse=GrayRaster::sampled(100,80,{0,20,10,20},[dense](int x,int y){return dense->pixel(x,y);},dense->retainedBytes(),outline);canvas.setSelection(sparse);auto after=canvas.captureRendered();save(after,directory/"selection.png");const int outside=edgeDifference(canvas,before,after,-10),inside=edgeDifference(canvas,before,after,10);const bool outsidePass=outside>=100,insidePass=inside>=100;std::cout<<(outsidePass?"PASS ":"FAIL ")<<"selection_outside_document_visible\n"<<(insidePass?"PASS ":"FAIL ")<<"selection_inside_document_control\n";QFile report(QString::fromStdWString((directory/"results.json").wstring()));require(report.open(QIODevice::WriteOnly),"Report failed");report.write(QJsonDocument(QJsonObject{{"schema","NATIVE_SELECTION_OUTSIDE_V1"},{"dpr",canvas.devicePixelRatioF()},{"failed",int(!outsidePass)+int(!insidePass)},{"checks",QJsonArray{QJsonObject{{"name","selection_outside_document_visible"},{"pass",outsidePass},{"maximum_channel_change",outside},{"minimum_channel_change",100}},QJsonObject{{"name","selection_inside_document_control"},{"pass",insidePass},{"maximum_channel_change",inside},{"minimum_channel_change",100}}}},{"mac_differential",false}}).toJson());return outsidePass&&insidePass?0:1;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}}
