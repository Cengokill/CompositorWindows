#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace compositor::graphics {
// Top-down 8-bit sRGB, premultiplied R,G,B,A. Padding is not image data.
struct Rgba8View { std::span<uint8_t> bytes; uint32_t width{}, height{}; size_t stride{}; };
struct ConstRgba8View { std::span<const uint8_t> bytes; uint32_t width{}, height{}; size_t stride{}; };
// Single-channel coverage, no color transform and no alpha channel.
struct Gray8View { std::span<uint8_t> bytes; uint32_t width{}, height{}; size_t stride{}; };
struct ConstGray8View { std::span<const uint8_t> bytes; uint32_t width{}, height{}; size_t stride{}; };
void validate(Rgba8View);
void validate(ConstRgba8View);
void validate(Gray8View);
void validate(ConstGray8View);
ConstRgba8View readOnly(Rgba8View);
ConstGray8View readOnly(Gray8View);
std::array<size_t, 4> alphaBounds(ConstRgba8View);
std::array<int64_t, 4> coverageBounds(ConstGray8View);
void extractAlpha(ConstRgba8View, Gray8View);
void unpremultiplyOpaque(Rgba8View);
void restoreAlpha(Rgba8View, ConstGray8View);
void clampPremultiplied(Rgba8View);
void applyLevels(Rgba8View, std::span<const float, 768> tables);
std::array<double, 1024> histogram(ConstRgba8View, const ConstGray8View* coverage = nullptr);
void gradientMap(Rgba8View, std::span<const uint8_t, 768> table);
void addNoise(Rgba8View, float amount, bool gaussian, bool monochromatic, uint32_t seed);
void grain(Rgba8View, double amount, double size, double roughness, uint32_t seed,
           double originX, double originY, double unitsPerPixel);
void lensDistort(ConstRgba8View source, Rgba8View destination, double k);
long wandMask(ConstRgba8View, size_t seedX, size_t seedY, size_t radius,
              int tolerance, bool contiguous, Gray8View);
struct WandOutline { std::vector<int32_t> points; std::vector<int32_t> loopLengths; };
WandOutline wandOutline(ConstGray8View);
// Return contracts are those of the upstream routines; -1 allocation errors throw.
bool contentFill(Rgba8View, ConstGray8View);
void spotHeal(Rgba8View, ConstGray8View, float opacity, int mode, uint32_t seed);
} // namespace compositor::graphics
