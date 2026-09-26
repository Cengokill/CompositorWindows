// Translation of MetalBrushCoverage.swift at a19db9011282399785dc18efcfded904627bdcc2.
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
float coverage(float squared,const BrushUniforms& u) {
    const float distance=std::sqrt(squared),radius=u.geometry[2],hardness=u.geometry[3];
    if(hardness>=1) return std::clamp((radius-distance)/u.canvas[2]+0.5f,0.0f,1.0f);
    const float t=std::clamp((distance/radius-hardness)/(1-hardness),0.0f,1.0f);
    return std::max(0.0f,(std::exp(-2.5f*t*t)-std::exp(-2.5f))/(1-std::exp(-2.5f)));
}
float tipDensity(float squared,const BrushUniforms& u) {return -std::log(std::max(1-coverage(squared,u),0.001f));}
float distanceSquared(Vec p,const BrushSegment& s) {
    const Vec a{s.x0,s.y0},v{s.x1-s.x0,s.y1-s.y0};
    float t=std::clamp(dot(p-a,v)/std::max(dot(v,v),1e-12f),0.0f,1.0f);
    const auto delta=p-(a+v*t); return dot(delta,delta);
}
float segmentDensity(Vec p,const BrushSegment& s,const BrushUniforms& u) {
    const Vec a{s.x0,s.y0},v{s.x1-s.x0,s.y1-s.y0};
    float length=std::sqrt(dot(v,v)); if(length<1e-6f)return tipDensity(dot(p-a,p-a),u);
    const auto direction=v*(1/length); float projection=dot(p-a,direction);
    const auto perpendicular=p-a-direction*projection; float squared=dot(perpendicular,perpendicular);
    float radiusSquared=u.geometry[2]*u.geometry[2]; if(squared>=radiusSquared)return 0;
    float reach=std::sqrt(radiusSquared-squared),lo=std::max(0.0f,projection-reach),hi=std::min(length,projection+reach);
    if(hi<=lo)return 0; float midpoint=(lo+hi)*0.5f,halfLength=(hi-lo)*0.5f;
    constexpr float nodes[]={0.1834346425f,0.5255324099f,0.7966664774f,0.9602898565f};
    constexpr float weights[]={0.3626837834f,0.3137066459f,0.2223810345f,0.1012285363f};
    float integral=0;
    for(int i=0;i<4;++i){float aa=midpoint-halfLength*nodes[i]-projection,bb=midpoint+halfLength*nodes[i]-projection;
        integral+=weights[i]*(tipDensity(squared+aa*aa,u)+tipDensity(squared+bb*bb,u));}
    return integral*halfLength/u.canvas[3];
}
}
BrushTile::BrushTile(uint32_t w,uint32_t h):width(w),height(h) {
    if(!w || !h || w>brushTileSize || h>brushTileSize)throw std::invalid_argument("Brush tiles must be 1..256 pixels");
    permanent.resize(size_t(w)*h); preview.resize(size_t(w)*h);
}
void validateBrush(const BrushTile& t,const BrushUniforms& u,std::span<const BrushSegment> settled,std::span<const BrushSegment> tail) {
    if(!t.width || !t.height || t.width>brushTileSize || t.height>brushTileSize ||
       t.permanent.size()!=size_t(t.width)*t.height || t.preview.size()!=size_t(t.width)*t.height)
        throw std::invalid_argument("Invalid brush tile storage");
    for(const auto& v:{u.mapping,u.geometry,u.canvas})for(float x:v)
        if(!std::isfinite(x) || std::abs(x)>1e7f)throw std::invalid_argument("Nonfinite or excessive brush coordinate");
    if(u.geometry[2]<=0 || u.geometry[3]<0 || u.geometry[3]>1 || u.canvas[0]<=0 || u.canvas[1]<=0 || u.canvas[2]<=0 || u.canvas[3]<0.25f)
        throw std::invalid_argument("Invalid brush settings");
    if(settled.size()>65536 || tail.size()>65536 || settled.size()+tail.size()>65536)
        throw std::length_error("Split brush submissions into at most 65536 segments");
    for(auto segments:{settled,tail})for(auto s:segments)for(float v:{s.x0,s.y0,s.x1,s.y1})
        if(!std::isfinite(v) || std::abs(v)>1e7f)throw std::invalid_argument("Invalid brush segment");
    for(float value:t.permanent)if(!std::isfinite(value) || value<0 || value>(u.geometry[3]>=1 ? 1.0f : 20.0f))
        throw std::invalid_argument("Invalid permanent brush state");
}
namespace {
void renderBrushCpuRows(BrushTile& tile,BrushUniforms u,std::span<const BrushSegment> settled,std::span<const BrushSegment> tail,uint32_t firstRow,uint32_t lastRow) {
    for(uint32_t y=firstRow;y<lastRow;++y)for(uint32_t x=0;x<tile.width;++x){
        const size_t i=size_t(y)*tile.width+x; float lx=float(x)+0.5f,ly=float(y)+0.5f;
        Vec p{u.geometry[0]+lx*u.mapping[0]+ly*u.mapping[2],u.geometry[1]+lx*u.mapping[1]+ly*u.mapping[3]};
        if(p.x<0 || p.y<0 || p.x>=u.canvas[0] || p.y>=u.canvas[1]){tile.preview[i]=0;continue;}
        float value=tile.permanent[i],preview=0;
        if(u.geometry[3]>=1){float a=std::numeric_limits<float>::infinity(),b=a;
            for(auto s:settled)a=std::min(a,distanceSquared(p,s));for(auto s:tail)b=std::min(b,distanceSquared(p,s));
            value=std::max(value,coverage(a,u));preview=std::max(value,coverage(b,u));tile.permanent[i]=value;
        }else{float transient=0;for(auto s:settled)value+=segmentDensity(p,s,u);for(auto s:tail)transient+=segmentDensity(p,s,u);
            tile.permanent[i]=std::min(value,20.0f);preview=1-std::exp(-std::min(value+transient,20.0f));}
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
