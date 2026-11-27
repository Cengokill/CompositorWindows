// Based on VolumetricLight.swift, MetalVolumetricLight.swift and DepthModel.swift
// at upstream 5c3489e. Copyright (c) 2026 Wonder Assembly LLC. MIT; see Compositor-MIT.txt.
//
// Windows implementation choices: the work runs on the CPU, so the very soft
// layers are blurred at a reduced resolution and sampled back, and the emission
// the beams gather is evaluated once per half-resolution pixel and then read
// bilinearly at each of the 96 samples (the Mac evaluates it at every sample).
#include "VolumetricLight.h"
#include "FastBlur.h"
#include "graphics/ParallelBatch.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace compositor::filters {
namespace {
using Plane=std::vector<float>;
void check(const std::function<bool()>& cancelled){if(cancelled&&cancelled())throw std::runtime_error("Filter cancelled");}
double bounded(double v,double low,double high,double fallback){return std::isfinite(v)?std::clamp(v,low,high):fallback;}
float clamp01(float v){return std::clamp(v,0.f,1.f);}
float smoothstep(float a,float b,float x){const float t=clamp01((x-a)/(b-a));return t*t*(3-2*t);}
float linear(double v){return float(v<=.04045?v/12.92:std::pow((v+.055)/1.055,2.4));}
float fract(float v){return v-std::floor(v);}
float hash(float n){return fract(std::sin(n*12.9898f)*43758.5453f);}
float around(float angle,float cells,float seed){
    const float u=(angle/6.2831853f+.5f)*cells,i=std::floor(u);float f=u-i;f=f*f*(3-2*f);
    const float a=hash(std::fmod(i,cells)+seed),b=hash(std::fmod(i+1,cells)+seed);return a+(b-a)*f;
}
// A plane at its own resolution, sampled at the pixel centres of a larger grid.
struct Field {
    int width{},height{};Plane values;
    float sample(double fx,double fy)const{
        const double u=std::clamp(fx-.5,0.,double(width-1)),v=std::clamp(fy-.5,0.,double(height-1));
        const int x0=int(u),y0=int(v),x1=std::min(x0+1,width-1),y1=std::min(y0+1,height-1);const float tx=float(u-x0),ty=float(v-y0);
        const float a=values[size_t(y0)*width+x0],b=values[size_t(y0)*width+x1],c=values[size_t(y1)*width+x0],d=values[size_t(y1)*width+x1];
        return (a+(b-a)*tx)+((c+(d-c)*tx)-(a+(b-a)*tx))*ty;
    }
    // At pixel (x,y) of a gridWidth x gridHeight grid.
    float at(int x,int y,int gridWidth,int gridHeight)const{
        if(width==gridWidth&&height==gridHeight)return values[size_t(y)*width+x];
        return sample((x+.5)*width/gridWidth,(y+.5)*height/gridHeight);
    }
};
// A Gaussian blur with edges clamped; wide ones are worked out on a box-reduced copy.
Field softBlur(const Plane& image,int w,int h,double sigma,const std::function<bool()>& cancelled,std::uint32_t workers){
    if(sigma<6)return {w,h,detail::boxGaussian(image,w,h,sigma,cancelled,workers)};
    const int k=std::max(2,int(sigma/3)),rw=(w+k-1)/k,rh=(h+k-1)/k;Plane reduced(size_t(rw)*rh);
    graphics::runParallelBatch(size_t(rh),workers,[&](size_t row){const int y=int(row);check(cancelled);for(int x=0;x<rw;++x){double sum=0;int count=0;
        for(int sy=y*k;sy<std::min(h,(y+1)*k);++sy)for(int sx=x*k;sx<std::min(w,(x+1)*k);++sx){sum+=image[size_t(sy)*w+sx];++count;}reduced[size_t(y)*rw+x]=float(sum/count);}});
    // The reduced copy's edge cells average fewer pixels; its sampling grid still spans the full extent.
    return {rw,rh,detail::boxGaussian(std::move(reduced),rw,rh,sigma/k,cancelled,workers)};
}
Plane expanded(const Field& field,int w,int h,const std::function<bool()>& cancelled,std::uint32_t workers){
    if(field.width==w&&field.height==h)return field.values;
    Plane out(size_t(w)*h);
    graphics::runParallelBatch(size_t(h),workers,[&](size_t row){const int y=int(row);check(cancelled);for(int x=0;x<w;++x)out[size_t(y)*w+x]=field.at(x,y,w,h);});
    return out;
}
Plane resampled(const GrayRaster& mask,int w,int h,const std::function<bool()>& cancelled,std::uint32_t workers){
    Field source{mask.width,mask.height,Plane(mask.pixels.size())};
    for(size_t i=0;i<mask.pixels.size();++i)source.values[i]=mask.pixels[i]/255.f;
    return expanded(source,w,h,cancelled,workers);
}
Plane resampled(const DepthMap& depth,int w,int h,const std::function<bool()>& cancelled,std::uint32_t workers){
    return expanded(Field{depth.width,depth.height,depth.values},w,h,cancelled,workers);
}
void validate(const GrayRaster* mask){
    if(mask&&(mask->width<1||mask->height<1||mask->width>30000||mask->height>30000||mask->pixels.size()!=size_t(mask->width)*mask->height))
        throw std::runtime_error("Invalid Volumetric Light subject mask");
}
void validate(const DepthMap* depth){
    if(!depth)return;
    if(depth->width<1||depth->height<1||depth->width>30000||depth->height>30000||depth->values.size()!=size_t(depth->width)*depth->height)throw std::runtime_error("Invalid Volumetric Light depth map");
    for(float v:depth->values)if(!std::isfinite(v))throw std::runtime_error("Invalid Volumetric Light depth map");
}
// Smoothed a touch, then made crisp, so the haze meets the subject cleanly.
Plane firmed(Plane mask,int w,int h,double side,const std::function<bool()>& cancelled,std::uint32_t workers){
    mask=detail::boxGaussian(std::move(mask),w,h,std::max(1.,side*.0015),cancelled,workers);
    for(auto& v:mask)v=clamp01(v*2.5f-1.1f);
    return mask;
}

struct Scene {
    Field near;std::array<Field,3> glows,ambient;
    // Half-size premultiplied brightness and subject, for the beams.
    int halfWidth{},halfHeight{};Plane halfBright,halfSubject;float peak{.05f};
};
Scene makeScene(const std::vector<std::uint8_t>& bytes,const Plane* subject,int w,int h,const std::function<bool()>& cancelled,std::uint32_t workers){
    Scene scene;const double side=std::max(w,h);const size_t n=size_t(w)*h;
    Plane channel(n);
    for(int c=0;c<3;++c){
        for(size_t i=0;i<n;++i)channel[i]=bytes[4*i+c]/255.f;check(cancelled);
        // The photo's own lights: brighter than half white and half again the light around them.
        const auto surround=softBlur(channel,w,h,side*.04,cancelled,workers);
        Plane lights(n);
        graphics::runParallelBatch(size_t(h),workers,[&](size_t row){const int y=int(row);check(cancelled);for(int x=0;x<w;++x){const size_t i=size_t(y)*w+x;
            lights[i]=std::max(0.f,channel[i]-std::max(.5f,surround.at(x,y,w,h)*1.5f));}});
        auto halo=softBlur(lights,w,h,side*.012,cancelled,workers);const auto bloom=softBlur(lights,w,h,side*.05,cancelled,workers);
        // The halo and twice the wider bloom, at the halo's resolution.
        for(int y=0;y<halo.height;++y)for(int x=0;x<halo.width;++x)halo.values[size_t(y)*halo.width+x]+=2*bloom.sample((x+.5)*bloom.width/halo.width,(y+.5)*bloom.height/halo.height);
        scene.glows[c]=std::move(halo);
        scene.ambient[c]=softBlur(channel,w,h,side*.12,cancelled,workers);
    }
    scene.near=subject?softBlur(*subject,w,h,std::max(1.,side*.003),cancelled,workers):Field{1,1,{0}};
    // The brightest green, lightly blurred so one hot pixel does not set it.
    for(size_t i=0;i<n;++i)channel[i]=bytes[4*i+1]/255.f;
    channel=detail::boxGaussian(std::move(channel),w,h,2,cancelled,workers);
    scene.peak=std::max(.05f,*std::max_element(channel.begin(),channel.end()));
    scene.halfWidth=(w+1)/2;scene.halfHeight=(h+1)/2;const int hw=scene.halfWidth,hh=scene.halfHeight;
    scene.halfBright.assign(size_t(hw)*hh,0);scene.halfSubject.assign(size_t(hw)*hh,0);
    graphics::runParallelBatch(size_t(hh),workers,[&](size_t row){const int y=int(row);check(cancelled);for(int x=0;x<hw;++x){float bright=0,blocked=0;int count=0;
        for(int sy=2*y;sy<std::min(h,2*y+2);++sy)for(int sx=2*x;sx<std::min(w,2*x+2);++sx){const size_t i=size_t(sy)*w+sx;bright+=(.2126f*bytes[4*i]+.7152f*bytes[4*i+1]+.0722f*bytes[4*i+2])/255.f;if(subject)blocked+=(*subject)[i];++count;}
        scene.halfBright[size_t(y)*hw+x]=bright/count;scene.halfSubject[size_t(y)*hw+x]=blocked/count;}});
    return scene;
}
struct BeamsKey {Point light;double size{},length{},shafts{};bool operator==(const BeamsKey&)const=default;};
Field makeBeams(const Scene& scene,Point light,double side,const VolumetricLightSettings& s,const std::function<bool()>& cancelled,std::uint32_t workers){
    const int hw=scene.halfWidth,hh=scene.halfHeight;const float halfSide=float(side/2);
    const float size=std::max(1.f,halfSide*float(.02+s.size/100*.4)),length=float(s.length/100),shafts=float(s.shafts/100);
    const float lx=float(light.x*.5),ly=float(light.y*.5);
    // What gives off light: the light itself, a soft disc, mostly where the photo
    // lets it through, and the photo's bright openings near it; the subject blocks both.
    Field emission{hw,hh,Plane(size_t(hw)*hh)};
    graphics::runParallelBatch(size_t(hh),workers,[&](size_t row){const int y=int(row);check(cancelled);for(int x=0;x<hw;++x){const size_t i=size_t(y)*hw+x;
        const float bright=clamp01(scene.halfBright[i]/scene.peak),blocked=scene.halfSubject[i];
        const float dx=x+.5f-lx,dy=y+.5f-ly,distance=std::hypot(dx,dy);
        const float open=clamp01((bright-.08f)/.3f),disc=std::exp(-2*(distance/size)*(distance/size));
        const float openings=clamp01((bright-.45f)/.45f)*std::exp(-distance/(halfSide*.5f));
        const float angle=std::atan2(dy,dx),wave=.6f*around(angle,97,3.1f)+.4f*around(angle,251,7.7f);
        const float broken=1-shafts+shafts*wave*wave*1.8f;
        emission.values[i]=(disc*(.25f+.75f*open)+openings)*(1-blocked)*broken;}});
    // Each pixel gathers the emission along the line back toward the light.
    Plane gathered(size_t(hw)*hh);constexpr int samples=96;
    graphics::runParallelBatch(size_t(hh),workers,[&](size_t row){const int y=int(row);check(cancelled);for(int x=0;x<hw;++x){
        const float hx=x+.5f,hy=y+.5f,sx=(lx-hx)*length/samples,sy=(ly-hy)*length/samples,jitter=hash(x*.618f+y*1.414f);
        float sum=0,weight=1;for(int i=0;i<samples;++i){sum+=emission.sample(hx+sx*(i+jitter),hy+sy*(i+jitter))*weight;weight*=.985f;}
        gathered[size_t(y)*hw+x]=sum/samples*4;}});
    return {hw,hh,detail::boxGaussian(std::move(gathered),hw,hh,.5,cancelled,workers)};
}
struct Depth {double contrast{};bool keepsSubjectClear{};Plane composed;std::vector<float> distances;};
Depth makeDepth(const VolumetricLightInputs& inputs,const VolumetricLightSettings& s,int w,int h,const std::function<bool()>& cancelled,std::uint32_t workers){
    Depth result{s.depthContrast,s.keepsSubjectClear};
    const auto spread=inputs.depth->equalized(float(s.depthContrast/100));
    result.distances.resize(spread.values.size());
    for(size_t i=0;i<spread.values.size();++i)result.distances[i]=1-spread.values[i];
    std::sort(result.distances.begin(),result.distances.end());check(cancelled);
    result.composed=composedDepth(spread,inputs.outline.get(),w,h,s.keepsSubjectClear,cancelled,workers);
    return result;
}
}

