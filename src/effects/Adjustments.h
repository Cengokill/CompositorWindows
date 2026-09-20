#pragma once
#include "core/Document.h"
#include <memory>
#include <string>
#include <string_view>

namespace compositor::effects {
struct AdjustmentRegion { double originX{},originY{},unitsPerPixel{1}; };
// Uses the exact raw names from LayerAdjustment: Hue/Saturation, Levels, Curves,
// Exposure, Gradient Map, Grain. Unknown kinds throw; no silent passthrough.
std::string defaultAdjustmentJson(std::string_view kind);
void validateAdjustmentJson(std::string_view json);
// Selection is optional grayscale coverage already mapped to the source pixel grid.
// All-zero coverage edits nothing. Each immutable tile is copied only if its pixels change.
// Levels intentionally retains the pinned Swift wrapper's documented double-alpha conflict.
std::shared_ptr<const Raster> applyAdjustment(std::shared_ptr<const Raster> source,
    std::string_view adjustmentJson,const GrayRaster* selection=nullptr,AdjustmentRegion region={});
}
