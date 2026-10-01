#pragma once
#include "core/Document.h"
#include <memory>

namespace compositor::text {
struct RasterizedText {std::shared_ptr<const Raster> raster;int width{},height{};};
RasterizedText rasterize(const TextContent& text);
}
