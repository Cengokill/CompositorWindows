// Translation of MetalBrushCoverage.swift at a19db9011282399785dc18efcfded904627bdcc2.
// Pen dynamics follow upstream bf75dd85 (Pen pressure).
// Original copyright (c) 2026 Wonder Assembly LLC; MIT, see upstream/LICENSE.
#include "BrushCoverage.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <functional>

namespace compositor::graphics {
namespace {
struct Vec { float x,y; Vec operator-(Vec b)const{return{x-b.x,y-b.y};} Vec operator+(Vec b)const{return{x+b.x,y+b.y};} Vec operator*(float s)const{return{x*s,y*s};} };
float dot(Vec a,Vec b){return a.x*b.x+a.y*b.y;}
float coverage(float squared,float radius,const BrushUniforms& u) {
    const float distance=std::sqrt(squared),hardness=u.geometry[3];
    if(hardness>=1) return std::clamp((radius-distance)/u.canvas[2]+0.5f,0.0f,1.0f);
    const float t=std::clamp((distance/radius-hardness)/(1-hardness),0.0f,1.0f);
    return std::max(0.0f,(std::exp(-2.5f*t*t)-std::exp(-2.5f))/(1-std::exp(-2.5f)));
}
float tipDensity(float squared,float radius,const BrushUniforms& u) {
    return -std::log(std::max(1-coverage(squared,radius,u),0.001f));
}
float interpolate(float a,float b,float t) { return a+(b-a)*t; }
float distanceSquared(Vec p,const BrushSegment& s,float& t) {
    const Vec a{s.x0,s.y0},v{s.x1-s.x0,s.y1-s.y0};
    t=std::clamp(dot(p-a,v)/std::max(dot(v,v),1e-12f),0.0f,1.0f);
    const auto delta=p-(a+v*t); return dot(delta,delta);
}
struct TipSweep {
    float length, projection, perpendicularSquared, radius0, radiusDelta;

