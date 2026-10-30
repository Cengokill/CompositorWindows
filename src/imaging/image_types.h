#pragma once
#include "core/DeepImage.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace compositor::imaging {
// Owned, top-left, premultiplied RGBA8 in the sRGB document space. Padding is permitted.
struct RgbaImage { std::uint32_t width{}, height{}; std::size_t stride{}; std::vector<std::uint8_t> pixels; };
// Coverage only. No alpha channel or color transform is applied to masks.
struct GrayMask { std::uint32_t width{}, height{}; std::size_t stride{}; std::vector<std::uint8_t> pixels; };
// RAW development requested for one file. An unset value keeps the file's as-shot setting.
struct RawAdjustments {
    std::optional<double> exposure; // EV
    std::optional<std::uint32_t> kelvin;
    std::optional<double> tint;
    std::optional<double> noiseReduction;
    bool operator==(const RawAdjustments&) const = default;
};
// What the file's RAW decoder can change, and the bounds it reports for Kelvin.
struct RawCapabilities {
    bool temperature{}, tint{};
    std::uint32_t minKelvin{}, maxKelvin{};
};
struct RawAsShot { double tint{}; std::uint32_t kelvin{}; };
struct ImportOptions {
    std::uint32_t maxSide{30000};
    std::uint64_t remainingPixels{100000000};
    std::uint64_t maxWorkingBytes{1200000000};
    bool applyExifOrientation{true};
    RawAdjustments raw;
    // Must be thread-safe and non-throwing; native inference polls on a worker thread.
    std::function<bool()> cancelled;
};
inline void checkCancelled(const ImportOptions& o) { if(o.cancelled && o.cancelled()) throw std::runtime_error("Image operation cancelled"); }
inline std::size_t checkedBytes(std::uint32_t w,std::uint32_t h,std::uint32_t channels,const ImportOptions& o={},std::uint32_t bytesPerSample=1) {
    const auto n=std::uint64_t(w)*h;
    const auto sampleBytes=std::uint64_t(channels)*bytesPerSample;
    if(!bytesPerSample || bytesPerSample>2 || !channels || channels>4 || !w || !h || w>std::min(o.maxSide,30000U) || h>std::min(o.maxSide,30000U) || n>std::min<std::uint64_t>(o.remainingPixels,100000000) || !sampleBytes || n>std::numeric_limits<std::size_t>::max()/sampleBytes || n*sampleBytes>o.maxWorkingBytes)
        throw std::runtime_error("Image exceeds dimensions, pixel, or allocation budget");
    return static_cast<std::size_t>(n*sampleBytes);
}
inline void validate(const RgbaImage& image) {
    checkedBytes(image.width,image.height,4);
    if(image.stride<std::size_t(image.width)*4 || image.stride>std::numeric_limits<std::size_t>::max()/image.height || image.pixels.size()<image.stride*image.height)
        throw std::runtime_error("Invalid RGBA buffer stride or storage");
    for(std::uint32_t y=0;y<image.height;++y) for(std::uint32_t x=0;x<image.width;++x) {
        const auto* p=&image.pixels[y*image.stride+x*4];
        if(p[0]>p[3] || p[1]>p[3] || p[2]>p[3]) throw std::runtime_error("RGBA buffer is not premultiplied");
    }
}
inline void validate(const GrayMask& mask) {
    checkedBytes(mask.width,mask.height,1);
    if(mask.stride<mask.width || mask.stride>std::numeric_limits<std::size_t>::max()/mask.height || mask.pixels.size()<mask.stride*mask.height)
        throw std::runtime_error("Invalid mask stride or storage");
}
enum class ProfileStatus { AssumedSrgbNoProfile, ConvertedEmbeddedProfile, DeclaredSrgb };
struct ImageMetadata {
    std::uint16_t originalOrientation{1};
    double dpiX{72},dpiY{72};
    ProfileStatus profileStatus{ProfileStatus::AssumedSrgbNoProfile};
    std::string decoder;
};
struct DecodedImage { RgbaImage image; ImageMetadata metadata; std::shared_ptr<const DeepImage> deep; };
// A RAW frame developed with the requested settings, plus what the decoder can change.
struct RawDevelopment { DecodedImage image; RawCapabilities capabilities; RawAsShot asShot; };
// Pdf is written only by the streaming document export (encodePdfAtomic); WicCodec has no PDF encoder.
enum class ImageFormat { Png, Jpeg, Tiff, Pdf, Exr };
struct ExportOptions {
    ImageFormat format{ImageFormat::Png};
    double dpi{72},jpegQuality{0.85};
    std::uint8_t matteR{255},matteG{255},matteB{255};
    // PDF only: resamples the flattened canvas, keeping dpi, so the printed page scales with it.
    double scale{1.0};
    std::function<bool()> cancelled;
};
}
