#include "filters/PixelFilters.h"
#include "graphics/PixelAlgorithms.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
using namespace compositor;
using namespace compositor::filters;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void fails(const std::function<void()>& action,const char* message){try{action();}catch(const std::exception&){return;}throw std::runtime_error(message);}
bool near(double a,double b){return std::abs(a-b)<1e-8;}
std::shared_ptr<const Raster> image(int w,int h,const std::function<Pixel(int,int)>& get){
    std::vector<std::uint8_t> p(std::size_t(w)*h*4);for(int y=0;y<h;++y)for(int x=0;x<w;++x){auto q=get(x,y);auto i=(std::size_t(y)*w+x)*4;p[i]=q.r;p[i+1]=q.g;p[i+2]=q.b;p[i+3]=q.a;}return Raster::fromRgba(w,h,p.data(),std::size_t(w)*4);
}
std::shared_ptr<const GrayRaster> mask(int w,int h,const std::function<std::uint8_t(int,int)>& get){auto p=std::make_shared<GrayRaster>();p->width=w;p->height=h;p->pixels.resize(std::size_t(w)*h);for(int y=0;y<h;++y)for(int x=0;x<w;++x)p->pixels[std::size_t(y)*w+x]=get(x,y);return p;}
Request request(Kind k,std::shared_ptr<const Raster> source){Request q;q.kind=k;q.source=std::move(source);q.transform={10,20,double(q.source->width),double(q.source->height)};return q;}
graphics::Rgba8View view(std::vector<std::uint8_t>& p,int w,int h){return {p,std::uint32_t(w),std::uint32_t(h),std::size_t(w)*4};}
void testSettings(){
    Settings s;s.radius=std::numeric_limits<double>::quiet_NaN();s.angle=200;s.distance=-2;s.amount=std::numeric_limits<double>::infinity();s.distortion=-120;
    auto n=s.normalized();require(n.radius==1&&n.angle==90&&n.distance==1&&n.amount==10&&n.distortion==-100,"source settings normalization");
    s.radius=300;s.amount=-4;s.distance=3000;n=s.normalized();require(n.radius==250&&n.amount==.1&&n.distance==2000,"source setting limits");
    s.radius=3;s.distance=16;require(blurMargin(Kind::GaussianBlur,s)==11&&blurMargin(Kind::MotionBlur,s)==10,"source blur margins");
}
void testDither(){
    auto white=image(8,8,[](int,int){return Pixel{255,255,255,255};});Settings s;s.ditherLevels=4;s.ditherStyle=0;
    auto result=runPixels(Kind::Dither,*white,s,1,1);require(result->pixel(0,0).r>200&&result->pixel(0,0).a==255,"opaque white dither stays bright");
    auto gray=image(8,8,[](int,int){return Pixel{128,128,128,255};});auto mid=runPixels(Kind::Dither,*gray,s,1,1);require(mid->pixel(0,0).r>0&&mid->pixel(0,0).r!=mid->pixel(1,0).r,"mid gray dither is not a black field");
}
void testMotion(){
    auto dot=image(41,41,[](int x,int y){return x==20&&y==20?Pixel{255,255,255,255}:Pixel{};});Settings s;s.distance=16;
    auto horizontal=runPixels(Kind::MotionBlur,*dot,s,1,0);require(horizontal->pixel(24,20).a>0&&horizontal->pixel(16,20).a>0&&horizontal->pixel(20,24).a==0,"source horizontal motion assertion");
    s.angle=90;auto vertical=runPixels(Kind::MotionBlur,*dot,s,1,0);require(vertical->pixel(20,24).a>0&&vertical->pixel(20,16).a>0&&vertical->pixel(24,20).a==0,"source vertical motion assertion");
    s.angle=45;auto diagonal=runPixels(Kind::MotionBlur,*dot,s,1,0);require(diagonal->pixel(23,17).a>0&&diagonal->pixel(17,23).a>0&&diagonal->pixel(17,17).a==0,"source CCW motion assertion");
    // Measured impulse variance freezes the source distance/sqrt(12) interpretation.
    double mass=0,variance=0;for(int x=0;x<41;++x){double a=horizontal->pixel(x,20).a;mass+=a;variance+=a*(x-20)*(x-20);}variance/=mass;require(std::abs(std::sqrt(variance)-16/std::sqrt(12.))<.3,"motion distance conversion");
}
void testProgressive(){
    auto stripes=image(96,96,[](int x,int){const auto v=std::uint8_t((x/2)%2?255:0);return Pixel{v,v,v,255};});
    Settings s;s.progressiveTo=5;s.progressiveStart=Point{48,16};s.progressiveEnd=Point{48,80};
    auto contrast=[](const Raster& r,int y){return std::abs(int(r.pixel(48,y).r)-int(r.pixel(50,y).r));};
    auto linear=runPixels(Kind::ProgressiveBlur,*stripes,s,1,0);
    require(contrast(*linear,10)>200&&contrast(*linear,85)<60,"linear progressive blur transition");
    s.progressiveTo=20.1;s.progressiveEnd=Point{48,48};
    auto beyond=runPixels(Kind::ProgressiveBlur,*stripes,s,1,0);
    require(contrast(*beyond,10)>200&&contrast(*beyond,80)<30,"non-integer To blurs pixels past the guide end");
    auto scaled=runPixels(Kind::ProgressiveBlur,*stripes,s,.25,0);
    require(contrast(*scaled,10)>200&&contrast(*scaled,80)<40,"scaled progressive amount still covers the strongest end");
    auto wide=image(160,160,[](int x,int){const auto v=std::uint8_t((x/8)%2?255:0);return Pixel{v,v,v,255};});
    auto fractional=request(Kind::ProgressiveBlur,wide);fractional.settings.progressiveTo=20.1;fractional.settings.progressiveStart=fractional.transform.fromUnit({.5,.1});fractional.settings.progressiveEnd=fractional.transform.fromUnit({.5,.5});
    fractional.preview=true;fractional.previewMaxEdge=80;
    auto reduced=apply(fractional);
    require(reduced.previewScale<1&&reduced.raster->width<160,"reduced progressive preview downsamples");
    auto rowRange=[&](double oy){const int w=reduced.raster->width,h=reduced.raster->height,bw=reduced.sourceBounds.width,bh=reduced.sourceBounds.height;const int y=std::clamp(int(std::floor((oy+.5-reduced.sourceBounds.y)*h/bh)),0,h-1);const int x0=std::clamp(int(std::ceil((0.5-reduced.sourceBounds.x)*w/bw)),0,w-1);const int x1=std::clamp(int(std::floor((159.5-reduced.sourceBounds.x)*w/bw)),x0,w-1);int lo=255,hi=0;for(int x=x0;x<=x1;++x){const int r=reduced.raster->pixel(x,y).r;lo=std::min(lo,r);hi=std::max(hi,r);}return hi-lo;};
    require(rowRange(16)>200&&rowRange(140)<120,"reduced preview keeps strongest blur past the guide end");
    s.progressiveTo=5;s.progressiveEnd=Point{48,80};
    s.progressiveInverted=true;auto inverted=runPixels(Kind::ProgressiveBlur,*stripes,s,1,0);
    require(contrast(*inverted,10)<60&&contrast(*inverted,85)>200,"progressive inversion");
    s.progressiveInverted=false;s.progressiveRadial=true;s.progressiveStart=Point{48,48};s.progressiveEnd=Point{72,48};
    auto radial=runPixels(Kind::ProgressiveBlur,*stripes,s,1,0);
    require(contrast(*radial,48)>200&&contrast(*radial,10)<60,"radial progressive blur transition");
    auto selection=mask(96,96,[](int,int y){return std::uint8_t(y<48?0:255);});
    auto selected=runPixels(Kind::ProgressiveBlur,*stripes,s,1,0,selection.get());
    require(selected->pixel(48,10)==stripes->pixel(48,10)&&selected->pixel(48,85)==radial->pixel(48,85),"progressive selection coverage");
    auto q=request(Kind::ProgressiveBlur,stripes);q.settings=s;q.settings.progressiveTo=0;q.settings.progressiveFrom=0;
    require(!apply(q).changed,"zero progressive blur is identity");
    q.settings.progressiveTo=5;q.settings.progressiveStart=Point{58,68};q.settings.progressiveEnd=Point{82,68};
    auto result=apply(q);require(result.changed&&result.sourceBounds.width>96&&result.sourceBounds.height>96,"progressive blur grows bounds");
    auto canvas=request(Kind::ProgressiveBlur,image(48,32,[](int,int){return Pixel{40,80,120,255};}));
    canvas.settings.progressiveTo=8;canvas.canvasWidth=48;canvas.canvasHeight=32;canvas.transform={0,0,48,32};
    auto clamped=apply(canvas);
    require(clamped.sourceBounds==PixelRect{0,0,48,32}&&clamped.raster->pixel(0,0).a==255&&clamped.raster->pixel(47,31).a==255,"canvas-filling layer keeps edge pixels without padding");
    auto leftEdge=request(Kind::ProgressiveBlur,image(32,24,[](int,int){return Pixel{40,80,120,255};}));
    leftEdge.settings.progressiveTo=8;leftEdge.canvasWidth=64;leftEdge.canvasHeight=48;leftEdge.transform={0,12,32,24};
    auto sided=apply(leftEdge);
    require(canvasTouchingSides(leftEdge.transform,32,24,64,48).left&&sided.sourceBounds.x==0&&sided.sourceBounds.width>32,"only the canvas-touching side stays unpadded");
    require(sided.raster->pixel(0,0).a==255,"left canvas edge stays opaque");
    auto translated=q;translated.transform={40,70,96,96,37,true,false};
    const auto start=translated.transform.fromUnit({.5,.5}),end=translated.transform.fromUnit({.75,.5});
    translated.settings.progressiveStart=start;translated.settings.progressiveEnd=end;
    auto identity=q;identity.transform={0,0,96,96};identity.settings.progressiveStart=Point{48,48};identity.settings.progressiveEnd=Point{72,48};
    require(apply(translated).raster->rgba()==apply(identity).raster->rgba(),"rotated flipped progressive mapping retains layer-space transition");
    q.deepSource=makeDeep(96,96,DeepImage::Format::Unorm16);auto mutableDeep=std::const_pointer_cast<DeepImage>(q.deepSource);
    for(int y=0;y<96;++y)for(int x=0;x<96;++x)mutableDeep->setPixel(x,y,{.1234f,.1234f,.1234f,1});
    result=apply(q);require(result.deep&&result.deep->format==DeepImage::Format::Unorm16,"progressive blur retains 16-bit source");
    int polls=0;q.limits.cancelled=[&]{return ++polls>12;};fails([&]{apply(q);},"progressive blur cancellation");
}
void testNoise(){
    auto gray=image(32,8,[](int x,int){return x<16?Pixel{128,128,128,255}:Pixel{};});Settings s;s.amount=10;auto color=runPixels(Kind::AddNoise,*gray,s,1,7),again=runPixels(Kind::AddNoise,*gray,s,1,7);require(color->rgba()==again->rgba(),"source stable seed");
    std::set<int> values;bool different=false;for(int y=0;y<8;++y)for(int x=0;x<32;++x){auto p=color->pixel(x,y);if(x<16){require(p.a==255&&p.r>=112&&p.r<=144,"source uniform range and alpha");values.insert(p.r);different|=p.r!=p.g;}else require(p==Pixel{},"source transparent noise");}require(values.size()>5&&different,"source color noise channels");
    s.gaussian=s.monochromatic=true;auto mono=runPixels(Kind::AddNoise,*gray,s,1,7);for(int y=0;y<8;++y)for(int x=0;x<16;++x){auto p=mono->pixel(x,y);require(p.r==p.g&&p.g==p.b&&p.a==255,"source monochrome Gaussian noise");}
    auto original=gray->rgba();graphics::addNoise(view(original,32,8),10,true,true,7);require(original==mono->rgba(),"noise equals upstream C bytes");
    require(runPixels(Kind::AddNoise,*gray,s,1,8)->rgba()!=mono->rgba(),"different seed changes pattern");
}
void testDeepBlur(){
    auto deep=makeDeep(32,16,DeepImage::Format::Unorm16);
    for(int y=0;y<16;++y)for(int x=0;x<32;++x){auto p=deep->at(x,y);p[0]=uint16_t(x<16?10001:10031);p[1]=20003;p[2]=30007;p[3]=65535;}
    auto original=deep->samples;
    for(auto kind:{Kind::GaussianBlur,Kind::MotionBlur}){
        auto q=request(kind,rasterFromDeep(*deep));q.deepSource=deep;q.settings.radius=2;q.settings.distance=8;
        q.canvasWidth=32;q.canvasHeight=16;q.transform={0,0,32,16};
        auto result=apply(q);require(result.changed&&result.deep&&result.deep->format==DeepImage::Format::Unorm16,"blur retains 16-bit pixels");
        const auto value=result.deep->at(16,8)[0];require(value>10001&&value<10031,"blur uses original sub-byte detail");
        require(deep->samples==original,"blur changed source pixels");
    }
}
void testLens(){
    auto source=image(40,30,[](int x,int y){int quadrant=(x>=20)+(y>=15)*2;return Pixel{std::uint8_t(quadrant*85),128,std::uint8_t(255-quadrant*85),255};});Settings s;auto neutral=runPixels(Kind::LensCorrection,*source,s,1,0);require(neutral->rgba()==source->rgba(),"lens zero exact identity");
    s.distortion=100;auto barrel=runPixels(Kind::LensCorrection,*source,s,1,0);require(barrel->pixel(0,0).a==255&&barrel->pixel(39,29).a==255,"source barrel corners");
    s.distortion=-100;auto pincushion=runPixels(Kind::LensCorrection,*source,s,1,0);require(pincushion->pixel(0,0).a==0&&pincushion->pixel(39,29).a==0&&pincushion->pixel(20,15)==source->pixel(20,15),"source pincushion corners and center");
    auto from=source->rgba(),dest=from;graphics::lensDistort(graphics::readOnly(view(from,40,30)),view(dest,40,30),-.35);require(dest==pincushion->rgba(),"lens equals upstream C bytes");
}
void testFill(){
    auto selected=mask(64,48,[](int x,int y){return std::uint8_t(x>=20&&x<32&&y>=16&&y<26?255:0);});auto source=image(64,48,[&](int x,int y){return selected->pixel(x,y)?Pixel{255,0,0,255}:Pixel{51,153,204,255};});
    auto q=request(Kind::ContentAwareFill,source);q.selection=SourceSelection{selected};auto result=apply(q);for(int y=0;y<48;++y)for(int x=0;x<64;++x){auto p=result.raster->pixel(x,y);require(std::abs(int(p.r)-51)<=1&&std::abs(int(p.g)-153)<=1&&std::abs(int(p.b)-204)<=1&&p.a==255,"source solid background fill");}require(source->pixel(20,16)==Pixel{255,0,0,255},"fill source immutable");
    auto stripesMask=mask(80,64,[](int x,int y){return std::uint8_t(x>=32&&x<44&&y>=26&&y<36?255:0);});auto stripes=image(80,64,[&](int x,int y){auto b=std::uint8_t((x/4)%2==0?51:204);return stripesMask->pixel(x,y)?Pixel{255,0,0,255}:Pixel{b,b,b,255};});q=request(Kind::ContentAwareFill,stripes);q.selection=SourceSelection{stripesMask};result=apply(q);int matching=0;for(int y=26;y<36;++y)for(int x=32;x<44;++x)matching+=std::abs(int(result.raster->pixel(x,y).r)-((x/4)%2==0?51:204))<=1;std::cout<<"fill stripes matched "<<matching<<" of 120\n";require(matching>=114,"source repeating texture threshold");
    q.selection=SourceSelection{mask(80,64,[](int,int){return std::uint8_t(255);})};fails([&]{apply(q);},"fill with no donor must fail");q.selection.reset();fails([&]{apply(q);},"fill with absent selection must fail");
    // Coverage outside the original source expands the layer grid while preserving placement.
    q=request(Kind::ContentAwareFill,Raster::filled(8,8,{51,153,204,255}));q.selection=SourceSelection{mask(4,4,[](int,int){return std::uint8_t(255);}),-2,2};result=apply(q);require(result.sourceBounds==PixelRect{-2,0,10,8}&&near(result.transform.x,8)&&result.raster->pixel(0,3).a==255,"fill extends into selected exterior");
}
void testCoverageAndPlacement(){
    auto src=image(19,15,[](int x,int y){return Pixel{std::uint8_t(40+x),std::uint8_t(50+y),60,128};});Settings s;s.amount=200;auto changed=runPixels(Kind::AddNoise,*src,s,1,99);auto coverage=mask(19,15,[](int x,int){return std::uint8_t(x<5?0:x<10?128:255);});auto blended=runPixels(Kind::AddNoise,*src,s,1,99,coverage.get());
    for(int y=0;y<15;++y)for(int x=0;x<19;++x){auto a=src->pixel(x,y),b=changed->pixel(x,y),p=blended->pixel(x,y);unsigned c=coverage->pixel(x,y);require(p.r==(b.r*c+a.r*(255-c)+127)/255&&p.a==128,"coverage endpoints and half blend");require(p.r<=p.a&&p.g<=p.a&&p.b<=p.a,"premultiplied filter result");}
    Transform placed{25.25,-13.5,87.3,21.4,37,true,false,Transform::Sampling::Nearest};PixelRect bounds{-7,-9,43,36};auto grown=placedGrid(placed,19,15,bounds);require(grown.rotation==placed.rotation&&grown.flipX&&grown.sampling==placed.sampling,"metadata preserved");
    for(Point pixel: {Point{0,0},Point{.5,.5},Point{8.75,11.1},Point{19,15}}){auto before=placed.fromUnit({pixel.x/19,pixel.y/15});auto after=grown.fromUnit({(pixel.x-bounds.x)/bounds.width,(pixel.y-bounds.y)/bounds.height});require(near(before.x,after.x)&&near(before.y,after.y),"rotated flipped placement preserves source pixels");}
    auto q=request(Kind::GaussianBlur,Raster::filled(10,6,{255,255,255,255}));q.settings.radius=2;q.transform=placed;q.preview=true;q.retainedBlurMargin=20;auto preview=apply(q);require(preview.sourceBounds==PixelRect{-20,-20,50,46},"retained preview blur margin");q.preview=false;auto commit=apply(q);require(commit.raster->width<preview.raster->width&&commit.raster->height<preview.raster->height,"commit removes empty blur padding");
    bool lowAlpha=false;for(int y=0;y<commit.raster->height;++y)lowAlpha|=commit.raster->pixel(0,y).a>0&&commit.raster->pixel(0,y).a<255;require(lowAlpha,"unclamped blur spreads through border");
    for(Point pixel:{Point{0,0},Point{10,6}}){auto before=placed.fromUnit({pixel.x/10,pixel.y/6});auto after=commit.transform.fromUnit({(pixel.x-commit.sourceBounds.x)/commit.sourceBounds.width,(pixel.y-commit.sourceBounds.y)/commit.sourceBounds.height});require(near(before.x,after.x)&&near(before.y,after.y),"trim keeps original pixel placement");}
}
void testRgbSplit(){
    auto source=image(64,32,[](int x,int){return x<32?Pixel{200,80,40,255}:Pixel{40,80,200,255};});
    Settings s;s.rgbSplitDistance=0;s.rgbSplitBlur=0;
    require(runPixels(Kind::RgbSplit,*source,s,1,0)->rgba()==source->rgba(),"zero RGB Split is identity");
    s.rgbSplitDistance=12;s.rgbSplitAngle=0;s.rgbSplitCenter=Point{32,16};
    auto linear=runPixels(Kind::RgbSplit,*source,s,1,0);
    require(linear->pixel(31,16)!=source->pixel(31,16)||linear->pixel(32,16)!=source->pixel(32,16),"linear RGB Split changes pixels");
    require(linear->pixel(31,16).r!=linear->pixel(31,16).b,"linear RGB Split separates channels");
    s.rgbSplitRadial=true;s.rgbSplitReach=24;
    auto radial=runPixels(Kind::RgbSplit,*source,s,1,0);
    require(radial->rgba()!=linear->rgba(),"radial RGB Split differs from linear");
    auto coverage=mask(64,32,[](int,int y){return std::uint8_t(y<16?0:255);});
    auto selected=runPixels(Kind::RgbSplit,*source,s,1,0,coverage.get());
    require(selected->pixel(16,4)==source->pixel(16,4)&&selected->pixel(16,24)==radial->pixel(16,24),"RGB Split selection coverage");
    auto q=request(Kind::RgbSplit,source);q.settings=s;
    fails([&]{apply(q);},"radial RGB Split without canvas bounds must fail instead of clipping");
    q.canvasWidth=160;q.canvasHeight=80;
    const auto radialBounds=filterWorkingBounds(q);
    require(radialBounds.x<=-10&&radialBounds.x+radialBounds.width>=150&&radialBounds.y<=-20&&radialBounds.y+radialBounds.height>=60,"radial working bounds cover the canvas");
    auto grown=apply(q);require(grown.changed&&grown.sourceBounds.width>64,"RGB Split grows bounds");
    q.settings.rgbSplitDistance=0;q.settings.rgbSplitBlur=0;require(!apply(q).changed,"neutral RGB Split apply is identity");
    q.settings.rgbSplitDistance=12;int polls=0;q.limits.cancelled=[&]{return ++polls>8;};fails([&]{apply(q);},"RGB Split cancellation");
    q.limits={};q.deepSource=makeDeep(64,32);fails([&]{apply(q);},"HDR RGB Split must fail");
    auto bright=image(32,32,[](int x,int y){return x==16&&y==16?Pixel{255,255,255,255}:Pixel{20,20,20,255};});
    Settings bloom;bloom.bloom=80;bloom.bloomThreshold=.5;
    auto filterBloom=runPixels(Kind::Bloom,*bright,bloom,1,0);
    Settings glow;glow.bloomGlow=true;glow.bloomAmount=80;glow.bloomRadius=8;
    auto glowBloom=runPixels(Kind::Bloom,*bright,glow,1,0);
    require(filterBloom->rgba()!=bright->rgba()&&glowBloom->rgba()!=bright->rgba()&&filterBloom->rgba()!=glowBloom->rgba(),"Bloom filter and Bloom / Glow kernels stay distinct");
    bloom.bloomThreshold=0;bloom.bloom=20;
    auto lowBloom=runPixels(Kind::Bloom,*bright,bloom,1,0);
    bloom.bloom=80;auto highBloom=runPixels(Kind::Bloom,*bright,bloom,1,0);
    require(lowBloom->rgba()!=highBloom->rgba(),"Bloom strength affects zero-threshold output");
    bloom.bloomThreshold=.000001;auto nearZeroBloom=runPixels(Kind::Bloom,*bright,bloom,1,0);
    require(highBloom->rgba()==nearZeroBloom->rgba(),"Bloom is continuous at zero threshold");
}
void testPreviewAndFailure(){
    auto q=request(Kind::LensCorrection,Raster::filled(3001,17,{100,100,100,255}));q.preview=true;q.settings.distortion=10;auto preview=apply(q);require(preview.raster->width==2048&&preview.raster->height==11&&near(preview.previewScale,2048./3001)&&preview.transform==q.transform,"2048 preview source scale");q.kind=Kind::AddNoise;preview=apply(q);require(preview.raster->width==3001&&preview.previewScale==1,"noise preview remains full resolution");
    q.kind=Kind::LensCorrection;q.settings.distortion=0;auto noop=apply(q);require(!noop.changed&&noop.raster==q.source,"neutral lens has no commit");q.kind=Kind::GaussianBlur;q.selection=SourceSelection{mask(3001,17,[](int,int){return std::uint8_t(0);})};noop=apply(q);require(!noop.changed&&noop.raster==q.source,"present empty selection has no commit");
    q.selection.reset();q.limits.maxWorkingBytes=1;fails([&]{apply(q);},"working budget must fail");q.limits={};q.limits.cancelled=[]{return true;};fails([&]{apply(q);},"pre-cancel must fail");int polls=0;q.source=Raster::filled(80,60,{255,255,255,255});q.transform={0,0,80,60};q.limits.cancelled=[&]{return ++polls>8;};fails([&]{apply(q);},"in-flight Gaussian cancellation must fail");require(polls>8&&q.source->pixel(0,0).a==255,"cancel preserves input");
    auto large=request(Kind::GaussianBlur,Raster::filled(7000,5000,{100,100,100,255}));large.preview=true;large.settings.radius=1;large.retainedBlurMargin=blurMargin(Kind::GaussianBlur,large.settings);large.limits.maxWorkingBytes=1200000000;auto largePreview=apply(large);require(largePreview.changed&&largePreview.raster->width<=2048&&largePreview.previewScale<1,"large gaussian preview downsamples before the working budget");
    // 10000×10000 (100 MP) Apply: triple-tile 1.2 GB charge failed; one tiled Raster plus linear intermediates fits the 4 GB default.
    const auto hundredMegapixel=estimatedWorkingBytes(10000,10000);
    require(hundredMegapixel>1200000000&&hundredMegapixel<=Limits{}.maxWorkingBytes,"100-megapixel Apply fits the 4 GB filter cap and exceeds the 1.2 GB import/merge cap");
    // Do not materialize 100 MP: reuse the shared count path via a 2×2 Apply under explicit budgets.
    auto tiny=request(Kind::AddNoise,Raster::filled(2,2,{100,100,100,255}));
    tiny.limits.maxWorkingBytes=estimatedWorkingBytes(2,2)-1;fails([&]{apply(tiny);},"apply fails when count exceeds the working budget");
    tiny.limits.maxWorkingBytes=estimatedWorkingBytes(2,2);require(apply(tiny).raster->width==2,"apply passes when count fits the working budget");
    q.limits={};q.kind=Kind(999);fails([&]{apply(q);},"unknown filter must fail");q.kind=Kind::AddNoise;auto malformed=std::make_shared<GrayRaster>();malformed->width=1;malformed->height=1;q.selection=SourceSelection{malformed};fails([&]{apply(q);},"malformed coverage must fail");
}
int sourceParity(){
    auto q=request(Kind::GaussianBlur,image(40,20,[](int x,int){return x<20?Pixel{255,255,255,255}:Pixel{};}));q.settings.radius=3;auto result=apply(q);
    // FilterTests.swift:28 indexes the raw committed buffer. When trimming
    // makes width < 39, x=38 crosses into the next row rather than sampling
    // transparent exterior through Raster::pixel's coordinate bounds check.
    const auto bytes=result.raster->rgba();auto alpha=[&](int x){const auto offset=(size_t(10)*result.raster->width+size_t(x))*4+3;require(offset<bytes.size(),"Upstream Gaussian raw offset exceeds committed allocation");return int(bytes[offset]);};
    std::cout<<"upstream FilterTests.swift:29-31 source Gaussian border assertions: alpha(0)="<<alpha(0)<<" expected255; alpha(20)="<<alpha(20)<<" expected20..235; alpha(38)="<<alpha(38)<<" expected0; grid="<<result.raster->width<<"x"<<result.raster->height<<"\n";
    bool passed=alpha(0)==255&&alpha(20)>20&&alpha(20)<235&&alpha(38)==0;
    std::cout<<(passed?"PASS":"FAIL")<<" source Gaussian raster assertions; upstream grow/unclamped/trim behavior conflicts with border assertion. Mac reference execution remains required.\n";return passed?0:1;
}
int benchmark(){
    auto source=image(512,512,[](int x,int y){return Pixel{std::uint8_t(x%256),std::uint8_t(y%256),128,255};});
    std::cout<<"CPU software /O2, 512x512 opaque source, apply includes growth, filter and final trim, four sequential runs; first cold, others warm\n";
    for(auto kind:{Kind::GaussianBlur,Kind::MotionBlur,Kind::AddNoise,Kind::LensCorrection}){
        auto q=request(kind,source);q.settings.radius=3;q.settings.distance=16;q.settings.distortion=-50;q.seed=7;
        for(int run=0;run<4;++run){auto start=std::chrono::steady_clock::now();auto output=apply(q);double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();std::cout<<"kind="<<int(kind)<<" run="<<run<<" elapsed_ms="<<ms<<" result="<<output.raster->width<<"x"<<output.raster->height<<"\n";}
    }return 0;
}
}
int main(int argc,char** argv){try{if(argc==2&&std::string(argv[1])=="--source-parity")return sourceParity();if(argc==2&&std::string(argv[1])=="--benchmark")return benchmark();testSettings();testDither();testMotion();testProgressive();testDeepBlur();testRgbSplit();testNoise();testLens();testFill();testCoverageAndPlacement();testPreviewAndFailure();std::cout<<"PASS filters: source settings, motion geometry, RGB Split, Bloom / Glow, exact C noise/lens, fill, selection, immutable placement, preview, budgets, cancellation\n";return 0;}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<"\n";return 1;}}