struct VolumetricLightCache::State {
    std::shared_ptr<const void> source;int width{},height{};
    std::shared_ptr<const GrayRaster> subject,outline;std::shared_ptr<const DepthMap> depth;
    std::optional<Scene> scene;std::optional<Depth> depthLevel;std::optional<std::pair<BeamsKey,Field>> beams;
};

VolumetricLightSettings VolumetricLightSettings::normalized()const{
    auto out=*this;for(auto& c:out.color)c=bounded(c,0,1,1);
    out.intensity=bounded(intensity,0,100,30);out.length=bounded(length,0,100,0);out.haze=bounded(haze,0,100,60);out.size=bounded(size,0,100,20);
    out.shafts=bounded(shafts,0,100,0);out.distance=bounded(distance,0,100,30);out.depthContrast=bounded(depthContrast,0,100,0);
    if(out.center&&!(std::isfinite(out.center->x)&&std::isfinite(out.center->y)))out.center.reset();
    return out;
}
bool VolumetricLightSettings::isIdentity()const{const auto s=normalized();return s.intensity==0&&s.haze==0;}

DepthMap DepthMap::equalized(float amount)const{
    if(!(amount>0)||values.size()<2)return *this;
    std::vector<size_t> order(values.size());std::iota(order.begin(),order.end(),size_t{0});
    std::stable_sort(order.begin(),order.end(),[&](size_t a,size_t b){return values[a]<values[b];});
    auto out=*this;const float last=float(values.size()-1);
    for(size_t rank=0;rank<order.size();++rank){const auto i=order[rank];out.values[i]+=(float(rank)/last-values[i])*amount;}
    return out;
}

