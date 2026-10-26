#pragma once
#include "imaging/image_types.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
// Deterministic synthetic focus-stack frames shared by the focus stack tests.
namespace focus_stack_fixture {
inline std::uint8_t noise(int x,int y){std::uint32_t h=std::uint32_t(x)*374761393u+std::uint32_t(y)*668265263u;h=(h^(h>>13))*1274126177u;return std::uint8_t((h^(h>>16))&0xff);}
// Opaque texture; pixels with blurred(x,y) true are 5x5 box-averaged.
template<class Blurred>
compositor::imaging::RgbaImage frame(int width,int height,Blurred blurred,int shiftX=0,int shiftY=0){
    compositor::imaging::RgbaImage image{std::uint32_t(width),std::uint32_t(height),std::size_t(width)*4,std::vector<std::uint8_t>(std::size_t(width)*height*4)};
    // Coarse 16 px value noise survives blur and drives alignment; fine noise carries the focus detail.
    auto coarse=[](int x,int y){
        const int gx=int(std::floor(x/16.0)),gy=int(std::floor(y/16.0));const double fx=(x-gx*16)/16.0,fy=(y-gy*16)/16.0;
        auto g=[](int i,int j){return double(noise(i*7919+100003,j*104729+7));};
        return (g(gx,gy)*(1-fx)+g(gx+1,gy)*fx)*(1-fy)+(g(gx,gy+1)*(1-fx)+g(gx+1,gy+1)*fx)*fy;
    };
    auto base=[&](int x,int y){const int sx=x-shiftX,sy=y-shiftY;return int(0.6*coarse(sx,sy)+0.4*noise(sx,sy));};
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){
        int value=base(x,y);
        if(blurred(x,y)){int sum=0;for(int j=-2;j<=2;++j)for(int i=-2;i<=2;++i)sum+=base(x+i,y+j);value=sum/25;}
        auto* p=&image.pixels[std::size_t(y)*image.stride+std::size_t(x)*4];
        p[0]=std::uint8_t(value);p[1]=std::uint8_t(255-value);p[2]=std::uint8_t(value/2);p[3]=255;
    }
    return image;
}
}
