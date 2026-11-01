// Compositing order translated from LiveMaskRenderer.swift, LayerGroups.swift,
// FolderMaskClip and ImageExporter.swift at the pinned Compositor revision.
// Copyright (c) 2026 Wonder Assembly LLC; MIT notice in upstream/LICENSE.
#include "StackRenderer.h"
#include "LayerEffects.h"
#include "RasterSampling.h"
#include "SamplingSource.h"
#include "Downsample.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <unordered_map>
#include <type_traits>

namespace compositor::graphics {
namespace {
uint8_t byte(double v){return uint8_t(std::clamp(std::lround(v),0L,255L));}
Pixel scale(Pixel p,double v){return{byte(p.r*v),byte(p.g*v),byte(p.b*v),byte(p.a*v)};}
Pixel interpolate(Pixel a,Pixel b,double t){return{byte(a.r+(b.r-a.r)*t),byte(a.g+(b.g-a.g)*t),byte(a.b+(b.b-a.b)*t),byte(a.a+(b.a-a.a)*t)};}
Pixel opaque(Pixel p){unsigned a=p.a;auto c=[&](unsigned v){return uint8_t(a?std::min(255u,(v*255+a/2)/a):0);};return{c(p.r),c(p.g),c(p.b),255};}
Pixel restore(Pixel p,uint8_t alpha){auto c=[&](unsigned v){return uint8_t((v*alpha+127)/255);};return{c(p.r),c(p.g),c(p.b),alpha};}
WidePixel scale(WidePixel p,double amount){for(auto& v:p)v=float(v*amount);return p;}
WidePixel interpolate(WidePixel a,WidePixel b,double t){for(int c=0;c<4;++c)a[c]=float(a[c]+(b[c]-a[c])*t);return a;}
WidePixel opaque(WidePixel p){if(p[3]>0)for(int c=0;c<3;++c)p[c]/=p[3];p[3]=1;return p;}
WidePixel restore(WidePixel p,float alpha){for(int c=0;c<3;++c)p[c]*=alpha;p[3]=alpha;return p;}
float alpha(WidePixel p){return p[3];}
uint8_t alpha(Pixel p){return p.a;}
WidePixel wide(Pixel p){return {p.r/255.f,p.g/255.f,p.b/255.f,p.a/255.f};}
Pixel shown(WidePixel p){return {byte(std::clamp(p[0],0.f,p[3])*255),byte(std::clamp(p[1],0.f,p[3])*255),byte(std::clamp(p[2],0.f,p[3])*255),byte(p[3]*255)};}
std::shared_ptr<const DeepImage> imageFromPixels(const std::vector<WidePixel>& pixels,int w,int h){auto out=makeDeep(w,h);for(int y=0;y<h;++y)for(int x=0;x<w;++x)out->setPixel(x,y,pixels[size_t(y)*w+x]);return out;}
WidePixel sampleWide(const Layer& layer,Point unit){
    const auto& image=*layer.deep;return sampleWidePixels(image.width,image.height,[&](int x,int y){return image.pixel(x,y);},unit,layer.transform.sampling);
}
bool hdrDocument(const Document& d){return std::any_of(d.layers.begin(),d.layers.end(),[](const Layer& l){return l.deep&&l.deep->isHdr();});}
Pixel composite(Pixel a,Pixel b,Blend mode){return blendPixel(a,b,mode);}
WidePixel composite(WidePixel a,WidePixel b,Blend mode){return blendWidePixel(a,b,mode);}
struct Inverse {
    double cosine,sine,x,y,width,height,flipX,flipY;
    explicit Inverse(const Transform& t):x(t.x),y(t.y),width(t.width),height(t.height),flipX(t.flipX?-1.:1.),flipY(t.flipY?-1.:1.){
        const double angle=std::remainder(t.rotation,360.)*std::numbers::pi/180;cosine=std::cos(angle);sine=std::sin(angle);
    }
    Point operator()(Point p)const{
        // Subtract the local origin before transforming. Finite differences of
        // far-away unit coordinates lose precision and change half-byte rounds.
        const double px=p.x-x-width/2,py=p.y-y-height/2;
        return {.5+(px*cosine+py*sine)/width*flipX,.5+(-px*sine+py*cosine)/height*flipY};
    }
};
std::shared_ptr<const Raster> rasterFromPixels(const std::vector<Pixel>& pixels,int w,int h){
    auto out=std::make_shared<Raster>();out->width=w;out->height=h;int columns=(w+255)/256,rows=(h+255)/256;
    out->tiles.reserve(size_t(columns)*rows);
    for(int ty=0;ty<rows;++ty)for(int tx=0;tx<columns;++tx){auto tile=std::make_shared<Raster::Tile>();
        int tw=std::min(256,w-tx*256),th=std::min(256,h-ty*256);
        for(int py=0;py<th;++py)std::copy_n(pixels.data()+size_t(ty*256+py)*w+tx*256,tw,tile->pixels.data()+size_t(py)*256);
        out->tiles.push_back(std::move(tile));}
    return out;
}
struct LayerInfo {
    const Layer& layer;
    Inverse imageInverse,maskInverse;
    Transform::Sampling maskSampling;
    uint8_t maskExterior{};
    std::shared_ptr<const ReducedSource> reducedImage,reducedMask,folderMask;
    int parent{-1},source{-1};
    std::vector<int> ancestors;
    explicit LayerInfo(const Layer& l):layer(l),imageInverse(l.transform),maskInverse(l.mask&&l.mask->placement?*l.mask->placement:l.transform),
        maskSampling(l.mask&&l.mask->placement?l.mask->placement->sampling:l.transform.sampling),
        maskExterior(l.mask&&l.mask->enabled&&l.mask->placement&&l.mask->raster?(l.mask->previewExterior?*l.mask->previewExterior:cachedMaskBackground(l.mask->raster)):0){}
};
template<class P> class Render {
    Document document;RenderRegion region;const AdjustmentCallback& callback;DeepAdjustmentCallback deepCallback;std::function<bool()> cancelled;std::shared_ptr<const LayerRenderPreview> preview;
    std::vector<LayerInfo> layers;std::unordered_map<std::string,int> ids;
    std::vector<std::vector<int>> children,stacks;std::vector<int> order;std::vector<bool> stacked;
    size_t pixelCount;
    int offsetX{},offsetY{};
    Point position(size_t i)const{return{region.x+(double(i%size_t(region.width)+offsetX)+0.5)*region.unitsPerPixel,region.y+(double(i/size_t(region.width)+offsetY)+0.5)*region.unitsPerPixel};}
    double mask(int index,Point p,bool layerPlacement=false)const{
        const auto& info=layers[size_t(index)];const auto& l=info.layer;
        if(!l.mask||!l.mask->enabled||!l.mask->raster)return 1;
        const auto& reduced=layerPlacement?info.folderMask:info.reducedMask;
        if(reduced)return reduced->sampleGray(layerPlacement?info.imageInverse(p):info.maskInverse(p),layerPlacement?l.transform.sampling:info.maskSampling,layerPlacement?0:info.maskExterior);
        if(preview&&preview->layer.id==l.id&&preview->mask)return preview->mask(layerPlacement?info.imageInverse(p):info.maskInverse(p),layerPlacement?l.transform.sampling:info.maskSampling,layerPlacement?0:info.maskExterior);
        return sampleMask(*l.mask->raster,layerPlacement?info.imageInverse(p):info.maskInverse(p),layerPlacement?l.transform.sampling:info.maskSampling,layerPlacement?0:info.maskExterior);
    }
    double folders(int index,Point p)const{double result=1;for(int a:layers[size_t(index)].ancestors)result*=layers[size_t(a)].layer.opacity*mask(a,p,true);return result;}
    P shaped(int index,Point p)const{
        const auto& info=layers[size_t(index)];const auto& l=info.layer;if(!l.raster)return{};
        P value{};
        if constexpr(std::is_same_v<P,WidePixel>){
            // Procedural gesture previews override their base image; otherwise
            // sample the retained deep pixels before any display clipping.
            if(preview&&preview->layer.id==l.id&&preview->deepImage)value=preview->deepImage(info.imageInverse(p),l.transform.sampling);
            else if(l.deep&&!(preview&&preview->layer.id==l.id&&preview->image))value=sampleWide(l,info.imageInverse(p));
            else value=wide(info.reducedImage?info.reducedImage->sample(info.imageInverse(p),l.transform.sampling):preview&&preview->layer.id==l.id&&preview->image?preview->image(info.imageInverse(p),l.transform.sampling):sampleRaster(*l.raster,info.imageInverse(p),l.transform.sampling));
        }else value=info.reducedImage?info.reducedImage->sample(info.imageInverse(p),l.transform.sampling):preview&&preview->layer.id==l.id&&preview->image?preview->image(info.imageInverse(p),l.transform.sampling):sampleRaster(*l.raster,info.imageInverse(p),l.transform.sampling);
        return scale(value,mask(index,p));
    }
    P own(int index,Point p,double factor=1)const{
        const auto& l=layers[size_t(index)].layer;if(!l.raster)return{};
        return scale(shaped(index,p),l.opacity*factor);
    }
    double dependency(int source,Point p)const{
        // Source visibility and containing-folder masks are intentionally absent:
        // upstream renders drawOwn into a separate transparent coverage context.
        if(source<0)return 1;
        const auto& info=layers[size_t(source)];
        return alpha(own(source,p,dependency(info.source,p)))/(std::is_same_v<P,Pixel>?255.:1.);
    }
    void drawOwn(int index,std::vector<P>& pixels,bool externalClip)const{
        const auto& info=layers[size_t(index)];
        for(size_t i=0;i<pixelCount;++i){if(i%size_t(region.width)==0&&cancelled&&cancelled())throw std::runtime_error("HDR render cancelled");auto p=position(i);double factor=externalClip?folders(index,p)*dependency(info.source,p):1;
            P content=own(index,p,factor);
            if(info.layer.effects.active()){
                if constexpr(std::is_same_v<P,Pixel>){
                    auto shapedAt=[this,index](Point q){return shaped(index,q);};auto fx=shadeLayer(info.layer,p,content,info.layer.opacity*factor,shapedAt);if(fx.hasUnder)pixels[i]=composite(pixels[i],fx.under,fx.underBlend);if(fx.hasGlow)pixels[i]=composite(pixels[i],fx.glow,fx.glowBlend);content=fx.content;
                }else{
                    const float peak=content[3]>0?std::max({1.f,content[0]/content[3],content[1]/content[3],content[2]/content[3]}):1;
                    auto normalized=content;for(int c=0;c<3;++c)normalized[c]/=peak;
                    auto shapedAt=[this,index](Point q){return shown(shaped(index,q));};auto fx=shadeLayer(info.layer,p,shown(normalized),info.layer.opacity*factor,shapedAt);
                    if(fx.hasUnder)pixels[i]=composite(pixels[i],wide(fx.under),fx.underBlend);if(fx.hasGlow)pixels[i]=composite(pixels[i],wide(fx.glow),fx.glowBlend);
                    if(fx.content!=shown(normalized)){content=wide(fx.content);for(int c=0;c<3;++c)content[c]*=peak;}
                }
            }
            pixels[i]=composite(pixels[i],content,info.layer.blend);}
    }
    void adjust(int index,std::vector<P>& pixels,bool folderClip)const{
        const auto& info=layers[size_t(index)];const auto& l=info.layer;
        std::vector<P> changed(pixelCount);
        if constexpr(std::is_same_v<P,Pixel>){
            if(!callback)throw std::runtime_error("Adjustment callback required for layer "+l.id);
            auto before=rasterFromPixels(pixels,region.width,region.height);auto adjusted=callback(l,before,region);
            if(!adjusted||adjusted->width!=region.width||adjusted->height!=region.height||adjusted->tiles.size()!=before->tiles.size())throw std::runtime_error("Adjustment callback returned invalid raster extent");
            for(auto& tile:adjusted->tiles)if(!tile)throw std::runtime_error("Adjustment callback returned missing tile");
            for(size_t i=0;i<pixelCount;++i){auto value=adjusted->pixel(int(i%size_t(region.width)),int(i/size_t(region.width)));if(value.r>value.a||value.g>value.a||value.b>value.a)throw std::runtime_error("Adjustment callback returned invalid premultiplication");changed[i]=value;}
        }else{
            if(!deepCallback)throw std::runtime_error("HDR adjustment callback required for layer "+l.id);
            auto adjusted=deepCallback(l,imageFromPixels(pixels,region.width,region.height),region);
            if(!adjusted||!adjusted->valid()||adjusted->width!=region.width||adjusted->height!=region.height)throw std::runtime_error("HDR adjustment changed region size");
            for(size_t i=0;i<pixelCount;++i)changed[i]=adjusted->pixel(int(i%size_t(region.width)),int(i/size_t(region.width)));
        }
        for(size_t i=0;i<pixelCount;++i){auto original=pixels[i],value=changed[i];
            if(l.blend!=Blend::Normal)value=restore(composite(opaque(original),opaque(value),l.blend),alpha(original));
            value=interpolate(original,value,l.opacity);
            auto p=position(i);pixels[i]=interpolate(original,value,mask(index,p,true)*(folderClip?folders(index,p):1));}
    }
public:
    Render(const Document& input,RenderRegion r,const AdjustmentCallback& cb,std::shared_ptr<const LayerRenderPreview> overridePreview,int ox=0,int oy=0,const DeepAdjustmentCallback& deepCb={},std::function<bool()> cancel={}):document(input),region(r),callback(cb),deepCallback(deepCb),cancelled(std::move(cancel)),preview(std::move(overridePreview)),pixelCount(size_t(r.width)*r.height),offsetX(ox),offsetY(oy){
        if constexpr(std::is_same_v<P,WidePixel>)if(uint64_t(pixelCount)*80>1200000000)throw std::runtime_error("HDR render exceeds working memory budget");
        if(preview){auto found=std::find_if(document.layers.begin(),document.layers.end(),[&](const Layer& layer){return layer.id==preview->layer.id;});if(found==document.layers.end()||!preview->identity)throw std::invalid_argument("Invalid layer render preview");*found=preview->layer;}
        const auto& d=document;
        validateDocument(document);layers.reserve(d.layers.size());
        for(size_t i=0;i<d.layers.size();++i){ids[d.layers[i].id]=int(i);layers.emplace_back(d.layers[i]);
            const auto& image=d.layers[i].raster;if(image){size_t expected=size_t((image->width+255)/256)*size_t((image->height+255)/256);
                if(image->tiles.size()!=expected)throw std::runtime_error("Invalid raster tile array");for(auto&t:image->tiles)if(!t)throw std::runtime_error("Missing raster tile");}}
        for(auto& info:layers){const auto& layer=info.layer;const bool overridden=preview&&preview->layer.id==layer.id;
            const auto reduce=[&](std::shared_ptr<const SamplingSource> source,const Transform& transform){std::shared_ptr<const ReducedSource> result;if(source&&transform.sampling!=Transform::Sampling::Nearest){const int level=DownsampleCache::levelFor(transform.width/(region.unitsPerPixel*source->width));if(level)result=ReducedSourceCache::shared().resolve(std::move(source),level);}return result;};
            if(layer.raster&&!(overridden&&preview->image&&!preview->imageSource))info.reducedImage=reduce(overridden&&preview->imageSource?preview->imageSource:samplingSource(layer.raster),layer.transform);
            if(layer.mask&&layer.mask->enabled&&layer.mask->raster&&!(overridden&&preview->mask&&!preview->maskSource)){auto source=overridden&&preview->maskSource?preview->maskSource:samplingSource(layer.mask->raster);info.reducedMask=reduce(source,layer.mask->placement.value_or(layer.transform));info.folderMask=reduce(std::move(source),layer.transform);}
        }
        children.resize(layers.size()+1);stacks.resize(layers.size());stacked.resize(layers.size());
        for(size_t i=0;i<layers.size();++i){auto& info=layers[i];const auto& l=info.layer;
            if(!l.parentId.empty())info.parent=ids.at(l.parentId);if(!l.maskSourceId.empty())info.source=ids.at(l.maskSourceId);
            children[size_t(info.parent+1)].push_back(int(i));}
        for(size_t i=0;i<layers.size();++i){auto parent=layers[i].parent;while(parent>=0){layers[i].ancestors.push_back(parent);parent=layers[size_t(parent)].parent;}}
        auto visit=[&](auto&& self,int parent)->void{for(int index:children[size_t(parent+1)]){const auto& l=layers[size_t(index)].layer;if(!l.visible)continue;
                if(l.group)self(self,index);else order.push_back(index);}};visit(visit,-1);
        for(size_t i=0;i<order.size();++i){int base=order[i];const auto& b=layers[size_t(base)];if(b.source>=0||!b.layer.adjustmentJson.empty())continue;
            for(size_t j=i+1;j<order.size();++j){int child=order[j];const auto& c=layers[size_t(child)];if(c.source!=base||c.parent!=b.parent)break;
                stacks[size_t(base)].push_back(child);stacked[size_t(child)]=true;}}
    }
    auto run()const{
        std::vector<P> result(pixelCount);
        for(int index:order){if(cancelled&&cancelled())throw std::runtime_error("HDR render cancelled");if(stacked[size_t(index)])continue;const auto& info=layers[size_t(index)];
            if(!info.layer.adjustmentJson.empty()){if(info.source<0)adjust(index,result,true);continue;}
            const auto& stack=stacks[size_t(index)];if(stack.empty()){drawOwn(index,result,true);continue;}
            std::vector<P> group(pixelCount);std::vector<decltype(graphics::alpha(P{}))> alphas(pixelCount);drawOwn(index,group,false);
            for(size_t i=0;i<pixelCount;++i){alphas[i]=alpha(group[i]);group[i]=opaque(group[i]);}
            for(int child:stack){if(!layers[size_t(child)].layer.adjustmentJson.empty())adjust(child,group,false);else drawOwn(child,group,false);}
            for(size_t i=0;i<pixelCount;++i){auto p=scale(restore(group[i],alphas[i]),folders(index,position(i)));result[i]=composite(result[i],p,info.layer.blend);}
        }
        if constexpr(std::is_same_v<P,Pixel>)return rasterFromPixels(result,region.width,region.height);
        else return imageFromPixels(result,region.width,region.height);
    }
};
}
std::shared_ptr<const DeepImage> StackRenderer::renderDeep(const Document& d,RenderRegion r,const std::function<bool()>& cancelled)const{
    if(r.width<1||r.height<1||r.width>30000||r.height>30000||uint64_t(r.width)*r.height>100000000||!std::isfinite(r.x)||!std::isfinite(r.y)||std::abs(r.x)>10000000||std::abs(r.y)>10000000||!std::isfinite(r.unitsPerPixel)||r.unitsPerPixel<1./32||r.unitsPerPixel>32768)throw std::invalid_argument("Invalid HDR render region");
    if(uint64_t(r.width)*r.height*80>1200000000)throw std::runtime_error("HDR render exceeds working memory budget");
    return Render<WidePixel>(d,r,adjustment_,preview_,0,0,deepAdjustment_,cancelled).run();
}
std::shared_ptr<const Raster> StackRenderer::render(const Document& d,int x,int y,int width,int height)const{
    if(width<1||height<1||width>30000||height>30000||uint64_t(width)*height>100000000 ||
       std::abs(int64_t(x))>10000000||std::abs(int64_t(y))>10000000)throw std::invalid_argument("Invalid render region");
    if(hdrDocument(d))return rasterFromDeep(*Render<WidePixel>(d,{double(x),double(y),width,height,1},adjustment_,preview_,0,0,deepAdjustment_).run());
    return Render<Pixel>(d,{double(x),double(y),width,height,1},adjustment_,preview_).run();
}
std::shared_ptr<const Raster> StackRenderer::renderScaled(const Document& d,double x,double y,int width,int height,double unitsPerPixel)const{
    if(width<1||height<1||width>30000||height>30000||uint64_t(width)*height>100000000||
       !std::isfinite(x)||!std::isfinite(y)||std::abs(x)>10000000||std::abs(y)>10000000||
       !std::isfinite(unitsPerPixel)||unitsPerPixel<1./32||unitsPerPixel>32768)
        throw std::invalid_argument("Invalid scaled render region");
    if(hdrDocument(d))return rasterFromDeep(*Render<WidePixel>(d,{x,y,width,height,unitsPerPixel},adjustment_,preview_,0,0,deepAdjustment_).run());
    return Render<Pixel>(d,{x,y,width,height,unitsPerPixel},adjustment_,preview_).run();
}
std::shared_ptr<const Raster> StackRenderer::renderScaledPatch(const Document& d,double tileX,double tileY,int offsetX,int offsetY,int width,int height,double unitsPerPixel)const{
    if(offsetX<0||offsetY<0||width<1||height<1||offsetX>255||offsetY>255||width>256-offsetX||height>256-offsetY||!std::isfinite(tileX)||!std::isfinite(tileY)||std::abs(tileX)>10000000||std::abs(tileY)>10000000||!std::isfinite(unitsPerPixel)||unitsPerPixel<1./32||unitsPerPixel>32768||std::any_of(d.layers.begin(),d.layers.end(),[](const Layer& layer){return !layer.adjustmentJson.empty();}))throw std::invalid_argument("Invalid bounded non-adjustment render patch");
    if(hdrDocument(d))return rasterFromDeep(*Render<WidePixel>(d,{tileX,tileY,width,height,unitsPerPixel},adjustment_,preview_,offsetX,offsetY,deepAdjustment_).run());
    return Render<Pixel>(d,{tileX,tileY,width,height,unitsPerPixel},adjustment_,preview_,offsetX,offsetY).run();
}
std::shared_ptr<const Raster> StackRenderer::renderCursorRegion(const Document& d,double x,double y,int width,int height,double unitsPerPixel)const{
    if(width<1||height<1||width>1024||height>1024||
       !std::isfinite(x)||!std::isfinite(y)||std::abs(x)>10000000||std::abs(y)>10000000||
       !std::isfinite(unitsPerPixel)||unitsPerPixel<=0||unitsPerPixel>32768||
       !std::isfinite(x+width*unitsPerPixel)||!std::isfinite(y+height*unitsPerPixel))
        throw std::invalid_argument("Invalid bounded cursor region");
    if(hdrDocument(d))return rasterFromDeep(*Render<WidePixel>(d,{x,y,width,height,unitsPerPixel},adjustment_,preview_,0,0,deepAdjustment_).run());
    return Render<Pixel>(d,{x,y,width,height,unitsPerPixel},adjustment_,preview_).run();
}

} // namespace compositor::graphics
