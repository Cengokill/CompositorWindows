#pragma once
#include "MaskSampling.h"

namespace compositor::graphics {
// Final fractional Windows sampler. Smooth and High share bilinear math;
// StackRenderer supplies Lanczos halvings first. CoreGraphics final filtering
// and geometric edge antialiasing remain unverified.
template<class Reader> inline Pixel sampleRasterPixels(int width,int height,const Reader& pixel,Point unit,Transform::Sampling sampling){
    double x=unit.x*width,y=unit.y*height;
    if(!std::isfinite(x)||!std::isfinite(y)||x<0||y<0||x>=width||y>=height)return {};
    if(sampling==Transform::Sampling::Nearest)return pixel(int(x),int(y));
    x-=.5;y-=.5;const int ix=int(std::floor(x)),iy=int(std::floor(y));const double fx=x-ix,fy=y-iy;
    const auto get=[&](int xx,int yy){return pixel(std::clamp(xx,0,width-1),std::clamp(yy,0,height-1));};
    const auto p00=get(ix,iy),p10=get(ix+1,iy),p01=get(ix,iy+1),p11=get(ix+1,iy+1);
    const auto channel=[&](uint8_t Pixel::*m){const double v=(1-fy)*((1-fx)*(p00.*m)+fx*(p10.*m))+fy*((1-fx)*(p01.*m)+fx*(p11.*m));return uint8_t(std::clamp(std::lround(v),0L,255L));};
    return {channel(&Pixel::r),channel(&Pixel::g),channel(&Pixel::b),channel(&Pixel::a)};
}
inline Pixel sampleRaster(const Raster& raster,Point unit,Transform::Sampling sampling){return sampleRasterPixels(raster.width,raster.height,[&](int x,int y){return raster.pixel(x,y);},unit,sampling);}
inline double sampleGray(const GrayRaster& raster,Point unit,Transform::Sampling sampling,uint8_t exterior=0){return sampleMask(raster,unit,sampling,exterior);}
}
