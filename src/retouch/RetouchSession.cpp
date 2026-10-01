// Source: CloneStamp.swift, BlurTool.swift, SmudgeLiquify.swift, BrushStroke.heal.
// Copyright (c) 2026 Wonder Assembly LLC; MIT license in LICENSE.
#include "RetouchSession.h"
#include "graphics/PixelAlgorithms.h"
#include "graphics/RasterSampling.h"
#include "graphics/SamplingSource.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <mutex>
#include <stdexcept>

namespace compositor::retouch {
namespace {
uint8_t byte(double value){return uint8_t(std::clamp(std::round(value),0.,255.));}
bool pointValid(Point p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::abs(p.x)<=1000000&&std::abs(p.y)<=1000000;}
bool healing(Mode mode){return mode==Mode::HealContentAware||mode==Mode::HealCreateTexture||mode==Mode::HealProximity;}
bool warp(Mode mode){return mode==Mode::Smudge||mode==Mode::Liquify;}
Pixel over(Pixel base,Pixel source,double amount){const double remain=1-source.a/255.*amount;return {byte(source.r*amount+base.r*remain),byte(source.g*amount+base.g*remain),byte(source.b*amount+base.b*remain),byte(source.a*amount+base.a*remain)};}
Pixel lerp(Pixel base,Pixel source,double amount){return {byte(base.r+(source.r-base.r)*amount),byte(base.g+(source.g-base.g)*amount),byte(base.b+(source.b-base.b)*amount),byte(base.a+(source.a-base.a)*amount)};}
void validSize(int w,int h){if(w<1||h<1||w>30000||h>30000||uint64_t(w)*h>100000000)throw std::invalid_argument("Retouch dimensions exceed budget");}
void validCanvas(int w,int h){if(w<1||h<1||w>30000||h>30000)throw std::invalid_argument("Invalid retouch canvas dimensions");}
void validRaster(const Raster& image){validSize(image.width,image.height);if(image.tiles.size()!=size_t((image.width+255)/256)*size_t((image.height+255)/256))throw std::invalid_argument("Invalid retouch tile array");for(auto&tile:image.tiles)if(!tile)throw std::invalid_argument("Missing retouch tile");}
std::shared_ptr<const Raster> fromPixels(const std::vector<Pixel>& pixels,int w,int h){
    auto image=std::make_shared<Raster>();image->width=w;image->height=h;const int columns=(w+255)/256,rows=(h+255)/256;
    for(int ty=0;ty<rows;++ty)for(int tx=0;tx<columns;++tx){auto tile=std::make_shared<Raster::Tile>();for(int y=0;y<std::min(256,h-ty*256);++y)std::copy_n(pixels.data()+size_t(ty*256+y)*w+tx*256,std::min(256,w-tx*256),tile->pixels.data()+size_t(y)*256);image->tiles.push_back(std::move(tile));}return image;
}
std::vector<Pixel> pixelsOf(const Raster& image){std::vector<Pixel> result(size_t(image.width)*image.height);for(int y=0;y<image.height;++y)for(int x=0;x<image.width;++x)result[size_t(y)*image.width+x]=image.pixel(x,y);return result;}
std::shared_ptr<const Raster> maskRaster(const std::shared_ptr<const GrayRaster>& gray){
    if(!gray)throw std::invalid_argument("Missing retouch mask");validSize(gray->width,gray->height);if(gray->pixels.size()!=size_t(gray->width)*gray->height)throw std::invalid_argument("Invalid retouch mask storage");
    std::vector<Pixel> pixels(gray->pixels.size());for(size_t i=0;i<pixels.size();++i){const auto value=gray->pixels[i];pixels[i]={value,value,value,255};}return fromPixels(pixels,gray->width,gray->height);
}
std::shared_ptr<const Raster> gaussian(const Raster& source,double sigma,bool clampEdges,Metrics& metrics){
    const int w=source.width,h=source.height,r=int(std::ceil(sigma*3));const size_t n=size_t(w)*h;
    std::vector<double> kernel(size_t(r)*2+1);double total=0;for(int i=-r;i<=r;++i){double weight=std::exp(-double(i*i)/(2*sigma*sigma));kernel[size_t(i+r)]=weight;total+=weight;}for(auto& v:kernel)v/=total;
    std::vector<float> scratch(n);std::vector<Pixel> output(n);metrics.gaussianScratchPixels=n;metrics.workingBufferPixels+=n;
    constexpr std::array<uint8_t Pixel::*,4> channels{&Pixel::r,&Pixel::g,&Pixel::b,&Pixel::a};
    for(auto channel:channels){
        for(int y=0;y<h;++y)for(int x=0;x<w;++x){double value=0;for(int k=-r;k<=r;++k){int xx=x+k;if(clampEdges)xx=std::clamp(xx,0,w-1);if(xx>=0&&xx<w)value+=(source.pixel(xx,y).*channel)*kernel[size_t(k+r)];}scratch[size_t(y)*w+x]=float(value);}
        for(int y=0;y<h;++y)for(int x=0;x<w;++x){double value=0;for(int k=-r;k<=r;++k){int yy=y+k;if(clampEdges)yy=std::clamp(yy,0,h-1);if(yy>=0&&yy<h)value+=scratch[size_t(yy)*w+x]*kernel[size_t(k+r)];}output[size_t(y)*w+x].*channel=byte(value);}
    }
    return fromPixels(output,w,h);
}
using PixelReader=std::function<Pixel(int,int)>;
// The immutable base grid is evaluated only where a stroke asks for pixels.
// Copies share completed tiles; a later dab detaches any published tile first.
struct SparseWorking {
    int width,height;PixelReader base;
    std::map<size_t,std::shared_ptr<Raster::Tile>> tiles;
    SparseWorking(int w,int h,PixelReader reader):width(w),height(h),base(std::move(reader)){}
    size_t key(int x,int y)const{return size_t(y/256)*size_t((width+255)/256)+size_t(x/256);}
    Pixel pixel(int x,int y)const{if(x<0||y<0||x>=width||y>=height)return {};auto found=tiles.find(key(x,y));return found==tiles.end()?base(x,y):found->second->pixels[size_t(y%256)*256+x%256];}
    Pixel& edit(int x,int y){
        const auto id=key(x,y);auto found=tiles.find(id);
        if(found==tiles.end()){
            if(uint64_t(tiles.size()+1)*256*256>100000000)throw std::length_error("Retouch working tiles exceed budget");
            auto tile=std::make_shared<Raster::Tile>();const int left=x/256*256,top=y/256*256;
            for(int yy=0;yy<std::min(256,height-top);++yy)for(int xx=0;xx<std::min(256,width-left);++xx)tile->pixels[size_t(yy)*256+xx]=base(left+xx,top+yy);
            found=tiles.emplace(id,std::move(tile)).first;
        }else if(found->second.use_count()!=1)found->second=std::make_shared<Raster::Tile>(*found->second);
        return found->second->pixels[size_t(y%256)*256+x%256];
    }
    uint64_t retainedPixels()const{return uint64_t(tiles.size())*256*256;}
};
// Same horizontal float intermediate and vertical double accumulation as the
// dense Gaussian, evaluated in 256-pixel tiles with the required edge halo.
struct SparseGaussian {
    int width,height,radius;bool clampEdges;PixelReader base;std::vector<double> kernel;
    std::map<size_t,std::pair<std::shared_ptr<const Raster::Tile>,uint64_t>> tiles;
    uint64_t tick{};Metrics& stats;
    SparseGaussian(int w,int h,double sigma,bool clamp,PixelReader reader,Metrics& metrics):width(w),height(h),radius(int(std::ceil(sigma*3))),clampEdges(clamp),base(std::move(reader)),stats(metrics){
        double total=0;for(int i=-radius;i<=radius;++i){const double weight=std::exp(-double(i*i)/(2*sigma*sigma));kernel.push_back(weight);total+=weight;}for(auto& v:kernel)v/=total;
    }
    Pixel source(int x,int y)const{if(clampEdges){x=std::clamp(x,0,width-1);y=std::clamp(y,0,height-1);}return x<0||y<0||x>=width||y>=height?Pixel{}:base(x,y);}
    Pixel pixel(int x,int y){
        const size_t id=size_t(y/256)*size_t((width+255)/256)+size_t(x/256);auto found=tiles.find(id);
        if(found==tiles.end()){
            const int left=x/256*256,top=y/256*256,w=std::min(256,width-left),h=std::min(256,height-top),rows=h+radius*2;
            auto tile=std::make_shared<Raster::Tile>();std::vector<float> scratch(size_t(w)*rows);
            stats.gaussianScratchPixels=std::max(stats.gaussianScratchPixels,uint64_t(scratch.size()));
            constexpr std::array<uint8_t Pixel::*,4> channels{&Pixel::r,&Pixel::g,&Pixel::b,&Pixel::a};
            for(auto channel:channels){
                for(int yy=0;yy<rows;++yy)for(int xx=0;xx<w;++xx){double value=0;for(int k=-radius;k<=radius;++k)value+=(source(left+xx+k,top+yy-radius).*channel)*kernel[size_t(k+radius)];scratch[size_t(yy)*w+xx]=float(value);}
                for(int yy=0;yy<h;++yy)for(int xx=0;xx<w;++xx){double value=0;for(int k=-radius;k<=radius;++k)value+=scratch[size_t(yy+k+radius)*w+xx]*kernel[size_t(k+radius)];tile->pixels[size_t(yy)*256+xx].*channel=byte(value);}
            }
            if(tiles.size()==32){auto oldest=std::min_element(tiles.begin(),tiles.end(),[](const auto&a,const auto&b){return a.second.second<b.second.second;});tiles.erase(oldest);}
            found=tiles.emplace(id,std::make_pair(std::move(tile),++tick)).first;stats.workingBufferPixels=std::max(stats.workingBufferPixels,uint64_t(tiles.size())*256*256);
        }else found->second.second=++tick;
        return found->second.first->pixels[size_t(y%256)*256+x%256];
    }
};
}
void validateCanvasExtent(int width,int height){validCanvas(width,height);}
std::shared_ptr<const graphics::SamplingSource> compositeSource(Document document){
    validateDocument(document);
    struct Cache{Document document;std::mutex mutex;uint64_t tick{};std::map<size_t,std::pair<std::shared_ptr<const Raster>,uint64_t>> tiles;};
    auto cache=std::make_shared<Cache>();cache->document=std::move(document);
    auto result=std::make_shared<graphics::SamplingSource>();result->width=cache->document.width;result->height=cache->document.height;result->identity=cache;
    result->rgba=[cache](int x,int y){
        const auto& d=cache->document;if(x<0||y<0||x>=d.width||y>=d.height)return Pixel{};std::lock_guard lock(cache->mutex);
        const size_t id=size_t(y/256)*size_t((d.width+255)/256)+size_t(x/256);auto found=cache->tiles.find(id);
        if(found==cache->tiles.end()){
            const int left=x/256*256,top=y/256*256;auto tile=SoftwareRenderer().render(d,left,top,std::min(256,d.width-left),std::min(256,d.height-top));
            if(cache->tiles.size()==32){auto oldest=std::min_element(cache->tiles.begin(),cache->tiles.end(),[](const auto&a,const auto&b){return a.second.second<b.second.second;});cache->tiles.erase(oldest);}
            found=cache->tiles.emplace(id,std::make_pair(std::move(tile),++cache->tick)).first;
        }else found->second.second=++cache->tick;
        return found->second.first->pixel(x%256,y%256);
    };return result;
}
void CloneAlignment::setSource(Point p){if(!pointValid(p))return;source_=p;offset_.reset();}
std::optional<Point> CloneAlignment::strokeOffset(Point p)const{if(!source_||!pointValid(p))return {};if(aligned&&offset_)return offset_;return Point{std::round(source_->x-p.x),std::round(source_->y-p.y)};}
std::optional<Point> CloneAlignment::beginStroke(Point p){auto result=strokeOffset(p);if(result)offset_=result;return result;}
std::optional<Point> CloneAlignment::samplePoint(Point p,bool active)const{if(!source_)return {};if(offset_&&(aligned||active))return Point{p.x+offset_->x,p.y+offset_->y};return source_;}

struct RetouchSession::Impl {
    std::shared_ptr<const Raster> original,published,sample;
    std::shared_ptr<const GrayRaster> originalMask,selection;
    mutable std::shared_ptr<const GrayRaster> grayPreview;
    mutable std::shared_ptr<const Raster> grayPreviewOwner;
    mutable std::shared_ptr<const Raster> warpPreview;
    mutable std::shared_ptr<const LayerRenderPreview> sparsePublishedPreview;
    mutable uint64_t warpPreviewRevision{~uint64_t{0}};
    Transform transform;
    int width,height;
    Settings settings;
    Sources sources;
    Metrics stats;
    std::unique_ptr<graphics::BrushSession> coverage;
    std::shared_ptr<graphics::D3D11BrushCoverage> accelerator;
    std::vector<Pixel> working,liquifyScratch;
    mutable int dirtyX0{},dirtyY0{},dirtyX1{},dirtyY1{};
    mutable bool dirtyAny{},warpPreviewShown{};
    mutable std::shared_ptr<const Raster> denseRaster;
    PixelReader sparseSample;
    std::unique_ptr<SparseGaussian> sparseGaussian;
    std::unique_ptr<SparseWorking> sparseWorking;
    bool sparseCanvas{};
    std::vector<std::array<float,4>> carried;
    std::optional<Point> last;
    bool started{},finished{},coverageStarted{},emptySelection{},maskTarget{};
    Layer originalLayer;
    std::unique_ptr<graphics::GrowingBrushSession> growing;
    mutable std::shared_ptr<const graphics::GrowingBrushSnapshot> rasterSnapshotOwner;
    mutable Layer materialized;
    Impl(std::shared_ptr<const Raster> image,Transform placement,int w,int h,Settings options,Sources input,std::shared_ptr<const GrayRaster> clip,std::shared_ptr<graphics::D3D11BrushCoverage> gpu)
        :original(std::move(image)),published(original),selection(std::move(clip)),transform(placement),width(w),height(h),settings(options),sources(std::move(input)),accelerator(std::move(gpu)){
        if(!original)throw std::invalid_argument("Missing retouch raster");validRaster(*original);validCanvas(w,h);sparseCanvas=uint64_t(w)*h>=25000000;if(!placement.valid())throw std::invalid_argument("Invalid retouch transform");
        if(int(settings.mode)<int(Mode::Clone)||int(settings.mode)>int(Mode::Liquify))throw std::invalid_argument("Invalid retouch mode");
        if(!std::isfinite(settings.radius)||settings.radius<.5||settings.radius>1000||!std::isfinite(settings.hardness)||settings.hardness<0||settings.hardness>1||!std::isfinite(settings.opacity)||settings.opacity<.01||settings.opacity>1)throw std::invalid_argument("Invalid retouch tip");
        if(selection){if(selection->width!=w||selection->height!=h||!selection->validStorage())throw std::invalid_argument("Retouch selection must use document grid");emptySelection=!selection->hasCoverage();}
        for(auto imageSource:{sources.currentLayer,sources.allLayers})if(imageSource){validRaster(*imageSource);if(imageSource->width!=w||imageSource->height!=h)throw std::invalid_argument("Retouch sample must use document grid");}
        for(auto source:{sources.currentLayerSource,sources.allLayersSource})if(source&&(!source->identity||!source->rgba||source->gray||source->width!=w||source->height!=h))throw std::invalid_argument("Retouch lazy sample must use document RGBA grid");
    }
    Impl(Layer layer,int w,int h,Settings options,Sources input,std::shared_ptr<const GrayRaster> clip,std::shared_ptr<graphics::D3D11BrushCoverage> gpu,bool mask,uint64_t budget)
        :Impl(mask?maskRaster(layer.mask?layer.mask->raster:nullptr):layer.raster?layer.raster:Raster::filled(1,1),mask&&layer.mask?layer.mask->placement.value_or(layer.transform):layer.transform,w,h,options,std::move(input),std::move(clip),std::move(gpu)){
        if(mask&&options.mode!=Mode::Blur)throw std::invalid_argument("Only Blur supports mask retouch");
        if(!mask&&!layer.raster&&(options.mode==Mode::Blur||warp(options.mode)))throw std::invalid_argument("Blur and warp require image pixels");
        originalLayer=std::move(layer);maskTarget=mask;if(mask)originalMask=originalLayer.mask->raster;else original=originalLayer.raster;
        published=original;graphics::BrushSessionSettings brush;brush.radius=warp(settings.mode)?std::max(1.,settings.radius)+2:settings.radius;brush.hardness=warp(settings.mode)?1:settings.hardness;brush.opacity=1;
        growing=std::make_unique<graphics::GrowingBrushSession>(originalLayer,brush,w,h,accelerator,selection,mask,budget,tileCompositor());
    }
    graphics::GrowingTileCompositor tileCompositor(){return [this](const graphics::GrowingTileInput& input,std::span<Pixel> output){
        for(int y=0;y<input.rect.height;++y)for(int x=0;x<input.rect.width;++x){const auto i=size_t(y)*256+x;const double sx=input.rect.x+x+.5,sy=input.rect.y+y+.5;
            const Point doc{input.mapping.tx+sx*input.mapping.a+sy*input.mapping.c,input.mapping.ty+sx*input.mapping.b+sy*input.mapping.d};
            const auto base=input.original[i];const double amount=input.rawCoverage[size_t(y)*input.rect.width+x]/255.*selected(doc);
            if(healing(settings.mode))output[i]=over(base,{31,31,31,255},amount*.45);
            else if(warp(settings.mode))output[i]=lerp(base,workingSample(doc),amount);
            else {const auto offset=settings.mode==Mode::Clone?*sources.cloneOffset:Point{};output[i]=over(base,sampleAt({doc.x+offset.x,doc.y+offset.y}),amount*settings.opacity);}
        }};}
    const Layer& materialize()const{auto snapshot=growing->preview();if(snapshot!=rasterSnapshotOwner){materialized=snapshot->materializeLayer();rasterSnapshotOwner=std::move(snapshot);}return materialized;}
    Point documentPoint(int x,int y)const{return transform.fromUnit({(x+.5)/original->width,(y+.5)/original->height});}
    double selected(Point p)const{return selection?selection->pixel(int(std::floor(p.x)),int(std::floor(p.y)))/255.:1;}
    PixelReader currentReader()const{
        if(sources.currentLayerSource){auto source=sources.currentLayerSource;return [source](int x,int y){return source->rgba(x,y);};}
        if(sources.currentLayer){auto raster=sources.currentLayer;return [raster](int x,int y){return raster->pixel(x,y);};}
        const auto image=original;const auto gray=originalMask;const auto placement=transform;const bool mask=maskTarget;const auto bg=mask?graphics::cachedMaskBackground(gray):uint8_t{0};
        return [image,gray,placement,mask,bg](int x,int y){const auto unit=placement.toUnit({x+.5,y+.5});if(mask){const auto v=byte(graphics::sampleGray(*gray,unit,placement.sampling,bg)*255);return Pixel{v,v,v,255};}return image?graphics::sampleRaster(*image,unit,placement.sampling):Pixel{};};
    }
    Pixel sampleAt(Point doc)const{
        if(sparseSample)return graphics::sampleRasterPixels(width,height,sparseSample,{doc.x/width,doc.y/height},Transform::Sampling::Smooth);
        return graphics::sampleRaster(*sample,{doc.x/width,doc.y/height},Transform::Sampling::Smooth);
    }
    void noteDirty(int x0,int y0,int x1,int y1){x0=std::clamp(x0,0,width);y0=std::clamp(y0,0,height);x1=std::clamp(x1,0,width);y1=std::clamp(y1,0,height);if(!dirtyAny){dirtyX0=x0;dirtyY0=y0;dirtyX1=x1;dirtyY1=y1;dirtyAny=true;return;}dirtyX0=std::min(dirtyX0,x0);dirtyY0=std::min(dirtyY0,y0);dirtyX1=std::max(dirtyX1,x1);dirtyY1=std::max(dirtyY1,y1);}
    Pixel workingPixel(int x,int y)const{return sparseWorking?sparseWorking->pixel(x,y):working[size_t(y)*width+x];}
    Pixel& editWorking(int x,int y){if(sparseWorking){auto& pixel=sparseWorking->edit(x,y);stats.workingBufferPixels=std::max(stats.workingBufferPixels,sparseWorking->retainedPixels());return pixel;}return working[size_t(y)*width+x];}
    std::shared_ptr<const LayerRenderPreview> sparseWarpPreview()const{
        if(sparsePublishedPreview&&warpPreviewRevision==stats.warpDabs)return sparsePublishedPreview;
        auto snapshot=std::make_shared<const SparseWorking>(*sparseWorking);
        double left=double(width),top=double(height),right=0,bottom=0;
        for(const auto point:std::array<Point,4>{{{0,0},{1,0},{0,1},{1,1}}}){const auto p=transform.fromUnit(point);left=std::min(left,p.x);top=std::min(top,p.y);right=std::max(right,p.x);bottom=std::max(bottom,p.y);}
        for(const auto& [id,tile]:snapshot->tiles){(void)tile;const int x=int(id%size_t((width+255)/256))*256,y=int(id/size_t((width+255)/256))*256;left=std::min(left,double(x));top=std::min(top,double(y));right=std::max(right,double(x+256));bottom=std::max(bottom,double(y+256));}
        if(sources.currentLayer||sources.currentLayerSource){left=0;top=0;right=width;bottom=height;}
        int x=std::clamp(int(std::floor(left))-1,0,width-1),y=std::clamp(int(std::floor(top))-1,0,height-1);
        // Binary extents retain exact half-pixel phase through normalized unit
        // coordinates. Extra coverage is lazy and stays on the document grid.
        const int w=std::min(width,int(std::bit_ceil(unsigned(std::clamp(int(std::ceil(right))+1,x+1,width)-x))));
        const int h=std::min(height,int(std::bit_ceil(unsigned(std::clamp(int(std::ceil(bottom))+1,y+1,height)-y))));
        x=std::min(x,width-w);y=std::min(y,height-h);
        auto source=std::make_shared<graphics::SamplingSource>();source->width=w;source->height=h;source->alignmentX=-x;source->alignmentY=-y;source->identity=snapshot;
        source->rgba=[snapshot,x,y](int xx,int yy){return snapshot->pixel(xx+x,yy+y);};
        auto result=std::make_shared<LayerRenderPreview>();result->layer=originalLayer;result->layer.raster=Raster::filled(1,1);result->layer.transform={double(x),double(y),double(w),double(h)};result->layer.shapeJson.clear();
        if(result->layer.mask&&!result->layer.mask->placement)result->layer.mask->placement=originalLayer.transform;
        result->identity=snapshot;result->lineage=originalLayer.raster;result->imageSource=source;
        result->image=[source](Point unit,Transform::Sampling sampling){return graphics::sampleRasterPixels(source->width,source->height,source->rgba,unit,sampling);};sparsePublishedPreview=result;warpPreviewRevision=stats.warpDabs;return result;
    }
    std::shared_ptr<const Raster> currentSample(){
        if(sources.currentLayer)return sources.currentLayer;
        if(sources.currentLayerSource){std::vector<Pixel> output(size_t(width)*height);for(int y=0;y<height;++y)for(int x=0;x<width;++x)output[size_t(y)*width+x]=sources.currentLayerSource->rgba(x,y);stats.workingBufferPixels+=output.size();return fromPixels(output,width,height);}
        std::vector<Pixel> output(size_t(width)*height);const uint8_t bg=maskTarget?graphics::cachedMaskBackground(originalMask):uint8_t{0};
        for(int y=0;y<height;++y)for(int x=0;x<width;++x){const auto unit=transform.toUnit({x+.5,y+.5});
            if(maskTarget){const auto v=byte(graphics::sampleGray(*originalMask,unit,transform.sampling,bg)*255);output[size_t(y)*width+x]={v,v,v,255};}
            else output[size_t(y)*width+x]=original?graphics::sampleRaster(*original,unit,transform.sampling):Pixel{};}
        ++stats.documentRasterizations;stats.workingBufferPixels+=output.size();return fromPixels(output,width,height);
    }
    void prepare(){
        if(settings.mode==Mode::Clone){
            if(!sources.cloneOffset||!pointValid(*sources.cloneOffset))throw std::invalid_argument("Clone source must be set before painting");
            if(settings.sampleAllLayers){if(sources.allLayersSource){auto source=sources.allLayersSource;sparseSample=[source](int x,int y){return source->rgba(x,y);};}else sample=sources.allLayers;if(!sample&&!sparseSample)throw std::invalid_argument("All-layer clone requires a frozen composite sample");}
            else if(sparseCanvas||sources.currentLayerSource)sparseSample=currentReader();else sample=currentSample();
        }
        else if(settings.mode==Mode::Blur){const double sigma=settings.blurRadius>0?std::clamp(settings.blurRadius,.1,250.):std::clamp(settings.radius*2/10,1.5,30.);if(sparseCanvas){sparseGaussian=std::make_unique<SparseGaussian>(width,height,sigma,maskTarget,currentReader(),stats);sparseSample=[this](int x,int y){return sparseGaussian->pixel(x,y);};}else sample=gaussian(*currentSample(),sigma,maskTarget,stats);}
        else if(warp(settings.mode)){if(sparseCanvas)sparseWorking=std::make_unique<SparseWorking>(width,height,currentReader());else {working=pixelsOf(*currentSample());stats.workingBufferPixels+=working.size();}}
        graphics::BrushSessionSettings brush;brush.radius=warp(settings.mode)?std::max(1.,settings.radius)+2:settings.radius;brush.hardness=warp(settings.mode)?1:settings.hardness;brush.opacity=1;
        if(!growing)coverage=std::make_unique<graphics::BrushSession>(original,brush,accelerator,nullptr,graphics::BrushSessionGeometry::forLayer(transform,original->width,original->height,width,height),graphics::BrushSession::Output::CoverageOnly);
    }
    Pixel workingSample(Point doc)const{
        // The sample image lives at pixel edges; interpolate its pixel centers.
        if(doc.x<0||doc.y<0||doc.x>=width||doc.y>=height)return {};
        const double xx=doc.x-.5,yy=doc.y-.5;const int x=int(std::floor(xx)),y=int(std::floor(yy));const double fx=xx-x,fy=yy-y;
        const auto get=[&](int a,int b){return workingPixel(std::clamp(a,0,width-1),std::clamp(b,0,height-1));};
        const auto p00=get(x,y),p10=get(x+1,y),p01=get(x,y+1),p11=get(x+1,y+1);
        const auto channel=[&](uint8_t Pixel::*m){return byte((1-fy)*((1-fx)*(p00.*m)+fx*(p10.*m))+fy*((1-fx)*(p01.*m)+fx*(p11.*m)));};return {channel(&Pixel::r),channel(&Pixel::g),channel(&Pixel::b),channel(&Pixel::a)};
    }
    void compose(){
        auto result=std::make_shared<Raster>(*published);bool changed=false;const size_t columns=size_t((original->width+255)/256);
        for(const auto& [key,tile]:coverage->coverageTiles()){
            const int x0=int(key%columns)*256,y0=int(key/columns)*256;std::shared_ptr<Raster::Tile> copy;
            for(uint32_t y=0;y<tile->height;++y)for(uint32_t x=0;x<tile->width;++x){const size_t local=size_t(y)*256+x;const auto base=original->tiles[key]->pixels[local];const auto doc=documentPoint(x0+int(x),y0+int(y));
                const double amount=tile->preview[size_t(y)*tile->width+x]/255.*selected(doc);Pixel next=base;
                if(healing(settings.mode))next=over(base,{31,31,31,255},amount*.45);
                else if(warp(settings.mode))next=lerp(base,workingSample(doc),amount);
                else {const auto offset=settings.mode==Mode::Clone?*sources.cloneOffset:Point{};next=over(base,sampleAt({doc.x+offset.x,doc.y+offset.y}),amount*settings.opacity);}
                if(next!=published->tiles[key]->pixels[local]){if(!copy){copy=std::make_shared<Raster::Tile>(*published->tiles[key]);++stats.publishedTileCopies;}copy->pixels[local]=next;}
            }
            if(copy){result->tiles[key]=std::move(copy);changed=true;}
        }
        if(changed)published=std::move(result);
    }
    float weight(float u)const{if(u>=1)return 0;const float h=float(std::clamp(settings.hardness,0.,.98));if(u<=h)return 1;const float t=(1-u)/(1-h);return t*t*(3-2*t);}
    void pickup(Point p){const int r=int(std::ceil(std::max(1.,settings.radius))),side=r*2+1,cx=int(std::round(p.x)),cy=int(std::round(p.y));carried.assign(size_t(side)*side,{});
        for(int dy=-r;dy<=r;++dy)for(int dx=-r;dx<=r;++dx){const int x=cx+dx,y=cy+dy;if(x<0||y<0||x>=width||y>=height)continue;const auto c=workingPixel(x,y);carried[size_t(dy+r)*side+dx+r]={float(c.r),float(c.g),float(c.b),float(c.a)};}}
    void smudge(Point p){const double radius=std::max(1.,settings.radius);const int r=int(std::ceil(radius)),side=2*r+1,cx=int(std::round(p.x)),cy=int(std::round(p.y));noteDirty(cx-r,cy-r,cx+r+1,cy+r+1);
        constexpr std::array<uint8_t Pixel::*,4> channels{&Pixel::r,&Pixel::g,&Pixel::b,&Pixel::a};
        for(int dy=-r;dy<=r;++dy)for(int dx=-r;dx<=r;++dx){int x=cx+dx,y=cy+dy;if(x<0||y<0||x>=width||y>=height)continue;float w=weight(std::sqrt(float(dx*dx+dy*dy))/float(radius));if(w<=0)continue;
            auto& under=editWorking(x,y);auto& color=carried[size_t(dy+r)*side+dx+r];const float keep=float(std::clamp(settings.opacity,0.,1.));for(size_t k=0;k<4;++k){float painted=float(under.*channels[k])+(color[k]-float(under.*channels[k]))*w*keep;under.*channels[k]=byte(painted);color[k]=painted;}}
    }
    void push(Point a,Point b){const double radius=std::max(1.,settings.radius);const int r=int(std::ceil(radius)),cx=int(std::round(b.x)),cy=int(std::round(b.y));const float mx=float(b.x-a.x)*float(settings.opacity),my=float(b.y-a.y)*float(settings.opacity);
        const int margin=int(std::ceil(std::max(std::abs(mx),std::abs(my))))+2,x0=std::max(0,cx-r-margin),x1=std::min(width-1,cx+r+margin),y0=std::max(0,cy-r-margin),y1=std::min(height-1,cy+r+margin);
        if(x0>x1||y0>y1)return;const int cw=x1-x0+1,ch=y1-y0+1;validSize(cw,ch);noteDirty(x0,y0,x1+1,y1+1);liquifyScratch.resize(size_t(cw)*ch);auto& scratch=liquifyScratch;for(int y=0;y<ch;++y)for(int x=0;x<cw;++x)scratch[size_t(y)*cw+x]=workingPixel(x+x0,y+y0);
        constexpr std::array<uint8_t Pixel::*,4> channels{&Pixel::r,&Pixel::g,&Pixel::b,&Pixel::a};
        for(int dy=-r;dy<=r;++dy)for(int dx=-r;dx<=r;++dx){const int x=cx+dx,y=cy+dy;if(x<x0||y<y0||x>x1||y>y1)continue;const float w=weight(std::sqrt(float(dx*dx+dy*dy))/float(radius));if(w<=0)continue;
            const float sx=std::clamp(float(x-x0)-mx*w,0.f,float(cw-1)),sy=std::clamp(float(y-y0)-my*w,0.f,float(ch-1));const int ix=std::min(cw-2,int(sx)),iy=std::min(ch-2,int(sy));if(ix<0||iy<0)continue;const float fx=sx-float(ix),fy=sy-float(iy);
            const auto p00=scratch[size_t(iy)*cw+ix],p10=scratch[size_t(iy)*cw+ix+1],p01=scratch[size_t(iy+1)*cw+ix],p11=scratch[size_t(iy+1)*cw+ix+1];auto& out=editWorking(x,y);
            for(auto channel:channels){const float top=float(p00.*channel)+(float(p10.*channel)-float(p00.*channel))*fx,bottom=float(p01.*channel)+(float(p11.*channel)-float(p01.*channel))*fx;out.*channel=byte(top+(bottom-top)*fy);}}
    }
    bool appendWarp(Point point){
        if(!last){last=point;if(settings.mode==Mode::Smudge)pickup(point);return true;}
        const auto from=*last;const double distance=std::hypot(point.x-from.x,point.y-from.y),diameter=std::max(2.,settings.radius*2),spacing=std::max(1.,diameter*(settings.mode==Mode::Smudge?.005:.025));if(distance<spacing)return false;
        const int steps=int(std::ceil(distance/spacing));Point previous=from;
        for(int step=1;step<=steps;++step){const double t=double(step)/steps;const Point next{from.x+(point.x-from.x)*t,from.y+(point.y-from.y)*t};if(settings.mode==Mode::Smudge)smudge(next);else push(previous,next);++stats.warpDabs;
            if(growing){if(!coverageStarted)coverageStarted=growing->begin(next);else growing->append(next);}
            else {if(!coverageStarted)coverageStarted=coverage->begin(next);else coverage->append(next);}previous=next;}
        last=point;if(!growing)compose();else if(!warp(settings.mode))growing->recomposeTouched(tileCompositor());return true;
    }
    void finishGrowingHeal(){
        const auto tiles=growing->coverageSnapshot();auto extent=growing->virtualBounds();int minX=extent.x+extent.width,minY=extent.y+extent.height,maxX=extent.x,maxY=extent.y;
        for(const auto& item:tiles){const auto& tile=*item.coverage;auto b=graphics::coverageBounds({tile.preview,tile.width,tile.height,tile.width});if(b[2]<=b[0]||b[3]<=b[1])continue;
            minX=std::min(minX,item.rect.x+int(b[0]));minY=std::min(minY,item.rect.y+int(b[1]));maxX=std::max(maxX,item.rect.x+int(b[2]));maxY=std::max(maxY,item.rect.y+int(b[3]));}
        if(minX>=maxX||minY>=maxY)return;
        const double reach=(std::max(maxX-minX,maxY-minY)+32)*3.2;
        minX=int(std::max(double(extent.x),std::floor(minX-reach)));minY=int(std::max(double(extent.y),std::floor(minY-reach)));
        maxX=int(std::min(double(extent.x+extent.width),std::ceil(maxX+reach)));maxY=int(std::min(double(extent.y+extent.height),std::ceil(maxY+reach)));
        const int w=maxX-minX,h=maxY-minY;validSize(w,h);auto base=growing->readOriginal({minX,minY,w,h});stats.healingRegionPixels=uint64_t(w)*h;
        auto rgba=std::make_shared<std::vector<uint8_t>>(size_t(w)*h*4);std::vector<uint8_t> gray(size_t(w)*h);
        for(int y=0;y<h;++y)for(int x=0;x<w;++x){const auto p=base->pixel(x,y);const size_t i=(size_t(y)*w+x)*4;(*rgba)[i]=p.r;(*rgba)[i+1]=p.g;(*rgba)[i+2]=p.b;(*rgba)[i+3]=p.a;}
        for(const auto& item:tiles){const int l=std::max(minX,item.rect.x),t=std::max(minY,item.rect.y),r=std::min(maxX,item.rect.x+item.rect.width),b=std::min(maxY,item.rect.y+item.rect.height);
            for(int y=t;y<b;++y)for(int x=l;x<r;++x)gray[size_t(y-minY)*w+x-minX]=item.coverage->preview[size_t(y-item.rect.y)*item.rect.width+x-item.rect.x];}
        const int mode=settings.mode==Mode::HealContentAware?0:settings.mode==Mode::HealCreateTexture?1:2;
        graphics::spotHeal({*rgba,uint32_t(w),uint32_t(h),size_t(w)*4},{gray,uint32_t(w),uint32_t(h),size_t(w)},float(settings.opacity),mode,settings.healingSeed);
        growing->recomposeTouched([rgba,minX,minY,w,h](const graphics::GrowingTileInput& input,std::span<Pixel> output){
            for(int y=0;y<input.rect.height;++y)for(int x=0;x<input.rect.width;++x){const int xx=input.rect.x+x,yy=input.rect.y+y;const size_t local=size_t(y)*256+x;output[local]=input.original[local];
                if(xx<minX||yy<minY||xx>=minX+w||yy>=minY+h)continue;const size_t i=(size_t(yy-minY)*w+xx-minX)*4;
                const Pixel healed{(*rgba)[i],(*rgba)[i+1],(*rgba)[i+2],(*rgba)[i+3]};const double sx=xx+.5,sy=yy+.5;
                const double selectionAmount=input.selection?input.selection->pixel(int(std::floor(input.mapping.tx+sx*input.mapping.a+sy*input.mapping.c)),int(std::floor(input.mapping.ty+sx*input.mapping.b+sy*input.mapping.d)))/255.:1;
                output[local]=lerp(input.original[local],healed,selectionAmount);
            }},true);
    }
    void finishHeal(){
        const size_t columns=size_t((original->width+255)/256);int minX=original->width,minY=original->height,maxX=0,maxY=0;
        for(const auto& [key,tile]:coverage->coverageTiles()){const int x0=int(key%columns)*256,y0=int(key/columns)*256;
            auto box=graphics::coverageBounds({tile->preview,tile->width,tile->height,tile->width});if(box[2]<=box[0]||box[3]<=box[1])continue;
            minX=std::min(minX,x0+int(box[0]));minY=std::min(minY,y0+int(box[1]));maxX=std::max(maxX,x0+int(box[2]));maxY=std::max(maxY,y0+int(box[3]));}
        if(minX>=maxX||minY>=maxY){published=original;return;}
        const double reach=(std::max(maxX-minX,maxY-minY)+32)*3.2;
        minX=std::max(0,int(std::floor(minX-reach)));minY=std::max(0,int(std::floor(minY-reach)));maxX=std::min(original->width,int(std::ceil(maxX+reach)));maxY=std::min(original->height,int(std::ceil(maxY+reach)));
        const int w=maxX-minX,h=maxY-minY;stats.healingRegionPixels=uint64_t(w)*h;std::vector<uint8_t> rgba(size_t(w)*h*4),gray(size_t(w)*h);
        for(int y=0;y<h;++y)for(int x=0;x<w;++x){const auto p=original->pixel(minX+x,minY+y);const size_t i=size_t(y)*w+x;rgba[i*4]=p.r;rgba[i*4+1]=p.g;rgba[i*4+2]=p.b;rgba[i*4+3]=p.a;const size_t key=size_t((minY+y)/256)*columns+size_t((minX+x)/256);auto found=coverage->coverageTiles().find(key);if(found!=coverage->coverageTiles().end())gray[i]=found->second->preview[size_t((minY+y)%256)*found->second->width+size_t((minX+x)%256)];}
        const int mode=settings.mode==Mode::HealContentAware?0:settings.mode==Mode::HealCreateTexture?1:2;
        graphics::spotHeal({rgba,uint32_t(w),uint32_t(h),size_t(w)*4},{gray,uint32_t(w),uint32_t(h),size_t(w)},float(settings.opacity),mode,settings.healingSeed);
        auto result=std::make_shared<Raster>(*original);
        for(const auto& [key,tile]:coverage->coverageTiles()){const int x0=int(key%columns)*256,y0=int(key/columns)*256;std::shared_ptr<Raster::Tile> copy;
            for(uint32_t y=0;y<tile->height;++y)for(uint32_t x=0;x<tile->width;++x){const int xx=x0+int(x),yy=y0+int(y);if(xx<minX||yy<minY||xx>=maxX||yy>=maxY)continue;const size_t index=(size_t(yy-minY)*w+xx-minX)*4,local=size_t(y)*256+x;const auto base=original->tiles[key]->pixels[local];const Pixel healed{rgba[index],rgba[index+1],rgba[index+2],rgba[index+3]};
                // BrushRaster.draw sets blendMode .copy for image data. C has
                // already applied coverage/opacity; selection clips this copy once.
                const auto next=lerp(base,healed,selected(documentPoint(xx,yy)));if(next!=base){if(!copy){copy=std::make_shared<Raster::Tile>(*original->tiles[key]);++stats.publishedTileCopies;}copy->pixels[local]=next;}}
            if(copy)result->tiles[key]=std::move(copy);}
        published=std::move(result);
    }
};
RetouchSession::RetouchSession(Layer layer,int w,int h,Settings settings,Sources sources,std::shared_ptr<const GrayRaster> selection,std::shared_ptr<graphics::D3D11BrushCoverage> accelerator,bool mask,uint64_t budget)
    :impl_(std::make_unique<Impl>(std::move(layer),w,h,settings,std::move(sources),std::move(selection),std::move(accelerator),mask,budget)){}
RetouchSession::RetouchSession(std::shared_ptr<const Raster> image,Transform transform,int w,int h,Settings settings,Sources sources,std::shared_ptr<const GrayRaster> selection,std::shared_ptr<graphics::D3D11BrushCoverage> accelerator)
    :impl_(std::make_unique<Impl>(std::move(image),transform,w,h,settings,std::move(sources),std::move(selection),std::move(accelerator))){}
RetouchSession::RetouchSession(std::shared_ptr<const GrayRaster> mask,Transform transform,int w,int h,Settings settings,std::shared_ptr<const GrayRaster> selection,std::shared_ptr<graphics::D3D11BrushCoverage> accelerator)
    :RetouchSession(maskRaster(mask),transform,w,h,settings,{},std::move(selection),std::move(accelerator)){
    if(settings.mode!=Mode::Blur)throw std::invalid_argument("Only Blur supports mask retouch");impl_->originalMask=std::move(mask);impl_->maskTarget=true;
}
RetouchSession::~RetouchSession()=default;
bool RetouchSession::begin(Point p){auto& s=*impl_;if(s.started||s.finished)throw std::logic_error("Retouch session already started or finished");if(!pointValid(p)||s.emptySelection)return false;s.prepare();s.started=true;
    if(warp(s.settings.mode))return s.appendWarp(p);if(s.growing)return s.coverageStarted=s.growing->begin(p);s.coverageStarted=s.coverage->begin(p);s.compose();return s.coverageStarted;}
bool RetouchSession::append(Point p){auto& s=*impl_;if(!s.started||s.finished)throw std::logic_error("Retouch session is not active");if(!pointValid(p))return false;if(warp(s.settings.mode))return s.appendWarp(p);if(s.growing)return s.growing->append(p);if(!s.coverage->append(p))return false;s.compose();return true;}
std::shared_ptr<const graphics::GrowingBrushSnapshot> RetouchSession::previewSnapshot()const{if(!impl_->growing)throw std::logic_error("Snapshot results require a Layer-based retouch session");return impl_->growing->preview();}
std::shared_ptr<const graphics::GrowingBrushSnapshot> RetouchSession::commitSnapshot(){auto& s=*impl_;if(!s.growing)throw std::logic_error("Snapshot results require a Layer-based retouch session");if(s.finished)return s.growing->preview();if(s.started&&s.coverageStarted){s.growing->flushCoverage();if(healing(s.settings.mode))s.finishGrowingHeal();else if(warp(s.settings.mode))s.growing->recomposeTouched(s.tileCompositor());}auto result=s.growing->commit();s.finished=true;return result;}
std::shared_ptr<const graphics::GrowingBrushSnapshot> RetouchSession::cancelSnapshot(){auto& s=*impl_;if(!s.growing)throw std::logic_error("Snapshot results require a Layer-based retouch session");s.finished=true;s.working.clear();s.sparseWorking.reset();s.carried.clear();return s.growing->cancel();}
std::shared_ptr<const LayerRenderPreview> RetouchSession::livePreview()const{auto& s=*impl_;if(!s.growing)throw std::logic_error("Live layer preview requires a Layer-based retouch session");if(!s.started||s.finished)return {};if(!warp(s.settings.mode))return s.growing->preview()->renderPreview();if(s.sparseWorking)return s.sparseWarpPreview();if(!s.dirtyAny&&s.denseRaster){auto held=std::make_shared<LayerRenderPreview>();held->layer=s.originalLayer;held->layer.raster=s.denseRaster;held->layer.transform={0,0,double(s.width),double(s.height)};held->layer.transform.sampling=Transform::Sampling::Nearest;held->layer.shapeJson.clear();if(held->layer.mask&&!held->layer.mask->placement)held->layer.mask->placement=s.originalLayer.transform;held->identity=s.denseRaster;held->lineage=s.originalLayer.raster;held->imageSource=graphics::samplingSource(s.denseRaster);held->image=[raster=s.denseRaster](Point unit,Transform::Sampling sampling){return graphics::sampleRaster(*raster,unit,sampling);};return held;}const int pad=int(std::ceil(s.settings.radius))+2;const bool first=!s.warpPreviewShown;const LayerRenderPreview::Damage box{double(s.dirtyX0-pad),double(s.dirtyY0-pad),double(s.dirtyX1+pad),double(s.dirtyY1+pad)};if(!s.denseRaster)s.denseRaster=fromPixels(s.working,s.width,s.height);else{auto next=std::make_shared<Raster>(*s.denseRaster);const int columns=(s.width+255)/256,rows=(s.height+255)/256;const int tx0=std::clamp(s.dirtyX0/256,0,std::max(0,columns-1)),tx1=std::clamp(std::max(s.dirtyX1-1,s.dirtyX0)/256,0,std::max(0,columns-1)),ty0=std::clamp(s.dirtyY0/256,0,std::max(0,rows-1)),ty1=std::clamp(std::max(s.dirtyY1-1,s.dirtyY0)/256,0,std::max(0,rows-1));for(int ty=ty0;ty<=ty1;++ty)for(int tx=tx0;tx<=tx1;++tx){const int x0=tx*256,y0=ty*256,tw=std::min(256,s.width-x0),th=std::min(256,s.height-y0);auto tile=std::make_shared<Raster::Tile>();for(int y=0;y<th;++y)std::copy_n(s.working.data()+std::size_t(y0+y)*s.width+x0,tw,tile->pixels.data()+std::size_t(y)*256);next->tiles[std::size_t(ty)*columns+tx]=std::move(tile);}s.denseRaster=std::move(next);}s.dirtyAny=false;s.warpPreviewShown=true;auto raster=s.denseRaster;auto result=std::make_shared<LayerRenderPreview>();result->layer=s.originalLayer;result->layer.raster=raster;result->layer.transform={0,0,double(s.width),double(s.height)};result->layer.transform.sampling=Transform::Sampling::Nearest;result->layer.shapeJson.clear();if(result->layer.mask&&!result->layer.mask->placement)result->layer.mask->placement=s.originalLayer.transform;result->identity=raster;result->lineage=s.originalLayer.raster;result->imageSource=graphics::samplingSource(raster);result->image=[raster](Point unit,Transform::Sampling sampling){return graphics::sampleRaster(*raster,unit,sampling);};result->damageComparedWith=[box,first](const LayerRenderPreview*)->std::optional<std::vector<LayerRenderPreview::Damage>>{if(first)return std::nullopt;return std::vector<LayerRenderPreview::Damage>{box};};return result;}
std::shared_ptr<const Raster> RetouchSession::preview()const{return impl_->growing?impl_->materialize().raster:impl_->published;}
std::shared_ptr<const Raster> RetouchSession::warpDocumentPreview()const{auto& s=*impl_;if(s.sparseWorking)throw std::length_error("Materializing a full warp canvas exceeds the raster budget; use livePreview");if(!s.started||!warp(s.settings.mode)||s.working.empty())throw std::logic_error("No active warp document preview");if(s.warpPreviewRevision!=s.stats.warpDabs){s.warpPreview=fromPixels(s.working,s.width,s.height);s.warpPreviewRevision=s.stats.warpDabs;}return s.warpPreview;}
std::shared_ptr<const Raster> RetouchSession::commit(){auto& s=*impl_;if(s.growing){commitSnapshot();return preview();}if(s.finished)return s.published;if(!s.started||!s.coverageStarted){s.finished=true;return s.published;}s.coverage->commit();if(healing(s.settings.mode))s.finishHeal();else s.compose();s.finished=true;return s.published;}
std::shared_ptr<const Raster> RetouchSession::cancel(){auto& s=*impl_;if(s.growing){cancelSnapshot();return preview();}s.published=s.original;s.finished=true;s.coverage.reset();s.working.clear();s.sparseWorking.reset();s.carried.clear();return s.original;}
std::shared_ptr<const GrayRaster> RetouchSession::previewMask()const{auto& s=*impl_;if(!s.maskTarget)throw std::logic_error("Retouch target is not a mask");if(s.growing)return s.materialize().mask->raster;if(s.published==s.original)return s.originalMask;if(s.grayPreviewOwner==s.published)return s.grayPreview;
    auto gray=std::make_shared<GrayRaster>();gray->width=s.original->width;gray->height=s.original->height;gray->pixels.resize(size_t(gray->width)*gray->height);for(int y=0;y<gray->height;++y)for(int x=0;x<gray->width;++x)gray->pixels[size_t(y)*gray->width+x]=s.published->pixel(x,y).r;s.grayPreview=gray;s.grayPreviewOwner=s.published;return gray;}
std::shared_ptr<const GrayRaster> RetouchSession::commitMask(){commit();return previewMask();}
std::shared_ptr<const GrayRaster> RetouchSession::cancelMask(){cancel();return previewMask();}
const Metrics& RetouchSession::metrics()const{return impl_->stats;}
}
