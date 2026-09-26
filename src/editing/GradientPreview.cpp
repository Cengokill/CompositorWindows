// Gradient.swift and BrushStroke.paintCanvas/BrushCommit at pinned upstream
// a19db9011282399785dc18efcfded904627bdcc2. Copyright (c) 2026 Wonder Assembly LLC.
// MIT notice: graphics/upstream/LICENSE.
#include "GradientPreview.h"
#include "graphics/RasterSampling.h"
#include "graphics/SamplingSource.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace compositor::editing {
namespace {
constexpr uint64_t pixelLimit=100000000;
uint8_t byte(double v){return uint8_t(std::clamp(std::lround(v),0L,255L));}
void dimensions(int w,int h){if(w<1||h<1||w>30000||h>30000)throw std::runtime_error("Invalid gradient source dimensions");}
void budget(Rect r,uint64_t remaining){if(r.empty()||r.width>30000||r.height>30000||r.width*r.height>double(remaining))throw std::runtime_error("Gradient exceeds allocated pixel budget");}
void grayCheck(const GrayRaster& g){dimensions(g.width,g.height);if(uint64_t(g.width)*g.height>pixelLimit||g.pixels.size()!=size_t(g.width)*g.height)throw std::runtime_error("Invalid gradient gray input");}
Rect unite(Rect a,Rect b){if(a.empty())return b;if(b.empty())return a;double x=std::min(a.x,b.x),y=std::min(a.y,b.y);return {x,y,std::max(a.x+a.width,b.x+b.width)-x,std::max(a.y+a.height,b.y+b.height)-y};}
Rect intersect(Rect a,Rect b){double x=std::max(a.x,b.x),y=std::max(a.y,b.y);return {x,y,std::max(0.,std::min(a.x+a.width,b.x+b.width)-x),std::max(0.,std::min(a.y+a.height,b.y+b.height)-y)};}
Rect integral(Rect a){double x=std::floor(a.x),y=std::floor(a.y);return {x,y,std::ceil(a.x+a.width)-x,std::ceil(a.y+a.height)-y};}
Rect inverse(Rect a,const Transform& t,int w,int h){auto p=t.toUnit({a.x,a.y}),q=t.toUnit({a.x+a.width,a.y}),r=t.toUnit({a.x,a.y+a.height}),s=t.toUnit({a.x+a.width,a.y+a.height});double x=std::min({p.x,q.x,r.x,s.x})*w,y=std::min({p.y,q.y,r.y,s.y})*h;return {x,y,std::max({p.x,q.x,r.x,s.x})*w-x,std::max({p.y,q.y,r.y,s.y})*h-y};}
uint64_t remainingBudget(const Document& d,const Layer& target,bool mask){uint64_t image=0,gray=0;for(const auto& l:d.layers)if(l.id!=target.id){if(l.raster)image+=uint64_t(l.raster->width)*l.raster->height;if(l.mask&&l.mask->raster)gray+=uint64_t(l.mask->raster->width)*l.mask->raster->height;}if(image>pixelLimit||gray>pixelLimit)throw std::runtime_error("Invalid existing document pixel budget");return mask?pixelLimit-gray:target.mask?std::min(pixelLimit-image,pixelLimit-gray):pixelLimit-image;}
}
struct GradientPreview::Impl {
    Layer original;
    Transform base;
    Point start,end,origin,stepX,stepY;
    GradientSettings settings;
    Pixel foreground,background;
    int baseWidth{},baseHeight{},canvasWidth{},canvasHeight{};
    Rect grid;
    std::shared_ptr<const GrayRaster> selection;
    bool mask{},active{};
    Impl(Layer layer,const Document& d,Point a,Point b,GradientSettings s,Pixel fg,Pixel bg,bool targetMask)
        :original(std::move(layer)),base(targetMask&&original.mask?original.mask->placement.value_or(original.transform):original.transform),start(a),end(b),settings(s),foreground(fg),background(bg),canvasWidth(d.width),canvasHeight(d.height),mask(targetMask){
        dimensions(d.width,d.height);
        if(!base.valid()||!std::isfinite(s.opacity)||int(s.shape)<0||int(s.shape)>1||int(s.style)<0||int(s.style)>1)throw std::runtime_error("Invalid gradient settings");
        for(double v:{a.x,a.y,b.x,b.y})if(!std::isfinite(v)||std::abs(v)>1e7)throw std::runtime_error("Invalid gradient endpoint");
        if(mask&&(!original.mask||!original.mask->raster))throw std::runtime_error("Gradient requires an owned mask");
        if(!mask&&(original.group||!original.adjustmentJson.empty()))throw std::runtime_error("Gradient requires editable image pixels");
        if(original.raster){const auto& r=*original.raster;dimensions(r.width,r.height);if(uint64_t(r.width)*r.height>pixelLimit||r.tiles.size()!=size_t((r.width+255)/256)*((r.height+255)/256)||std::any_of(r.tiles.begin(),r.tiles.end(),[](const auto& t){return !t;}))throw std::runtime_error("Invalid gradient image input");}
        if(original.mask&&original.mask->raster)grayCheck(*original.mask->raster);
        baseWidth=mask&&original.mask->placement?original.mask->raster->width:original.raster?original.raster->width:int(std::round(base.width));
        baseHeight=mask&&original.mask->placement?original.mask->raster->height:original.raster?original.raster->height:int(std::round(base.height));dimensions(baseWidth,baseHeight);
        origin=base.fromUnit({0,0});auto right=base.fromUnit({1,0}),bottom=base.fromUnit({0,1});stepX={(right.x-origin.x)/baseWidth,(right.y-origin.y)/baseWidth};stepY={(bottom.x-origin.x)/baseHeight,(bottom.y-origin.y)/baseHeight};
        Rect originalBounds{0,0,double(baseWidth),double(baseHeight)},canvas{0,0,double(d.width),double(d.height)},area=canvas;
        grid=originalBounds;
        if(d.selection){selection=d.selection->coverage;if(!selection)throw std::runtime_error("Present gradient selection has no coverage");if(!selection->validStorage())throw std::runtime_error("Invalid gradient selection");if(selection->width!=d.width||selection->height!=d.height)throw std::runtime_error("Gradient selection must match canvas");auto bounds=coverageBounds(*selection);if(bounds.empty())return;if(d.selection->outline)bounds=d.selection->outline->bounds();area=intersect(canvas,{bounds.x-1,bounds.y-1,bounds.width+2,bounds.height+2});}
        if(std::hypot(b.x-a.x,b.y-a.y)<.5||s.opacity<=0||area.empty())return;
        auto extent=mask?originalBounds:unite(originalBounds,integral(inverse(canvas,base,baseWidth,baseHeight)));
        if(extent.width>1e9||extent.height>1e9)throw std::runtime_error("Gradient virtual extent exceeds source limit");
        auto affected=intersect(integral(inverse(area,base,baseWidth,baseHeight)),extent);if(affected.empty())return;
        const double left=std::floor((affected.x-extent.x)/256)*256+extent.x,top=std::floor((affected.y-extent.y)/256)*256+extent.y;
        const double rightEdge=std::min(extent.x+extent.width,std::ceil((affected.x+affected.width-extent.x)/256)*256+extent.x),bottomEdge=std::min(extent.y+extent.height,std::ceil((affected.y+affected.height-extent.y)/256)*256+extent.y);
        grid={left,top,rightEdge-left,bottomEdge-top};if(mask||original.raster)grid=unite(grid,originalBounds);
        budget(grid,remainingBudget(d,original,mask));active=true;
    }
    Point documentPoint(int x,int y)const{return {origin.x+(x+.5)*stepX.x+(y+.5)*stepY.x,origin.y+(x+.5)*stepX.y+(y+.5)*stepY.y};}
    Transform placement(Rect r)const{auto t=base;t.width=base.width*r.width/baseWidth;t.height=base.height*r.height/baseHeight;auto c=base.fromUnit({(r.x+r.width/2)/baseWidth,(r.y+r.height/2)/baseHeight});t.x=c.x-t.width/2;t.y=c.y-t.height/2;if(!t.valid())throw std::runtime_error("Gradient result transform exceeds limits");return t;}
    double coverage(Point p)const{
        if(p.x<0||p.y<0||p.x>=canvasWidth||p.y>=canvasHeight)return 0;
        if(!selection)return 1;
        double x=p.x-.5,y=p.y-.5;int ix=int(std::floor(x)),iy=int(std::floor(y));double fx=x-ix,fy=y-iy;
        return byte((1-fy)*((1-fx)*selection->pixel(ix,iy)+fx*selection->pixel(ix+1,iy))+fy*((1-fx)*selection->pixel(ix,iy+1)+fx*selection->pixel(ix+1,iy+1)))/255.;
    }
    Pixel originalPixel(int x,int y)const{if(x<0||y<0||x>=baseWidth||y>=baseHeight)return {};if(!mask)return original.raster?original.raster->pixel(x,y):Pixel{};const auto& g=*original.mask->raster;auto v=g.pixel(int((x+.5)*g.width/baseWidth),int((y+.5)*g.height/baseHeight));return {v,v,v,255};}
    Pixel pixel(int x,int y)const{
        auto old=originalPixel(x,y);if(!active)return old;auto p=documentPoint(x,y);double amount=coverage(p);if(amount==0)return old;
        double dx=end.x-start.x,dy=end.y-start.y,l2=dx*dx+dy*dy;
        double t=settings.shape==GradientShape::Radial?std::hypot(p.x-start.x,p.y-start.y)/std::sqrt(l2):((p.x-start.x)*dx+(p.y-start.y)*dy)/l2;t=std::clamp(t,0.,1.);if(settings.reversed)t=1-t;
        amount*=std::clamp(settings.opacity,0.,1.);if(settings.style==GradientStyle::ForegroundToTransparent)amount*=1-t;
        auto channel=[&](uint8_t f,uint8_t b,uint8_t o){double c=settings.style==GradientStyle::ForegroundToTransparent?f:f+(int(b)-f)*t;return byte(c*amount+o*(1-amount));};
        if(mask){auto v=channel(foreground.r,background.r,old.r);return {v,v,v,255};}
        return {channel(foreground.r,background.r,old.r),channel(foreground.g,background.g,old.g),channel(foreground.b,background.b,old.b),byte(255*amount+old.a*(1-amount))};
    }
    uint8_t inheritedMask(int x,int y)const{if(x<0||y<0||x>=baseWidth||y>=baseHeight)return 255;const auto& g=*original.mask->raster;return g.pixel(int((x+.5)*g.width/baseWidth),int((y+.5)*g.height/baseHeight));}
    uint8_t maskExterior()const{ // Same 96px edge-majority thumbnail contract as GrowingBrush.
        double factor=std::min(1.,96./std::max(baseWidth,baseHeight));int w=std::max(1,int(baseWidth*factor)),h=std::max(1,int(baseHeight*factor));uint64_t sum=0,count=0;
        for(int y=0;y<h;++y)for(int x=0;x<w;++x){if(y!=0&&y!=h-1&&x!=0&&x!=w-1)continue;double x0=double(x)*baseWidth/w,x1=double(x+1)*baseWidth/w,y0=double(y)*baseHeight/h,y1=double(y+1)*baseHeight/h,value=0;for(int yy=int(std::floor(y0));yy<int(std::ceil(y1));++yy)for(int xx=int(std::floor(x0));xx<int(std::ceil(x1));++xx)value+=pixel(xx,yy).r*(std::min(x1,double(xx+1))-std::max(x0,double(xx)))*(std::min(y1,double(yy+1))-std::max(y0,double(yy)));sum+=byte(value/((x1-x0)*(y1-y0)));++count;}return sum*2>=count*255?255:0;
    }
};
GradientPreview::GradientPreview(Layer original,const Document& d,Point a,Point b,GradientSettings s,Pixel fg,Pixel bg,bool mask):impl_(std::make_shared<Impl>(std::move(original),d,a,b,s,fg,bg,mask)){}
Rect GradientPreview::allocatedBounds()const{return impl_->grid;}
uint64_t GradientPreview::allocatedPixelCount()const{return impl_->active?uint64_t(impl_->grid.width)*uint64_t(impl_->grid.height):0;}
uint64_t GradientPreview::previewOwnedPixelBytes()const{return 0;}
std::shared_ptr<const LayerRenderPreview> GradientPreview::renderPreview()const{
    const auto s=impl_;if(!s->active)return {};auto p=std::make_shared<LayerRenderPreview>();p->layer=s->original;p->identity=s;p->lineage=s;
    auto source=std::make_shared<graphics::SamplingSource>();source->width=int(s->grid.width);source->height=int(s->grid.height);source->identity=s;
    const int x=int(s->grid.x),y=int(s->grid.y);
    // The pending source retains RasterSnapshot's phase. BrushCommit assembles
    // a fresh image on Apply, so the committed raster resets alignment to zero.
    const auto gray=s->mask?s->original.mask->raster:std::shared_ptr<const GrayRaster>{};
    source->alignmentX=(gray?gray->width==s->baseWidth?gray->samplingOriginX:0:s->original.raster?s->original.raster->samplingOriginX:0)-x;
    source->alignmentY=(gray?gray->height==s->baseHeight?gray->samplingOriginY:0:s->original.raster?s->original.raster->samplingOriginY:0)-y;
    if(s->mask){if(p->layer.mask->placement)p->layer.mask->previewExterior=s->maskExterior();source->gray=[s,x,y](int px,int py){return s->pixel(x+px,y+py).r;};p->maskSource=source;p->mask=[source](Point unit,Transform::Sampling sampling,uint8_t exterior){return graphics::sampleMaskPixels(source->width,source->height,source->gray,unit,sampling,exterior);};}
    else{p->layer.transform=s->placement(s->grid);p->layer.shapeJson.clear();if(!p->layer.raster){static const auto placeholder=Raster::filled(1,1);p->layer.raster=placeholder;}source->rgba=[s,x,y](int px,int py){return s->pixel(x+px,y+py);};p->imageSource=source;
        p->image=[source](Point unit,Transform::Sampling sampling){return graphics::sampleRasterPixels(source->width,source->height,source->rgba,unit,sampling);};
        if(p->layer.mask&&!p->layer.mask->placement&&s->grid!=Rect{0,0,double(s->baseWidth),double(s->baseHeight)}){auto m=std::make_shared<graphics::SamplingSource>();m->width=source->width;m->height=source->height;const auto& old=*s->original.mask->raster;m->alignmentX=(old.width==s->baseWidth?old.samplingOriginX:0)-x;m->alignmentY=(old.height==s->baseHeight?old.samplingOriginY:0)-y;m->identity=s;m->gray=[s,x,y](int px,int py){return s->inheritedMask(x+px,y+py);};p->maskSource=m;p->mask=[m](Point unit,Transform::Sampling sampling,uint8_t exterior){return graphics::sampleMaskPixels(m->width,m->height,m->gray,unit,sampling,exterior);};}}
    return p;
}
Layer GradientPreview::materializeLayer()const{
    const auto& s=*impl_;if(!s.active)return s.original;auto result=s.original;int w=int(s.grid.width),h=int(s.grid.height),ox=int(s.grid.x),oy=int(s.grid.y);
    if(s.mask){auto gray=std::make_shared<GrayRaster>();gray->width=w;gray->height=h;gray->pixels.resize(size_t(w)*h);for(int y=0;y<h;++y)for(int x=0;x<w;++x)gray->pixels[size_t(y)*w+x]=s.pixel(ox+x,oy+y).r;result.mask->raster=gray;result.mask->previewExterior.reset();return result;}
    auto image=std::make_shared<Raster>(*Raster::filled(w,h));const int columns=(w+255)/256;int left=w,top=h,right=0,bottom=0;
    for(size_t key=0;key<image->tiles.size();++key){auto tile=std::make_shared<Raster::Tile>();int tx=int(key%columns)*256,ty=int(key/columns)*256;for(int y=0;y<std::min(256,h-ty);++y)for(int x=0;x<std::min(256,w-tx);++x){auto value=s.pixel(ox+tx+x,oy+ty+y);tile->pixels[size_t(y)*256+x]=value;if(value.a){left=std::min(left,tx+x);top=std::min(top,ty+y);right=std::max(right,tx+x+1);bottom=std::max(bottom,ty+y+1);}}image->tiles[key]=std::move(tile);}
    if(right<=left){left=top=0;right=w;bottom=h;}Rect crop{double(ox+left),double(oy+top),double(right-left),double(bottom-top)};
    if(left||top||right!=w||bottom!=h){auto tight=std::make_shared<Raster>(*Raster::filled(right-left,bottom-top));int cols=(tight->width+255)/256;for(size_t key=0;key<tight->tiles.size();++key){auto tile=std::make_shared<Raster::Tile>();int tx=int(key%cols)*256,ty=int(key/cols)*256;for(int y=0;y<std::min(256,tight->height-ty);++y)for(int x=0;x<std::min(256,tight->width-tx);++x)tile->pixels[size_t(y)*256+x]=image->pixel(left+tx+x,top+ty+y);tight->tiles[key]=std::move(tile);}image=std::move(tight);}
    result.raster=image;result.transform=s.placement(crop);result.shapeJson.clear();
    if(result.mask&&!result.mask->placement&&crop!=Rect{0,0,double(s.baseWidth),double(s.baseHeight)}){auto gray=std::make_shared<GrayRaster>();gray->width=int(crop.width);gray->height=int(crop.height);gray->pixels.resize(size_t(gray->width)*gray->height);for(int y=0;y<gray->height;++y)for(int x=0;x<gray->width;++x)gray->pixels[size_t(y)*gray->width+x]=s.inheritedMask(int(crop.x)+x,int(crop.y)+y);result.mask->raster=gray;}
    return result;
}
}
