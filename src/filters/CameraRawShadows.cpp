// Adapted from Compositor/Rendering/AdjustPixels.c, upstream commit
// 735c2cc23c0efe7c2c2f85a66741b253e053cf2b.
// Copyright (c) 2026 Wonder Assembly LLC. MIT license.
#include "CameraRawShadows.h"
#include "graphics/ParallelBatch.h"
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
using ByteDecode = std::array<double, 256>;
float alphaOf(float value) { return value; }
float alphaOf(std::uint8_t value) { return value / 255.f; }
std::array<double, 3> linear(const float* p, const ByteDecode&) {
    return {decode(std::clamp(double(p[0] / p[3]), 0., 1.)), decode(std::clamp(double(p[1] / p[3]), 0., 1.)), decode(std::clamp(double(p[2] / p[3]), 0., 1.))};
}
std::array<double, 3> linear(const std::uint8_t* p, const ByteDecode& opaque) {
    if (p[3] == 255) return {opaque[p[0]], opaque[p[1]], opaque[p[2]]};
    const float alpha = alphaOf(p[3]);
    auto channel = [alpha](std::uint8_t value) { return decode(std::clamp(double((value / 255.f) / alpha), 0., 1.)); };
    return {channel(p[0]), channel(p[1]), channel(p[2])};
}
float shadowChannel(double linearValue, float alpha, float) { return float(encode(linearValue) * alpha); }
std::uint8_t shadowChannel(double linearValue, float alpha, std::uint8_t) {
    const float encoded = float(encode(linearValue) * alpha);
    return std::uint8_t(std::lround(std::clamp(encoded, 0.f, 1.f) * 255));
}
double luminance(const std::array<double, 3>& p) { return 0.2126*p[0] + 0.7152*p[1] + 0.0722*p[2]; }
int gridScale(int width, int height) {
    return std::max({1, std::min(width, height) / 384, int(std::ceil(std::sqrt(double(width) * height / 2000000.)))});
}
std::uint32_t workerLimit(int width, int height) {
    return std::uint64_t(width) * height < 128 * 1024 ? 1u : 0u;
}
// Edge windows shrink rather than repeating samples, as in the upstream kernel.
void boxMean(const std::vector<float>& src, std::vector<float>& dst, std::vector<float>& temp, int w, int h, int radius,
             const std::function<bool()>& cancelled) {
    const auto workers = workerLimit(w, h);
    graphics::runParallelBatch(std::size_t(h), workers, [&](std::size_t row) {
        const int y = int(row);
        stop(cancelled); double sum=0; int lo=0, hi=0;
        for (int x=0; x<w; ++x) {
            const int wantLo=std::max(0,x-radius), wantHi=std::min(w,x+radius+1);
            while (hi<wantHi) sum+=src[std::size_t(y)*w+hi++];
            while (lo<wantLo) sum-=src[std::size_t(y)*w+lo++];
            temp[std::size_t(y)*w+x]=float(sum/(hi-lo));
        }
    });
    graphics::runParallelBatch(std::size_t(w), workers, [&](std::size_t column) {
        const int x = int(column);
        stop(cancelled);
        double sum=0; int lo=0, hi=0;
        for (int y=0; y<h; ++y) {
            const int wantLo=std::max(0,y-radius), wantHi=std::min(h,y+radius+1);
            while (hi<wantHi) sum+=temp[std::size_t(hi++)*w+x];
            while (lo<wantLo) sum-=temp[std::size_t(lo++)*w+x];
            dst[std::size_t(y)*w+x]=float(sum/(hi-lo));
        }
    });
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
template<class PixelAt>
CameraRawShadowAnalysis analyze(int width, int height, const PixelAt& at, const ByteDecode& opaque,
                                const std::function<bool()>& cancelled) {
    const int scale = gridScale(width, height);
    const int w = (width + scale - 1) / scale, h = (height + scale - 1) / scale;
    const std::size_t cells = std::size_t(w) * h;
    std::vector<float> low(cells), weight(cells);
    struct Totals { double edge{}, edgeWeight{}, all{}, allWeight{}; };
    std::vector<Totals> rows(static_cast<std::size_t>(h));
    // Each job owns one row of the reduced grid, including all the input
    // pixels that contribute to those cells. Accumulate each cell in input order.
    graphics::runParallelBatch(rows.size(), workerLimit(width, height), [&](std::size_t row) {
        auto& totals = rows[row];
        const int start = int(row) * scale, end = std::min(height, start + scale);
        for (int y = start; y < end; ++y) {
            stop(cancelled);
            for (int x = 0; x < width; ++x) {
                const auto* p = at(x, y);
                const float alpha = std::clamp(alphaOf(p[3]), 0.f, 1.f);
                if (!(alpha > 0)) continue;
                const double l = luminance(linear(p, opaque));
                totals.all += l * alpha; totals.allWeight += alpha;
                const auto green = p[1];
                if ((x > 0 && at(x - 1, y)[1] != green) || (x + 1 < width && at(x + 1, y)[1] != green) ||
                    (y > 0 && at(x, y - 1)[1] != green) || (y + 1 < height && at(x, y + 1)[1] != green)) {
                    totals.edge += l * alpha; totals.edgeWeight += alpha;
                }
                const auto cell = row * w + x / scale;
                low[cell] += float(std::log2(std::max(floorY, l))) * alpha; weight[cell] += alpha;
            }
        }
    });
    Totals total;
    for (const auto& row : rows) {
        total.edge += row.edge; total.edgeWeight += row.edgeWeight;
        total.all += row.all; total.allWeight += row.allWeight;
    }
    // Sparse edges are not representative: blend in the image mean until the
    // edge sample covers at least ten percent of the visible pixel weight.
    const double average=total.allWeight>0 ? total.all/total.allWeight : 0.18;
    const double fallback=std::max(0.,total.allWeight*.1-total.edgeWeight);
    const double edgeWeight=total.edgeWeight+fallback;
    const double key=std::log2(std::max(floorY,edgeWeight>0 ? (total.edge+fallback*average)/edgeWeight : average));
    for(std::size_t i=0;i<cells;++i) low[i]=weight[i]>0 ? low[i]/weight[i] : float(key);
    CameraRawShadowAnalysis result{scale, w, h, key, std::vector<float>(cells), std::vector<float>(cells)};
    auto& mean = result.mean;
    auto& square = result.square;
    std::vector<float> a(cells), b(cells), temp(cells);
    const int radius=std::max(1,int(std::lround(.25*std::min(w,h))));
    boxMean(low,mean,temp,w,h,radius,cancelled);
    for(std::size_t i=0;i<cells;++i) a[i]=low[i]*low[i];
    boxMean(a,square,temp,w,h,radius,cancelled);
    for(std::size_t i=0;i<cells;++i) { float variance=std::max(0.f,square[i]-mean[i]*mean[i]); a[i]=variance/(variance+1.5f); b[i]=mean[i]-a[i]*mean[i]; }
    boxMean(a,mean,temp,w,h,radius,cancelled); boxMean(b,square,temp,w,h,radius,cancelled);
    return result;
}

template<class Channel>
void adaptiveShadows(std::vector<Channel>& pixels, int width, int height, double amount, double previewScale,
                     const std::function<bool()>& cancelled, CameraRawShadowAnalysis* cached) {
    if (amount == 0) return;
    if (width < 1 || height < 1 || pixels.size() != std::size_t(width) * height * 4)
        throw std::runtime_error("Invalid Camera Raw shadow image");
    stop(cancelled);
    ByteDecode opaque{};
    if constexpr (sizeof(Channel) == 1)
        for (std::size_t i = 0; i < opaque.size(); ++i) opaque[i] = decode(float(i) / 255.f);
    auto at = [&](int x, int y) { return pixels.data() + (std::size_t(y) * width + x) * 4; };
    CameraRawShadowAnalysis local;
    auto& analysis = cached ? *cached : local;
    if (!analysis.scale) analysis = analyze(width, height, at, opaque, cancelled);
    const auto& [scale, w, h, key, mean, square] = analysis;
    const double effectiveScale = std::isfinite(previewScale) && previewScale > 0 ? previewScale : 1;
    const auto coeff = coefficients(amount, std::max(width, height) / effectiveScale >= 1020 ? 1 : 0);
    const double kp=std::clamp((key+11)/2.,0.,5.); const int k0=int(kp), k1=std::min(k0+1,5); const double kt=kp-k0;
    graphics::runParallelBatch(std::size_t(height), workerLimit(width, height), [&](std::size_t row) {
        const int y = int(row);
        stop(cancelled); const double fy=std::clamp((y+.5)/scale-.5,0.,double(h-1)); const int y0=int(fy),y1=std::min(y0+1,h-1); const double ty=fy-y0;
        for(int x=0;x<width;++x) {
            auto* p=at(x,y); if (!(p[3]>0)) continue;
            auto rgb=linear(p, opaque); const double lum=luminance(rgb); if(!(lum>0)) continue;
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
            const float alpha = alphaOf(p[3]);
            for(int c=0;c<3;++c) p[c]=shadowChannel(rgb[c]*gain, alpha, p[c]);
        }
    });
}
}

std::uint64_t cameraRawShadowWorkingBytes(int width, int height) {
    const int scale = gridScale(width, height);
    const auto rows = std::uint64_t((height + scale - 1) / scale);
    const auto columns = std::uint64_t((width + scale - 1) / scale);
    return rows * (columns * 7 * sizeof(float) + 4 * sizeof(double));
}
void applyAdaptiveShadows(std::vector<float>& pixels, int width, int height, double amount,
                          double previewScale, const std::function<bool()>& cancelled, CameraRawShadowAnalysis* analysis) {
    adaptiveShadows(pixels, width, height, amount, previewScale, cancelled, analysis);
}
void applyAdaptiveShadows(std::vector<std::uint8_t>& pixels, int width, int height, double amount,
                          double previewScale, const std::function<bool()>& cancelled, CameraRawShadowAnalysis* analysis) {
    adaptiveShadows(pixels, width, height, amount, previewScale, cancelled, analysis);
}
}