Point volumetricBrightestPoint(const Raster& image){
    const int w=image.width,h=image.height;const Point center{w/2.,h/2.};
    if(w<1||h<1)return center;
    constexpr int side=64;
    const int sw=std::max(1,w>=h?side:side*w/std::max(1,h)),sh=std::max(1,h>=w?side:side*h/std::max(1,w));
    std::vector<double> gray(size_t(sw)*sh);
    for(int y=0;y<sh;++y)for(int x=0;x<sw;++x){
        const int x0=int(std::int64_t(x)*w/sw),x1=std::max(x0+1,int(std::int64_t(x+1)*w/sw)),y0=int(std::int64_t(y)*h/sh),y1=std::max(y0+1,int(std::int64_t(y+1)*h/sh));
        double sum=0;for(int sy=y0;sy<y1;++sy)for(int sx=x0;sx<x1;++sx){const auto p=image.pixel(sx,sy);sum+=.2126*p.r+.7152*p.g+.0722*p.b;}
        gray[size_t(y)*sw+x]=sum/(double(x1-x0)*(y1-y0));
    }
    // Lightly smoothed, so a speck does not count as an area.
    std::vector<double> bright(gray.size());
    for(int y=0;y<sh;++y)for(int x=0;x<sw;++x){double sum=0,count=0;for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){const int sx=x+dx,sy=y+dy;if(sx<0||sy<0||sx>=sw||sy>=sh)continue;sum+=gray[size_t(sy)*sw+sx];++count;}bright[size_t(y)*sw+x]=sum/count;}
    const double peak=*std::max_element(bright.begin(),bright.end());
    if(!(peak>0))return center;
    const double threshold=peak*.75;std::vector<int> area(bright.size(),-1);double bestScore=-1;size_t best=0;
    for(size_t start=0;start<bright.size();++start){
        if(bright[start]<threshold||area[start]>=0)continue;
        std::vector<size_t> stack{start};double score=0;size_t brightest=start;area[start]=int(start);
        while(!stack.empty()){const auto index=stack.back();stack.pop_back();score+=bright[index];if(bright[index]>bright[brightest])brightest=index;
            const int x=int(index%sw),y=int(index/sw);const int next[4][2]{{x-1,y},{x+1,y},{x,y-1},{x,y+1}};
            for(const auto& p:next){if(p[0]<0||p[1]<0||p[0]>=sw||p[1]>=sh)continue;const size_t i=size_t(p[1])*sw+p[0];if(area[i]<0&&bright[i]>=threshold){area[i]=int(start);stack.push_back(i);}}}
        if(score>bestScore){bestScore=score;best=brightest;}
    }
    return {(double(best%sw)+.5)*w/sw,(double(best/sw)+.5)*h/sh};
}