    float radius(float t) const { return radius0+radiusDelta*t; }
    float squaredDistance(float t) const {
        const float along=length*t-projection;
        return along*along+perpendicularSquared;
    }
    bool reaches(float t) const {
        const float r=radius(t);
        return squaredDistance(t)<r*r;
    }
    float closestEdge() const {
        // Maximize radius(t) minus distance(t). When the radius changes at
        // least as fast as the center moves, the widest disc contains all the others.
        const float change=std::abs(radiusDelta);
        if(change>=length)return radiusDelta>0 ? 1.f : 0.f;
        const float tangent=std::sqrt((length-change)*(length+change));
        return std::clamp((projection+std::sqrt(perpendicularSquared)*radiusDelta/tangent)/length,0.f,1.f);
    }
    float reachLimit(float inside,float endpoint) const {
        if(reaches(endpoint))return endpoint;
        float outside=endpoint;
        for(int i=0;i<20;++i){
            const float mid=(inside+outside)*0.5f;
            if(reaches(mid))inside=mid;
            else outside=mid;
        }
        return inside;
    }
};
TipSweep tipSweep(Vec p,const BrushSegment& s,float radius) {
    const Vec offset{p.x-s.x0,p.y-s.y0},v{s.x1-s.x0,s.y1-s.y0};
    const float length=std::sqrt(dot(v,v));
    const Vec direction=length>0 ? v*(1/length) : Vec{1,0};
    const float perpendicular=offset.y*direction.x-offset.x*direction.y;
    return {length,dot(offset,direction),perpendicular*perpendicular,radius*s.size0,radius*(s.size1-s.size0)};
}
float hardCoverage(Vec p,const BrushSegment& s,const BrushUniforms& u) {
    const auto sweep=tipSweep(p,s,u.geometry[2]);
    const auto opacity=[&](float t){return interpolate(s.strength0,s.strength1,t);};
    const auto tip=[&](float t){return coverage(sweep.squaredDistance(t),sweep.radius(t),u);};
    const float closest=sweep.closestEdge(),strongest=s.strength1>s.strength0 ? 1.f : 0.f;
    const float maximumTip=tip(closest);
    float best=maximumTip*opacity(closest);
    if(maximumTip==0 || closest==strongest || s.strength0==s.strength1)return best;

    // Between the fullest tip coverage and the stronger endpoint, coverage
    // decreases and opacity increases. Their product is unimodal where positive.
    float lo=0,hi=1;
    best=std::max(best,tip(strongest)*opacity(strongest));
    for(int i=0;i<24;++i){
        const float left=lo+(hi-lo)/3,right=hi-(hi-lo)/3;
        const float a=interpolate(closest,strongest,left),b=interpolate(closest,strongest,right);
        const float scoreA=tip(a)*opacity(a),scoreB=tip(b)*opacity(b);
        best=std::max({best,scoreA,scoreB});
        if(scoreA<scoreB)lo=left;
        else hi=right;
    }
    return best;
}
float segmentStrength(Vec p,const BrushSegment& s,float radius) {
    const auto sweep=tipSweep(p,s,radius);
    const float strongest=s.strength1>s.strength0 ? 1.f : 0.f;
    if(sweep.reaches(strongest))return std::max(s.strength0,s.strength1);

    // A linearly changing disc covers a single interval along the segment.
    // Find the edge of that interval on the side with greater pressure.
    const float inside=sweep.closestEdge();
    if(!sweep.reaches(inside))return 0;
    if(s.strength0==s.strength1)return s.strength0;
    return interpolate(s.strength0,s.strength1,sweep.reachLimit(inside,strongest));
}
float segmentDensity(Vec p,const BrushSegment& s,const BrushUniforms& u) {
    const Vec a{s.x0,s.y0},v{s.x1-s.x0,s.y1-s.y0};
    if(s.strength0==0 && s.strength1==0)return 0;
    const float radius=u.geometry[2];
    float length=std::sqrt(dot(v,v)); if(length<1e-6f)return tipDensity(dot(p-a,p-a),radius*s.size0,u);
    const auto direction=v*(1/length); float projection=dot(p-a,direction);
    const auto perpendicular=p-a-direction*projection; float squared=dot(perpendicular,perpendicular);
    const float widest=radius*std::max(s.size0,s.size1);
    float radiusSquared=widest*widest; if(squared>=radiusSquared)return 0;
    float reach=std::sqrt(radiusSquared-squared),lo=std::max(0.0f,projection-reach),hi=std::min(length,projection+reach);
    if(hi<=lo)return 0;
    const bool variableSize=s.size0!=s.size1;
    const float radiusDelta=radius*(s.size1-s.size0);
    int panels=1;
    if(variableSize){
        // Integrate over the actual taper so narrow regions of coverage
        // are sampled even when the segment's widest tip is much larger.
        const TipSweep sweep{length,projection,squared,radius*s.size0,radiusDelta};
        const float inside=sweep.closestEdge();
        if(!sweep.reaches(inside))return 0;
        lo=length*sweep.reachLimit(inside,0.f);
        hi=length*sweep.reachLimit(inside,1.f);
        // Steep tapers need an extra interval for the changing density profile.
        panels=std::abs(radiusDelta)>=length ? 3 : 2;
    }
    if(hi<=lo)return 0;
    const float halfLength=(hi-lo)/(2.f*static_cast<float>(panels));
    constexpr float nodes[]={0.1834346425f,0.5255324099f,0.7966664774f,0.9602898565f};
    constexpr float weights[]={0.3626837834f,0.3137066459f,0.2223810345f,0.1012285363f};
    float integral=0;
    for(int panel=0;panel<panels;++panel){
        const float midpoint=variableSize?lo+(2.f*static_cast<float>(panel)+1.f)*halfLength:(lo+hi)*0.5f;
        for(int i=0;i<4;++i){
            const float aa=midpoint-halfLength*nodes[i]-projection,bb=midpoint+halfLength*nodes[i]-projection;
            const float sizeA=variableSize?interpolate(s.size0,s.size1,std::clamp((projection+aa)/length,0.f,1.f)):s.size0;
            const float sizeB=variableSize?interpolate(s.size0,s.size1,std::clamp((projection+bb)/length,0.f,1.f)):s.size0;
            float densityA=tipDensity(squared+aa*aa,radius*sizeA,u);
            float densityB=tipDensity(squared+bb*bb,radius*sizeB,u);
            if(variableSize){
                densityA/=std::max(0.25f,u.canvas[3]*sizeA);
                densityB/=std::max(0.25f,u.canvas[3]*sizeB);
            }
            integral+=weights[i]*(densityA+densityB);
        }
    }
    const float spacing=variableSize?1.f:std::max(0.25f,u.canvas[3]*s.size0);
    return integral*halfLength/spacing;
}
}
BrushTile::BrushTile(uint32_t w,uint32_t h,bool capped):width(w),height(h) {
    if(!w || !h || w>brushTileSize || h>brushTileSize)throw std::invalid_argument("Brush tiles must be 1..256 pixels");
    permanent.resize(size_t(w)*h); preview.resize(size_t(w)*h);
    if(capped)pressureCap.resize(size_t(w)*h);
}
void validateBrush(const BrushTile& t,const BrushUniforms& u,std::span<const BrushSegment> settled,std::span<const BrushSegment> tail) {
    if(!t.width || !t.height || t.width>brushTileSize || t.height>brushTileSize ||
       t.permanent.size()!=size_t(t.width)*t.height || t.preview.size()!=size_t(t.width)*t.height ||
       (!t.pressureCap.empty() && (t.pressureCap.size()!=t.permanent.size() || u.geometry[3]>=1)))
        throw std::invalid_argument("Invalid brush tile storage");
    for(const auto& v:{u.mapping,u.geometry,u.canvas})for(float x:v)
        if(!std::isfinite(x) || std::abs(x)>1e7f)throw std::invalid_argument("Nonfinite or excessive brush coordinate");
    if(u.geometry[2]<=0 || u.geometry[3]<0 || u.geometry[3]>1 || u.canvas[0]<=0 || u.canvas[1]<=0 || u.canvas[2]<=0 || u.canvas[3]<0.25f)
        throw std::invalid_argument("Invalid brush settings");
    if(settled.size()>65536 || tail.size()>65536 || settled.size()+tail.size()>65536)
        throw std::length_error("Split brush submissions into at most 65536 segments");
    for(auto segments:{settled,tail})for(const auto& s:segments) {
        for(float v:{s.x0,s.y0,s.x1,s.y1})
            if(!std::isfinite(v) || std::abs(v)>1e7f)throw std::invalid_argument("Invalid brush segment");
        if(!(s.size0>0 && s.size0<=1 && s.size1>0 && s.size1<=1 &&
             s.strength0>=0 && s.strength0<=1 && s.strength1>=0 && s.strength1<=1))
            throw std::invalid_argument("Invalid brush segment dynamics");
    }
    for(float value:t.permanent)if(!std::isfinite(value) || value<0 || value>(u.geometry[3]>=1 ? 1.0f : 20.0f))
        throw std::invalid_argument("Invalid permanent brush state");
    for(float value:t.pressureCap)if(!(value>=0 && value<=1))
        throw std::invalid_argument("Invalid permanent brush pressure cap");
}
namespace {
void renderBrushCpuRows(BrushTile& tile,BrushUniforms u,std::span<const BrushSegment> settled,std::span<const BrushSegment> tail,uint32_t firstRow,uint32_t lastRow) {
    const bool fixedTip=std::ranges::none_of(settled,&BrushSegment::hasDynamics)&&
                        std::ranges::none_of(tail,&BrushSegment::hasDynamics);
    for(uint32_t y=firstRow;y<lastRow;++y)for(uint32_t x=0;x<tile.width;++x){
        const size_t i=size_t(y)*tile.width+x; float lx=float(x)+0.5f,ly=float(y)+0.5f;
        Vec p{u.geometry[0]+lx*u.mapping[0]+ly*u.mapping[2],u.geometry[1]+lx*u.mapping[1]+ly*u.mapping[3]};
        if(p.x<0 || p.y<0 || p.x>=u.canvas[0] || p.y>=u.canvas[1]){tile.preview[i]=0;continue;}
        float value=tile.permanent[i],preview=0;
        if(u.geometry[3]>=1 && fixedTip) {
            float a=std::numeric_limits<float>::infinity(),b=a,t;
            for(const auto& s:settled)a=std::min(a,distanceSquared(p,s,t));
            for(const auto& s:tail)b=std::min(b,distanceSquared(p,s,t));
            value=std::max(value,coverage(a,u.geometry[2],u));
            tile.permanent[i]=value;
            preview=std::max(value,coverage(b,u.geometry[2],u));
        }else if(u.geometry[3]>=1) {
            for(const auto& s:settled)value=std::max(value,hardCoverage(p,s,u));
            tile.permanent[i]=value;
            for(const auto& s:tail)value=std::max(value,hardCoverage(p,s,u));
            preview=value;
        }else{
            float transient=0;
            for(const auto& s:settled)value+=segmentDensity(p,s,u);
            for(const auto& s:tail)transient+=segmentDensity(p,s,u);
            tile.permanent[i]=std::min(value,20.0f);
            float strength=1;
            if(!tile.pressureCap.empty()) {
                // Accumulated soft coverage is scaled by the firmest press to reach this pixel.
                strength=tile.pressureCap[i];
                for(const auto& s:settled)strength=std::max(strength,segmentStrength(p,s,u.geometry[2]));
                tile.pressureCap[i]=strength;
                for(const auto& s:tail)strength=std::max(strength,segmentStrength(p,s,u.geometry[2]));
            }
            preview=strength*(1-std::exp(-std::min(value+transient,20.0f)));
        }
        tile.preview[i]=static_cast<uint8_t>(std::lround(255*preview));
    }
}
}
void renderBrushCpu(BrushTile& tile,BrushUniforms u,std::span<const BrushSegment> settled,std::span<const BrushSegment> tail) {
    validateBrush(tile,u,settled,tail);
    renderBrushCpuRows(tile,u,settled,tail,0,tile.height);
}
#ifdef COMPOSITOR_BRUSH_BATCH_TESTING
// Deterministic exception/concurrency injection in the standalone test target.
// This symbol and callback are absent from the production compilation.
void brushCpuBatchTestHook(size_t jobIndex);
#endif
void renderBrushCpuBatch(std::span<const BrushTileRender> requests,std::span<const BrushSegment> settled,
                         std::span<const BrushSegment> tail,uint32_t workerLimit){
    if(workerLimit>brushCpuMaxWorkers)throw std::invalid_argument("Brush CPU worker limit must be0..16");
    if(requests.empty())return;
    if(requests.size()>4096)throw std::length_error("Brush batch exceeds bounded tile count");
    std::vector<const BrushTile*> unique;unique.reserve(requests.size());
    for(const auto& request:requests){if(!request.tile)throw std::invalid_argument("Missing brush batch tile");unique.push_back(request.tile);}
    std::sort(unique.begin(),unique.end(),std::less<const BrushTile*>{});
    if(std::adjacent_find(unique.begin(),unique.end())!=unique.end())throw std::invalid_argument("Duplicate brush batch tile");
    for(const auto& request:requests)validateBrush(*request.tile,request.uniforms,settled,tail);
    struct Rows {const BrushTileRender* request;uint32_t first,last;};
    std::vector<Rows> rows;rows.reserve(requests.size()*8);
    for(const auto& request:requests)for(uint32_t first=0;first<request.tile->height;first+=32)
        rows.push_back({&request,first,std::min(first+32,request.tile->height)});
    runParallelBatch(rows.size(),workerLimit,[&](size_t index){
#ifdef COMPOSITOR_BRUSH_BATCH_TESTING
        brushCpuBatchTestHook(index);
#endif
        const auto& job=rows[index];const auto& request=*job.request;
        renderBrushCpuRows(*request.tile,request.uniforms,settled,tail,job.first,job.last);
    });
}
} // namespace compositor::graphics
