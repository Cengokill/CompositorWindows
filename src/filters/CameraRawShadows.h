#pragma once
#include <functional>
#include <vector>

namespace compositor::filters {
// Premultiplied, encoded sRGB RGBA. Throws on cancellation or allocation failure.
void applyAdaptiveShadows(std::vector<float>& pixels, int width, int height, double amount,
                          double previewScale, const std::function<bool()>& cancelled);
}
