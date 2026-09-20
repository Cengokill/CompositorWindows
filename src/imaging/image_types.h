#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace compositor::imaging {
// Owned, top-left, premultiplied RGBA8 in the sRGB document space. Padding is permitted.
struct RgbaImage { std::uint32_t width{}, height{}; std::size_t stride{}; std::vector<std::uint8_t> pixels; };
// Coverage only. No alpha channel or color transform is applied to masks.
struct GrayMask { std::uint32_t width{}, height{}; std::size_t stride{}; std::vector<std::uint8_t> pixels; };
struct ImportOptions {
    std::uint32_t maxSide{30000};
    std::uint64_t remainingPixels{100000000};
    std::uint64_t maxWorkingBytes{1200000000};
    bool applyExifOrientation{true};
    // Must be thread-safe and non-throwing; native inference polls on a worker thread.
    std::function<bool()> cancelled;
};
inline void checkCancelled(const ImportOptions& o) { if(o.cancelled && o.cancelled()) throw std::runtime_error("Image operation cancelled"); }
inline std::size_t checkedBytes(std::uint32_t w,std::uint32_t h,std::uint32_t channels,const ImportOptions& o={}) {
    const auto n=std::uint64_t(w)*h;
    if(!channels || channels>4 || !w || !h || w>std::min(o.maxSide,30000U) || h>std::min(o.maxSide,30000U) || n>std::min(o.remainingPixels,100000000ULL) || n>std::numeric_limits<std::size_t>::max()/channels || n*channels>o.maxWorkingBytes)
        throw std::runtime_error("Image exceeds dimensions, pixel, or allocation budget");
    return static_cast<std::size_t>(n*channels);
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
struct DecodedImage { RgbaImage image; ImageMetadata metadata; };
enum class ImageFormat { Png, Jpeg, Tiff };
struct ExportOptions {
    ImageFormat format{ImageFormat::Png};
    double dpi{72},jpegQuality{0.85};
    std::uint8_t matteR{255},matteG{255},matteB{255};
    std::function<bool()> cancelled;
};
}
