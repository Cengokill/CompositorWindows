#pragma once
// Source semantics: LayerMask.background/clipImage, MIT notice in upstream/LICENSE.
#include "core/Document.h"
#include <algorithm>
#include <cmath>
#include <mutex>
#include <unordered_map>

namespace compositor::graphics {
// An optional supplied thumbnail reproduces the source threshold exactly.
// Otherwise masks larger than96 pixels use an area-downsample approximation
// to the source CoreGraphics high-quality thumbnail (not Mac differential).
inline uint8_t maskBackground(const GrayRaster& mask,const GrayRaster* thumbnail=nullptr){
    const auto& source=thumbnail?*thumbnail:mask;
    if(source.width<1||source.height<1)return 255;
    const double factor=thumbnail?1:std::min(1.,96./std::max(source.width,source.height));
    const int width=std::max(1,int(source.width*factor)),height=std::max(1,int(source.height*factor));
    uint64_t sum=0,count=0;
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){if(y!=0&&y!=height-1&&x!=0&&x!=width-1)continue;
        const double x0=double(x)*source.width/width,x1=double(x+1)*source.width/width,y0=double(y)*source.height/height,y1=double(y+1)*source.height/height;
        double value=0;
        for(int yy=int(std::floor(y0));yy<int(std::ceil(y1));++yy)for(int xx=int(std::floor(x0));xx<int(std::ceil(x1));++xx){
            const double area=(std::min(x1,double(xx+1))-std::max(x0,double(xx)))*(std::min(y1,double(yy+1))-std::max(y0,double(yy)));
            value+=source.pixel(xx,yy)*area;
        }
        sum+=uint64_t(std::clamp(std::lround(value/((x1-x0)*(y1-y0))),0L,255L));++count;
    }
    return sum*2>=count*255?255:0;
}
inline uint8_t cachedMaskBackground(const std::shared_ptr<const GrayRaster>& mask){
    struct Entry{std::weak_ptr<const GrayRaster> owner;uint8_t value;};
    static std::mutex mutex;static std::unordered_map<const GrayRaster*,Entry> cache;
    std::lock_guard lock(mutex);
    if(auto found=cache.find(mask.get());found!=cache.end()&&!found->second.owner.expired())return found->second.value;
    if(cache.size()>=128)cache.clear();
    const auto result=maskBackground(*mask);cache.insert_or_assign(mask.get(),Entry{mask,result});return result;
}
template<class Reader> inline double sampleMaskPixels(int width,int height,const Reader& pixel,Point unit,Transform::Sampling sampling,uint8_t exterior=0){
    double x=unit.x*width,y=unit.y*height;
    if(!std::isfinite(x)||!std::isfinite(y)||x<0||y<0||x>=width||y>=height)return exterior/255.;
    if(sampling==Transform::Sampling::Nearest)return pixel(int(x),int(y))/255.;
    x-=.5;y-=.5;const int ix=int(std::floor(x)),iy=int(std::floor(y));const double fx=x-ix,fy=y-iy;
    const auto get=[&](int xx,int yy){return double(pixel(std::clamp(xx,0,width-1),std::clamp(yy,0,height-1)));};
    return ((1-fy)*((1-fx)*get(ix,iy)+fx*get(ix+1,iy))+fy*((1-fx)*get(ix,iy+1)+fx*get(ix+1,iy+1)))/255.;
}
inline double sampleMask(const GrayRaster& mask,Point unit,Transform::Sampling sampling,uint8_t exterior=0){return sampleMaskPixels(mask.width,mask.height,[&](int x,int y){return mask.pixel(x,y);},unit,sampling,exterior);}
}
