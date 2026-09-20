#include "ui/NativeCanvas.h"
#include "graphics/Downsample.h"
#include "graphics/SamplingSource.h"
#include "graphics/RasterSampling.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace compositor;using namespace compositor::graphics;
namespace {
constexpr int tolerance=1; // Frozen before baseline capture; no Mac differential.
void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void flush(){QApplication::processEvents();QThread::msleep(5);QApplication::processEvents();}
std::shared_ptr<const Raster> fixture(bool stripes){std::vector<Pixel> data(128*64);for(int y=0;y<64;++y)for(int x=0;x<128;++x){const auto v=uint8_t(x%2?255:0);data[size_t(y)*128+x]=stripes?Pixel{v,v,v,255}:Pixel{uint8_t((37*x+11*y+19)%256),uint8_t((7*x+53*y+47)%256),uint8_t((83*x+3*y+91)%256),255};}return Raster::fromRgba(128,64,reinterpret_cast<const uint8_t*>(data.data()),128*4);}
void save(const QDir& directory,const QString& name,const QImage& frame){require(frame.save(directory.filePath(name+".png")),"PNG save");auto image=frame.convertToFormat(QImage::Format_RGBA8888);QFile file(directory.filePath(name+".rgba"));require(file.open(QIODevice::WriteOnly),"raw capture open");for(int y=0;y<image.height();++y)require(file.write(reinterpret_cast<const char*>(image.constScanLine(y)),image.width()*4)==image.width()*4,"raw capture write");}
}
int main(int argc,char** argv){QApplication app(argc,argv);std::cout<<std::unitbuf;QDir evidence(argc>1?QString::fromLocal8Bit(argv[1]):"physical-viewport-results");if(!evidence.mkpath("."))return 2;QJsonArray rows;int failures=0;try{
    Document doc;doc.width=128;doc.height=64;Layer layer;layer.id=newId();layer.transform={48,24,32,16};layer.raster=fixture(false);doc.layers={layer};CompositeCache cache;NativeCanvas canvas(true);canvas.resize(192,144);canvas.setDocumentSize(doc.width,doc.height);const auto missingProfile=evidence.absoluteFilePath("__geometry_reference_missing_profile__.icc");require(!QFileInfo::exists(missingProfile),"Geometry profile fixture must be absent");canvas.setDisplayProfileOverride(std::filesystem::path(missingProfile.toStdWString()));double requested=0;canvas.viewportProvider=[&](double x,double y,double w,double h,double units){requested=units;return cache.renderViewport(doc,x,y,w,h,units,64,256);};canvas.show();flush();
    const double dpr=canvas.devicePixelRatioF();require(dpr==1||dpr==2,"Run frozen DPR1 or2");
    for(bool stripes:{false,true})for(double zoom:{.7,1.,1.999,2.,4.,10.749,32.})for(int variant=0;variant<5;++variant)for(bool fractional:{false,true}){
        const QString id=QString("%1-z%2-v%3-pan%4-dpr%5").arg(stripes?"stripes":"asymmetric").arg(zoom,0,'f',3).arg(variant).arg(fractional?1:0).arg(int(dpr));
        doc.layers[0].raster=fixture(stripes);auto& transform=doc.layers[0].transform;transform={48,24,32,16,variant==3?180.:variant==4?25.:0.,variant==1,variant==2,Transform::Sampling::High};cache.reset();canvas.zoomAt(zoom,{canvas.width()/2.,canvas.height()/2.});canvas.pan={fractional?.375/dpr:0,fractional?-.25/dpr:0};canvas.showPixelGrid=false;canvas.repaint();flush();canvas.repaint();flush();require(canvas.deviceReady()&&canvas.deviceError().isEmpty(),"Native WARP capture device");require(!canvas.presentationConvertsColor()&&!canvas.presentationDiagnostic().isEmpty(),"Explicit sRGB geometry fallback");auto actual=canvas.captureRendered().convertToFormat(QImage::Format_RGBA8888);QImage expected=actual,diff(actual.size(),QImage::Format_RGBA8888);diff.fill(Qt::black);require(actual.width()==int(canvas.width()*dpr)&&actual.height()==int(canvas.height()*dpr),"Physical swap chain dimensions");
        const bool crisp=zoom>=2;const double expectedUnits=crisp?1:1/zoom;const int level=DownsampleCache::levelFor(transform.width*(crisp?1:zoom)/doc.layers[0].raster->width);auto reduced=level?ReducedSourceCache::shared().resolve(samplingSource(doc.layers[0].raster),level):std::shared_ptr<const ReducedSource>{};int maximum=0,count=0,over=0;
        for(int y=0;y<actual.height();++y)for(int x=0;x<actual.width();++x){const auto point=canvas.documentPoint({(x+.5)/dpr,(y+.5)/dpr});const auto samplePoint=crisp?Point{std::floor(point.x())+.5,std::floor(point.y())+.5}:Point{point.x(),point.y()};const auto unit=transform.toUnit(samplePoint);if(unit.x<.2||unit.x>=.8||unit.y<.2||unit.y>=.8)continue;const auto sample=reduced?reduced->sample(unit,transform.sampling):sampleRaster(*doc.layers[0].raster,unit,transform.sampling);if(sample.a!=255)continue;const auto* pixel=actual.constScanLine(y)+x*4;const int error=std::max({std::abs(int(pixel[0])-sample.r),std::abs(int(pixel[1])-sample.g),std::abs(int(pixel[2])-sample.b)});maximum=std::max(maximum,error);over+=error>tolerance;++count;expected.setPixelColor(x,y,QColor(sample.r,sample.g,sample.b));diff.setPixelColor(x,y,QColor(std::min(255,error*4),0,0));}
        const bool pass=count>=16&&maximum<=tolerance&&std::abs(requested-expectedUnits)<1e-12;failures+=pass?0:1;rows.append(QJsonObject{{"id",id},{"pattern",stripes?"stripes":"asymmetric"},{"zoom",zoom},{"dpr",dpr},{"variant",variant},{"fractional_pan",fractional},{"source_level",level},{"requested_units",requested},{"expected_units",expectedUnits},{"crisp_document_pixels",crisp},{"compared_pixels",count},{"minimum_compared_pixels",16},{"maximum_difference",maximum},{"pixels_over_tolerance",over},{"tolerance",tolerance},{"passed",pass},{"width",actual.width()},{"height",actual.height()}});std::cout<<(pass?"PASS ":"FAIL ")<<id.toStdString()<<" max="<<maximum<<" pixels="<<count<<" request="<<requested<<'\n';
        if(!pass||(zoom==4&&variant==0&&fractional)){save(evidence,id+"-actual",actual);save(evidence,id+"-expected",expected);save(evidence,id+"-diff",diff);}
    }
    canvas.viewportProvider={};QFile file(evidence.filePath("results.json"));require(file.open(QIODevice::WriteOnly),"result file");file.write(QJsonDocument(QJsonObject{{"suite","PHYSICAL_VIEWPORT_V1"},{"fixture","128x64 source placed32x16 in128x64 document;192x144 logical canvas"},{"backend","D3D11_WARP"},{"presentation","explicit_missing_profile_sRGB_fallback"},{"sample_reference","EditorCanvas: physical centers below2; nearest document pixel centers from2"},{"tolerance",tolerance},{"passed",rows.size()-failures},{"failed",failures},{"cases",rows},{"mac_differential",false}}).toJson());std::cout<<rows.size()-failures<<" passed, "<<failures<<" failed\n";return failures?1:0;
}catch(const std::exception& error){std::cout<<"ERROR "<<error.what()<<'\n';return 2;}}
