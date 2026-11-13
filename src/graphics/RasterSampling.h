#pragma once
#include "MaskSampling.h"
#include <emmintrin.h>

namespace compositor::graphics {
template<class Reader> inline WidePixel sampleWidePixels(int width,int height,const Reader& pixel,Point unit,Transform::Sampling sampling){
    double x=unit.x*width,y=unit.y*height;
    if(!std::isfinite(x)||!std::isfinite(y)||x<0||y<0||x>=width||y>=height)return {};
    if(sampling==Transform::Sampling::Nearest)return pixel(int(x),int(y));
    x-=.5;y-=.5;const int ix=int(std::floor(x)),iy=int(std::floor(y));const double fx=x-ix,fy=y-iy;
    auto get=[&](int px,int py){return pixel(std::clamp(px,0,width-1),std::clamp(py,0,height-1));};
    const auto a=get(ix,iy),b=get(ix+1,iy),c=get(ix,iy+1),d=get(ix+1,iy+1);WidePixel out{};
    for(int k=0;k<4;++k)out[k]=float((1-fy)*((1-fx)*a[k]+fx*b[k])+fy*((1-fx)*c[k]+fx*d[k]));return out;
}
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
    const auto bilinear=[&](__m128d a,__m128d b,__m128d c,__m128d d){
        const auto left=_mm_set1_pd(1-fx),right=_mm_set1_pd(fx);
        const auto upper=_mm_add_pd(_mm_mul_pd(left,a),_mm_mul_pd(right,b));
        const auto lower=_mm_add_pd(_mm_mul_pd(left,c),_mm_mul_pd(right,d));
        const auto value=_mm_add_pd(_mm_mul_pd(_mm_set1_pd(1-fy),upper),_mm_mul_pd(_mm_set1_pd(fy),lower));
        const auto integer=_mm_cvttpd_epi32(value);
        // Round positive samples exactly as lround, including values just
        // below a half integer that would round up after adding 0.5.
        const auto fraction=_mm_sub_pd(value,_mm_cvtepi32_pd(integer));
        const auto increment=_mm_cvttpd_epi32(
            _mm_and_pd(_mm_cmpge_pd(fraction,_mm_set1_pd(.5)),_mm_set1_pd(1.)));
        return _mm_add_epi32(integer,increment);
    };
    const auto rg=bilinear(_mm_set_pd(p00.g,p00.r),_mm_set_pd(p10.g,p10.r),
        _mm_set_pd(p01.g,p01.r),_mm_set_pd(p11.g,p11.r));
    const auto ba=bilinear(_mm_set_pd(p00.a,p00.b),_mm_set_pd(p10.a,p10.b),
        _mm_set_pd(p01.a,p01.b),_mm_set_pd(p11.a,p11.b));
    const auto packed=_mm_packus_epi16(_mm_packs_epi32(_mm_unpacklo_epi64(rg,ba),_mm_setzero_si128()),_mm_setzero_si128());
    const auto channels=uint32_t(_mm_cvtsi128_si32(packed));
    return {uint8_t(channels),uint8_t(channels>>8),uint8_t(channels>>16),uint8_t(channels>>24)};
}
inline Pixel sampleRaster(const Raster& raster,Point unit,Transform::Sampling sampling){return sampleRasterPixels(raster.width,raster.height,[&](int x,int y){return raster.pixel(x,y);},unit,sampling);}
inline double sampleGray(const GrayRaster& raster,Point unit,Transform::Sampling sampling,uint8_t exterior=0){return sampleMask(raster,unit,sampling,exterior);}
}
