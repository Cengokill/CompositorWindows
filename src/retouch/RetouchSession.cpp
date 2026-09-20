// Source: CloneStamp.swift, BlurTool.swift, SmudgeLiquify.swift, BrushStroke.heal.
// Copyright (c) 2026 Wonder Assembly LLC; MIT license in LICENSE.
#include "RetouchSession.h"
#include "graphics/PixelAlgorithms.h"
#include "graphics/RasterSampling.h"
#include <algorithm>
#include <cmath>
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
    mutable uint64_t warpPreviewRevision{~uint64_t{0}};
    Transform transform;
    int width,height;
    Settings settings;
    Sources sources;
    Metrics stats;
    std::unique_ptr<graphics::BrushSession> coverage;
    std::shared_ptr<graphics::D3D11BrushCoverage> accelerator;
    std::vector<Pixel> working;
    std::vector<std::array<float,4>> carried;
    std::optional<Point> last;
    bool started{},finished{},coverageStarted{},emptySelection{},maskTarget{};
    Impl(std::shared_ptr<const Raster> image,Transform placement,int w,int h,Settings options,Sources input,std::shared_ptr<const GrayRaster> clip,std::shared_ptr<graphics::D3D11BrushCoverage> gpu)
        :original(std::move(image)),published(original),selection(std::move(clip)),transform(placement),width(w),height(h),settings(options),sources(std::move(input)),accelerator(std::move(gpu)){
        if(!original)throw std::invalid_argument("Missing retouch raster");validRaster(*original);validSize(w,h);if(!placement.valid())throw std::invalid_argument("Invalid retouch transform");
        if(int(settings.mode)<int(Mode::Clone)||int(settings.mode)>int(Mode::Liquify))throw std::invalid_argument("Invalid retouch mode");
        if(!std::isfinite(settings.radius)||settings.radius<.5||settings.radius>1000||!std::isfinite(settings.hardness)||settings.hardness<0||settings.hardness>1||!std::isfinite(settings.opacity)||settings.opacity<.01||settings.opacity>1)throw std::invalid_argument("Invalid retouch tip");
        if(selection){if(selection->width!=w||selection->height!=h||selection->pixels.size()!=size_t(w)*h)throw std::invalid_argument("Retouch selection must use document grid");emptySelection=std::none_of(selection->pixels.begin(),selection->pixels.end(),[](uint8_t v){return v!=0;});}
        for(auto imageSource:{sources.currentLayer,sources.allLayers})if(imageSource){validRaster(*imageSource);if(imageSource->width!=w||imageSource->height!=h)throw std::invalid_argument("Retouch sample must use document grid");}
    }
    Point documentPoint(int x,int y)const{return transform.fromUnit({(x+.5)/original->width,(y+.5)/original->height});}
    double selected(Point p)const{return selection?selection->pixel(int(std::floor(p.x)),int(std::floor(p.y)))/255.:1;}
    std::shared_ptr<const Raster> currentSample(){
        if(sources.currentLayer)return sources.currentLayer;
        std::vector<Pixel> output(size_t(width)*height);const uint8_t bg=maskTarget?graphics::cachedMaskBackground(originalMask):uint8_t{0};
        for(int y=0;y<height;++y)for(int x=0;x<width;++x){const auto unit=transform.toUnit({x+.5,y+.5});
            if(maskTarget){const auto v=byte(graphics::sampleGray(*originalMask,unit,transform.sampling,bg)*255);output[size_t(y)*width+x]={v,v,v,255};}
            else output[size_t(y)*width+x]=graphics::sampleRaster(*original,unit,transform.sampling);}
        ++stats.documentRasterizations;stats.workingBufferPixels+=output.size();return fromPixels(output,width,height);
    }
    void prepare(){
        if(settings.mode==Mode::Clone){if(!sources.cloneOffset||!pointValid(*sources.cloneOffset))throw std::invalid_argument("Clone source must be set before painting");sample=settings.sampleAllLayers?sources.allLayers:currentSample();if(!sample)throw std::invalid_argument("All-layer clone requires a frozen composite sample");}
        else if(settings.mode==Mode::Blur)sample=gaussian(*currentSample(),std::clamp(settings.radius*2/10,1.5,30.),maskTarget,stats);
        else if(warp(settings.mode)){working=pixelsOf(*currentSample());stats.workingBufferPixels+=working.size();}
        graphics::BrushSessionSettings brush;brush.radius=warp(settings.mode)?std::max(1.,settings.radius)+2:settings.radius;brush.hardness=warp(settings.mode)?1:settings.hardness;brush.opacity=1;
        coverage=std::make_unique<graphics::BrushSession>(original,brush,accelerator,nullptr,graphics::BrushSessionGeometry::forLayer(transform,original->width,original->height,width,height),graphics::BrushSession::Output::CoverageOnly);
    }
    Pixel workingSample(Point doc)const{
        // The sample image lives at pixel edges; interpolate its pixel centers.
        if(doc.x<0||doc.y<0||doc.x>=width||doc.y>=height)return {};
        const double xx=doc.x-.5,yy=doc.y-.5;const int x=int(std::floor(xx)),y=int(std::floor(yy));const double fx=xx-x,fy=yy-y;
        const auto get=[&](int a,int b){return working[size_t(std::clamp(b,0,height-1))*width+std::clamp(a,0,width-1)];};
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
                else {const auto offset=settings.mode==Mode::Clone?*sources.cloneOffset:Point{};const auto copied=graphics::sampleRaster(*sample,{(doc.x+offset.x)/width,(doc.y+offset.y)/height},Transform::Sampling::Smooth);next=over(base,copied,amount*settings.opacity);}
                if(next!=published->tiles[key]->pixels[local]){if(!copy){copy=std::make_shared<Raster::Tile>(*published->tiles[key]);++stats.publishedTileCopies;}copy->pixels[local]=next;}
            }
            if(copy){result->tiles[key]=std::move(copy);changed=true;}
        }
        if(changed)published=std::move(result);
    }
    float weight(float u)const{if(u>=1)return 0;const float h=float(std::clamp(settings.hardness,0.,.98));if(u<=h)return 1;const float t=(1-u)/(1-h);return t*t*(3-2*t);}
    void pickup(Point p){const int r=int(std::ceil(std::max(1.,settings.radius))),side=r*2+1,cx=int(std::round(p.x)),cy=int(std::round(p.y));carried.assign(size_t(side)*side,{});
        for(int dy=-r;dy<=r;++dy)for(int dx=-r;dx<=r;++dx){const int x=cx+dx,y=cy+dy;if(x<0||y<0||x>=width||y>=height)continue;const auto c=working[size_t(y)*width+x];carried[size_t(dy+r)*side+dx+r]={float(c.r),float(c.g),float(c.b),float(c.a)};}}
    void smudge(Point p){const double radius=std::max(1.,settings.radius);const int r=int(std::ceil(radius)),side=2*r+1,cx=int(std::round(p.x)),cy=int(std::round(p.y));
        constexpr std::array<uint8_t Pixel::*,4> channels{&Pixel::r,&Pixel::g,&Pixel::b,&Pixel::a};
        for(int dy=-r;dy<=r;++dy)for(int dx=-r;dx<=r;++dx){int x=cx+dx,y=cy+dy;if(x<0||y<0||x>=width||y>=height)continue;float w=weight(std::sqrt(float(dx*dx+dy*dy))/float(radius));if(w<=0)continue;
            auto& under=working[size_t(y)*width+x];auto& color=carried[size_t(dy+r)*side+dx+r];for(size_t k=0;k<4;++k){float painted=float(under.*channels[k])+(color[k]-float(under.*channels[k]))*w;under.*channels[k]=byte(painted);color[k]=painted+(color[k]-painted)*float(settings.opacity);}}
    }
    void push(Point a,Point b){const double radius=std::max(1.,settings.radius);const int r=int(std::ceil(radius)),cx=int(std::round(b.x)),cy=int(std::round(b.y));const float mx=float(b.x-a.x)*float(settings.opacity),my=float(b.y-a.y)*float(settings.opacity);
        const int margin=int(std::ceil(std::max(std::abs(mx),std::abs(my))))+2,x0=std::max(0,cx-r-margin),x1=std::min(width-1,cx+r+margin),y0=std::max(0,cy-r-margin),y1=std::min(height-1,cy+r+margin);
        if(x0>x1||y0>y1)return;const int cw=x1-x0+1,ch=y1-y0+1;std::vector<Pixel> scratch(size_t(cw)*ch);for(int y=0;y<ch;++y)std::copy_n(working.data()+size_t(y+y0)*width+x0,cw,scratch.data()+size_t(y)*cw);
        constexpr std::array<uint8_t Pixel::*,4> channels{&Pixel::r,&Pixel::g,&Pixel::b,&Pixel::a};
        for(int dy=-r;dy<=r;++dy)for(int dx=-r;dx<=r;++dx){const int x=cx+dx,y=cy+dy;if(x<x0||y<y0||x>x1||y>y1)continue;const float w=weight(std::sqrt(float(dx*dx+dy*dy))/float(radius));if(w<=0)continue;
            const float sx=std::clamp(float(x-x0)-mx*w,0.f,float(cw-1)),sy=std::clamp(float(y-y0)-my*w,0.f,float(ch-1));const int ix=std::min(cw-2,int(sx)),iy=std::min(ch-2,int(sy));if(ix<0||iy<0)continue;const float fx=sx-float(ix),fy=sy-float(iy);
            const auto p00=scratch[size_t(iy)*cw+ix],p10=scratch[size_t(iy)*cw+ix+1],p01=scratch[size_t(iy+1)*cw+ix],p11=scratch[size_t(iy+1)*cw+ix+1];auto& out=working[size_t(y)*width+x];
            for(auto channel:channels){const float top=float(p00.*channel)+(float(p10.*channel)-float(p00.*channel))*fx,bottom=float(p01.*channel)+(float(p11.*channel)-float(p01.*channel))*fx;out.*channel=byte(top+(bottom-top)*fy);}}
    }
    bool appendWarp(Point point){
        if(!last){last=point;if(settings.mode==Mode::Smudge)pickup(point);return true;}
        const auto from=*last;const double distance=std::hypot(point.x-from.x,point.y-from.y),diameter=std::max(2.,settings.radius*2),spacing=std::max(1.,diameter*(settings.mode==Mode::Smudge?.08:.025));if(distance<spacing)return false;
        const int steps=int(std::ceil(distance/spacing));Point previous=from;
        for(int step=1;step<=steps;++step){const double t=double(step)/steps;const Point next{from.x+(point.x-from.x)*t,from.y+(point.y-from.y)*t};if(settings.mode==Mode::Smudge)smudge(next);else push(previous,next);++stats.warpDabs;
            if(!coverageStarted){coverageStarted=coverage->begin(next);}else coverage->append(next);previous=next;}
        last=point;compose();return true;
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
RetouchSession::RetouchSession(std::shared_ptr<const Raster> image,Transform transform,int w,int h,Settings settings,Sources sources,std::shared_ptr<const GrayRaster> selection,std::shared_ptr<graphics::D3D11BrushCoverage> accelerator)
    :impl_(std::make_unique<Impl>(std::move(image),transform,w,h,settings,std::move(sources),std::move(selection),std::move(accelerator))){}
RetouchSession::RetouchSession(std::shared_ptr<const GrayRaster> mask,Transform transform,int w,int h,Settings settings,std::shared_ptr<const GrayRaster> selection,std::shared_ptr<graphics::D3D11BrushCoverage> accelerator)
    :RetouchSession(maskRaster(mask),transform,w,h,settings,{},std::move(selection),std::move(accelerator)){
    if(settings.mode!=Mode::Blur)throw std::invalid_argument("Only Blur supports mask retouch");impl_->originalMask=std::move(mask);impl_->maskTarget=true;
}
RetouchSession::~RetouchSession()=default;
bool RetouchSession::begin(Point p){auto& s=*impl_;if(s.started||s.finished)throw std::logic_error("Retouch session already started or finished");if(!pointValid(p)||s.emptySelection)return false;s.prepare();s.started=true;
    if(warp(s.settings.mode))return s.appendWarp(p);s.coverageStarted=s.coverage->begin(p);s.compose();return s.coverageStarted;}
bool RetouchSession::append(Point p){auto& s=*impl_;if(!s.started||s.finished)throw std::logic_error("Retouch session is not active");if(!pointValid(p))return false;if(warp(s.settings.mode))return s.appendWarp(p);if(!s.coverage->append(p))return false;s.compose();return true;}
std::shared_ptr<const Raster> RetouchSession::preview()const{return impl_->published;}
std::shared_ptr<const Raster> RetouchSession::warpDocumentPreview()const{auto& s=*impl_;if(!s.started||!warp(s.settings.mode)||s.working.empty())throw std::logic_error("No active warp document preview");if(s.warpPreviewRevision!=s.stats.warpDabs){s.warpPreview=fromPixels(s.working,s.width,s.height);s.warpPreviewRevision=s.stats.warpDabs;}return s.warpPreview;}
std::shared_ptr<const Raster> RetouchSession::commit(){auto& s=*impl_;if(s.finished)return s.published;if(!s.started||!s.coverageStarted){s.finished=true;return s.published;}s.coverage->commit();if(healing(s.settings.mode))s.finishHeal();else s.compose();s.finished=true;return s.published;}
std::shared_ptr<const Raster> RetouchSession::cancel(){auto& s=*impl_;s.published=s.original;s.finished=true;s.coverage.reset();s.working.clear();s.carried.clear();return s.original;}
std::shared_ptr<const GrayRaster> RetouchSession::previewMask()const{auto& s=*impl_;if(!s.maskTarget)throw std::logic_error("Retouch target is not a mask");if(s.published==s.original)return s.originalMask;if(s.grayPreviewOwner==s.published)return s.grayPreview;
    auto gray=std::make_shared<GrayRaster>();gray->width=s.original->width;gray->height=s.original->height;gray->pixels.resize(size_t(gray->width)*gray->height);for(int y=0;y<gray->height;++y)for(int x=0;x<gray->width;++x)gray->pixels[size_t(y)*gray->width+x]=s.published->pixel(x,y).r;s.grayPreview=gray;s.grayPreviewOwner=s.published;return gray;}
std::shared_ptr<const GrayRaster> RetouchSession::commitMask(){commit();return previewMask();}
std::shared_ptr<const GrayRaster> RetouchSession::cancelMask(){cancel();return previewMask();}
const Metrics& RetouchSession::metrics()const{return impl_->stats;}
}
