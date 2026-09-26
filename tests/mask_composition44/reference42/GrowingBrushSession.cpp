// Sparse growth and immediate immutable paint snapshots from BrushStroke.swift
// and EditorSession+Brush.swift at a19db9011282399785dc18efcfded904627bdcc2.
// Copyright (c) 2026 Wonder Assembly LLC. MIT notice: upstream/LICENSE.
#include "GrowingBrushSession.h"
#include "RasterSampling.h"
#include "SamplingSource.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <set>
#include <stdexcept>
namespace compositor::graphics {
namespace {
using Clock=std::chrono::steady_clock;
struct Counters {std::atomic<uint64_t> materialized{},reused{};};
struct PaintedTile {std::shared_ptr<const Raster::Tile> pixels;BrushSourceRect alpha;};
using PaintedMap=std::map<BrushTileKey,PaintedTile>;
int floorDiv(int value,int divisor){const int64_t v=value,d=divisor;return int(v>=0?v/d:-1-((-1-v)/d));}
BrushSourceRect unite(BrushSourceRect a,BrushSourceRect b){if(a.empty())return b;if(b.empty())return a;int l=std::min(a.x,b.x),t=std::min(a.y,b.y),r=std::max(a.x+a.width,b.x+b.width),bottom=std::max(a.y+a.height,b.y+b.height);return {l,t,r-l,bottom-t};}
void budget(BrushSourceRect bounds,uint64_t pixels){if(bounds.empty()||bounds.width>30000||bounds.height>30000||uint64_t(bounds.width)*bounds.height>pixels)throw std::runtime_error("Growing brush exceeds allocated bounds budget");}
bool validPoint(Point p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::abs(p.x)<=1e7&&std::abs(p.y)<=1e7;}
uint8_t byte(double v){return uint8_t(std::clamp(v+.5,0.,255.));}
Pixel paint(Pixel original,const BrushSessionSettings& settings,double amount){if(amount<=0)return original;if(settings.erase)return {byte(original.r*(1-amount)),byte(original.g*(1-amount)),byte(original.b*(1-amount)),byte(original.a*(1-amount))};Pixel result{byte(settings.color[0]*amount+original.r*(1-amount)),byte(settings.color[1]*amount+original.g*(1-amount)),byte(settings.color[2]*amount+original.b*(1-amount)),byte(255*amount+original.a*(1-amount))};result.r=std::min(result.r,result.a);result.g=std::min(result.g,result.a);result.b=std::min(result.b,result.a);return result;}
BrushSegment segment(Point a,Point b){return {float(a.x),float(a.y),float(b.x),float(b.y)};}
}
struct GrowingBrushSnapshot::Impl {
    Layer original;
    Transform base;
    BrushSourceRect extent,crop;
    int baseWidth{},baseHeight{},phaseX{},phaseY{};
    bool mask{},changed{};
    PaintedMap tiles;
    std::shared_ptr<Counters> counters;
    Point alignment()const{if(mask){const auto& gray=*original.mask->raster;return gray.width==baseWidth&&gray.height==baseHeight?Point{double(gray.samplingOriginX),double(gray.samplingOriginY)}:Point{};}return original.raster?Point{double(original.raster->samplingOriginX),double(original.raster->samplingOriginY)}:Point{};}
    Pixel basePixel(int x,int y)const{
        if(x<0||y<0||x>=baseWidth||y>=baseHeight)return {};
        if(mask){const auto& gray=*original.mask->raster;auto value=gray.pixel(int((x+.5)*gray.width/baseWidth),int((y+.5)*gray.height/baseHeight));return {value,value,value,255};}
        return original.raster?original.raster->pixel(x,y):Pixel{};
    }
    BrushTileKey key(int x,int y)const{return {floorDiv(x-phaseX,256),floorDiv(y-phaseY,256)};}
    Point origin(BrushTileKey key)const{return {double(key.x*256+phaseX),double(key.y*256+phaseY)};}
    Pixel pixel(int x,int y)const{auto k=key(x,y);auto found=tiles.find(k);if(found==tiles.end())return basePixel(x,y);int px=x-(k.x*256+phaseX),py=y-(k.y*256+phaseY);return found->second.pixels->pixels[size_t(py)*256+px];}
    void copyRow(int x,int y,int count,Pixel* destination)const{
        while(count>0){auto k=key(x,y);int px=x-(k.x*256+phaseX),py=y-(k.y*256+phaseY);int run=std::min(count,256-px);auto overlay=tiles.find(k);if(run<=0||px<0||py<0||px>=256||py>=256)throw std::runtime_error("Invalid sparse row-copy coordinates");
            if(overlay!=tiles.end())std::copy_n(overlay->second.pixels->pixels.data()+size_t(py)*256+px,run,destination);
            else if(mask){for(int i=0;i<run;++i)destination[i]=basePixel(x+i,y);}
            else if(original.raster&&y>=0&&y<baseHeight){if(x<0)run=std::min(run,-x);else if(x<baseWidth){run=std::min({run,baseWidth-x,256-x%256});const auto& source=original.raster->tiles[size_t(y/256)*((baseWidth+255)/256)+x/256];std::copy_n(source->pixels.data()+size_t(y%256)*256+x%256,run,destination);}}
            x+=run;count-=run;destination+=run;
        }
    }
    bool hasOverlay(BrushSourceRect rect)const{auto first=key(rect.x,rect.y),last=key(rect.x+rect.width-1,rect.y+rect.height-1);for(int y=first.y;y<=last.y;++y)for(int x=first.x;x<=last.x;++x)if(tiles.contains({x,y}))return true;return false;}
    Transform place(BrushSourceRect rect)const{auto result=base;result.width=base.width*rect.width/baseWidth;result.height=base.height*rect.height/baseHeight;auto center=base.fromUnit({(rect.x+rect.width/2.)/baseWidth,(rect.y+rect.height/2.)/baseHeight});result.x=center.x-result.width/2;result.y=center.y-result.height/2;if(!result.valid())throw std::runtime_error("Growing brush produced invalid layer geometry");return result;}
    Transform placed()const{return changed?place(crop):base;}
    uint8_t maskExterior()const{
        if(!original.mask->placement||original.group||!original.adjustmentJson.empty())return 0;
        const double factor=std::min(1.,96./std::max(baseWidth,baseHeight));int w=std::max(1,int(baseWidth*factor)),h=std::max(1,int(baseHeight*factor));uint64_t sum=0,count=0;
        for(int y=0;y<h;++y)for(int x=0;x<w;++x){if(y!=0&&y!=h-1&&x!=0&&x!=w-1)continue;double x0=double(x)*baseWidth/w,x1=double(x+1)*baseWidth/w,y0=double(y)*baseHeight/h,y1=double(y+1)*baseHeight/h,value=0;for(int yy=int(std::floor(y0));yy<int(std::ceil(y1));++yy)for(int xx=int(std::floor(x0));xx<int(std::ceil(x1));++xx)value+=pixel(xx,yy).r*(std::min(x1,double(xx+1))-std::max(x0,double(xx)))*(std::min(y1,double(yy+1))-std::max(y0,double(yy)));sum+=uint64_t(std::clamp(std::lround(value/((x1-x0)*(y1-y0))),0L,255L));++count;}
        return sum*2>=count*255?255:0;
    }
    std::shared_ptr<const Raster> materialize(BrushSourceRect rect)const{
        budget(rect,100000000);auto out=std::make_shared<Raster>(*Raster::filled(rect.width,rect.height));int columns=(rect.width+255)/256;const auto gridOrigin=alignment();out->samplingOriginX=int(gridOrigin.x)-rect.x;out->samplingOriginY=int(gridOrigin.y)-rect.y;
        for(size_t index=0;index<out->tiles.size();++index){int x=int(index%columns)*256+rect.x,y=int(index/columns)*256+rect.y,w=std::min(256,rect.x+rect.width-x),h=std::min(256,rect.y+rect.height-y);auto k=key(x,y);auto at=origin(k);auto overlay=tiles.find(k);
            if(overlay!=tiles.end()&&at.x==x&&at.y==y){out->tiles[index]=overlay->second.pixels;++counters->reused;continue;}
            if(!mask&&original.raster&&x>=0&&y>=0&&x+w<=baseWidth&&y+h<=baseHeight&&x%256==0&&y%256==0&&!hasOverlay({x,y,w,h})){out->tiles[index]=original.raster->tiles[size_t(y/256)*((baseWidth+255)/256)+x/256];++counters->reused;continue;}
            if(!hasOverlay({x,y,w,h})&&(!mask&&!original.raster||x>=baseWidth||y>=baseHeight||x+w<=0||y+h<=0))continue;
            auto tile=std::make_shared<Raster::Tile>();for(int row=0;row<h;++row)copyRow(x,y+row,w,tile->pixels.data()+size_t(row)*256);out->tiles[index]=std::move(tile);++counters->materialized;
        }return out;
    }
};
GrowingBrushSnapshot::GrowingBrushSnapshot(std::shared_ptr<const Impl> impl):impl_(std::move(impl)){}
BrushSourceRect GrowingBrushSnapshot::sourceBounds()const{return impl_->crop;}
Transform GrowingBrushSnapshot::transform()const{return impl_->placed();}
bool GrowingBrushSnapshot::changed()const{return impl_->changed;}
bool GrowingBrushSnapshot::isMask()const{return impl_->mask;}
Pixel GrowingBrushSnapshot::pixel(int x,int y)const{if(std::abs(int64_t(x))>1000000000||std::abs(int64_t(y))>1000000000)throw std::runtime_error("Invalid sparse pixel coordinate");return impl_->pixel(x,y);}
std::shared_ptr<const Raster> GrowingBrushSnapshot::sample(int x,int y,int width,int height)const{if(std::abs(int64_t(x))>1000000000||std::abs(int64_t(y))>1000000000)throw std::runtime_error("Invalid sparse sample origin");return impl_->materialize({x,y,width,height});}
std::shared_ptr<const LayerRenderPreview> GrowingBrushSnapshot::renderPreview()const{
    if(!impl_->changed)return {};
    auto preview=std::make_shared<LayerRenderPreview>();preview->layer=impl_->original;preview->identity=impl_;preview->lineage=impl_->counters;
    const auto state=impl_;const auto bounds=state->crop;
    const auto readRow=[state,bounds](int x,int y,int count,Pixel* output){std::fill_n(output,count,Pixel{});state->copyRow(bounds.x+x,bounds.y+y,count,output);};
    const auto rawSource=[&](){auto source=std::make_shared<SamplingSource>();source->width=bounds.width;source->height=bounds.height;const auto origin=state->alignment();source->alignmentX=int(origin.x)-bounds.x;source->alignmentY=int(origin.y)-bounds.y;source->identity=state;
        source->reuseDomain=state->counters;
        source->dependencies=[state,bounds](int x,int y,int width,int height){
            const auto first=state->key(bounds.x+x,bounds.y+y),last=state->key(bounds.x+x+width-1,bounds.y+y+height-1);
            std::vector<std::shared_ptr<const void>> tokens;
            for(int ty=first.y;ty<=last.y;++ty)for(int tx=first.x;tx<=last.x;++tx){auto found=state->tiles.find({tx,ty});tokens.push_back(found==state->tiles.end()?std::shared_ptr<const void>{}:found->second.pixels);}
            return tokens;
        };
        return source;};
    preview->damageComparedWith=[state](const LayerRenderPreview* previous)->std::optional<std::vector<LayerRenderPreview::Damage>>{
        if(!previous)return {}; // First preview/cancel also changes source-edge clipping.
        if(previous&&(previous->lineage!=state->counters||previous->layer.id!=state->original.id))return {};
        auto prior=previous?std::static_pointer_cast<const Impl>(previous->identity):std::shared_ptr<const Impl>{};
        // A placed mask's exterior depends on its edge majority, so a stroke
        // there can change coverage arbitrarily far outside the painted pixels.
        if(state->mask&&state->original.mask->placement)return {};
        std::set<BrushTileKey> keys;for(const auto& [key,tile]:state->tiles){(void)tile;keys.insert(key);}if(prior)for(const auto& [key,tile]:prior->tiles){(void)tile;keys.insert(key);}
        std::vector<LayerRenderPreview::Damage> regions;
        auto add=[&](double left,double top,double right,double bottom){LayerRenderPreview::Damage region{INFINITY,INFINITY,-INFINITY,-INFINITY};for(Point point:std::array<Point,4>{{{left-1,top-1},{right+1,top-1},{left-1,bottom+1},{right+1,bottom+1}}}){const auto mapped=state->base.fromUnit({point.x/state->baseWidth,point.y/state->baseHeight});region.left=std::min(region.left,mapped.x);region.top=std::min(region.top,mapped.y);region.right=std::max(region.right,mapped.x);region.bottom=std::max(region.bottom,mapped.y);}regions.push_back(region);};
        for(auto key:keys){auto current=state->tiles.find(key);const PaintedTile* next=current==state->tiles.end()?nullptr:&current->second;const PaintedTile* old=nullptr;if(prior){auto found=prior->tiles.find(key);if(found!=prior->tiles.end())old=&found->second;}if(next&&old&&next->pixels==old->pixels)continue;
            const auto origin=state->origin(key);int left=256,top=256,right=0,bottom=0;
            for(int y=0;y<256;++y)for(int x=0;x<256;++x){const int sx=int(origin.x)+x,sy=int(origin.y)+y;const auto before=old?old->pixels->pixels[size_t(y)*256+x]:state->basePixel(sx,sy),after=next?next->pixels->pixels[size_t(y)*256+x]:state->basePixel(sx,sy);if(before!=after){left=std::min(left,x);top=std::min(top,y);right=std::max(right,x+1);bottom=std::max(bottom,y+1);}}
            if(right<=left||bottom<=top)continue;
            // One source-pixel halo covers canonical bilinear interpolation.
            add(origin.x+left,origin.y+top,origin.x+right,origin.y+bottom);
        }
        // Extending/shrinking a crop changes clamping at its former nonzero
        // outline, even where source bytes did not change. Inspect only four
        // one-pixel edges, never the full image or the empty virtual extent.
        if(prior&&prior->crop!=state->crop){const auto a=prior->crop,b=state->crop;auto vertical=[&](int x){int lo=a.y+a.height,hi=a.y;for(int y=a.y;y<a.y+a.height;++y)if(prior->pixel(x,y).a){lo=std::min(lo,y);hi=std::max(hi,y+1);}if(hi>lo)add(x,lo,x+1,hi);};auto horizontal=[&](int y){int lo=a.x+a.width,hi=a.x;for(int x=a.x;x<a.x+a.width;++x)if(prior->pixel(x,y).a){lo=std::min(lo,x);hi=std::max(hi,x+1);}if(hi>lo)add(lo,y,hi,y+1);};if(a.x!=b.x)vertical(a.x);if(a.x+a.width!=b.x+b.width)vertical(a.x+a.width-1);if(a.y!=b.y)horizontal(a.y);if(a.y+a.height!=b.y+b.height)horizontal(a.y+a.height-1);}
        return regions;
    };
    if(state->mask){
        // A committed mask uses this full source grid, including uniform-mask
        // expansion. Keep its original placement and let the stack supply units.
        if(preview->layer.mask->placement)preview->layer.mask->previewExterior=state->maskExterior();
        preview->mask=[state,bounds](Point unit,Transform::Sampling sampling,uint8_t exterior){return sampleMaskPixels(bounds.width,bounds.height,[&](int x,int y){return state->pixel(bounds.x+x,bounds.y+y).r;},unit,sampling,exterior);};
        auto source=rawSource();source->gray=[state,bounds](int x,int y){return state->pixel(bounds.x+x,bounds.y+y).r;};source->readRow=readRow;preview->maskSource=std::move(source);
    }else{
        preview->layer.transform=state->placed();preview->layer.shapeJson.clear();
        // The image handle marks a visible image for graph/culling validation;
        // all reads are overridden below. Blank virtual canvases allocate one tile.
        if(!preview->layer.raster)preview->layer.raster=Raster::filled(1,1);
        preview->image=[state,bounds](Point unit,Transform::Sampling sampling){return sampleRasterPixels(bounds.width,bounds.height,[&](int x,int y){return state->pixel(bounds.x+x,bounds.y+y);},unit,sampling);};
        auto source=rawSource();source->rgba=[state,bounds](int x,int y){return state->pixel(bounds.x+x,bounds.y+y);};source->readRow=readRow;preview->imageSource=std::move(source);
        if(preview->layer.mask&&!preview->layer.mask->placement&&bounds!=BrushSourceRect{0,0,state->baseWidth,state->baseHeight}){
            preview->mask=[state,bounds](Point unit,Transform::Sampling sampling,uint8_t exterior){return sampleMaskPixels(bounds.width,bounds.height,[&](int x,int y){const int sx=bounds.x+x,sy=bounds.y+y;if(sx<0||sy<0||sx>=state->baseWidth||sy>=state->baseHeight)return uint8_t(255);const auto& original=*state->original.mask->raster;return original.pixel(int((sx+.5)*original.width/state->baseWidth),int((sy+.5)*original.height/state->baseHeight));},unit,sampling,exterior);};
            auto maskSource=rawSource();const auto& old=*state->original.mask->raster;maskSource->alignmentX=(old.width==state->baseWidth?old.samplingOriginX:0)-bounds.x;maskSource->alignmentY=(old.height==state->baseHeight?old.samplingOriginY:0)-bounds.y;maskSource->gray=[state,bounds](int x,int y){const int sx=bounds.x+x,sy=bounds.y+y;if(sx<0||sy<0||sx>=state->baseWidth||sy>=state->baseHeight)return uint8_t(255);const auto& original=*state->original.mask->raster;return original.pixel(int((sx+.5)*original.width/state->baseWidth),int((sy+.5)*original.height/state->baseHeight));};preview->maskSource=std::move(maskSource);
        }
    }
    return preview;
}
Layer GrowingBrushSnapshot::previewLayer(double x,double y,double width,double height,uint64_t maxPixels)const{
    if(!validPoint({x,y})||!validPoint({x+width,y+height})||!std::isfinite(width)||!std::isfinite(height)||width<=0||height<=0||maxPixels<1||maxPixels>100000000)throw std::invalid_argument("Invalid growing brush viewport");
    if(!impl_->changed)return impl_->original;
    double left=INFINITY,top=INFINITY,right=-INFINITY,bottom=-INFINITY;for(auto p:std::array<Point,4>{{{x,y},{x+width,y},{x,y+height},{x+width,y+height}}}){auto u=impl_->base.toUnit(p);left=std::min(left,u.x*impl_->baseWidth);right=std::max(right,u.x*impl_->baseWidth);top=std::min(top,u.y*impl_->baseHeight);bottom=std::max(bottom,u.y*impl_->baseHeight);}
    left=std::floor(left)-2;top=std::floor(top)-2;right=std::ceil(right)+2;bottom=std::ceil(bottom)+2;
    left=std::max(left,double(impl_->crop.x));top=std::max(top,double(impl_->crop.y));right=std::min(right,double(impl_->crop.x+impl_->crop.width));bottom=std::min(bottom,double(impl_->crop.y+impl_->crop.height));
    Layer layer=impl_->original;if(!impl_->mask)layer.shapeJson.clear();
    if(left>=right||top>=bottom){if(impl_->mask){const auto exterior=impl_->maskExterior();layer.mask->raster=std::make_shared<GrayRaster>(GrayRaster{1,1,{exterior}});layer.mask->previewExterior=exterior;}else layer.raster.reset();return layer;}
    if(std::abs(left)>1e9||std::abs(top)>1e9||std::abs(right)>1e9||std::abs(bottom)>1e9||right-left>1e9||bottom-top>1e9)throw std::runtime_error("Growing brush viewport source extent too large");
    BrushSourceRect rect{int(left),int(top),int(right-left),int(bottom-top)};auto placement=impl_->place(rect);uint64_t step=1;auto pixels=[&](){return ((uint64_t(rect.width)+step-1)/step)*((uint64_t(rect.height)+step-1)/step);};while(pixels()>maxPixels||(uint64_t(rect.width)+step-1)/step>30000||(uint64_t(rect.height)+step-1)/step>30000)step*=2;
    const int w=int((uint64_t(rect.width)+step-1)/step),h=int((uint64_t(rect.height)+step-1)/step);auto sourcePoint=[&](int px,int py){return Point{rect.x+(px+.5)*rect.width/w,rect.y+(py+.5)*rect.height/h};};
    if(impl_->mask){const uint8_t exterior=impl_->maskExterior();auto mask=std::make_shared<GrayRaster>(GrayRaster{w,h,std::vector<uint8_t>(size_t(w)*h)});for(int py=0;py<h;++py)for(int px=0;px<w;++px){auto p=sourcePoint(px,py);mask->pixels[size_t(py)*w+px]=impl_->pixel(int(p.x),int(p.y)).r;}layer.mask->raster=std::move(mask);layer.mask->placement=placement;layer.mask->previewExterior=exterior;if(layer.group||!layer.adjustmentJson.empty()){layer.transform=placement;layer.mask->placement.reset();}return layer;}
    layer.transform=placement;if(step==1)layer.raster=impl_->materialize(rect);else{auto raster=std::make_shared<Raster>(*Raster::filled(w,h));int columns=(w+255)/256;for(size_t index=0;index<raster->tiles.size();++index){auto tile=std::make_shared<Raster::Tile>();int tx=int(index%columns)*256,ty=int(index/columns)*256;for(int row=0;row<std::min(256,h-ty);++row)for(int col=0;col<std::min(256,w-tx);++col){auto p=sourcePoint(tx+col,ty+row);tile->pixels[size_t(row)*256+col]=impl_->pixel(int(std::floor(p.x)),int(std::floor(p.y)));}raster->tiles[index]=std::move(tile);++impl_->counters->materialized;}layer.raster=std::move(raster);}
    if(layer.mask&&!layer.mask->placement){const auto& original=*layer.mask->raster;auto mask=std::make_shared<GrayRaster>(GrayRaster{w,h,std::vector<uint8_t>(size_t(w)*h,255)});for(int py=0;py<h;++py)for(int px=0;px<w;++px){auto p=sourcePoint(px,py);if(p.x>=0&&p.y>=0&&p.x<impl_->baseWidth&&p.y<impl_->baseHeight)mask->pixels[size_t(py)*w+px]=original.pixel(int(p.x*original.width/impl_->baseWidth),int(p.y*original.height/impl_->baseHeight));}layer.mask->raster=std::move(mask);}return layer;
}
Layer GrowingBrushSnapshot::materializeLayer()const{
    auto layer=impl_->original;if(layer.mask)layer.mask->previewExterior.reset();if(!impl_->changed)return layer;auto bounds=impl_->crop;budget(bounds,100000000);
    if(impl_->mask){auto gray=std::make_shared<GrayRaster>();gray->width=bounds.width;gray->height=bounds.height;const auto origin=impl_->alignment();gray->samplingOriginX=int(origin.x)-bounds.x;gray->samplingOriginY=int(origin.y)-bounds.y;gray->pixels.resize(size_t(gray->width)*gray->height);for(int y=0;y<gray->height;++y)for(int x=0;x<gray->width;++x)gray->pixels[size_t(y)*gray->width+x]=impl_->pixel(bounds.x+x,bounds.y+y).r;layer.mask->raster=std::move(gray);return layer;}
    layer.raster=impl_->materialize(bounds);layer.transform=impl_->placed();layer.shapeJson.clear();
    if(layer.mask&&!layer.mask->placement&&bounds!=BrushSourceRect{0,0,impl_->baseWidth,impl_->baseHeight}){const auto& original=*layer.mask->raster;auto mask=std::make_shared<GrayRaster>();mask->width=bounds.width;mask->height=bounds.height;mask->samplingOriginX=(original.width==impl_->baseWidth?original.samplingOriginX:0)-bounds.x;mask->samplingOriginY=(original.height==impl_->baseHeight?original.samplingOriginY:0)-bounds.y;mask->pixels.assign(size_t(mask->width)*mask->height,255);for(int y=0;y<mask->height;++y)for(int x=0;x<mask->width;++x){int sx=bounds.x+x,sy=bounds.y+y;if(sx>=0&&sy>=0&&sx<impl_->baseWidth&&sy<impl_->baseHeight)mask->pixels[size_t(y)*mask->width+x]=original.pixel(int((sx+.5)*original.width/impl_->baseWidth),int((sy+.5)*original.height/impl_->baseHeight));}layer.mask->raster=std::move(mask);}
    return layer;
}
struct GrowingBrushSession::Impl {
    std::shared_ptr<const GrowingBrushSnapshot::Impl> state;
    std::shared_ptr<const GrowingBrushSnapshot> published,originalSnapshot;
    BrushSessionSettings settings;
    BrushSessionGeometry mapping;
    std::shared_ptr<D3D11BrushCoverage> accelerator;
    std::shared_ptr<const GrayRaster> selection;
    std::map<BrushTileKey,std::shared_ptr<const BrushTile>> coverage;
    std::set<BrushTileKey> tailKeys;
    std::vector<Point> samples;
    std::optional<BrushSourceRect> allocated;
    GrowingBrushMetrics metrics;
    uint64_t limit{};
    bool begun{},finished{},emptySelection{};
    std::string error;
    GrowingTileCompositor compositor;
    Impl(Layer layer,BrushSessionSettings value,int cw,int ch,std::shared_ptr<D3D11BrushCoverage> gpu,std::shared_ptr<const GrayRaster> selected,bool mask,uint64_t pixelBudget,GrowingTileCompositor compose):settings(value),accelerator(std::move(gpu)),selection(std::move(selected)),limit(pixelBudget),compositor(std::move(compose)){
        if(!layer.transform.valid()||cw<1||ch<1||cw>30000||ch>30000||pixelBudget<1||pixelBudget>100000000||(!mask&&(layer.group||!layer.adjustmentJson.empty()))||mask&&(!layer.mask||!layer.mask->raster))throw std::invalid_argument("Invalid growing brush target");
        if(!std::isfinite(value.radius)||value.radius<.5||value.radius>(compositor?1002:1000)||!std::isfinite(value.hardness)||value.hardness<0||value.hardness>1||!std::isfinite(value.opacity)||value.opacity<.01||value.opacity>1)throw std::invalid_argument("Invalid growing brush settings");
        auto initial=std::make_shared<GrowingBrushSnapshot::Impl>();initial->original=std::move(layer);initial->mask=mask;initial->counters=std::make_shared<Counters>();initial->base=mask?initial->original.mask->placement.value_or(initial->original.transform):initial->original.transform;
        auto original=initial->original.raster;bool placed=mask&&initial->original.mask->placement.has_value();initial->baseWidth=placed?initial->original.mask->raster->width:original?original->width:int(std::round(initial->original.transform.width));initial->baseHeight=placed?initial->original.mask->raster->height:original?original->height:int(std::round(initial->original.transform.height));
        int w=initial->baseWidth,h=initial->baseHeight;if(w<1||h<1||w>30000||h>30000)throw std::invalid_argument("Invalid growing brush source grid");
        // A mask edit budgets its own grid. The unchanged image remains subject
        // to the general raster limit, not the caller's remaining mask budget.
        if(original){budget({0,0,original->width,original->height},mask?100000000:pixelBudget);if(original->tiles.size()!=size_t((original->width+255)/256)*((original->height+255)/256)||std::any_of(original->tiles.begin(),original->tiles.end(),[](const auto& tile){return !tile;}))throw std::invalid_argument("Invalid brush source tiles");}
        if(mask){auto gray=initial->original.mask->raster;if(gray->width<1||gray->height<1||gray->pixels.size()!=size_t(gray->width)*gray->height)throw std::invalid_argument("Invalid brush mask");settings.erase=false;settings.color={value.color[0],value.color[0],value.color[0]};}
        mapping=BrushSessionGeometry::forLayer(initial->base,w,h,cw,ch);auto extent=BrushSourceRect{0,0,w,h};
        if(!mask){double l=0,t=0,r=w,b=h;for(auto p:std::array<Point,4>{{{0,0},{double(cw),0},{0,double(ch)},{double(cw),double(ch)}}}){auto u=initial->base.toUnit(p);l=std::min(l,std::floor(u.x*w));t=std::min(t,std::floor(u.y*h));r=std::max(r,std::ceil(u.x*w));b=std::max(b,std::ceil(u.y*h));}if(r-l>1e9||b-t>1e9||std::abs(l)>1e9||std::abs(t)>1e9)throw std::runtime_error("Growing brush virtual extent exceeds source limit");extent={int(l),int(t),int(r-l),int(b-t)};}
        initial->extent=extent;initial->phaseX=extent.x-floorDiv(extent.x,256)*256;initial->phaseY=extent.y-floorDiv(extent.y,256)*256;initial->crop={0,0,w,h};
        if(selection){if(selection->width!=cw||selection->height!=ch||!selection->validStorage())throw std::invalid_argument("Brush selection must use document coordinates");emptySelection=!selection->hasCoverage();}
        state=std::move(initial);published=std::shared_ptr<const GrowingBrushSnapshot>(new GrowingBrushSnapshot(state));originalSnapshot=published;
    }
    bool composeTile(GrowingBrushSnapshot::Impl& next,BrushTileKey key,int x,int y,const BrushTile& tile,const GrowingTileCompositor& compose,bool retainCoveredTiles=false){
        auto base=std::make_unique<Raster::Tile>();
        for(uint32_t row=0;row<tile.height;++row)for(uint32_t col=0;col<tile.width;++col)base->pixels[size_t(row)*256+col]=state->basePixel(x+int(col),y+int(row));
        auto output=std::make_shared<Raster::Tile>(*base);
        compose({{x,y,int(tile.width),int(tile.height)},mapping,base->pixels,tile.preview,selection},output->pixels);
        bool changed=false,differs=false;int l=256,t=256,r=0,b=0;
        for(uint32_t row=0;row<tile.height;++row)for(uint32_t col=0;col<tile.width;++col){const auto p=output->pixels[size_t(row)*256+col];
            if(p.r>p.a||p.g>p.a||p.b>p.a)throw std::runtime_error("Retouch compositor produced invalid premultiplied pixels");
            changed|=p!=state->pixel(x+int(col),y+int(row));differs|=p!=base->pixels[size_t(row)*256+col];
            if(p.a){l=std::min(l,int(col));t=std::min(t,int(row));r=std::max(r,int(col)+1);b=std::max(b,int(row)+1);}}
        const bool retain=retainCoveredTiles&&std::any_of(tile.preview.begin(),tile.preview.end(),[](uint8_t value){return value!=0;});
        if(!changed&&!(retain&&!state->tiles.contains(key)))return false;
        if(differs||retain)next.tiles[key]={std::move(output),r>l?BrushSourceRect{x+l,y+t,r-l,b-t}:BrushSourceRect{}};else next.tiles.erase(key);
        ++metrics.coverage.outputTileAllocations;return true;
    }
    void finishBounds(GrowingBrushSnapshot::Impl& next,std::optional<BrushSourceRect> nextAllocated)const{
        next.changed=!next.tiles.empty();BrushSourceRect bounds=next.mask||next.original.raster?BrushSourceRect{0,0,next.baseWidth,next.baseHeight}:BrushSourceRect{};
        if(!next.mask)for(const auto& [key,tile]:next.tiles){(void)key;bounds=unite(bounds,tile.alpha);}
        next.crop=bounds.empty()?nextAllocated.value_or(BrushSourceRect{0,0,next.baseWidth,next.baseHeight}):bounds;budget(next.crop,limit);next.placed();
    }
    std::set<BrushTileKey> affected(std::span<const BrushSegment> segments)const{
        std::set<BrushTileKey> result;double det=mapping.a*mapping.d-mapping.b*mapping.c,reach=settings.radius+2;auto inverse=[&](double x,double y){x-=mapping.tx;y-=mapping.ty;return Point{(mapping.d*x-mapping.c*y)/det,(-mapping.b*x+mapping.a*y)/det};};const auto extent=state->extent;
        for(auto seg:segments){double l=std::max(0.,double(std::min(seg.x0,seg.x1))-reach),t=std::max(0.,double(std::min(seg.y0,seg.y1))-reach),r=std::min(double(mapping.canvasWidth),double(std::max(seg.x0,seg.x1))+reach),b=std::min(double(mapping.canvasHeight),double(std::max(seg.y0,seg.y1))+reach);if(l>=r||t>=b)continue;double x0=INFINITY,y0=INFINITY,x1=-INFINITY,y1=-INFINITY;for(auto p:std::array{inverse(l,t),inverse(r,t),inverse(l,b),inverse(r,b)}){x0=std::min(x0,p.x);y0=std::min(y0,p.y);x1=std::max(x1,p.x);y1=std::max(y1,p.y);}int left=int(std::clamp(std::floor(x0),double(extent.x),double(extent.x+extent.width))),top=int(std::clamp(std::floor(y0),double(extent.y),double(extent.y+extent.height))),right=int(std::clamp(std::ceil(x1),double(extent.x),double(extent.x+extent.width))),bottom=int(std::clamp(std::ceil(y1),double(extent.y),double(extent.y+extent.height)));if(left>=right||top>=bottom)continue;auto first=state->key(left,top),last=state->key(right-1,bottom-1);auto firstPoint=state->origin(first),lastPoint=state->origin(last);BrushSourceRect candidate{int(firstPoint.x),int(firstPoint.y),std::min(extent.x+extent.width,int(lastPoint.x)+256)-int(firstPoint.x),std::min(extent.y+extent.height,int(lastPoint.y)+256)-int(firstPoint.y)};budget(unite(allocated.value_or(state->mask||state->original.raster?BrushSourceRect{0,0,state->baseWidth,state->baseHeight}:BrushSourceRect{}),candidate),limit);for(int y=first.y;y<=last.y;++y)for(int x=first.x;x<=last.x;++x)result.insert({x,y});}
        return result;
    }
    void render(std::span<const BrushSegment> settled,std::span<const BrushSegment> tail){
        auto begin=Clock::now();auto nextTail=affected(tail),changed=affected(settled);changed.insert(nextTail.begin(),nextTail.end());changed.insert(tailKeys.begin(),tailKeys.end());if(changed.empty()){tailKeys=std::move(nextTail);return;}
        auto nextAllocated=allocated;struct Work{BrushTileKey key;int x,y;std::shared_ptr<BrushTile> tile;BrushUniforms uniforms;};std::vector<Work> work;work.reserve(changed.size());
        for(auto key:changed){auto p=state->origin(key);int x=int(p.x),y=int(p.y),w=std::min(256,state->extent.x+state->extent.width-x),h=std::min(256,state->extent.y+state->extent.height-y);BrushSourceRect rect{x,y,w,h};if(!coverage.contains(key)){auto bounds=nextAllocated.value_or(state->mask||state->original.raster?BrushSourceRect{0,0,state->baseWidth,state->baseHeight}:BrushSourceRect{});nextAllocated=unite(bounds,rect);budget(*nextAllocated,limit);}
            auto found=coverage.find(key);auto tile=found==coverage.end()?std::make_shared<BrushTile>(uint32_t(w),uint32_t(h)):std::make_shared<BrushTile>(*found->second);BrushUniforms u;u.mapping={float(mapping.a),float(mapping.b),float(mapping.c),float(mapping.d)};u.geometry={float(mapping.tx+x*mapping.a+y*mapping.c),float(mapping.ty+x*mapping.b+y*mapping.d),float(settings.radius),float(settings.hardness)};u.canvas={float(mapping.canvasWidth),float(mapping.canvasHeight),float(std::max(.001,std::min(std::hypot(mapping.a,mapping.b),std::hypot(mapping.c,mapping.d)))),float(std::max(.25,settings.radius*2*(settings.hardness>=1?.015:.025)))};work.push_back({key,x,y,std::move(tile),u});}
        auto prepared=Clock::now();metrics.coverage.tilePreparationMilliseconds+=std::chrono::duration<double,std::milli>(prepared-begin).count();
        std::vector<BrushTileRender> requests;requests.reserve(work.size());
        for(auto& item:work)requests.push_back({item.tile.get(),item.uniforms});
        if(accelerator){
            try{accelerator->renderBatch(requests,settled,tail);}
            catch(const std::exception& exception){
                error=exception.what();accelerator.reset();++metrics.coverage.acceleratorFallbacks;
                renderBrushCpuBatch(requests,settled,tail);
            }
        }else renderBrushCpuBatch(requests,settled,tail);
        auto covered=Clock::now();metrics.coverage.coverageRenderMilliseconds+=std::chrono::duration<double,std::milli>(covered-prepared).count();auto next=std::make_shared<GrowingBrushSnapshot::Impl>(*state);auto nextCoverage=coverage;bool pixelsChanged=false;
        for(auto& item:work){auto oldCoverage=coverage.find(item.key);auto oldPixels=state->tiles.find(item.key);std::shared_ptr<Raster::Tile> output;
            if(compositor){pixelsChanged|=composeTile(*next,item.key,item.x,item.y,*item.tile,compositor);nextCoverage[item.key]=item.tile;++metrics.coverage.coverageTileAllocations;continue;}
            for(uint32_t y=0;y<item.tile->height;++y)for(uint32_t x=0;x<item.tile->width;++x){size_t ci=size_t(y)*item.tile->width+x;uint8_t prior=oldCoverage==coverage.end()?0:oldCoverage->second->preview[ci];if(prior==item.tile->preview[ci])continue;double amount=item.tile->preview[ci]/255.*settings.opacity;int sx=item.x+int(x),sy=item.y+int(y);if(selection&&amount>0){double px=sx+.5,py=sy+.5;amount*=selection->pixel(int(std::floor(mapping.tx+px*mapping.a+py*mapping.c)),int(std::floor(mapping.ty+px*mapping.b+py*mapping.d)))/255.;}auto base=state->basePixel(sx,sy);auto value=paint(base,settings,amount);auto previous=oldPixels==state->tiles.end()?base:oldPixels->second.pixels->pixels[size_t(y)*256+x];if(value==previous)continue;if(!output){output=std::make_shared<Raster::Tile>();if(oldPixels!=state->tiles.end())*output=*oldPixels->second.pixels;else for(uint32_t row=0;row<item.tile->height;++row)for(uint32_t col=0;col<item.tile->width;++col)output->pixels[size_t(row)*256+col]=state->basePixel(item.x+int(col),item.y+int(row));++metrics.coverage.outputTileAllocations;}output->pixels[size_t(y)*256+x]=value;}
            nextCoverage[item.key]=item.tile;++metrics.coverage.coverageTileAllocations;if(output){bool differs=false;int l=256,t=256,r=0,b=0;for(uint32_t y=0;y<item.tile->height;++y)for(uint32_t x=0;x<item.tile->width;++x){auto pixel=output->pixels[size_t(y)*256+x];if(!differs&&pixel!=state->basePixel(item.x+int(x),item.y+int(y)))differs=true;if(pixel.a){l=std::min(l,int(x));t=std::min(t,int(y));r=std::max(r,int(x)+1);b=std::max(b,int(y)+1);}}if(differs)next->tiles[item.key]={std::move(output),r>l?BrushSourceRect{item.x+l,item.y+t,r-l,b-t}:BrushSourceRect{}};else next->tiles.erase(item.key);pixelsChanged=true;}
        }
        if(pixelsChanged){next->changed=!next->tiles.empty();BrushSourceRect bounds=next->mask||next->original.raster?BrushSourceRect{0,0,next->baseWidth,next->baseHeight}:BrushSourceRect{};if(!next->mask)for(const auto& [key,tile]:next->tiles){(void)key;bounds=unite(bounds,tile.alpha);}next->crop=bounds.empty()?nextAllocated.value_or(BrushSourceRect{0,0,next->baseWidth,next->baseHeight}):bounds;budget(next->crop,limit);next->placed();}
        coverage.swap(nextCoverage);tailKeys.swap(nextTail);allocated=nextAllocated;metrics.coverage.settledSegments+=settled.size();metrics.coverage.touchedTiles=coverage.size();metrics.coverage.coverageStorageBytes=0;for(const auto& [key,tile]:coverage){(void)key;metrics.coverage.coverageStorageBytes+=tile->permanent.size()*sizeof(float)+tile->preview.size();}
        if(pixelsChanged){state=std::move(next);published=std::shared_ptr<const GrowingBrushSnapshot>(new GrowingBrushSnapshot(state));++metrics.snapshots;++metrics.coverage.publishedSnapshots;}
        metrics.coverage.tileCompositionMilliseconds+=std::chrono::duration<double,std::milli>(Clock::now()-covered).count();
    }
};
GrowingBrushSession::GrowingBrushSession(Layer layer,BrushSessionSettings settings,int cw,int ch,std::shared_ptr<D3D11BrushCoverage> accelerator,std::shared_ptr<const GrayRaster> selection,bool mask,uint64_t budget,GrowingTileCompositor compositor):impl_(std::make_unique<Impl>(std::move(layer),settings,cw,ch,std::move(accelerator),std::move(selection),mask,budget,std::move(compositor))){}
GrowingBrushSession::~GrowingBrushSession()=default;
bool GrowingBrushSession::begin(Point point){if(impl_->begun||impl_->finished)throw std::logic_error("Growing stroke already started");if(!validPoint(point)||impl_->emptySelection)return false;impl_->begun=true;try{return append(point);}catch(...){impl_->begun=false;throw;}}
bool GrowingBrushSession::append(Point point){if(!impl_->begun||impl_->finished)throw std::logic_error("Growing stroke is not active");if(!validPoint(point)||!impl_->samples.empty()&&impl_->samples.back()==point)return false;auto next=impl_->samples;next.push_back(point);if(next.size()>4)next.erase(next.begin());size_t n=next.size();std::vector<BrushSegment> settled,tail;if(n==1)settled.push_back(segment(point,point));else if(n>=3)settled=brushContinuousCurve(next[n-3],next[n-2],next[n>=4?n-4:0],point);if(n>=2)tail.push_back(segment(next[n-2],point));impl_->render(settled,tail);impl_->samples.swap(next);++impl_->metrics.coverage.acceptedSamples;return true;}
std::shared_ptr<const GrowingBrushSnapshot> GrowingBrushSession::preview()const{return impl_->published;}
void GrowingBrushSession::flushCoverage(){if(!impl_->finished&&impl_->samples.size()>=2){auto n=impl_->samples.size();auto settled=brushContinuousCurve(impl_->samples[n-2],impl_->samples[n-1],impl_->samples[n>=3?n-3:0],impl_->samples[n-1]);impl_->render(settled,{});impl_->samples={impl_->samples.back()};}}
BrushSourceRect GrowingBrushSession::virtualBounds()const{return impl_->state->extent;}
std::vector<GrowingCoverageTile> GrowingBrushSession::coverageSnapshot()const{std::vector<GrowingCoverageTile> result;for(const auto& [key,tile]:impl_->coverage){const auto p=impl_->state->origin(key);result.push_back({{int(p.x),int(p.y),int(tile->width),int(tile->height)},tile});}return result;}
std::shared_ptr<const Raster> GrowingBrushSession::readOriginal(BrushSourceRect rect)const{return impl_->originalSnapshot->sample(rect.x,rect.y,rect.width,rect.height);}
void GrowingBrushSession::recomposeTouched(GrowingTileCompositor compositor,bool retainCoveredTiles){auto& s=*impl_;if(s.finished)throw std::logic_error("Growing stroke is finished");if(!compositor)throw std::invalid_argument("Missing tile compositor");auto next=std::make_shared<GrowingBrushSnapshot::Impl>(*s.state);bool changed=false;for(const auto& [key,tile]:s.coverage){auto p=s.state->origin(key);changed|=s.composeTile(*next,key,int(p.x),int(p.y),*tile,compositor,retainCoveredTiles);}if(changed){s.finishBounds(*next,s.allocated);s.state=std::move(next);s.published=std::shared_ptr<const GrowingBrushSnapshot>(new GrowingBrushSnapshot(s.state));++s.metrics.snapshots;++s.metrics.coverage.publishedSnapshots;}s.compositor=std::move(compositor);}
std::shared_ptr<const GrowingBrushSnapshot> GrowingBrushSession::commit(){flushCoverage();impl_->finished=true;return impl_->published;}
std::shared_ptr<const GrowingBrushSnapshot> GrowingBrushSession::cancel(){impl_->published=impl_->originalSnapshot;impl_->state=impl_->originalSnapshot->impl_;impl_->coverage.clear();impl_->tailKeys.clear();impl_->samples.clear();impl_->finished=true;impl_->metrics.coverage.touchedTiles=0;impl_->metrics.coverage.coverageStorageBytes=0;return impl_->published;}
GrowingBrushMetrics GrowingBrushSession::metrics()const{auto result=impl_->metrics;result.sparsePixelTiles=impl_->state->tiles.size();result.sparsePixelBytes=result.sparsePixelTiles*sizeof(Raster::Tile);result.materializedTiles=impl_->state->counters->materialized.load();result.reusedTiles=impl_->state->counters->reused.load();return result;}
const std::string& GrowingBrushSession::acceleratorError()const{return impl_->error;}
}