std::pair<float,float> volumetricRamp(double distance,const std::vector<float>& distances){
    const float share=float(bounded(distance,0,100,30)/100);
    if(distances.empty())return {share*.6f,share*.6f+.5f};
    auto at=[&](float s){return distances[std::min(distances.size()-1,size_t(std::max(0.f,s)*float(distances.size()-1)))];};
    const float start=std::min(at(share),.9f);
    return {start,std::max(at(std::min(1.f,share+.45f)),start+.45f)};
}

std::vector<float> composedDepth(const DepthMap& predicted,const GrayRaster* outline,int w,int h,bool keepsSubjectClear,const std::function<bool()>& cancelled,std::uint32_t workers){
    validate(&predicted);validate(outline);
    if(w<1||h<1||w>30000||h>30000)throw std::runtime_error("Invalid Volumetric Light depth size");
    const double side=std::max(w,h);
    auto depth=detail::boxGaussian(resampled(predicted,w,h,cancelled,workers),w,h,side*.0015,cancelled,workers);
    if(!outline)return depth;
    const auto subject=firmed(resampled(*outline,w,h,cancelled,workers),w,h,side*1.,cancelled,workers);
    // The smooth parts are worked out on a copy no larger than 1024 pixels; the
    // subject's edge and the model's own depth stay at full size.
    const double r=std::min(1.,1024/side);const int rw=std::max(1,int(std::lround(w*r))),rh=std::max(1,int(std::lround(h*r)));const size_t rn=size_t(rw)*rh;
    Plane small=resampled(predicted,rw,rh,cancelled,workers);small=detail::boxGaussian(std::move(small),rw,rh,side*.0015*r,cancelled,workers);
    Plane mask=firmed(resampled(*outline,rw,rh,cancelled,workers),rw,rh,side*r,cancelled,workers);
    auto blurred=[&](const Plane& p,double sigma){return detail::boxGaussian(p,rw,rh,sigma*r,cancelled,workers);};
    const double reach=side*.02;
    // The backdrop is read from beyond the band the model's nearness bleeds into,
    // then spread back in under the subject, each place an average of the backdrop near it.
    Plane beyond=blurred(mask,reach),weightedIn(rn);for(size_t i=0;i<rn;++i){beyond[i]=clamp01(beyond[i]*-6+1);weightedIn[i]=small[i]*beyond[i];}
    const auto weighted=blurred(weightedIn,reach*2.5),spread=blurred(beyond,reach*2.5);
    Field behind{rw,rh,Plane(rn)},closeness{rw,rh,blurred(mask,reach*1.5)};
    for(size_t i=0;i<rn;++i){behind.values[i]=weighted[i]/clamp01(spread[i]+.001f);closeness.values[i]=clamp01(closeness.values[i]*4);}
    Field inside{rw,rh,{}};
    if(!keepsSubjectClear){
        // At its own depth, averaged locally, so people apart from each other each keep their own.
        Plane product(rn);for(size_t i=0;i<rn;++i)product[i]=small[i]*mask[i];
        const auto sum=blurred(product,side*.08),weight=blurred(mask,side*.08);inside.values.resize(rn);
        for(size_t i=0;i<rn;++i)inside.values[i]=clamp01(sum[i]/clamp01(weight[i]+.001f));
    }
    graphics::runParallelBatch(size_t(h),workers,[&](size_t row){const int y=int(row);check(cancelled);for(int x=0;x<w;++x){const size_t i=size_t(y)*w+x;
        const float c=closeness.at(x,y,w,h),around=depth[i]*(1-c)+behind.at(x,y,w,h)*c;
        const float in=keepsSubjectClear?std::max(depth[i],.9f):inside.at(x,y,w,h);
        depth[i]=around+(in-around)*subject[i];}});
    return depth;
}

