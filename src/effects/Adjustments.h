#pragma once
#include "core/Document.h"
#include <memory>
#include <functional>
#include <string>
#include <string_view>

namespace compositor::effects {
struct AdjustmentRegion { double originX{},originY{},unitsPerPixel{1},documentWidth{},documentHeight{}; int previewMaxEdge{};
    // When neededLeft is at least 0, only these source pixels of the result are kept by the
    // caller. Gaussian Blur, Bloom / Glow and Sharpen then compute only them; elsewhere their
    // result holds valid but unspecified pixels. Other kinds compute every pixel.
    int neededLeft{-1},neededTop{-1},neededRight{-1},neededBottom{-1}; };
// Uses the exact raw names from LayerAdjustment: Hue/Saturation, Levels, Curves,
// Exposure, Gradient Map, Grain. Unknown kinds throw; no silent passthrough.
std::string defaultAdjustmentJson(std::string_view kind);
void validateAdjustmentJson(std::string_view json);
int adjustmentPadding(std::string_view json);
int adjustmentStackPadding(const Document&);
// The padding of the visible adjustment layers above the layer with this id, in document units.
int adjustmentPaddingAbove(const Document&,std::string_view layerId);
bool hasLensAdjustment(const Document&);
// Selection is optional grayscale coverage already mapped to the source pixel grid.
// All-zero coverage edits nothing. Each immutable tile is copied only if its pixels change.
// Levels unpremultiplies once, matching the repaired shared C path. A second conversion would quarter a half-alpha gray.
std::shared_ptr<const Raster> applyAdjustment(std::shared_ptr<const Raster> source,
    std::string_view adjustmentJson,const GrayRaster* selection=nullptr,AdjustmentRegion region={},
    const std::function<bool()>& cancelled={});
std::shared_ptr<const DeepImage> applyAdjustment(std::shared_ptr<const DeepImage> source,
    std::string_view adjustmentJson,const GrayRaster* selection=nullptr,AdjustmentRegion region={},
    const std::function<bool()>& cancelled={});
}
