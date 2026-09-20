#pragma once
#include "MaskSampling.h"

namespace compositor::graphics {
// Canonical Windows sampler. Smooth and High currently share bilinear math;
// CoreGraphics High/Lanczos and geometric edge antialiasing remain unverified.
inline Pixel sampleRaster(const Raster& raster,Point unit,Transform::Sampling sampling){
    double x=unit.x*raster.width,y=unit.y*raster.height;
    if(!std::isfinite(x)||!std::isfinite(y)||x<0||y<0||x>=raster.width||y>=raster.height)return {};
    if(sampling==Transform::Sampling::Nearest)return raster.pixel(int(x),int(y));
    x-=.5;y-=.5;const int ix=int(std::floor(x)),iy=int(std::floor(y));const double fx=x-ix,fy=y-iy;
    const auto get=[&](int xx,int yy){return raster.pixel(std::clamp(xx,0,raster.width-1),std::clamp(yy,0,raster.height-1));};
    const auto p00=get(ix,iy),p10=get(ix+1,iy),p01=get(ix,iy+1),p11=get(ix+1,iy+1);
    const auto channel=[&](uint8_t Pixel::*m){const double v=(1-fy)*((1-fx)*(p00.*m)+fx*(p10.*m))+fy*((1-fx)*(p01.*m)+fx*(p11.*m));return uint8_t(std::clamp(std::lround(v),0L,255L));};
    return {channel(&Pixel::r),channel(&Pixel::g),channel(&Pixel::b),channel(&Pixel::a)};
}
inline double sampleGray(const GrayRaster& raster,Point unit,Transform::Sampling sampling,uint8_t exterior=0){return sampleMask(raster,unit,sampling,exterior);}
}