std::shared_ptr<const Raster> volumetricDepthLayer(const VolumetricLightInputs& inputs,const VolumetricLightSettings& raw,int w,int h,std::uint64_t maxBytes,const std::function<bool()>& cancelled){
    if(!inputs.depth)throw std::runtime_error("The depth model is unavailable");
    if(w<1||h<1||w>30000||h>30000||std::uint64_t(w)*h>100000000)throw std::runtime_error("Invalid Extract Depth size");
    if(std::uint64_t(w)*h>maxBytes/24)throw std::runtime_error("Extract Depth exceeds transient allocation budget");
    const auto s=raw.normalized();
    const auto values=composedDepth(inputs.depth->equalized(float(s.depthContrast/100)),inputs.outline.get(),w,h,s.keepsSubjectClear,cancelled);
    std::vector<std::uint8_t> bytes(values.size()*4);
    for(size_t i=0;i<values.size();++i){const auto v=std::uint8_t(std::lround(clamp01(values[i])*255));bytes[4*i]=bytes[4*i+1]=bytes[4*i+2]=v;bytes[4*i+3]=255;}
    check(cancelled);return Raster::fromRgba(w,h,bytes.data(),size_t(w)*4,cancelled);
}

std::shared_ptr<const Raster> renderVolumetricLight(const Raster& source,const VolumetricLightInputs& inputs,Point light,const VolumetricLightSettings& raw,
                                                    std::uint64_t maxBytes,const std::function<bool()>& cancelled,VolumetricLightCache* cache,const std::shared_ptr<const void>& identity,std::uint32_t workers){
    const int w=source.width,h=source.height;
    if(w<1||h<1||w>30000||h>30000)throw std::runtime_error("Invalid Volumetric Light image");
    const auto n=std::uint64_t(w)*h;
    if(n>100000000)throw std::runtime_error("Invalid Volumetric Light image");
    validate(inputs.subject.get());validate(inputs.outline.get());validate(inputs.depth.get());
    if(!std::isfinite(light.x)||!std::isfinite(light.y))throw std::runtime_error("Invalid Volumetric Light position");
    // Byte image, output, masks and several float planes at full size.
    if(n>maxBytes/40)throw std::runtime_error("Volumetric Light exceeds transient allocation budget");
    check(cancelled);const auto s=raw.normalized();auto bytes=source.rgba();
    if(s.isIdentity())return Raster::fromRgba(w,h,bytes.data(),size_t(w)*4);
    const double side=std::max(w,h);

    // The cache holds one source at one grid; anything else starts over.
    VolumetricLightCache::State* kept=nullptr;
    if(cache&&!identity)cache=nullptr;
    if(cache){
        if(!cache->state)cache->state=std::make_shared<VolumetricLightCache::State>();
        auto& state=*cache->state;
        if(state.width!=w||state.height!=h||state.subject!=inputs.subject||state.outline!=inputs.outline||state.depth!=inputs.depth||state.source!=identity){
            state=VolumetricLightCache::State{};state.source=identity;state.width=w;state.height=h;state.subject=inputs.subject;state.outline=inputs.outline;state.depth=inputs.depth;
        }
        kept=&state;
    }
    std::optional<Plane> subject;
    auto subjectPlane=[&]()->const Plane*{if(!inputs.subject)return nullptr;if(!subject)subject=resampled(*inputs.subject,w,h,cancelled,workers);return &*subject;};

    std::optional<Scene> localScene;const Scene* scene=nullptr;
    if(kept&&kept->scene)scene=&*kept->scene;
    else{auto made=makeScene(bytes,subjectPlane(),w,h,cancelled,workers);if(cache)++cache->sceneBuilds;
        if(kept){kept->scene=std::move(made);scene=&*kept->scene;}else{localScene=std::move(made);scene=&*localScene;}}

    std::optional<Depth> localDepth;const Depth* depth=nullptr;
    if(inputs.depth){
        if(kept&&kept->depthLevel&&kept->depthLevel->contrast==s.depthContrast&&kept->depthLevel->keepsSubjectClear==s.keepsSubjectClear)depth=&*kept->depthLevel;
        else{auto made=makeDepth(inputs,s,w,h,cancelled,workers);if(cache)++cache->depthBuilds;
            if(kept){kept->depthLevel=std::move(made);depth=&*kept->depthLevel;}else{localDepth=std::move(made);depth=&*localDepth;}}
    }

    const BeamsKey key{light,s.size,s.length,s.shafts};
    std::optional<Field> localBeams;const Field* beams=nullptr;
    if(kept&&kept->beams&&kept->beams->first==key)beams=&kept->beams->second;
    else{auto made=makeBeams(*scene,light,side,s,cancelled,workers);if(cache)++cache->beamBuilds;
        if(kept){kept->beams.emplace(key,std::move(made));beams=&kept->beams->second;}else{localBeams=std::move(made);beams=&*localBeams;}}

    // The haze veils the photo in the air's color, thicker the further away; the
    // photo's own lights glow into it; then the beams are screened over.
    const std::array<float,3> tint{linear(s.color[0]),linear(s.color[1]),linear(s.color[2])};
    const float strength=float(s.intensity/100*5),haze=float(s.haze/100);
    auto ramp=volumetricRamp(s.distance,depth?depth->distances:std::vector<float>{});ramp.second=std::max(ramp.second,ramp.first+.01f);
    graphics::runParallelBatch(size_t(h),workers,[&](size_t row){const int y=int(row);check(cancelled);for(int x=0;x<w;++x){
        const size_t i=size_t(y)*w+x;const int alpha=bytes[4*i+3];if(alpha<=0)continue;const float a=alpha/255.f;
        const float gathered=beams->at(x,y,w,h),front=clamp01(scene->near.at(x,y,w,h));
        float far;
        if(depth)far=1-clamp01(depth->composed[i]);
        else{const float down=(y+.5f)/float(h);far=(1-smoothstep(.45f,1.05f,down))*(1-.8f*front);}
        const float glow=std::exp(-std::hypot(x+.5f-float(light.x),y+.5f-float(light.y))/float(side*.35));
        const float r=smoothstep(ramp.first,ramp.second,far),fog=1-std::exp(-3*haze*(.12f+.88f*r));
        for(int c=0;c<3;++c){
            const float air=std::max(0.f,scene->ambient[c].at(x,y,w,h)),bloom=std::max(0.f,scene->glows[c].at(x,y,w,h));
            const float fogColor=air*(1+(tint[c]-1)*.6f)*.9f+tint[c]*glow*.45f,base=bytes[4*i+c]/255.f/a;
            const float veiled=base+(fogColor-base)*fog+bloom*haze*1.6f*(.3f+.7f*r);
            const float added=tint[c]*strength*gathered*.5f*(1-.92f*front);
            const float lit=1-(1-std::min(veiled,1.f))*std::exp(-added);
            bytes[4*i+c]=std::uint8_t(std::clamp(std::lround(lit*a*255),0L,long(alpha)));
        }}});
    check(cancelled);return Raster::fromRgba(w,h,bytes.data(),size_t(w)*4,cancelled);
}
}
