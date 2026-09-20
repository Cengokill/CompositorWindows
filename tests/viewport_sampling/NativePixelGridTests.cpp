#include "ui/NativeCanvas.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <iostream>
#include <stdexcept>
using namespace compositor;
namespace {
constexpr int tolerance=1;
void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void flush(){QApplication::processEvents();QThread::msleep(10);QApplication::processEvents();}
void save(const QDir& evidence,const QString& name,const QImage& image){require(image.save(evidence.filePath(name+".png")),"PNG write");QFile raw(evidence.filePath(name+".rgba"));require(raw.open(QIODevice::WriteOnly),"Raw write");for(int y=0;y<image.height();++y)require(raw.write(reinterpret_cast<const char*>(image.constScanLine(y)),image.width()*4)==image.width()*4,"Raw row write");}
int delta(QColor actual,QColor expected){return std::max({std::abs(actual.red()-expected.red()),std::abs(actual.green()-expected.green()),std::abs(actual.blue()-expected.blue())});}
}
int main(int argc,char** argv){QApplication app(argc,argv);std::cout<<std::unitbuf;QDir evidence(argc>1?QString::fromLocal8Bit(argv[1]):"native-pixel-grid-results");if(!evidence.mkpath("."))return 2;QJsonArray cases;int failures=0;try{
    Document doc;doc.width=16;doc.height=16;Layer layer;layer.id=newId();layer.transform={0,0,16,16};std::vector<Pixel> pixels(256);for(int y=0;y<16;++y)for(int x=0;x<16;++x)pixels[size_t(y)*16+x]=x%2?Pixel{0,0,255,255}:Pixel{255,0,0,255};layer.raster=Raster::fromRgba(16,16,reinterpret_cast<const uint8_t*>(pixels.data()),64);doc.layers={layer};CompositeCache cache;NativeCanvas canvas(true);canvas.resize(192,144);canvas.setDocumentSize(16,16);const auto missing=evidence.absoluteFilePath("__missing_geometry_profile__.icc");require(!QFileInfo::exists(missing),"Missing-profile fixture exists");canvas.setDisplayProfileOverride(std::filesystem::path(missing.toStdWString()));canvas.viewportProvider=[&](double x,double y,double w,double h,double units){return cache.renderViewport(doc,x,y,w,h,units,64,256);};canvas.show();flush();const double dpr=canvas.devicePixelRatioF();require(dpr==1||dpr==2,"Frozen DPR1 or2");
    auto capture=[&]{canvas.repaint();flush();auto result=canvas.captureRendered().convertToFormat(QImage::Format_RGBA8888);require(canvas.deviceReady()&&canvas.deviceError().isEmpty(),"WARP device");require(!canvas.presentationConvertsColor()&&!canvas.presentationDiagnostic().isEmpty(),"sRGB geometry fallback");return result;};
    auto record=[&](const char* id,bool passed,QJsonObject data){data.insert("id",id);data.insert("passed",passed);cases.append(data);failures+=!passed;std::cout<<(passed?"PASS ":"FAIL ")<<id<<' '<<QJsonDocument(data).toJson(QJsonDocument::Compact).toStdString()<<'\n';};
    canvas.zoomAt(1,{96,72});canvas.pan={};auto actual=capture();const auto topLeft=canvas.viewMapping().toView({0,0});const int left=int(std::lround(topLeft.x*dpr)),top=int(std::lround(topLeft.y*dpr));int maximum=0;for(int y=0;y<16;++y)for(int x=0;x<16;++x)maximum=std::max(maximum,delta(actual.pixelColor(left+x,top+y),x%2?QColor(0,0,255):QColor(255,0,0)));save(evidence,"actual-pixels",actual);record("one_document_pixel_is_one_physical_pixel",maximum<=tolerance&&std::abs(canvas.pointsPerPixel()-1/dpr)<1e-12,{{"dpr",dpr},{"compared_pixels",256},{"physical_width",16},{"maximum_difference",maximum},{"tolerance",tolerance}});
    doc.layers[0].raster=Raster::filled(16,16,{255,255,255,255});cache.reset();canvas.zoomAt(7.999,{96,72});canvas.pan={.5/dpr,.5/dpr};canvas.showPixelGrid=false;auto off=capture();canvas.showPixelGrid=true;auto on=capture();int changed=0;for(int y=0;y<on.height();++y)for(int x=0;x<on.width();++x)changed+=on.pixel(x,y)!=off.pixel(x,y);record("pixel_grid_absent_below_physical_zoom8",changed==0,{{"zoom",7.999},{"changed_pixels",changed}});
    // Source EditorCanvas.swift 904-919: 1/backingScale hairline, .55 gray at .45 alpha.
    // Half-physical-pixel pan places every hairline exactly on one pixel, removing
    // edge-antialias ambiguity. A single union path fills intersections once.
    canvas.zoomAt(8,{96,72});canvas.pan={.5/dpr,.5/dpr};canvas.showPixelGrid=false;off=capture();canvas.showPixelGrid=true;on=capture();QImage expected=off,diff=off;diff.fill(Qt::black);const auto origin=canvas.viewMapping().toView({0,0});const int x0=int(std::floor(origin.x*dpr)),y0=int(std::floor(origin.y*dpr));maximum=0;changed=0;int compared=0;for(int y=8;y<120;++y)for(int x=8;x<120;++x){const QColor reference=(x%8==0||y%8==0)?QColor(203,203,203):QColor(255,255,255);const int error=delta(on.pixelColor(x0+x,y0+y),reference);maximum=std::max(maximum,error);changed+=on.pixel(x0+x,y0+y)!=off.pixel(x0+x,y0+y);++compared;expected.setPixelColor(x0+x,y0+y,reference);diff.setPixelColor(x0+x,y0+y,QColor(std::min(255,error*4),0,0));}save(evidence,"grid-at8-off",off);save(evidence,"grid-at8-actual",on);save(evidence,"grid-at8-expected",expected);save(evidence,"grid-at8-diff",diff);record("pixel_grid_source_hairline_color_and_union_at8",maximum<=tolerance&&changed>0,{{"zoom",8},{"dpr",dpr},{"compared_pixels",compared},{"changed_pixels",changed},{"expected_full_coverage_gray",203},{"maximum_difference",maximum},{"tolerance",tolerance}});
    canvas.viewportProvider={};QFile file(evidence.filePath("results.json"));require(file.open(QIODevice::WriteOnly),"Results write");file.write(QJsonDocument(QJsonObject{{"suite","NATIVE_PIXEL_GRID_V1"},{"backend","D3D11_WARP"},{"presentation","explicit_missing_profile_sRGB_fallback"},{"source","EditorCanvas.swift:693-694,896-919"},{"mac_differential",false},{"passed",cases.size()-failures},{"failed",failures},{"cases",cases}}).toJson());return failures?1:0;
}catch(const std::exception& error){std::cout<<"ERROR "<<error.what()<<'\n';return 2;}}
