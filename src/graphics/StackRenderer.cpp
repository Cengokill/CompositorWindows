// Compositing order translated from LiveMaskRenderer.swift, LayerGroups.swift,
// FolderMaskClip and ImageExporter.swift at the pinned Compositor revision.
// Copyright (c) 2026 Wonder Assembly LLC; MIT notice in upstream/LICENSE.
#include "StackRenderer.h"
#include "LayerEffects.h"
#include "RasterSampling.h"
#include "BilinearRows.h"
#include "CompositeRows.h"
#include "SamplingSource.h"
#include "Downsample.h"
#include "ParallelBatch.h"
#include <algorithm>
#include <climits>
#include <cstddef>
#include <cmath>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <type_traits>

namespace compositor::graphics {
// The row samplers read Pixel as its four packed bytes.
static_assert(sizeof(Pixel)==4&&std::is_standard_layout_v<Pixel>&&offsetof(Pixel,r)==0&&offsetof(Pixel,a)==3);
namespace {
// clamp(lround(v),0,255) without the library call made for every channel of every sample.
// Truncation then the exact fraction rounds halves away from zero like lround.
uint8_t byte(double v){if(v>=255.5)return 255;if(!(v>=.5))return 0;const int whole=int(v);return uint8_t(whole+(v-whole>=.5));}
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
Pixel composite(Pixel a,Pixel b,Blend mode){
    if(mode==Blend::Normal&&a.a==255){
        if(!b.a)return a;
        if(b.a==255)return b;
        const unsigned remaining=255-b.a;
        // For an opaque destination, source-over is the source channel
        // plus the destination channel scaled by the remaining coverage.
        const auto channel=[&](unsigned dst,unsigned src){
            return uint8_t(std::min(255u,src+(dst*remaining+127)/255));
        };
        return {channel(a.r,b.r),channel(a.g,b.g),channel(a.b,b.b),255};
    }
    if(mode==Blend::Normal){
        // blendPixel's Normal case, expression for expression (its mixed colour
        // is the source colour), without the per-pixel dispatch and lround call.
        const double da=a.a/255.,sa=b.a/255.;
        if(sa==0)return a;
        if(da==0)return b;
        Pixel out;out.a=byte((sa+da-sa*da)*255);
        const auto channel=[&](uint8_t dst,uint8_t src){const double bc=dst/(255.*da),sc=src/(255.*sa);return std::min(out.a,byte(255*(sa*(1-da)*sc+da*(1-sa)*bc+sa*da*sc)));};
        out.r=channel(a.r,b.r);out.g=channel(a.g,b.g);out.b=channel(a.b,b.b);
        return out;
    }
    return blendPixel(a,b,mode);
}
WidePixel composite(WidePixel a,WidePixel b,Blend mode){return blendWidePixel(a,b,mode);}
// composite()'s opaque-destination Normal case for four packed pixels. A source
// alpha of 255 leaves remaining 0, so the formula returns the source as well.
// (v*0x8081)>>23 is v/255 for every v up to 255*255+127.
bool opaque4(__m128i pixels){const auto alphas=_mm_set1_epi32(int(0xFF000000));return _mm_movemask_epi8(_mm_cmpeq_epi32(_mm_and_si128(pixels,alphas),alphas))==0xFFFF;}
__m128i overOpaque4(__m128i destination,__m128i source){
    const auto zero=_mm_setzero_si128(),alphas=_mm_set1_epi32(int(0xFF000000));
    const auto half=[&](__m128i dst,__m128i src){
        const auto alpha=_mm_shufflehi_epi16(_mm_shufflelo_epi16(src,_MM_SHUFFLE(3,3,3,3)),_MM_SHUFFLE(3,3,3,3));
        const auto scaled=_mm_add_epi16(_mm_mullo_epi16(dst,_mm_sub_epi16(_mm_set1_epi16(255),alpha)),_mm_set1_epi16(127));
        const auto quotient=_mm_srli_epi16(_mm_mulhi_epu16(scaled,_mm_set1_epi16(-32639)),7); // 0x8081
        return _mm_min_epi16(_mm_add_epi16(src,quotient),_mm_set1_epi16(255));
    };
    const auto blended=_mm_or_si128(_mm_packus_epi16(half(_mm_unpacklo_epi8(destination,zero),_mm_unpacklo_epi8(source,zero)),
        half(_mm_unpackhi_epi8(destination,zero),_mm_unpackhi_epi8(source,zero))),alphas);
    // A transparent source returns the destination unchanged.
    const auto transparent=_mm_cmpeq_epi32(_mm_and_si128(source,alphas),zero);
    return _mm_or_si128(_mm_and_si128(transparent,destination),_mm_andnot_si128(transparent,blended));
}
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
    bool parallel{};
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
        const auto coverage=mask(index,p);
        return coverage==1?value:scale(value,coverage);
    }
    P own(int index,Point p,double factor=1)const{
        const auto& l=layers[size_t(index)].layer;if(!l.raster)return{};
        const auto sample=shaped(index,p);
        const auto opacity=l.opacity*factor;
        return opacity==1?sample:scale(sample,opacity);
    }
    double dependency(int source,Point p)const{
        // Source visibility and containing-folder masks are intentionally absent:
        // upstream renders drawOwn into a separate transparent coverage context.
        if(source<0)return 1;
        const auto& info=layers[size_t(source)];
        return alpha(own(source,p,dependency(info.source,p)))/(std::is_same_v<P,Pixel>?255.:1.);
    }
    template<class DrawRows> void runRows(const DrawRows& drawRows)const{
        if(parallel&&pixelCount>=32768){
            // Poll on the calling thread. The cancellation callback belongs
            // to the caller and need not be safe to invoke from row workers.
            if(cancelled&&cancelled())throw std::runtime_error("Stack render cancelled");
            constexpr int rowsPerBand=32;
            runParallelRenderBatch(size_t((region.height+rowsPerBand-1)/rowsPerBand),[&](size_t band){
                const int first=int(band)*rowsPerBand;
                drawRows(first,std::min(first+rowsPerBand,region.height),false);
            });
            if(cancelled&&cancelled())throw std::runtime_error("Stack render cancelled");
        }else drawRows(0,region.height,true);
    }
    void drawOwn(int index,std::vector<P>& pixels,bool externalClip)const{
        const auto& info=layers[size_t(index)];
        runRows([&](int first,int last,bool pollRows){
        ReducedTileReadScope tiles;
        for(int row=first;row<last;++row){
        if(pollRows&&cancelled&&cancelled())throw std::runtime_error("Stack render cancelled");
        const size_t begin=size_t(row)*region.width;
        for(size_t i=begin;i<begin+region.width;++i){auto p=position(i);double factor=externalClip?folders(index,p)*dependency(info.source,p):1;
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
        });
    }
    bool reaches(int index,double left,double top,double right,double bottom)const{
        // Unit extent that sampling accepts: the image, or a reduction grid that can extend past it.
        const auto& info=layers[size_t(index)];
        double u0=0,v0=0,u1=1,v1=1;
        if(info.reducedImage){
            const auto& grid=info.reducedImage->grid();const auto& source=*info.reducedImage->source();
            u0=std::min(0.,double(grid.x)/source.width);v0=std::min(0.,double(grid.y)/source.height);
            u1=std::max(1.,(double(grid.x)+double(grid.width)*grid.step)/source.width);v1=std::max(1.,(double(grid.y)+double(grid.height)*grid.step)/source.height);
        }
        double minX=INFINITY,minY=INFINITY,maxX=-INFINITY,maxY=-INFINITY;
        for(const Point corner:{Point{u0,v0},Point{u1,v0},Point{u0,v1},Point{u1,v1}}){
            const auto p=info.layer.transform.fromUnit(corner);
            minX=std::min(minX,p.x);minY=std::min(minY,p.y);maxX=std::max(maxX,p.x);maxY=std::max(maxY,p.y);
        }
        if(!std::isfinite(minX)||!std::isfinite(minY)||!std::isfinite(maxX)||!std::isfinite(maxY))return true;
        // A pixel and a document unit of slack cover the rounding between this and Inverse.
        const double slack=region.unitsPerPixel+1;
        return maxX+slack>left&&minX-slack<right&&maxY+slack>top&&minY-slack<bottom;
    }
    // Pixels clamp(left+i,0,width-1) of image row y: the original raster, or the reduced grid.
    static void readClampedRow(const Layer& layer,const ReducedSource* reduced,int width,int y,int left,int count,Pixel* output){
        const auto read=[&](int x,int run,Pixel* into){
            if(reduced){reduced->readPixels(x,y,run,into);return;}
            const auto& raster=*layer.raster;const size_t columns=size_t(raster.width+255)/256;
            while(run>0){const int chunk=std::min(run,256-(x&255));std::copy_n(raster.tiles[size_t(y>>8)*columns+size_t(x>>8)]->pixels.data()+size_t(y&255)*256+size_t(x&255),chunk,into);x+=chunk;into+=chunk;run-=chunk;}
        };
        const int before=std::clamp(-left,0,count),inside=std::clamp(std::min(left+count,width)-std::max(left,0),0,count-before);
        if(inside>0)read(std::max(left,0),inside,output+before);
        if(before){Pixel edge;read(0,1,&edge);std::fill_n(output,before,edge);}
        if(before+inside<count){Pixel edge;read(width-1,1,&edge);std::fill_n(output+before+inside,count-before-inside,edge);}
    }
public:
    struct NormalPlan {
        struct Geometry {const Transform* transform{};const ReducedSource* reduced{};int width{},height{};double sine{};};
        std::vector<int> stack;std::vector<uint8_t> repeatsAbove;std::vector<double> opacities;
        std::vector<std::array<uint8_t,256>> scaled;std::vector<Blend> blends;std::vector<uint8_t> normal;
        std::vector<Geometry> geometries;std::vector<int> geometryOf;
        struct Fixed {bool ok{};uint32_t multiplier{},offset{};};std::vector<Fixed> fixedOpacity;
    };
    // The per-region part of the normal stack: nullopt when a layer needs drawOwn or adjust.
    std::optional<NormalPlan> normalPlan()const{
        if constexpr(!std::is_same_v<P,Pixel>)return std::nullopt;
        else{
        if(!parallel||order.size()<=1||!std::all_of(order.begin(),order.end(),[&](int index){
            const auto& info=layers[size_t(index)];
            const auto& layer=info.layer;
            // A layer mask is part of shaped(). A folder mask varies per pixel, so drawOwn keeps it.
            return info.source<0&&layer.raster&&!layer.deep&&layer.adjustmentJson.empty()&&!layer.effects.active()&&
                std::none_of(info.ancestors.begin(),info.ancestors.end(),[&](int a){const auto& folder=layers[size_t(a)].layer;return folder.mask&&folder.mask->enabled&&folder.mask->raster;});
        }))return std::nullopt;
        NormalPlan plan;
        // A layer that shows the same raster object through an equal transform and
        // mask as the layer above it samples the same value at every point. The
        // caller guarantees no adjustment, effect, clipping, folder mask, preview or
        // deep data, so a folder only multiplies its children's opacity.
        // A layer reads nothing (transparent, a no-op in every blend) outside the
        // sampled extent of its image, so layers that miss this region are skipped.
        auto& stack=plan.stack;stack.reserve(order.size());
        const double units=region.unitsPerPixel,left=region.x+offsetX*units,top=region.y+offsetY*units;
        const double right=left+region.width*units,bottom=top+region.height*units;
        for(int index:order)if(reaches(index,left,top,right,bottom))stack.push_back(index);
        if(stack.empty())return plan;
        auto& repeatsAbove=plan.repeatsAbove;repeatsAbove.assign(stack.size(),0);
        for(size_t k=0;k+1<stack.size();++k){
            const auto& below=layers[size_t(stack[k])].layer;const auto& above=layers[size_t(stack[k+1])].layer;
            repeatsAbove[k]=below.raster==above.raster&&below.transform==above.transform&&below.mask==above.mask;
        }
        // drawOwn's opacity: the layer's own times folders(), whose unmasked
        // ancestors each contribute opacity*1, in the same order.
        auto& opacities=plan.opacities;opacities.resize(stack.size());
        auto& scaled=plan.scaled;scaled.resize(stack.size());
        for(size_t k=0;k<stack.size();++k){
            const auto& info=layers[size_t(stack[k])];
            double folder=1;for(int a:info.ancestors)folder*=layers[size_t(a)].layer.opacity*1.;
            opacities[k]=info.layer.opacity*(folder*1.);
            if(opacities[k]!=1)for(int value=0;value<256;++value)scaled[k][size_t(value)]=byte(value*opacities[k]);
            plan.fixedOpacity.emplace_back();
            if(opacities[k]!=1)plan.fixedOpacity.back().ok=opacityFixedPoint(opacities[k],plan.fixedOpacity.back().multiplier,plan.fixedOpacity.back().offset);
        }
        auto& blends=plan.blends;blends.resize(stack.size());
        for(size_t k=0;k<stack.size();++k)blends[k]=layers[size_t(stack[k])].layer.blend;
        for(const auto blend:blends)plan.normal.push_back(blend==Blend::Normal);
        // Layers without a layer mask sampled bilinearly through an equal transform
        // and image grid share their taps, so each geometry is mapped once per row.
        auto& geometries=plan.geometries;auto& geometryOf=plan.geometryOf;geometryOf.assign(stack.size(),-1);
        for(size_t k=0;k<stack.size();++k){
            const auto& info=layers[size_t(stack[k])];const auto& l=info.layer;
            if(l.transform.sampling==Transform::Sampling::Nearest||(l.mask&&l.mask->enabled&&l.mask->raster))continue;
            typename NormalPlan::Geometry geometry{&l.transform,info.reducedImage.get(),info.reducedImage?info.reducedImage->grid().width:l.raster->width,info.reducedImage?info.reducedImage->grid().height:l.raster->height,info.imageInverse.sine};
            const auto same=[&](const typename NormalPlan::Geometry& g){
                if(*g.transform!=*geometry.transform||g.width!=geometry.width||g.height!=geometry.height||bool(g.reduced)!=bool(geometry.reduced))return false;
                if(!g.reduced)return true;
                const auto &a=g.reduced->grid(),&b=geometry.reduced->grid();
                return a.x==b.x&&a.y==b.y&&a.step==b.step&&g.reduced->source()->width==geometry.reduced->source()->width&&g.reduced->source()->height==geometry.reduced->source()->height;
            };
            const auto found=std::find_if(geometries.begin(),geometries.end(),same);
            geometryOf[k]=int(found-geometries.begin());
            if(found==geometries.end())geometries.push_back(geometry);
        }
        return plan;
        }
    }
    // Rows first..last-1 of the normal stack described by plan.
    void drawNormalRows(const NormalPlan& plan,std::vector<P>& pixels,int first,int last,bool pollRows)const{
        const auto& stack=plan.stack;const auto& repeatsAbove=plan.repeatsAbove;const auto& opacities=plan.opacities;
        const auto& scaled=plan.scaled;const auto& blends=plan.blends;const auto& geometries=plan.geometries;const auto& geometryOf=plan.geometryOf;
        if(stack.empty())return;
        const auto withOpacity=[&](Pixel p,size_t k){const auto& table=scaled[k];return Pixel{table[p.r],table[p.g],table[p.b],table[p.a]};};
        {
            ReducedTileReadScope tiles;
            // One row at a time, one layer at a time from the top: each layer's
            // samples for the row are read together instead of every layer per pixel.
            const size_t width=size_t(region.width),count=stack.size();
            // An unrotated layer (sine exactly 0) maps x from the column alone and y from
            // the row alone, so its column taps hold for the whole band.
            struct RowTaps {bool mapped{},uniform{},upright{},columnsMapped{};int y{},left{},right{};double fy{};std::vector<int> x;std::vector<double> fx,fxComplement;std::vector<float> fxWide,fxComplementWide;std::vector<uint8_t> inside;};
            // Each layer keeps its last two source rows: the next output row usually
            // reads one of them again, and always does when zoomed in.
            struct Line {int y{INT_MIN},left{},span{};bool widened{};std::vector<uint32_t> packed;std::vector<float> wide;};
            // Buffers stay with the thread between bands: a frame has hundreds of bands,
            // and allocating and clearing them each time costs more than some bands do.
            struct Scratch {std::vector<P> sources,raw;std::vector<int> stop;std::vector<double> centreX;std::vector<Point> points;std::vector<RowTaps> taps;std::vector<uint8_t> outside;std::vector<std::array<Line,2>> lines;};
            static thread_local Scratch scratch;
            const auto atLeast=[](auto& buffer,size_t size){if(buffer.size()<size)buffer.resize(size);};
            atLeast(scratch.sources,count*width);atLeast(scratch.raw,width);atLeast(scratch.stop,width);atLeast(scratch.centreX,width);atLeast(scratch.points,width);
            atLeast(scratch.taps,geometries.size());atLeast(scratch.outside,width);atLeast(scratch.lines,count);
            auto& sources=scratch.sources;auto& raw=scratch.raw;auto& stop=scratch.stop;auto& centreX=scratch.centreX;auto& points=scratch.points;auto& taps=scratch.taps;auto& lines=scratch.lines;
            const auto& outside=scratch.outside;
            // position()'s pixel centres: x depends only on the column and y only on the row.
            for(size_t column=0;column<width;++column)centreX[column]=region.x+(double(column+offsetX)+0.5)*region.unitsPerPixel;
            for(size_t g=0;g<geometries.size();++g){auto& entry=taps[g];entry.mapped=entry.columnsMapped=false;entry.upright=geometries[g].sine==0;}
            for(size_t k=0;k<count;++k)for(auto& slot:lines[k])slot.y=INT_MIN;
            const bool wide=bilinearRowAvxAvailable();
            const auto widenColumns=[&](RowTaps& entry){
                entry.fxWide.resize(width*4);entry.fxComplementWide.resize(width*4);
                widenWeights(entry.fx.data(),width,entry.fxWide.data());widenWeights(entry.fxComplement.data(),width,entry.fxComplementWide.data());
            };
            const auto line=[&](size_t k,const typename NormalPlan::Geometry& geometry,int y,int left,int span,const Line* keep)->const Line&{
                for(auto& slot:lines[k])if(slot.y==y&&slot.left==left&&slot.span==span)return slot;
                auto& slot=&lines[k][0]==keep?lines[k][1]:lines[k][0];
                slot.y=y;slot.left=left;slot.span=span;slot.packed.resize(size_t(span));
                readClampedRow(layers[size_t(stack[k])].layer,geometry.reduced,geometry.width,y,left,span,reinterpret_cast<Pixel*>(slot.packed.data()));
                // Zoomed out, a sample reads few of a row's pixels and the next output row
                // rarely reuses them, so the AVX sampler converts the four it reads instead.
                slot.widened=wide&&size_t(span)*4<=width*5;
                if(slot.widened){slot.wide.resize(size_t(span)*4);widenPixels(slot.packed.data(),size_t(span),slot.wide.data());}
                return slot;
            };
            for(int row=first;row<last;++row){
                if(pollRows&&cancelled&&cancelled())throw std::runtime_error("Stack render cancelled");
                const size_t begin=size_t(row)*width;
                const double centreY=region.y+(double(size_t(row)+offsetY)+0.5)*region.unitsPerPixel;
                bool pointsReady=false;
                const auto rowPoints=[&]{if(!pointsReady){for(size_t column=0;column<width;++column)points[column]={centreX[column],centreY};pointsReady=true;}};
                std::fill_n(stop.begin(),width,-1);
                for(size_t g=0;g<geometries.size();++g)taps[g].mapped=false;
                // Once every pixel of the row is opaque, no lower layer is read.
                size_t active=width;
                for(size_t k=count;k-->0&&active;){
                    const int index=stack[k];
                    const double opacity=opacities[k];
                    P* source=sources.data()+k*width;
                    const bool repeat=k+1<count&&repeatsAbove[k];
                    const P* above=repeat?source+width:nullptr;
                    const bool sameOpacity=repeat&&opacity==opacities[k+1];
                    const RowTaps* rowTaps=nullptr;
                    if constexpr(std::is_same_v<P,Pixel>)if(!repeat&&geometryOf[k]>=0){
                        auto& entry=taps[size_t(geometryOf[k])];const auto& geometry=geometries[size_t(geometryOf[k])];
                        // shaped()'s coordinates: imageInverse, then the reduced grid's mapping.
                        const auto& inverse=layers[size_t(index)].imageInverse;
                        const auto mapped=[&](Point centre){const auto unit=inverse(centre);return geometry.reduced?geometry.reduced->mapped(unit):unit;};
                        if(entry.upright&&!entry.mapped){
                            if(!entry.columnsMapped){
                                entry.columnsMapped=true;entry.left=INT_MAX;entry.right=INT_MIN;
                                entry.x.resize(width);entry.fx.resize(width);entry.fxComplement.resize(width);entry.inside.assign(width,0);
                                for(size_t column=0;column<width;++column){
                                    int tap;double fraction;
                                    if(!bilinearTap(geometry.width,mapped({centreX[column],centreY}).x,tap,fraction))continue;
                                    entry.inside[column]=1;entry.x[column]=tap;entry.fx[column]=fraction;entry.fxComplement[column]=1-fraction;
                                    entry.left=std::min(entry.left,tap);entry.right=std::max(entry.right,tap+1);
                                }
                                if(wide)widenColumns(entry);
                            }
                            entry.mapped=entry.uniform=true;
                            // Outside the image vertically, the whole row reads transparent.
                            if(!bilinearTap(geometry.height,mapped({centreX[0],centreY}).y,entry.y,entry.fy))entry.y=INT_MIN;
                        }else if(!entry.mapped){
                            entry.mapped=true;entry.uniform=true;entry.left=INT_MAX;entry.right=INT_MIN;
                            entry.x.resize(width);entry.fx.resize(width);entry.fxComplement.resize(width);entry.inside.assign(width,0);
                            bool firstTap=true;
                            for(size_t column=0;column<width;++column){
                                BilinearTaps tap;
                                if(!bilinearTaps(geometry.width,geometry.height,mapped({centreX[column],centreY}),tap))continue;
                                if(firstTap){entry.y=tap.y;entry.fy=tap.fy;firstTap=false;}
                                else if(tap.y!=entry.y||tap.fy!=entry.fy)entry.uniform=false;
                                entry.inside[column]=1;entry.x[column]=tap.x;entry.fx[column]=tap.fx;entry.fxComplement[column]=1-tap.fx;
                                entry.left=std::min(entry.left,tap.x);entry.right=std::max(entry.right,tap.x+1);
                            }
                            if(wide)widenColumns(entry);
                        }
                        // A rotated layer reads different source rows across the row and keeps shaped().
                        if(entry.uniform)rowTaps=&entry;
                    }
                    if(rowTaps){
                        // Both source rows once, with sampleRasterPixels' edge clamping.
                        const auto& geometry=geometries[size_t(geometryOf[k])];
                        BilinearRow bilinear{};
                        if(rowTaps->upright&&rowTaps->y==INT_MIN)bilinear.inside=outside.data();
                        else if(rowTaps->left<=rowTaps->right){
                            const int span=rowTaps->right-rowTaps->left+1;
                            const auto& upper=line(k,geometry,std::clamp(rowTaps->y,0,geometry.height-1),rowTaps->left,span,nullptr);
                            const auto& lower=line(k,geometry,std::clamp(rowTaps->y+1,0,geometry.height-1),rowTaps->left,span,&upper);
                            bilinear={upper.packed.data(),lower.packed.data(),upper.widened?upper.wide.data():nullptr,upper.widened?lower.wide.data():nullptr,rowTaps->left,rowTaps->x.data(),rowTaps->fx.data(),rowTaps->fxComplement.data(),rowTaps->fxWide.data(),rowTaps->fxComplementWide.data(),rowTaps->inside.data(),rowTaps->fy,plan.fixedOpacity[k].ok,plan.fixedOpacity[k].multiplier,plan.fixedOpacity[k].offset};
                        }else bilinear.inside=rowTaps->inside.data();
                        // raw is only read by a lower layer that repeats this one.
                        const bool rawNeeded=k>0&&repeatsAbove[k-1];
                        active-=sampleBilinearRow(bilinear,stop.data(),blends[k]==Blend::Normal?int(k):-1,width,opacity,rawNeeded?reinterpret_cast<uint32_t*>(raw.data()):nullptr,reinterpret_cast<uint32_t*>(source));
                        continue;
                    }
                    rowPoints();
                    for(size_t column=0;column<width;++column){
                        if(stop[column]>=0)continue;
                        P value;
                        if(repeat)value=sameOpacity?above[column]:(opacity==1?raw[column]:withOpacity(raw[column],k));
                        else{raw[column]=shaped(index,points[column]);value=opacity==1?raw[column]:withOpacity(raw[column],k);}
                        source[column]=value;
                        // An opaque Normal layer replaces everything beneath it, so lower
                        // layers are never sampled there. Other blends are replayed.
                        if(blends[k]==Blend::Normal&&alpha(value)==255){stop[column]=int(k);--active;}
                    }
                }
                // Source-over rounds at each layer: replay the samples above the
                // opaque one bottom to top. Transparent samples are no-ops in every blend.
                const auto replay=[&](size_t column,size_t from,P pixel){
                    for(size_t k=from;k<count;++k){
                        const P& value=sources[k*width+column];
                        if(blends[k]!=Blend::Normal||alpha(value)!=0)pixel=composite(pixel,value,blends[k]);
                    }
                    pixels[begin+column]=pixel;
                };
                size_t column=0;
                if constexpr(std::is_same_v<P,Pixel>){
                    // Sixteen pixels that start at the same layer advance together, four
                    // at a time while each four is opaque and the layer Normal, which
                    // most layers are. Four independent chains keep the processor busy.
                    constexpr size_t blocks=4;
                    for(;column+4*blocks<=width;column+=4*blocks){
                        const int opaque=stop[column];
                        if(!std::all_of(stop.begin()+ptrdiff_t(column),stop.begin()+ptrdiff_t(column+4*blocks),[&](int value){return value==opaque;})){
                            for(size_t j=column;j<column+4*blocks;++j)replay(j,size_t(stop[j]+1),stop[j]>=0?sources[size_t(stop[j])*width+j]:P{});
                            continue;
                        }
                        if(compositeRowsAvx2Available()){
                            // Eight pixels a register: the same arithmetic in AVX2 lanes. Other
                            // layers come back here and use composite() for all sixteen.
                            P* out=pixels.data()+begin+column;
                            for(size_t j=0;j<4*blocks;++j)out[j]=opaque>=0?sources[size_t(opaque)*width+column+j]:P{};
                            struct Context {const P* sources;size_t width,column;const Blend* blends;} context{sources.data(),width,column,blends.data()};
                            compositeSixteenAvx2(reinterpret_cast<const uint32_t*>(sources.data())+column,width,plan.normal.data(),size_t(opaque+1),count,reinterpret_cast<uint32_t*>(out),
                                [](void* data,size_t k,uint32_t* sixteen){
                                    const auto& c=*static_cast<const Context*>(data);auto* pixel=reinterpret_cast<Pixel*>(sixteen);
                                    for(size_t j=0;j<16;++j){const Pixel& v=c.sources[k*c.width+c.column+j];if(c.blends[k]!=Blend::Normal||v.a!=0)pixel[j]=composite(pixel[j],v,c.blends[k]);}
                                },&context);
                            continue;
                        }
                        std::array<__m128i,blocks> state;
                        for(size_t b=0;b<blocks;++b)state[b]=opaque>=0?_mm_loadu_si128(reinterpret_cast<const __m128i*>(sources.data()+size_t(opaque)*width+column+4*b)):_mm_setzero_si128();
                        for(size_t k=size_t(opaque+1);k<count;++k){
                            const P* layerRow=sources.data()+k*width+column;
                            for(size_t b=0;b<blocks;++b){
                                const auto value=_mm_loadu_si128(reinterpret_cast<const __m128i*>(layerRow+4*b));
                                if(blends[k]==Blend::Normal&&opaque4(state[b])){state[b]=overOpaque4(state[b],value);continue;}
                                alignas(16) std::array<Pixel,4> four;_mm_store_si128(reinterpret_cast<__m128i*>(four.data()),state[b]);
                                for(size_t j=0;j<4;++j){const P& v=layerRow[4*b+j];if(blends[k]!=Blend::Normal||alpha(v)!=0)four[j]=composite(four[j],v,blends[k]);}
                                state[b]=_mm_load_si128(reinterpret_cast<const __m128i*>(four.data()));
                            }
                        }
                        for(size_t b=0;b<blocks;++b)_mm_storeu_si128(reinterpret_cast<__m128i*>(pixels.data()+begin+column+4*b),state[b]);
                    }
                }
                for(;column<width;++column)replay(column,size_t(stop[column]+1),stop[column]>=0?sources[size_t(stop[column])*width+column]:P{});
            }
        }
    }
private:
    void adjust(int index,std::vector<P>& pixels,bool folderClip)const{
        const auto& info=layers[size_t(index)];const auto& l=info.layer;
        if(l.opacity==0||(folderClip&&std::any_of(info.ancestors.begin(),info.ancestors.end(),[&](int parent){return layers[size_t(parent)].layer.opacity==0;})))return;
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
    Render(const Document& input,RenderRegion r,const AdjustmentCallback& cb,std::shared_ptr<const LayerRenderPreview> overridePreview,int ox=0,int oy=0,const DeepAdjustmentCallback& deepCb={},std::function<bool()> cancel={},bool parallelRows=false):document(input),region(r),callback(cb),deepCallback(deepCb),cancelled(std::move(cancel)),preview(std::move(overridePreview)),pixelCount(size_t(r.width)*r.height),offsetX(ox),offsetY(oy),parallel(parallelRows&&!preview){
        if constexpr(std::is_same_v<P,WidePixel>)if(uint64_t(pixelCount)*80>1200000000)throw std::runtime_error("HDR render exceeds working memory budget");
        if(preview){auto found=std::find_if(document.layers.begin(),document.layers.end(),[&](const Layer& layer){return layer.id==preview->layer.id;});if(found==document.layers.end()||!preview->identity)throw std::invalid_argument("Invalid layer render preview");*found=preview->layer;}
        const auto& d=document;
        validateDocument(document);layers.reserve(d.layers.size());
        for(size_t i=0;i<d.layers.size();++i){
            if(cancelled&&cancelled())throw std::runtime_error("Stack render cancelled");
            ids[d.layers[i].id]=int(i);layers.emplace_back(d.layers[i]);
        }
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
        if constexpr(std::is_same_v<P,Pixel>){
            if(const auto plan=normalPlan()){
                runRows([&](int first,int last,bool pollRows){drawNormalRows(*plan,result,first,last,pollRows);});
                return rasterFromPixels(result,region.width,region.height);
            }
        }
        for(int index:order){if(cancelled&&cancelled())throw std::runtime_error("Stack render cancelled");if(stacked[size_t(index)])continue;const auto& info=layers[size_t(index)];
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
namespace {
void validateScaled(double x,double y,int width,int height,double unitsPerPixel){
    if(width<1||height<1||width>30000||height>30000||uint64_t(width)*height>100000000||
       !std::isfinite(x)||!std::isfinite(y)||std::abs(x)>10000000||std::abs(y)>10000000||
       !std::isfinite(unitsPerPixel)||unitsPerPixel<1./32||unitsPerPixel>32768)
        throw std::invalid_argument("Invalid scaled render region");
}
}
std::shared_ptr<const Raster> StackRenderer::renderScaled(const Document& d,double x,double y,int width,int height,double unitsPerPixel,const std::function<bool()>& cancelled)const{
    validateScaled(x,y,width,height,unitsPerPixel);
    if(hdrDocument(d))return rasterFromDeep(*Render<WidePixel>(d,{x,y,width,height,unitsPerPixel},adjustment_,preview_,0,0,deepAdjustment_,cancelled,true).run());
    return Render<Pixel>(d,{x,y,width,height,unitsPerPixel},adjustment_,preview_,0,0,{},cancelled,true).run();
}
std::vector<std::shared_ptr<const Raster>> StackRenderer::renderScaledTiles(const Document& d,const std::vector<ScaledTile>& tiles,double unitsPerPixel,const std::function<bool()>& cancelled)const{
    for(const auto& tile:tiles){
        validateScaled(tile.x,tile.y,tile.width,tile.height,unitsPerPixel);
        if(tile.offsetX<0||tile.offsetY<0||tile.offsetX>30000-tile.width||tile.offsetY>30000-tile.height)throw std::invalid_argument("Invalid scaled tile offset");
    }
    std::vector<std::shared_ptr<const Raster>> result(tiles.size());
    const auto poll=[&]{if(cancelled&&cancelled())throw std::runtime_error("Stack render cancelled");};
    // Jobs run in groups so that the caller's cancellation is polled between them.
    const auto runGroups=[&](size_t count,const std::function<void(size_t)>& job){
        constexpr size_t group=128;
        for(size_t first=0;first<count;first+=group){poll();runParallelRenderBatch(std::min(group,count-first),[&](size_t index){job(first+index);});}
        poll();
    };
    if(hdrDocument(d)||preview_){
        runGroups(tiles.size(),[&](size_t i){
            const auto& tile=tiles[i];
            result[i]=tile.offsetX||tile.offsetY?renderScaledPatch(d,tile.x,tile.y,tile.offsetX,tile.offsetY,tile.width,tile.height,unitsPerPixel):renderScaled(d,tile.x,tile.y,tile.width,tile.height,unitsPerPixel);
        });
        return result;
    }
    std::vector<std::optional<Render<Pixel>>> renders(tiles.size());
    std::vector<std::optional<Render<Pixel>::NormalPlan>> plans(tiles.size());
    runGroups(tiles.size(),[&](size_t i){
        renders[i].emplace(d,RenderRegion{tiles[i].x,tiles[i].y,tiles[i].width,tiles[i].height,unitsPerPixel},adjustment_,preview_,tiles[i].offsetX,tiles[i].offsetY,deepAdjustment_,std::function<bool()>{},true);
        plans[i]=renders[i]->normalPlan();
    });
    // Bands of every tile together, so a slow tile or processor does not hold the frame.
    struct Job {size_t tile;int first,last;};
    std::vector<Job> jobs;
    std::vector<std::vector<Pixel>> pixels(tiles.size());
    constexpr int rowsPerBand=16;
    for(size_t i=0;i<tiles.size();++i){
        if(!plans[i]){jobs.push_back({i,-1,-1});continue;}
        pixels[i].resize(size_t(tiles[i].width)*tiles[i].height);
        for(int first=0;first<tiles[i].height;first+=rowsPerBand)jobs.push_back({i,first,std::min(first+rowsPerBand,tiles[i].height)});
    }
    runGroups(jobs.size(),[&](size_t j){
        const auto& job=jobs[j];
        if(job.first<0)result[job.tile]=renders[job.tile]->run();
        else renders[job.tile]->drawNormalRows(*plans[job.tile],pixels[job.tile],job.first,job.last,false);
    });
    for(size_t i=0;i<tiles.size();++i)if(plans[i])result[i]=rasterFromPixels(pixels[i],tiles[i].width,tiles[i].height);
    return result;
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
