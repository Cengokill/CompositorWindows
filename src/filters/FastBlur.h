#pragma once

#include "graphics/ParallelBatch.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <vector>

namespace compositor::filters::detail {
// Three sliding box passes approximate a Gaussian without a radius-dependent cost.
// Rows and columns are independent, so the result does not depend on `workers`
// (1 runs on the caller; 0 uses the shared pool).
inline std::vector<float> boxGaussian(std::vector<float> image,int w,int h,double sigma,const std::function<bool()>& cancelled,std::uint32_t workers=1){
    if(sigma<.15)return image;
    const int radius=std::max(1,int(std::round((std::sqrt(4*sigma*sigma+1)-1)/2)));
    std::vector<float> scratch(image.size());
    const double inv=1.0/(2*radius+1);
    auto check=[&]{if(cancelled&&cancelled())throw std::runtime_error("Filter cancelled");};
    for(int pass=0;pass<3;++pass){
        graphics::runParallelBatch(size_t(h),workers,[&](size_t row){const int y=int(row);check();double sum=0;for(int x=-radius;x<=radius;++x)sum+=image[size_t(y)*w+std::clamp(x,0,w-1)];
            for(int x=0;x<w;++x){scratch[size_t(y)*w+x]=float(sum*inv);sum-=image[size_t(y)*w+std::clamp(x-radius,0,w-1)];sum+=image[size_t(y)*w+std::clamp(x+radius+1,0,w-1)];}});
        graphics::runParallelBatch(size_t(w),workers,[&](size_t column){const int x=int(column);check();double sum=0;for(int y=-radius;y<=radius;++y)sum+=scratch[size_t(std::clamp(y,0,h-1))*w+x];
            for(int y=0;y<h;++y){image[size_t(y)*w+x]=float(sum*inv);sum-=scratch[size_t(std::clamp(y-radius,0,h-1))*w+x];sum+=scratch[size_t(std::clamp(y+radius+1,0,h-1))*w+x];}});
    }
    return image;
}
}
