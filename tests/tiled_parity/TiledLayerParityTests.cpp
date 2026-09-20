// Native contract port of CompositorTests/TiledLayerTests.swift at a19db901.
// Source copyright (c) 2026 Wonder Assembly LLC, MIT (graphics/upstream/LICENSE).
#include "ui/MainWindow.h"
#include "graphics/StackRenderer.h"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>

using namespace compositor;
namespace {
struct Frame {int width{},height{};std::vector<Pixel> pixels;};
struct Patch {int x{},y{};Frame image;};
struct Gray {int width{},height{};std::vector<uint8_t> pixels;};
struct GrayPatch {int x{},y{};Gray image;};
struct Result {QString id;QJsonValue argument;QJsonArray checks;bool passed{true};};
QDir evidence;Result* currentInvocation{};QJsonArray allResults;
void check(int sourceLine,const QString& name,bool passed,int nativeLine,const QString& kind="require",QJsonObject extra={}){
    extra["source_line"]=sourceLine;extra["native_line"]=nativeLine;extra["name"]=name;extra["kind"]=kind;extra["passed"]=passed;
    currentInvocation->checks.append(extra);currentInvocation->passed&=passed;
    if(!passed)std::cout<<"FAIL assertion "<<currentInvocation->id.toStdString()<<" L"<<sourceLine<<" "<<name.toStdString()<<'\n';
    if(!passed&&kind=="require")throw std::runtime_error(name.toStdString());
}
#define REQUIRE(source,condition,name) check(source,name,bool(condition),__LINE__)
#define EXPECT(source,condition,name) check(source,name,bool(condition),__LINE__,"expect")
void write(const QString& file,const QByteArray& bytes){QFile output(evidence.filePath(file));if(!output.open(QIODevice::WriteOnly)||output.write(bytes)!=bytes.size())throw std::runtime_error("Evidence write failed");}
void save(const QString& prefix,const Frame& frame){
    QImage image(reinterpret_cast<const uchar*>(frame.pixels.data()),frame.width,frame.height,frame.width*4,QImage::Format_RGBA8888_Premultiplied);
    if(!image.save(evidence.filePath(prefix+".png")))throw std::runtime_error("PNG evidence write failed");
    write(prefix+".rgba",QByteArray(reinterpret_cast<const char*>(frame.pixels.data()),qsizetype(frame.pixels.size()*4)));
    write(prefix+".json",QJsonDocument(QJsonObject{{"width",frame.width},{"height",frame.height},{"row_bytes",frame.width*4},{"format","premultiplied_RGBA8_top_down"}}).toJson());
}
Frame noise(int width,int height,uint32_t seed,uint8_t alpha=255){
    Frame image{width,height,std::vector<Pixel>(size_t(width)*height)};REQUIRE(12,!image.pixels.empty(),"noise context.data");
    uint32_t state=seed;for(auto& pixel:image.pixels){state=state*1664525u+1013904223u;pixel={uint8_t(unsigned(uint8_t(state>>24))*alpha/255),uint8_t(unsigned(uint8_t(state>>16))*alpha/255),uint8_t(unsigned(uint8_t(state>>8))*alpha/255),alpha};}
    REQUIRE(21,image.pixels.size()==size_t(width)*height,"noise makeImage");return image;
}
Gray grayNoise(int width,int height,uint32_t seed){
    Gray image{width,height,std::vector<uint8_t>(size_t(width)*height)};REQUIRE(37,!image.pixels.empty(),"grayNoise context.data");
    uint32_t state=seed;for(auto& pixel:image.pixels){state=state*1664525u+1013904223u;pixel=uint8_t(state>>24);}
    REQUIRE(43,image.pixels.size()==size_t(width)*height,"grayNoise makeImage");return image;
}
Frame composite(Frame base,std::initializer_list<Patch> patches){
    for(const auto& patch:patches){if(patch.x<0||patch.y<0||patch.x+patch.image.width>base.width||patch.y+patch.image.height>base.height)throw std::runtime_error("Invalid source patch bounds");for(int y=0;y<patch.image.height;++y)std::copy_n(patch.image.pixels.data()+size_t(y)*patch.image.width,patch.image.width,base.pixels.data()+size_t(patch.y+y)*base.width+patch.x);}
    REQUIRE(27,base.pixels.size()==size_t(base.width)*base.height,"composite makeImage");return base;
}
Gray maskComposite(Gray base,std::initializer_list<GrayPatch> patches){
    for(const auto& patch:patches){if(patch.x<0||patch.y<0||patch.x+patch.image.width>base.width||patch.y+patch.image.height>base.height)throw std::runtime_error("Invalid source mask patch bounds");for(int y=0;y<patch.image.height;++y)std::copy_n(patch.image.pixels.data()+size_t(y)*patch.image.width,patch.image.width,base.pixels.data()+size_t(patch.y+y)*base.width+patch.x);}
    REQUIRE(49,base.pixels.size()==size_t(base.width)*base.height,"maskComposite makeImage");return base;
}
std::shared_ptr<const Raster> raster(const Frame& frame){return Raster::fromRgba(frame.width,frame.height,reinterpret_cast<const uint8_t*>(frame.pixels.data()),size_t(frame.width)*4);}
std::shared_ptr<const GrayRaster> raster(const Gray& frame){return std::make_shared<GrayRaster>(GrayRaster{frame.width,frame.height,frame.pixels});}
std::shared_ptr<const Raster> replace(const std::shared_ptr<const Raster>& base,const Patch& patch){return base->replacing(patch.x,patch.y,patch.image.width,patch.image.height,patch.image.pixels.data(),size_t(patch.image.width));}
// GrayRaster is dense in the production model. Apply the live mask replacement
// to a copy of that model rather than using the independent dense reference helper.
std::shared_ptr<const GrayRaster> replace(const std::shared_ptr<const GrayRaster>& base,const GrayPatch& patch){auto out=std::make_shared<GrayRaster>(*base);for(int y=0;y<patch.image.height;++y)for(int x=0;x<patch.image.width;++x)out->pixels[size_t(patch.y+y)*out->width+patch.x+x]=patch.image.pixels[size_t(y)*patch.image.width+x];return out;}
Frame bytes(const Raster& source,int sourceLine){Frame out{source.width,source.height,std::vector<Pixel>(size_t(source.width)*source.height)};for(int y=0;y<out.height;++y)for(int x=0;x<out.width;++x)out.pixels[size_t(y)*out.width+x]=source.pixel(x,y);REQUIRE(sourceLine,!out.pixels.empty(),"render context.data");return out;}
Layer layer(std::shared_ptr<const Raster> source,Transform transform){Layer out;out.id="painted-layer";out.raster=std::move(source);out.transform=transform;return out;}
Document document(Layer image,int side){Document out;out.id="tiled-parity-frame";out.width=out.height=side;out.layers.push_back(std::move(image));return out;}
Transform placement(int width,int height,double scale,double rotation,int side){return {(side-width*scale)/2,(side-height*scale)/2,width*scale,height*scale,rotation,false,false,Transform::Sampling::High};}
Frame reference(const Layer& image,int side,int sourceLine=33){return bytes(*graphics::StackRenderer().render(document(image,side),0,0,side,side),sourceLine);}
Frame tiled(CompositeCache& cache,const Layer& image,int side,int sourceLine=33){return bytes(*cache.render(document(image,side)),sourceLine);}
struct Difference {int maximum{},x{},y{},channel{};uint64_t pixels{},aboveTwo{};};
Difference difference(const Frame& expected,const Frame& actual,const Frame* silhouette=nullptr,std::optional<QRectF> excluded={}){
    if(expected.width!=actual.width||expected.height!=actual.height||expected.pixels.size()!=actual.pixels.size())throw std::runtime_error("Frame comparison dimensions differ");
    if(silhouette&&(silhouette->width!=expected.width||silhouette->height!=expected.height))throw std::runtime_error("Inside reference dimensions differ");
    Difference out;for(int y=0;y<expected.height;++y)for(int x=0;x<expected.width;++x){
        // CGRect.contains uses an inclusive minimum and exclusive maximum.
        if(excluded&&x>=excluded->left()&&x<excluded->right()&&y>=excluded->top()&&y<excluded->bottom())continue;
        if(silhouette){if(x==0||y==0||x==expected.width-1||y==expected.height-1)continue;bool inside=true;for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)if(silhouette->pixels[size_t(y+dy)*expected.width+x+dx].a<255)inside=false;if(!inside)continue;}
        ++out.pixels;const auto* a=reinterpret_cast<const uint8_t*>(&expected.pixels[size_t(y)*expected.width+x]);const auto* b=reinterpret_cast<const uint8_t*>(&actual.pixels[size_t(y)*actual.width+x]);int local=0;for(int c=0;c<4;++c){const int d=std::abs(int(a[c])-int(b[c]));local=std::max(local,d);if(d>out.maximum){out.maximum=d;out.x=x;out.y=y;out.channel=c;}}if(local>2)++out.aboveTwo;
    }return out;
}
void compare(int sourceLine,const QString& name,const Frame& expected,const Frame& actual,int tolerance,const Frame* inside=nullptr,std::optional<QRectF> excluded={}){
    const auto diff=difference(expected,actual,inside,excluded);QJsonObject detail{{"maximum",diff.maximum},{"tolerance",tolerance},{"compared_pixels",qint64(diff.pixels)},{"pixels_above_two",qint64(diff.aboveTwo)},{"worst_x",diff.x},{"worst_y",diff.y},{"worst_channel",diff.channel}};
    // Additional native safeguard: a nonempty source interior must be tested.
    REQUIRE(0,diff.pixels>0,"comparison has eligible pixels");
    if(diff.maximum>tolerance){const auto prefix=currentInvocation->id+"-L"+QString::number(sourceLine)+"-"+name;save(prefix+"-expected",expected);save(prefix+"-actual",actual);Frame heat{expected.width,expected.height,std::vector<Pixel>(expected.pixels.size())};for(size_t i=0;i<heat.pixels.size();++i){const auto a=expected.pixels[i],b=actual.pixels[i];const int d=std::max({std::abs(int(a.r)-b.r),std::abs(int(a.g)-b.g),std::abs(int(a.b)-b.b),std::abs(int(a.a)-b.a)});heat.pixels[i]={uint8_t(std::min(255,d*8)),0,0,255};}save(prefix+"-diff8x",heat);detail["artifact_prefix"]=prefix;}
    check(sourceLine,name,diff.maximum<=tolerance,__LINE__,"expect",detail);std::cout<<"  L"<<sourceLine<<" "<<name.toStdString()<<" max="<<diff.maximum<<" tolerance="<<tolerance<<" pixels="<<diff.pixels<<'\n';
}
void tiledLayers(double scale,double rotation){
    const auto base=noise(1600,1000,7);const Patch committed{1100,400,noise(256,256,99)};const int side=int(std::ceil(1700*scale)),tolerance=rotation==0?2:12;const auto transform=placement(1600,1000,scale,rotation,side);
    const auto finished=composite(base,{committed});auto painted=layer(replace(raster(base),committed),transform);CompositeCache cache;
    const auto expected=reference(layer(raster(finished),transform),side),drawn=tiled(cache,painted,side);compare(89,"committed",expected,drawn,tolerance,&expected);
    const Patch stroke{900,500,noise(256,256,5)};const auto expectedLive=reference(layer(raster(composite(finished,{stroke})),transform),side);painted.raster=replace(painted.raster,stroke);
    const auto live=tiled(cache,painted,side);compare(101,"live-on-committed",expectedLive,live,tolerance,&expectedLive);
    const auto expectedFirst=reference(layer(raster(composite(base,{stroke})),transform),side);CompositeCache plainCache;auto first=layer(raster(base),transform);tiled(plainCache,first,side);first.raster=replace(first.raster,stroke);
    compare(112,"first-stroke",expectedFirst,tiled(plainCache,first,side),tolerance,&expectedFirst);
}
void maskStrokes(double scale,double rotation){
    const auto source=noise(1600,1000,11);const auto old=grayNoise(1600,1000,12);const GrayPatch patch{900,500,grayNoise(256,256,13)};
    const int side=int(std::ceil(1700*scale)),tolerance=rotation==0?2:12;const auto transform=placement(1600,1000,scale,rotation,side);auto image=layer(raster(source),transform);
    const auto unmasked=reference(image,side);auto expectedLayer=image;expectedLayer.mask=Mask{raster(maskComposite(old,{patch}))};const auto expected=reference(expectedLayer,side);
    CompositeCache cache;image.mask=Mask{raster(old)};tiled(cache,image,side);image.mask->raster=replace(image.mask->raster,patch);
    compare(136,"live-mask",expected,tiled(cache,image,side),tolerance,&unmasked);
}
void edge(double zoom){
    const auto source=noise(3360,1812,11);const int pixels=1200;const double device=2;Transform transform{50*device,50*device,336*zoom*device,181*zoom*device,0,false,false,Transform::Sampling::High};auto image=layer(raster(source),transform);
    const auto plain=reference(image,pixels,159);auto white=raster(Gray{1,1,{255}});REQUIRE(167,white&&white->pixel(0,0)==255,"solid reveal mask");auto maskedLayer=image;maskedLayer.mask=Mask{white};const auto masked=reference(maskedLayer,pixels,159);
    for(const int origin:{0,256}){Frame same{256,256,std::vector<Pixel>(256*256)};for(int y=0;y<256;++y)for(int x=0;x<256;++x)same.pixels[size_t(y)*256+x]=source.pixels[size_t(origin+y)*source.width+origin+x];REQUIRE(171,same.pixels.size()==256*256,"source image crop "+QString::number(origin));
        CompositeCache paintCache;tiled(paintCache,image,pixels,159);auto painted=image;painted.raster=replace(image.raster,{origin,origin,same});compare(177,"image-origin"+QString::number(origin),plain,tiled(paintCache,painted,pixels,159),2);
        Gray reveal{256,256,std::vector<uint8_t>(256*256,255)};REQUIRE(182,reveal.pixels.size()==256*256,"white patch makeImage "+QString::number(origin));
        CompositeCache maskCache;tiled(maskCache,maskedLayer,pixels,159);auto maskPainting=maskedLayer;auto expanded=raster(Gray{3360,1812,std::vector<uint8_t>(3360*1812,255)});maskPainting.mask->raster=replace(expanded,{origin,origin,reveal});
        compare(187,"mask-origin"+QString::number(origin),masked,tiled(maskCache,maskPainting,pixels,159),2);
    }
}
void translucent(double scale,double rotation){
    const auto base=noise(1600,1000,21,128);const Patch patch{900,500,noise(256,256,22,128)};const int side=int(std::ceil(1700*scale)),tolerance=rotation==0?2:12;const auto transform=placement(1600,1000,scale,rotation,side);
    const auto silhouette=reference(layer(raster(noise(1600,1000,1)),transform),side);const auto expected=reference(layer(raster(composite(base,{patch})),transform),side);
    CompositeCache cache;auto image=layer(raster(base),transform);tiled(cache,image,side);image.raster=replace(image.raster,patch);compare(212,"translucent-live",expected,tiled(cache,image,side),tolerance,&silhouette);
}
void flush(){QElapsedTimer clock;clock.start();do{QApplication::processEvents();QThread::msleep(5);}while(clock.elapsed()<60);}
template<class T>T* control(MainWindow& window,const char* name){auto* item=window.findChild<T*>(name);REQUIRE(0,item,"native UI control "+QString::fromLatin1(name));return item;}
void canvasShift(bool paintingMask){
    MainWindow window(true);Document doc;doc.id=newId();doc.width=800;doc.height=600;auto photo=layer(raster(noise(2400,1800,3)),{100,75,600,450,0,false,false,Transform::Sampling::High});photo.name="Photo";doc.layers.push_back(photo);auto& project=window.addProject(doc);
    auto findLayer=[&]() -> Layer& {auto found=std::find_if(project.document->layers.begin(),project.document->layers.end(),[&](const Layer& value){return value.id==photo.id;});REQUIRE(222,found!=project.document->layers.end(),"active image index");return *found;};findLayer();
    if(paintingMask){control<QAction>(window,"maskAddReveal")->trigger();EXPECT(229,project.maskSelected,"new mask is selected");}
    for(auto* action:window.findChildren<QAction*>())if(action->text()=="Brush (B)"){action->trigger();break;}
    auto* toolbar=control<QToolBar>(window,"brushOptions");auto tip=toolbar->findChildren<QDoubleSpinBox*>();REQUIRE(0,tip.size()==3,"brush controls");tip[0]->setValue(30);
    auto& canvas=*project.canvas;canvas.setFixedSize(800,600);window.resize(1220,940);window.show();canvas.setFocus();flush();canvas.fit();canvas.showPixelGrid=false;
    // Source invokes the session directly; it does not create hover/cursor input.
    if(canvas.pointerLeave)canvas.pointerLeave();canvas.pointerHover={};canvas.pointerLeave={};
    REQUIRE(0,canvas.width()==800&&canvas.height()==600&&std::abs(canvas.devicePixelRatioF()-1)<1e-9,"source canvas800x600 backingScale1");
    const Point brush{400,300};auto topLeft=canvas.viewMapping().toView({brush.x-60,brush.y-40}),bottomRight=canvas.viewMapping().toView({brush.x+80,brush.y+40});
    const auto originalImage=findLayer().raster;const auto originalMask=findLayer().mask?findLayer().mask->raster:std::shared_ptr<const GrayRaster>{};
    auto snapshot=[&](const QString& name){canvas.repaint();flush();canvas.repaint();flush();REQUIRE(242,canvas.deviceReady(),"canvas bitmap representation");auto image=canvas.captureRendered().convertToFormat(QImage::Format_RGBA8888);REQUIRE(244,!image.isNull()&&image.constBits(),"canvas bitmapData");Frame frame{image.width(),image.height(),std::vector<Pixel>(size_t(image.width())*image.height())};for(int y=0;y<image.height();++y)std::copy_n(reinterpret_cast<const Pixel*>(image.constScanLine(y)),image.width(),frame.pixels.data()+size_t(y)*image.width());save(currentInvocation->id+"-"+name,frame);return frame;};
    const auto before=snapshot("before");canvas.pointerDown({brush.x,brush.y},{});canvas.pointerMove({brush.x+20,brush.y},{});REQUIRE(0,bool(project.brushPreview),"actual live GrowingBrushSnapshot preview exists");const auto during=snapshot("during");canvas.pointerUp({brush.x+20,brush.y},{});const auto after=snapshot("after");
    const bool committed=paintingMask?(findLayer().mask&&findLayer().mask->raster!=originalMask):(findLayer().raster!=originalImage);EXPECT(paintingMask?292:284,committed,"stroke committed to native raster");REQUIRE(0,!project.brushPreview,"live preview cleared after commit");
    const double perPoint=double(before.width)/canvas.width();QRectF box(topLeft.x*perPoint,topLeft.y*perPoint,(bottomRight.x-topLeft.x)*perPoint,(bottomRight.y-topLeft.y)*perPoint);
    compare(paintingMask?293:285,"start-outside-brush",before,during,3,nullptr,box);compare(paintingMask?294:286,"finish-outside-brush",during,after,3,nullptr,box);
}
void run(const QString& name,const QString& suffix,QJsonValue argument,const std::function<void()>& body){Result item{"ACC-UT-TiledLayerTests-"+name+suffix,argument};currentInvocation=&item;try{body();}catch(const std::exception& error){item.passed=false;item.checks.append(QJsonObject{{"kind","exception"},{"passed",false},{"message",error.what()}});}allResults.append(QJsonObject{{"id",item.id},{"argument",item.argument},{"passed",item.passed},{"checks",item.checks}});std::cout<<(item.passed?"PASS ":"FAIL ")<<item.id.toStdString()<<'\n';std::cout.flush();currentInvocation=nullptr;}
}
int main(int argc,char** argv){QApplication app(argc,argv);std::cout.setf(std::ios::unitbuf);evidence=QDir(argc>1?QString::fromLocal8Bit(argv[1]):"tiled-parity-results");if(!evidence.mkpath("."))return 2;
    const std::array<std::pair<double,double>,3> opaque{{{.2,0},{.7,0},{.3,25}}};for(int i=0;i<3;++i){const auto [scale,rotation]=opaque[size_t(i)];run("tiledLayersDrawLikeOneImage",QString("-arg%1").arg(i+1,2,10,QChar('0')),QJsonArray{scale,rotation},[=]{tiledLayers(scale,rotation);});}
    for(int i=0;i<3;++i){const auto [scale,rotation]=opaque[size_t(i)];run("maskStrokesDrawLikeOneMask",QString("-arg%1").arg(i+1,2,10,QChar('0')),QJsonArray{scale,rotation},[=]{maskStrokes(scale,rotation);});}
    for(int i=0;i<2;++i){const double zoom=i==0?10.749:1;run("paintingAtTheLayersEdgeDoesNotChangeIt",QString("-arg%1").arg(i+1,2,10,QChar('0')),zoom,[=]{edge(zoom);});}
    const std::array<std::pair<double,double>,2> soft{{{.3,0},{.7,25}}};for(int i=0;i<2;++i){const auto [scale,rotation]=soft[size_t(i)];run("translucentStrokesDrawLikeOneImage",QString("-arg%1").arg(i+1,2,10,QChar('0')),QJsonArray{scale,rotation},[=]{translucent(scale,rotation);});}
    run("paintingAScaledDownLayerDoesNotShiftItsPixels",{},QJsonValue(),[]{canvasShift(false);});run("paintingAScaledDownLayersMaskDoesNotShiftItsPixels",{},QJsonValue(),[]{canvasShift(true);});
    int passed=0;for(const auto& item:allResults)passed+=item.toObject()["passed"].toBool()?1:0;
    QJsonObject report{{"schema_version",1},{"baseline_sha","a19db9011282399785dc18efcfded904627bdcc2"},{"suite","TiledLayerTests_native_contract"},{"invocations",allResults},{"passed",passed},{"failed",allResults.size()-passed},{"mac_differential",false},{"upstream_swift_executed",false}};write("results.json",QJsonDocument(report).toJson());std::cout<<"TiledLayer invocations "<<passed<<" passed, "<<allResults.size()-passed<<" failed\n";return passed==12?0:1;
}
