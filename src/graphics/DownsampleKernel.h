#pragma once
#include <array>
#include <cmath>
#include <numbers>
namespace compositor::graphics {
// vImage HighQualityResampling's Lanczos5 family, evaluated at the 2x halving
// centers. Exact Accelerate coefficients/rounding still require Mac comparison.
inline const std::array<double,20>& halvingWeights(){
    static const auto weights=[](){std::array<double,20> result{};double sum=0;
        const auto sinc=[](double x){return x==0?1:std::sin(std::numbers::pi*x)/(std::numbers::pi*x);};
        for(int i=0;i<20;++i){const double x=(i-9.5)/2;sum+=(result[size_t(i)]=sinc(x)*sinc(x/5));}
        for(auto& value:result)value/=sum;return result;}();
    return weights;
}
}
