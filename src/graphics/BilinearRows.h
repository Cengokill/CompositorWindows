#pragma once
#include <cstddef>
#include <cstdint>

namespace compositor::graphics {
// One output row of one layer whose samples all read the same two source rows.
// Pixels are packed premultiplied RGBA bytes, as in Pixel. The source rows hold
// columns left, left+1, ... already clamped to the image: packed for the portable
// sampler, and as four floats per pixel (widenPixels, exact for bytes) for AVX.
struct BilinearRow {
    const uint32_t* upper{};
    const uint32_t* lower{};
    const float* upperWide{};
    const float* lowerWide{};
    int left{};
    const int* x{};
    const double* fx{};
    const double* fxComplement{}; // 1-fx
    // fx and 1-fx rounded to float, each repeated for the four channels.
    const float* fxWide{};
    const float* fxComplementWide{};
    const uint8_t* inside{};
    double fy{};
    // opacityFixedPoint for the opacity passed with this row, when it found one.
    bool opacityFixed{};
    uint32_t opacityMultiplier{},opacityOffset{};
};
// Integers with (n*multiplier+offset)>>16 equal to clamp(lround(n*opacity),0,255)
// for every n from 0 to 255, checked for each n. False when there are none.
bool opacityFixedPoint(double opacity,uint32_t& multiplier,uint32_t& offset);
// For each column whose stop is negative: raw is bilinearPixel of its taps
// (transparent outside the image) and scaled is each raw channel times opacity
// rounded like clamp(lround(v),0,255). raw may be null. With stopAt at least 0,
// a column whose scaled alpha is 255 gets that stop. Returns how many did.
// Columns that already have a stop are left untouched.
size_t sampleBilinearRow(const BilinearRow&,int* stop,int stopAt,size_t width,double opacity,uint32_t* raw,uint32_t* scaled);
// The AVX sampler's source rows and column weights. Only when bilinearRowAvxAvailable().
void widenPixels(const uint32_t* pixels,size_t count,float* wide);
void widenWeights(const double* weights,size_t count,float* wide);
bool bilinearRowAvxAvailable();
// Tests turn the AVX and AVX2 paths off to check the portable ones on any processor.
void setWideVectorsAllowed(bool);
// Both implementations, for tests that compare them.
size_t sampleBilinearRowPortable(const BilinearRow&,int* stop,int stopAt,size_t width,double opacity,uint32_t* raw,uint32_t* scaled);
size_t sampleBilinearRowAvx(const BilinearRow&,int* stop,int stopAt,size_t width,double opacity,uint32_t* raw,uint32_t* scaled);
}
