#pragma once
#include <algorithm>
#include <cstdint>
#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace compositor {
class Raster;
using WidePixel = std::array<float,4>;
// Premultiplied sRGB RGBA, native-endian 16-bit samples, packed rows, top-left origin.
// HDR uses IEEE binary16 in extended sRGB, with white at 1. RGB is premultiplied
// but may exceed alpha. The Raster is only the clipped, dithered SDR presentation.
struct DeepImage {
    enum class Format { Unorm16, HalfFloat };
    int width{}, height{};
    Format format{Format::Unorm16};
    std::vector<std::uint16_t> samples;
    bool isHdr() const { return format == Format::HalfFloat; }
    bool valid() const { return width > 0 && height > 0 && width <= 30000 && height <= 30000 && std::uint64_t(width)*height <= 100000000 && samples.size() == std::size_t(width) * std::size_t(height) * 4; }
    const std::uint16_t* at(int x, int y) const { return samples.data() + (std::size_t(y) * std::size_t(width) + std::size_t(x)) * 4; }
    std::uint16_t* at(int x, int y) { return samples.data() + (std::size_t(y) * std::size_t(width) + std::size_t(x)) * 4; }
    std::array<float,4> pixel(int x,int y) const;
    void setPixel(int x,int y,const std::array<float,4>&);
    bool operator==(const DeepImage&) const = default;
};
float halfToFloat(std::uint16_t);
std::uint16_t floatToHalf(float);
double srgbToLinear(double);
double linearToSrgb(double);
std::shared_ptr<DeepImage> makeDeep(int width,int height,DeepImage::Format format=DeepImage::Format::HalfFloat);
std::shared_ptr<const DeepImage> hdrTo16Bit(const DeepImage&,const std::function<bool()>& cancelled={});
// Normalize each straight RGB triple by max(1, peak), retaining that scale for
// bounded SDR-only color operators. Alpha is never used as a brightness scale.
struct NormalizedDeep { std::shared_ptr<const Raster> raster; std::vector<float> scales; };
NormalizedDeep normalizeDeep(const DeepImage&,const std::function<bool()>& cancelled={});
std::shared_ptr<const DeepImage> restoreDeep(const Raster&,const std::vector<float>& scales,const std::function<bool()>& cancelled={});
inline std::uint16_t promote8(std::uint8_t value) { return std::uint16_t(std::uint16_t(value) * 257); }
// Photoshop's 16-bit RGB scale stores white as 32768. Expanding by two reaches 65535.
inline std::uint16_t scalePhotoshop16(std::uint16_t sample) {
    return sample >= 32768 ? std::uint16_t{65535} : std::uint16_t(sample * 2);
}
inline std::uint8_t ditherChannel(std::uint16_t value, int x, int y) {
    static constexpr int bayer[4][4]{{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    const int level = (int(value) * 255 + bayer[y & 3][x & 3] * 256) / 65535;
    return std::uint8_t(std::clamp(level, 0, 255));
}
std::shared_ptr<const DeepImage> deepFromPlanes(int width, int height, const std::uint16_t* red, const std::uint16_t* green, const std::uint16_t* blue, const std::uint16_t* alpha);
std::shared_ptr<const Raster> rasterFromDeep(const DeepImage& image);
}
