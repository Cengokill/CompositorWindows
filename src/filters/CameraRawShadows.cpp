// Adapted from Compositor/Rendering/AdjustPixels.c, upstream commit
// 735c2cc23c0efe7c2c2f85a66741b253e053cf2b.
// Copyright (c) 2026 Wonder Assembly LLC. MIT license.
#include "CameraRawShadows.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace compositor::filters {
namespace {
#include "CameraRawShadowCoefficients.inc"
constexpr std::size_t plane = 35 * 6;
constexpr std::size_t perAmount = 3 * plane;
constexpr double floorY = 1.0 / 8192;
void stop(const std::function<bool()>& cancelled) {
    if (cancelled && cancelled()) throw std::runtime_error("Filter cancelled");
}
double decode(double v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); }
double encode(double v) { v = std::clamp(v, 0.0, 1.0); return v <= 0.0031308 ? v * 12.92 : 1.055 * std::pow(v, 1.0 / 2.4) - 0.055; }
std::array<double, 3> linear(const float* p) {
    if (!(p[3] > 0)) return {};
    return {decode(std::clamp(double(p[0] / p[3]), 0., 1.)), decode(std::clamp(double(p[1] / p[3]), 0., 1.)), decode(std::clamp(double(p[2] / p[3]), 0., 1.))};
}
double luminance(const std::array<double, 3>& p) { return 0.2126*p[0] + 0.7152*p[1] + 0.0722*p[2]; }
// Edge windows shrink rather than repeating samples, as in the upstream kernel.
void boxMean(const std::vector<float>& src, std::vector<float>& dst, int w, int h, int radius,
             const std::function<bool()>& cancelled) {
    std::vector<float> temp(src.size());
    for (int y=0; y<h; ++y) {
        stop(cancelled); double sum=0; int lo=0, hi=0;
        for (int x=0; x<w; ++x) {
            const int wantLo=std::max(0,x-radius), wantHi=std::min(w,x+radius+1);
            while (hi<wantHi) sum+=src[std::size_t(y)*w+hi++];
            while (lo<wantLo) sum-=src[std::size_t(y)*w+lo++];
            temp[std::size_t(y)*w+x]=float(sum/(hi-lo));
        }
    }
    for (int x=0; x<w; ++x) {
        if (!(x%64)) stop(cancelled);
        double sum=0; int lo=0, hi=0;
        for (int y=0; y<h; ++y) {
            const int wantLo=std::max(0,y-radius), wantHi=std::min(h,y+radius+1);
            while (hi<wantHi) sum+=temp[std::size_t(hi++)*w+x];
            while (lo<wantLo) sum-=temp[std::size_t(lo++)*w+x];
            dst[std::size_t(y)*w+x]=float(sum/(hi-lo));
        }
    }
}
std::array<float, perAmount> coefficients(double amount, int set) {
    constexpr std::array<double, 6> values{-100,-50,0,25,50,100};
    constexpr std::array<int, 6> indices{0,1,-1,2,3,4};
    amount=std::clamp(amount,-100.,100.); int i=0;
    while (i<4 && amount>values[i+1]) ++i;
    const float t=float((amount-values[i])/(values[i+1]-values[i]));
    std::array<float,perAmount> out{};
    for (std::size_t j=0;j<perAmount;++j) {
        auto value=[&](int n) { return indices[n]<0 ? 0.f : shadowTable[5+(set*5+indices[n])*perAmount+j]; };
        out[j]=value(i)*(1-t)+value(i+1)*t;
    }
    return out;
}
}
void applyAdaptiveShadows(std::vector<float>& pixels, int width, int height, double amount,
                          double previewScale, const std::function<bool()>& cancelled) {
    if (amount==0) return;
    if (width<1 || height<1 || pixels.size()!=std::size_t(width)*std::size_t(height)*4)
        throw std::runtime_error("Invalid Camera Raw shadow image");
    stop(cancelled);
    const double effectiveScale=std::isfinite(previewScale) && previewScale>0 ? previewScale : 1;
    const auto coeff=coefficients(amount, std::max(width,height)/effectiveScale>=1020 ? 1 : 0);
    double total=0, count=0, all=0, allCount=0;
    auto at=[&](int x,int y) { return pixels.data()+(std::size_t(y)*width+x)*4; };
    for(int y=0;y<height;++y) {
        stop(cancelled);
        for(int x=0;x<width;++x) {
            const float* p=at(x,y); if (!(p[3]>0)) continue;
            const double alpha=std::clamp(double(p[3]),0.,1.);
            double l=luminance(linear(p)); all+=l*alpha; allCount+=alpha;
            const float g=p[1];
            if ((x>0 && at(x-1,y)[1]!=g) || (x+1<width && at(x+1,y)[1]!=g) ||
                (y>0 && at(x,y-1)[1]!=g) || (y+1<height && at(x,y+1)[1]!=g)) { total+=l*alpha; count+=alpha; }
        }
    }
    // Sparse edges are not representative: blend in the image mean until the
    // edge sample covers at least ten percent of the visible pixel weight.
    const double average=allCount>0 ? all/allCount : 0.18;
    const double fallback=std::max(0.,allCount*.1-count);
    const double key=std::log2(std::max(floorY,count+fallback>0 ? (total+fallback*average)/(count+fallback) : average));
    // Bound working memory even for very wide or tall documents.
    const double cellsLimit=2000000.;
    const int scale=std::max({1,std::min(width,height)/384,int(std::ceil(std::sqrt(double(width)*height/cellsLimit)))});
    const int w=(width+scale-1)/scale,h=(height+scale-1)/scale;
    const std::size_t cells=std::size_t(w)*h;
    std::vector<float> low(cells), weight(cells), mean(cells), square(cells), a(cells), b(cells);
    for(int y=0;y<height;++y) {
        stop(cancelled);
        for(int x=0;x<width;++x) {
            const float* p=at(x,y); if (!(p[3]>0)) continue;
            const auto i=std::size_t(y/scale)*w+x/scale;
            const float alpha=std::clamp(p[3],0.f,1.f);
            low[i]+=float(std::log2(std::max(floorY,luminance(linear(p)))))*alpha; weight[i]+=alpha;
        }
    }
    for(std::size_t i=0;i<cells;++i) low[i]=weight[i]>0 ? low[i]/weight[i] : float(key);
    const int radius=std::max(1,int(std::lround(.25*std::min(w,h))));
    boxMean(low,mean,w,h,radius,cancelled);
    for(std::size_t i=0;i<cells;++i) a[i]=low[i]*low[i];
    boxMean(a,square,w,h,radius,cancelled);
    for(std::size_t i=0;i<cells;++i) { float variance=std::max(0.f,square[i]-mean[i]*mean[i]); a[i]=variance/(variance+1.5f); b[i]=mean[i]-a[i]*mean[i]; }
    boxMean(a,mean,w,h,radius,cancelled); boxMean(b,square,w,h,radius,cancelled);
    const double kp=std::clamp((key+11)/2.,0.,5.); const int k0=int(kp), k1=std::min(k0+1,5); const double kt=kp-k0;
    for(int y=0;y<height;++y) {
        stop(cancelled); const double fy=std::clamp((y+.5)/scale-.5,0.,double(h-1)); const int y0=int(fy),y1=std::min(y0+1,h-1); const double ty=fy-y0;
        for(int x=0;x<width;++x) {
            float* p=at(x,y); if (!(p[3]>0)) continue;
            auto rgb=linear(p); const double lum=luminance(rgb); if(!(lum>0)) continue;
            const double fx=std::clamp((x+.5)/scale-.5,0.,double(w-1)); const int x0=int(fx),x1=std::min(x0+1,w-1); const double tx=fx-x0;
            auto sample=[&](const std::vector<float>& v) { return (v[std::size_t(y0)*w+x0]*(1-tx)+v[std::size_t(y0)*w+x1]*tx)*(1-ty)+(v[std::size_t(y1)*w+x0]*(1-tx)+v[std::size_t(y1)*w+x1]*tx)*ty; };
            // Hold the log-domain lookup at the floor while scaling the actual
            // pixel, so the gain extends continuously all the way to black.
            const double level=std::log2(std::max(floorY,lum)),region=sample(mean)*level+sample(square),detail=level-region;
            const double xp=std::clamp((region-key+14)/.5,0.,34.); const int xc=std::min(int(xp),33); const double xt=xp-xc;
            double term[3]{};
            for(int t=0;t<3;++t) {
                auto get=[&](int k,int c){return coeff[std::size_t(t)*plane+std::size_t(k)*35+c];};
                const double r0=get(k0,xc)*(1-xt)+get(k0,xc+1)*xt, r1=get(k1,xc)*(1-xt)+get(k1,xc+1)*xt;
                term[t]=r0*(1-kt)+r1*kt;
            }
            const double gain=std::exp2(term[0]+term[1]*std::max(detail,0.)+term[2]*std::min(detail,0.));
            for(int c=0;c<3;++c) p[c]=float(encode(rgb[c]*gain)*p[3]);
        }
    }
}
}
