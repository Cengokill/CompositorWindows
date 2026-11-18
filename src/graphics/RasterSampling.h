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
// The four taps around a sample and its fractional position between them.
inline Pixel bilinearPixel(Pixel p00,Pixel p10,Pixel p01,Pixel p11,double fx,double fy){
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
// A sample's taps: the clamped pixel indices left of and above it, and the fractions.
struct BilinearTaps {int x{},y{};double fx{},fy{};};
// One axis of bilinearTaps: false outside the image along that axis.
inline bool bilinearTap(int size,double unit,int& tap,double& fraction){
    double v=unit*size;
    if(!std::isfinite(v)||v<0||v>=size)return false;
    v-=.5;tap=floorToInt(v);fraction=v-tap;
    return true;
}
// False outside the image, where a sample is transparent. Nearest sampling has no taps.
inline bool bilinearTaps(int width,int height,Point unit,BilinearTaps& taps){
    // Each axis depends only on its own coordinate, so the two halves are independent.
    BilinearTaps result;
    if(!bilinearTap(width,unit.x,result.x,result.fx)||!bilinearTap(height,unit.y,result.y,result.fy))return false;
    taps=result;
    return true;
}
template<class Reader> inline Pixel sampleRasterPixels(int width,int height,const Reader& pixel,Point unit,Transform::Sampling sampling){
    if(sampling==Transform::Sampling::Nearest){
        const double x=unit.x*width,y=unit.y*height;
        if(!std::isfinite(x)||!std::isfinite(y)||x<0||y<0||x>=width||y>=height)return {};
        return pixel(int(x),int(y));
    }
    BilinearTaps taps;if(!bilinearTaps(width,height,unit,taps))return {};
    const auto get=[&](int xx,int yy){return pixel(std::clamp(xx,0,width-1),std::clamp(yy,0,height-1));};
    return bilinearPixel(get(taps.x,taps.y),get(taps.x+1,taps.y),get(taps.x,taps.y+1),get(taps.x+1,taps.y+1),taps.fx,taps.fy);
}
// The sampler only reads clamped in-bounds pixels, so read the tile directly
// instead of calling Raster::pixel and its bounds check for each of four taps.
inline Pixel sampleRaster(const Raster& raster,Point unit,Transform::Sampling sampling){
    const size_t columns=size_t(raster.width+255)/256;
    return sampleRasterPixels(raster.width,raster.height,[&](int x,int y){return raster.tiles[size_t(y>>8)*columns+size_t(x>>8)]->pixels[size_t(y&255)*256+size_t(x&255)];},unit,sampling);
}
inline double sampleGray(const GrayRaster& raster,Point unit,Transform::Sampling sampling,uint8_t exterior=0){return sampleMask(raster,unit,sampling,exterior);}
}
