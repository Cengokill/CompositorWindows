// Stroke geometry/lifecycle translated from BrushStroke.swift at the pinned
// baseline. Copyright (c) 2026 Wonder Assembly LLC. MIT: upstream/LICENSE.
#include "BrushSession.h"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <limits>
#include <stdexcept>

namespace compositor::graphics {
namespace {
BrushSegment segment(Point a,Point b){return{float(a.x),float(a.y),float(b.x),float(b.y)};}
bool validPoint(Point p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::abs(p.x)<=1e7&&std::abs(p.y)<=1e7;}
// Inputs here are finite, nonnegative convex combinations of bytes. A direct
// conversion after adding one half preserves halfway-away rounding and avoids
// four libm lround calls for every pixel during every pointer event.
uint8_t byte(double value){return static_cast<uint8_t>(std::clamp(value+0.5,0.,255.));}
Pixel paintPixel(Pixel base,const BrushSessionSettings& settings,double amount){
    if(amount<=0)return base;
    if(settings.erase)return{byte(base.r*(1-amount)),byte(base.g*(1-amount)),byte(base.b*(1-amount)),byte(base.a*(1-amount))};
    Pixel p{byte(settings.color[0]*amount+base.r*(1-amount)),byte(settings.color[1]*amount+base.g*(1-amount)),
        byte(settings.color[2]*amount+base.b*(1-amount)),byte(255*amount+base.a*(1-amount))};
    p.r=std::min(p.r,p.a);p.g=std::min(p.g,p.a);p.b=std::min(p.b,p.a);return p;
}
}
std::vector<BrushSegment> brushContinuousCurve(Point start,Point end,Point before,Point after){
    auto knot=[](double t,Point a,Point b){return t+std::max(0.0001,std::sqrt(std::hypot(b.x-a.x,b.y-a.y)));};
    auto mix=[](Point a,Point b,double ta,double tb,double t){double wa=(tb-t)/(tb-ta),wb=(t-ta)/(tb-ta);return Point{a.x*wa+b.x*wb,a.y*wa+b.y*wb};};
    double t0=0,t1=knot(t0,before,start),t2=knot(t1,start,end),t3=knot(t2,end,after);
    auto point=[&](double u){if(u==0)return start;if(u==1)return end;double t=t1+(t2-t1)*u;
        auto a=mix(before,start,t0,t1,t),b=mix(start,end,t1,t2,t),c=mix(end,after,t2,t3,t);
        return mix(mix(a,b,t0,t2,t),mix(b,c,t1,t3,t),t1,t2,t);};
    std::vector<BrushSegment> result;
    auto subdivide=[&](auto&& self,Point a,Point b,double lo,double hi,int depth)->void {
        double dx=b.x-a.x,dy=b.y-a.y,squared=dx*dx+dy*dy;
        auto error=[&](Point p){double t=squared>0?std::clamp(((p.x-a.x)*dx+(p.y-a.y)*dy)/squared,0.,1.):0;
            return std::hypot(p.x-a.x-t*dx,p.y-a.y-t*dy);};
        double mid=(lo+hi)/2;auto m=point(mid);
        double deviation=std::max({error(m),error(point((lo+mid)/2)),error(point((mid+hi)/2))});
        if(deviation<=0.2||depth>=10){result.push_back(segment(a,b));return;}
        self(self,a,m,lo,mid,depth+1);self(self,m,b,mid,hi,depth+1);
    };
    subdivide(subdivide,start,end,0,1,0);return result;
}
BrushSessionGeometry BrushSessionGeometry::forLayer(const Transform& t,int sw,int sh,int cw,int ch){
    if(!t.valid()||sw<=0||sh<=0)throw std::invalid_argument("Invalid brush layer transform");
    auto o=t.fromUnit({0,0}),x=t.fromUnit({1,0}),y=t.fromUnit({0,1});
    return{(x.x-o.x)/sw,(x.y-o.y)/sw,(y.x-o.x)/sh,(y.y-o.y)/sh,o.x,o.y,cw,ch};
}
BrushSession::BrushSession(std::shared_ptr<const Raster> original,BrushSessionSettings settings,
    std::shared_ptr<D3D11BrushCoverage> accelerator,std::shared_ptr<const GrayRaster> selection,BrushSessionGeometry geometry,Output output)
    :original_(std::move(original)),published_(original_),settings_(settings),geometry_(geometry),accelerator_(std::move(accelerator)),selection_(std::move(selection)),output_(output) {
    if(!original_||original_->width<1||original_->height<1||original_->width>30000||original_->height>30000 ||
        uint64_t(original_->width)*original_->height>100000000 ||
        original_->tiles.size()!=size_t((original_->width+255)/256)*size_t((original_->height+255)/256))
        throw std::invalid_argument("Invalid original brush raster");
    for(auto& tile:original_->tiles)if(!tile)throw std::invalid_argument("Missing original brush tile");
    if(!std::isfinite(settings.radius)||settings.radius<0.5||settings.radius>1002 ||
        !std::isfinite(settings.hardness)||settings.hardness<0||settings.hardness>1 ||
        !std::isfinite(settings.opacity)||settings.opacity<0.01||settings.opacity>1)
        throw std::invalid_argument("Invalid brush settings");
    if(!geometry_.canvasWidth)geometry_.canvasWidth=original_->width;
    if(!geometry_.canvasHeight)geometry_.canvasHeight=original_->height;
    if(geometry_.canvasWidth<1||geometry_.canvasHeight<1||geometry_.canvasWidth>30000||geometry_.canvasHeight>30000)
        throw std::invalid_argument("Invalid brush canvas");
    for(double v:{geometry.a,geometry.b,geometry.c,geometry.d,geometry.tx,geometry.ty})
        if(!std::isfinite(v)||std::abs(v)>1e7)throw std::invalid_argument("Invalid brush mapping");
    if(std::abs(geometry_.a*geometry_.d-geometry_.b*geometry_.c)<1e-12)throw std::invalid_argument("Singular brush mapping");
    if(selection_){
        if(selection_->width!=geometry_.canvasWidth||selection_->height!=geometry_.canvasHeight||selection_->pixels.size()!=size_t(selection_->width)*selection_->height)
            throw std::invalid_argument("Brush selection must use the document grid");
        emptySelection_=std::none_of(selection_->pixels.begin(),selection_->pixels.end(),[](uint8_t v){return v!=0;});
    }
}
bool BrushSession::begin(Point p){
    if(begun_||finished_)throw std::logic_error("Brush session already started or finished");
    if(!validPoint(p)||emptySelection_)return false;
    begun_=true;try{return append(p);}catch(...){begun_=false;throw;}
}
bool BrushSession::append(Point p){
    if(!begun_||finished_)throw std::logic_error("Brush session is not active");
    if(!validPoint(p)||(!samples_.empty()&&samples_.back()==p))return false;
    auto next=samples_;next.push_back(p);if(next.size()>4)next.erase(next.begin());
    auto n=next.size();std::vector<BrushSegment> settled,tail;
    if(n==1)settled.push_back(segment(p,p));
    else if(n>=3)settled=brushContinuousCurve(next[n-3],next[n-2],next[n>=4?n-4:0],p);
    if(n>=2)tail.push_back(segment(next[n-2],p));
    render(settled,tail);samples_.swap(next);++metrics_.acceptedSamples;return true;
}
std::set<size_t> BrushSession::affectedKeys(std::span<const BrushSegment> segments)const {
    std::set<size_t> result;double determinant=geometry_.a*geometry_.d-geometry_.b*geometry_.c;
    auto inverse=[&](double x,double y){x-=geometry_.tx;y-=geometry_.ty;return Point{(geometry_.d*x-geometry_.c*y)/determinant,(-geometry_.b*x+geometry_.a*y)/determinant};};
    const double reach=settings_.radius+2;const int columns=(original_->width+255)/256;
    for(auto s:segments){
        double left=std::max(0.,double(std::min(s.x0,s.x1))-reach),right=std::min(double(geometry_.canvasWidth),double(std::max(s.x0,s.x1))+reach);
        double top=std::max(0.,double(std::min(s.y0,s.y1))-reach),bottom=std::min(double(geometry_.canvasHeight),double(std::max(s.y0,s.y1))+reach);
        if(left>=right||top>=bottom)continue;
        const auto corners=std::array{inverse(left,top),inverse(right,top),inverse(left,bottom),inverse(right,bottom)};
        double x0=corners[0].x,y0=corners[0].y,x1=x0,y1=y0;
        for(auto p:corners){x0=std::min(x0,p.x);y0=std::min(y0,p.y);x1=std::max(x1,p.x);y1=std::max(y1,p.y);}
        x0=std::clamp(std::floor(x0),0.,double(original_->width));x1=std::clamp(std::ceil(x1),0.,double(original_->width));
        y0=std::clamp(std::floor(y0),0.,double(original_->height));y1=std::clamp(std::ceil(y1),0.,double(original_->height));
        if(x0>=x1||y0>=y1)continue;
        for(int row=int(y0)/256;row<=(int(y1)-1)/256;++row)for(int column=int(x0)/256;column<=(int(x1)-1)/256;++column)
            result.insert(size_t(row)*columns+column);
    }
    return result;
}
void BrushSession::render(std::span<const BrushSegment> settled,std::span<const BrushSegment> tail){
    const auto preparationStart=std::chrono::steady_clock::now();
    auto nextTail=affectedKeys(tail),changed=affectedKeys(settled);changed.insert(nextTail.begin(),nextTail.end());changed.insert(tailKeys_.begin(),tailKeys_.end());
    if(changed.empty()){tailKeys_=std::move(nextTail);return;}
    auto nextCoverage=coverage_;auto nextRaster=std::make_shared<Raster>(*published_);bool outputChanged=false;
    const auto columns=size_t((original_->width+255)/256);
    struct Work {size_t key;std::shared_ptr<BrushTile> tile;BrushUniforms uniforms;};
    std::vector<Work> work;work.reserve(changed.size());
    for(auto key:changed){
        int x=int(key%columns)*256,y=int(key/columns)*256;
        uint32_t w=uint32_t(std::min(256,original_->width-x)),h=uint32_t(std::min(256,original_->height-y));
        auto old=coverage_.find(key);
        auto tile=old==coverage_.end()?std::make_shared<BrushTile>(w,h):std::make_shared<BrushTile>(*old->second);
        ++metrics_.coverageTileAllocations;
        BrushUniforms u;u.mapping={float(geometry_.a),float(geometry_.b),float(geometry_.c),float(geometry_.d)};
        u.geometry={float(geometry_.tx+x*geometry_.a+y*geometry_.c),float(geometry_.ty+x*geometry_.b+y*geometry_.d),float(settings_.radius),float(settings_.hardness)};
        u.canvas={float(geometry_.canvasWidth),float(geometry_.canvasHeight),float(std::max(0.001,std::min(std::hypot(geometry_.a,geometry_.b),std::hypot(geometry_.c,geometry_.d)))),
            float(std::max(0.25,settings_.radius*2*(settings_.hardness>=1?0.015:0.025)))};
        work.push_back({key,std::move(tile),u});
    }
    const auto coverageStart=std::chrono::steady_clock::now();
    metrics_.tilePreparationMilliseconds+=std::chrono::duration<double,std::milli>(coverageStart-preparationStart).count();
    if(accelerator_){
        std::vector<BrushTileRender> requests;requests.reserve(work.size());for(auto& item:work)requests.push_back({item.tile.get(),item.uniforms});
        try{accelerator_->renderBatch(requests,settled,tail);}catch(const std::exception& e){
            acceleratorError_=e.what();accelerator_.reset();++metrics_.acceleratorFallbacks;
            for(auto& item:work)renderBrushCpu(*item.tile,item.uniforms,settled,tail);}
    }else for(auto& item:work)renderBrushCpu(*item.tile,item.uniforms,settled,tail);
    const auto compositionStart=std::chrono::steady_clock::now();
    metrics_.coverageRenderMilliseconds+=std::chrono::duration<double,std::milli>(compositionStart-coverageStart).count();
    for(auto& item:work){
        auto key=item.key;auto& tile=item.tile;uint32_t w=tile->width,h=tile->height;
        int x=int(key%columns)*256,y=int(key/columns)*256;
        nextCoverage[key]=tile;
        if(output_==Output::CoverageOnly)continue;
        const auto oldCoverage=coverage_.find(key);
        const auto& base=*original_->tiles[key];const auto& previous=*published_->tiles[key];
        std::shared_ptr<Raster::Tile> output;
        for(uint32_t py=0;py<h;++py)for(uint32_t px=0;px<w;++px){
            const size_t coverageIndex=size_t(py)*w+px;
            const auto before=oldCoverage==coverage_.end()?uint8_t{0}:oldCoverage->second->preview[coverageIndex];
            // The original/settings/selection are immutable throughout a stroke.
            // Identical coverage therefore proves an identical published pixel.
            if(tile->preview[coverageIndex]==before)continue;
            size_t local=size_t(py)*256+px;double amount=tile->preview[coverageIndex]/255.*settings_.opacity;
            if(selection_&&amount>0){double sx=x+px+0.5,sy=y+py+0.5;
                double dx=geometry_.tx+sx*geometry_.a+sy*geometry_.c,dy=geometry_.ty+sx*geometry_.b+sy*geometry_.d;
                // Selection coverage already exists on the document pixel grid.
                amount*=selection_->pixel(int(std::floor(dx)),int(std::floor(dy)))/255.;}
            auto painted=paintPixel(base.pixels[local],settings_,amount);
            if(painted!=previous.pixels[local]){if(!output){output=std::make_shared<Raster::Tile>(previous);++metrics_.outputTileAllocations;}
                output->pixels[local]=painted;}
        }
        if(output){nextRaster->tiles[key]=std::move(output);outputChanged=true;}
    }
    coverage_.swap(nextCoverage);tailKeys_.swap(nextTail);metrics_.settledSegments+=settled.size();metrics_.touchedTiles=coverage_.size();
    metrics_.coverageStorageBytes=0;for(auto&[key,tile]:coverage_){(void)key;metrics_.coverageStorageBytes+=tile->permanent.size()*sizeof(float)+tile->preview.size();}
    if(outputChanged){published_=std::move(nextRaster);++metrics_.publishedSnapshots;}
    metrics_.tileCompositionMilliseconds+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-compositionStart).count();
}
std::shared_ptr<const Raster> BrushSession::commit(){
    if(finished_)return published_;
    if(samples_.size()>=2){auto n=samples_.size();auto settled=brushContinuousCurve(samples_[n-2],samples_[n-1],samples_[n>=3?n-3:0],samples_[n-1]);
        render(settled,{});samples_={samples_.back()};}
    finished_=true;return published_;
}
std::shared_ptr<const Raster> BrushSession::cancel(){
    published_=original_;coverage_.clear();tailKeys_.clear();samples_.clear();finished_=true;
    metrics_.touchedTiles=0;metrics_.coverageStorageBytes=0;return original_;
}
} // namespace compositor::graphics
