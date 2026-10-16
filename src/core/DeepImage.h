#pragma once
#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

namespace compositor {
class Raster;
// Premultiplied sRGB RGBA, native-endian 16-bit samples, packed rows, top-left origin.
// An absent buffer means the layer is 8-bit. Tools that do not understand 16-bit pixels
// drop it; the Raster remains the dithered working copy used by the canvas.
struct DeepImage {
    int width{}, height{};
    std::vector<std::uint16_t> samples;
    bool valid() const { return width > 0 && height > 0 && samples.size() == std::size_t(width) * std::size_t(height) * 4; }
    const std::uint16_t* at(int x, int y) const { return samples.data() + (std::size_t(y) * std::size_t(width) + std::size_t(x)) * 4; }
    std::uint16_t* at(int x, int y) { return samples.data() + (std::size_t(y) * std::size_t(width) + std::size_t(x)) * 4; }
    bool operator==(const DeepImage&) const = default;
};
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
