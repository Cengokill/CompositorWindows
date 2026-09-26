// SelectionEdits.swift, SelectionClipboard.swift, BrushStroke.swift and Gradient.swift
// at the pinned revision. Copyright (c) 2026 Wonder Assembly LLC;
// MIT notice in graphics/upstream/LICENSE.
#include "PixelEdits.h"
#include "GradientPreview.h"
#include "graphics/MaskSampling.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>

namespace compositor::editing {
namespace {
void sizeCheck(int w,int h){if(w<1||h<1||w>30000||h>30000||uint64_t(w)*h>100000000)throw std::runtime_error("Pixel edit exceeds raster budget");}
void grayCheck(const GrayRaster& g){sizeCheck(g.width,g.height);if(g.pixels.size()!=size_t(g.width)*g.height)throw std::runtime_error("Invalid mask storage");}
uint8_t byte(double v){return uint8_t(std::clamp(std::lround(v),0L,255L));}
Rect intersect(Rect a,Rect b){double x=std::max(a.x,b.x),y=std::max(a.y,b.y),right=std::min(a.x+a.width,b.x+b.width),bottom=std::min(a.y+a.height,b.y+b.height);return {x,y,std::max(0.,right-x),std::max(0.,bottom-y)};}
Rect unite(Rect a,Rect b){if(a.empty())return b;if(b.empty())return a;double x=std::min(a.x,b.x),y=std::min(a.y,b.y);return {x,y,std::max(a.x+a.width,b.x+b.width)-x,std::max(a.y+a.height,b.y+b.height)-y};}
Rect integral(Rect r){double x=std::floor(r.x),y=std::floor(r.y);return {x,y,std::ceil(r.x+r.width)-x,std::ceil(r.y+r.height)-y};}
Rect toPixels(Rect rect,const Transform& t,int w,int h){
    auto a=t.toUnit({rect.x,rect.y}),b=t.toUnit({rect.x+rect.width,rect.y}),c=t.toUnit({rect.x,rect.y+rect.height}),d=t.toUnit({rect.x+rect.width,rect.y+rect.height});
    double x=std::min({a.x,b.x,c.x,d.x})*w,y=std::min({a.y,b.y,c.y,d.y})*h;return {x,y,std::max({a.x,b.x,c.x,d.x})*w-x,std::max({a.y,b.y,c.y,d.y})*h-y};
}
Transform transformFor(const Transform& t,Rect grid,int baseWidth,int baseHeight){
    auto out=t;out.width=grid.width*t.width/baseWidth;out.height=grid.height*t.height/baseHeight;auto center=t.fromUnit({(grid.x+grid.width/2)/baseWidth,(grid.y+grid.height/2)/baseHeight});out.x=center.x-out.width/2;out.y=center.y-out.height/2;
    if(!out.valid())throw std::runtime_error("Edited transform exceeds project limits");return out;
}
std::shared_ptr<const Raster> editRaster(const std::shared_ptr<const Raster>& src,const std::function<Pixel(Pixel,int,int)>& edit){
    std::shared_ptr<Raster> out;int columns=(src->width+255)/256;
    for(size_t key=0;key<src->tiles.size();++key){int tx=int(key%columns)*256,ty=int(key/columns)*256;std::shared_ptr<Raster::Tile> changed;
        for(int y=0;y<std::min(256,src->height-ty);++y)for(int x=0;x<std::min(256,src->width-tx);++x){size_t pos=size_t(y)*256+x;auto old=src->tiles[key]->pixels[pos],value=edit(old,tx+x,ty+y);if(value!=old){if(!out)out=std::make_shared<Raster>(*src);if(!changed){changed=std::make_shared<Raster::Tile>(*src->tiles[key]);out->tiles[key]=changed;}changed->pixels[pos]=value;}}
    }return out?out:src;
}
std::shared_ptr<const Raster> reframe(const std::shared_ptr<const Raster>& src,Rect grid){
    int w=int(grid.width),h=int(grid.height),ox=int(grid.x),oy=int(grid.y);sizeCheck(w,h);
    if(src&&ox==0&&oy==0&&w==src->width&&h==src->height)return src;
    auto out=std::make_shared<Raster>(*Raster::filled(w,h));if(!src)return out;int columns=(w+255)/256,sourceColumns=(src->width+255)/256;
    for(size_t key=0;key<out->tiles.size();++key){int dx=int(key%columns)*256,dy=int(key/columns)*256,sx=dx+ox,sy=dy+oy;int tw=std::min(256,w-dx),th=std::min(256,h-dy);
        if(sx>=0&&sy>=0&&sx+tw<=src->width&&sy+th<=src->height&&sx%256==0&&sy%256==0){out->tiles[key]=src->tiles[size_t(sy/256)*sourceColumns+sx/256];continue;}
        if(sx>=src->width||sy>=src->height||sx+tw<=0||sy+th<=0)continue;
        auto tile=std::make_shared<Raster::Tile>();for(int y=0;y<th;++y)for(int x=0;x<tw;++x)tile->pixels[size_t(y)*256+x]=src->pixel(sx+x,sy+y);out->tiles[key]=tile;
    }return out;
}
Rect alphaBounds(const Raster& raster){int l=raster.width,t=raster.height,r=0,b=0;for(int y=0;y<raster.height;++y)for(int x=0;x<raster.width;++x)if(raster.pixel(x,y).a){l=std::min(l,x);t=std::min(t,y);r=std::max(r,x+1);b=std::max(b,y+1);}return r<=l?Rect{0,0,double(raster.width),double(raster.height)}:Rect{double(l),double(t),double(r-l),double(b-t)};}
std::shared_ptr<const GrayRaster> reframeMask(const GrayRaster& src,Rect grid,int baseWidth,int baseHeight){
    grayCheck(src);int w=int(grid.width),h=int(grid.height);sizeCheck(w,h);auto out=std::make_shared<GrayRaster>();out->width=w;out->height=h;out->pixels.resize(size_t(w)*h,255);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){double px=x+grid.x+.5,py=y+grid.y+.5;if(px>=0&&py>=0&&px<baseWidth&&py<baseHeight)out->pixels[size_t(y)*w+x]=src.pixel(int(px*src.width/baseWidth),int(py*src.height/baseHeight));}return out;
}
Rect editGrid(const Layer& layer,const Document& doc,int baseWidth,int baseHeight,PixelEdit operation,Rect selectedBounds){
    Rect original{0,0,double(baseWidth),double(baseHeight)},canvas{0,0,double(doc.width),double(doc.height)};
    auto extent=unite(original,integral(toPixels(canvas,layer.transform,baseWidth,baseHeight)));
    if(extent.width>1000000000||extent.height>1000000000)throw std::runtime_error("Expanded working grid exceeds source limit");
    Rect area=doc.selection?intersect(canvas,{selectedBounds.x-1,selectedBounds.y-1,selectedBounds.width+2,selectedBounds.height+2}):canvas;
    auto affected=intersect(integral(toPixels(area,layer.transform,baseWidth,baseHeight)),extent);if(operation==PixelEdit::Clear)affected=intersect(affected,original);if(affected.empty())return {};
    double left=std::floor((affected.x-extent.x)/256)*256+extent.x,top=std::floor((affected.y-extent.y)/256)*256+extent.y;
    double right=std::min(extent.x+extent.width,std::ceil((affected.x+affected.width-extent.x)/256)*256+extent.x),bottom=std::min(extent.y+extent.height,std::ceil((affected.y+affected.height-extent.y)/256)*256+extent.y);
    Rect allocated{left,top,right-left,bottom-top};if(layer.raster)allocated=unite(allocated,original);
    sizeCheck(int(allocated.width),int(allocated.height));return allocated;
}
bool noSelectionPixels(const Document& doc,Rect& bounds){
    if(!doc.selection)return false;if(!doc.selection->coverage)throw std::runtime_error("Present selection has no coverage");
    const auto& coverage=*doc.selection->coverage;if(coverage.width!=doc.width||coverage.height!=doc.height)throw std::runtime_error("Selection dimensions do not match canvas");bounds=coverageBounds(coverage);if(bounds.empty())return true;if(doc.selection->outline)bounds=doc.selection->outline->bounds();return false;
}
struct GradientPaint {
    Point start,end;
    GradientSettings settings;
    Pixel foreground,background;
    std::array<double,4> at(Point p)const {
        double dx=end.x-start.x,dy=end.y-start.y,lengthSquared=dx*dx+dy*dy;
        double t=settings.shape==GradientShape::Radial?std::hypot(p.x-start.x,p.y-start.y)/std::sqrt(lengthSquared):((p.x-start.x)*dx+(p.y-start.y)*dy)/lengthSquared;
        t=std::clamp(t,0.,1.);if(settings.reversed)t=1-t;
        if(settings.style==GradientStyle::ForegroundToTransparent)return {double(foreground.r),double(foreground.g),double(foreground.b),(1-t)*std::clamp(settings.opacity,0.,1.)};
        return {foreground.r+(int(background.r)-foreground.r)*t,foreground.g+(int(background.g)-foreground.g)*t,foreground.b+(int(background.b)-foreground.b)*t,std::clamp(settings.opacity,0.,1.)};
    }
};
struct PixelMapping {
    Point origin,stepX,stepY;
    PixelMapping(const Transform& t,int w,int h){origin=t.fromUnit({0,0});auto right=t.fromUnit({1,0}),bottom=t.fromUnit({0,1});stepX={(right.x-origin.x)/w,(right.y-origin.y)/w};stepY={(bottom.x-origin.x)/h,(bottom.y-origin.y)/h};}
    Point operator()(int x,int y)const{return {origin.x+(x+.5)*stepX.x+(y+.5)*stepY.x,origin.y+(x+.5)*stepX.y+(y+.5)*stepY.y};}
};
}
static Layer editLayerImpl(const Layer& layer,const Document& doc,PixelEdit operation,Pixel color,bool targetMask,uint8_t maskBackground,const GradientPaint* gradient){
    if(!layer.transform.valid()||doc.width<1||doc.height<1||doc.width>30000||doc.height>30000)throw std::runtime_error("Invalid pixel edit geometry");
    Rect selectedBounds;if(noSelectionPixels(doc,selectedBounds)||(operation==PixelEdit::Clear&&!doc.selection))return layer;
    if(targetMask){
        if(!layer.mask||!layer.mask->raster)throw std::runtime_error("Pixel edit requires an owned mask");
        auto source=layer.mask->raster;grayCheck(*source);int w=source->width,h=source->height;
        // Brush edits use the layer grid for an attached mask, even when its stored
        // coverage is uniform. Invert only expands a uniform mask for a selection.
        if(operation!=PixelEdit::Invert&&!layer.mask->placement){w=layer.raster?layer.raster->width:int(std::round(layer.transform.width));h=layer.raster?layer.raster->height:int(std::round(layer.transform.height));}
        else if(operation==PixelEdit::Invert&&doc.selection&&w==1&&h==1){w=layer.raster?layer.raster->width:int(std::round(layer.transform.width));h=layer.raster?layer.raster->height:int(std::round(layer.transform.height));}
        sizeCheck(w,h);auto transform=layer.mask->placement.value_or(layer.transform);auto coverage=mappedCoverage(doc,transform,w,h,operation!=PixelEdit::Invert);PixelMapping mapping(transform,w,h);
        std::shared_ptr<GrayRaster> out;for(int y=0;y<h;++y)for(int x=0;x<w;++x){auto old=source->pixel(int((x+.5)*source->width/w),int((y+.5)*source->height/h));double a=coverage?coverage->pixel(x,y)/255.:1.;double adjusted=operation==PixelEdit::Invert?uint8_t(255-old):operation==PixelEdit::Clear?maskBackground:color.r;if(gradient){auto ramp=gradient->at(mapping(x,y));adjusted=ramp[0];a*=ramp[3];}auto value=byte(old+(adjusted-old)*a);
            if(value!=old){if(!out){out=std::make_shared<GrayRaster>();out->width=w;out->height=h;out->pixels.resize(size_t(w)*h);for(int sy=0;sy<h;++sy)for(int sx=0;sx<w;++sx)out->pixels[size_t(sy)*w+sx]=source->pixel(int((sx+.5)*source->width/w),int((sy+.5)*source->height/h));}out->pixels[size_t(y)*w+x]=value;}}
        if(!out)return layer;auto result=layer;result.mask->raster=out;return result;
    }
    if(layer.group||!layer.adjustmentJson.empty())throw std::runtime_error("This layer has no editable image pixel target");
    if(!layer.raster&&operation!=PixelEdit::Fill)return layer;
    int baseWidth=layer.raster?layer.raster->width:int(std::round(layer.transform.width)),baseHeight=layer.raster?layer.raster->height:int(std::round(layer.transform.height));
    if(baseWidth<1||baseHeight<1||baseWidth>30000||baseHeight>30000)throw std::runtime_error("Invalid pixel edit grid");if(layer.raster)sizeCheck(baseWidth,baseHeight);
    Rect grid=operation==PixelEdit::Invert?Rect{0,0,double(baseWidth),double(baseHeight)}:editGrid(layer,doc,baseWidth,baseHeight,operation,selectedBounds);if(grid.empty())return layer;
    auto transform=transformFor(layer.transform,grid,baseWidth,baseHeight);auto source=reframe(layer.raster,grid);auto coverage=mappedCoverage(doc,transform,source->width,source->height,operation!=PixelEdit::Invert);PixelMapping mapping(transform,source->width,source->height);
    auto changed=editRaster(source,[&](Pixel old,int x,int y){double a=coverage?coverage->pixel(x,y)/255.:1.;if(a==0)return old;
        if(operation==PixelEdit::Clear)return Pixel{byte(old.r*(1-a)),byte(old.g*(1-a)),byte(old.b*(1-a)),byte(old.a*(1-a))};
        if(operation==PixelEdit::Invert)return Pixel{byte(old.r+(int(old.a)-2*int(old.r))*a),byte(old.g+(int(old.a)-2*int(old.g))*a),byte(old.b+(int(old.a)-2*int(old.b))*a),old.a};
        if(gradient){auto ramp=gradient->at(mapping(x,y));a*=ramp[3];return Pixel{byte(ramp[0]*a+old.r*(1-a)),byte(ramp[1]*a+old.g*(1-a)),byte(ramp[2]*a+old.b*(1-a)),byte(255*a+old.a*(1-a))};}
        return Pixel{byte(color.r*a+old.r*(1-a)),byte(color.g*a+old.g*(1-a)),byte(color.b*a+old.b*(1-a)),byte(255*a+old.a*(1-a))};
    });
    if(changed==source)return layer;
    if(operation!=PixelEdit::Invert){auto crop=alphaBounds(*changed);changed=reframe(changed,crop);grid.x+=crop.x;grid.y+=crop.y;grid.width=crop.width;grid.height=crop.height;transform=transformFor(layer.transform,grid,baseWidth,baseHeight);}
    auto result=layer;result.raster=changed;result.transform=transform;result.shapeJson.clear();
    if(result.mask&&!result.mask->placement&&grid!=Rect{0,0,double(baseWidth),double(baseHeight)})result.mask->raster=reframeMask(*result.mask->raster,grid,baseWidth,baseHeight);
    return result;
}
Layer editLayer(const Layer& layer,const Document& doc,PixelEdit operation,Pixel color,bool targetMask,uint8_t maskBackground){return editLayerImpl(layer,doc,operation,color,targetMask,maskBackground,nullptr);}
Layer gradientLayer(const Layer& layer,const Document& doc,Point start,Point end,GradientSettings settings,Pixel foreground,Pixel background,bool targetMask){
    return GradientPreview(layer,doc,start,end,settings,foreground,background,targetMask).materializeLayer();
}
std::optional<CopiedPixels> copyPixels(const Document& doc,std::optional<std::string_view> layerID,const IRasterBackend& backend,const SelectionOutline* outline,bool targetMask){
    Rect selectedBounds;if(noSelectionPixels(doc,selectedBounds))return {};
    Rect bounds=doc.selection?(outline?outline->bounds():doc.selection->outline?doc.selection->outline->bounds():selectedBounds):Rect{0,0,double(doc.width),double(doc.height)};if(bounds.empty())return {};
    double x=std::floor(bounds.x+.001),y=std::floor(bounds.y+.001);auto region=intersect({x,y,std::ceil(bounds.x+bounds.width-.001)-x,std::ceil(bounds.y+bounds.height-.001)-y},{0,0,double(doc.width),double(doc.height)});if(region.empty())return {};
    int rx=int(region.x),ry=int(region.y),w=int(region.width),h=int(region.height);sizeCheck(w,h);std::shared_ptr<const Raster> image;
    if(layerID){auto at=std::find_if(doc.layers.begin(),doc.layers.end(),[&](const Layer& l){return l.id==*layerID;});if(at==doc.layers.end())throw std::runtime_error("Copy layer does not exist");
        if(targetMask){if(!at->mask||!at->mask->raster)return {};const auto& gray=*at->mask->raster;grayCheck(gray);auto t=at->mask->placement.value_or(at->transform);uint8_t outside=at->mask->placement?graphics::cachedMaskBackground(at->mask->raster):0;
            image=editRaster(Raster::filled(w,h,{outside,outside,outside,255}),[&](Pixel old,int px,int py){auto u=t.toUnit({rx+px+.5,ry+py+.5});if(u.x<0||u.y<0||u.x>=1||u.y>=1)return old;auto value=gray.pixel(int(u.x*gray.width),int(u.y*gray.height));return Pixel{value,value,value,255};});
        }else {if(!at->raster)return {};Document current=doc;current.layers={*at};auto& layer=current.layers.front();layer.parentId.clear();layer.maskSourceId.clear();layer.mask.reset();layer.opacity=1;layer.blend=Blend::Normal;layer.visible=true;layer.group=false;layer.adjustmentJson.clear();layer.shapeJson.clear();current.selection.reset();image=backend.render(current,rx,ry,w,h);}
    }else {if(targetMask)throw std::runtime_error("Copy merged cannot target an owned mask");if(std::none_of(doc.layers.begin(),doc.layers.end(),[](const Layer& layer){return bool(layer.raster);}))return {};image=backend.render(doc,rx,ry,w,h);}
    if(!image||image->width!=w||image->height!=h)throw std::runtime_error("Copy renderer returned an invalid region");
    if(doc.selection){const auto& mask=*doc.selection->coverage;image=editRaster(image,[&](Pixel p,int px,int py){double a=mask.pixel(rx+px,ry+py)/255.;return Pixel{byte(p.r*a),byte(p.g*a),byte(p.b*a),byte(p.a*a)};});}
    return CopiedPixels{image,{double(rx),double(ry)}};
}
}
