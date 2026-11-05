#pragma once
#include <cstdint>
#include <functional>
#include <vector>

namespace compositor::filters {
// Independent of the Shadows amount. A session can reuse it for the same input grade.
struct CameraRawShadowAnalysis {
    int scale{}, width{}, height{};
    double key{};
    std::vector<float> mean, square;
};
std::uint64_t cameraRawShadowWorkingBytes(int width, int height);
// Premultiplied, encoded sRGB RGBA. Throws on cancellation or allocation failure.
void applyAdaptiveShadows(std::vector<float>& pixels, int width, int height, double amount,
                          double previewScale, const std::function<bool()>& cancelled, CameraRawShadowAnalysis* analysis = nullptr);
void applyAdaptiveShadows(std::vector<std::uint8_t>& pixels, int width, int height, double amount,
                          double previewScale, const std::function<bool()>& cancelled, CameraRawShadowAnalysis* analysis = nullptr);
}
