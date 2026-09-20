#pragma once
#include "Selection.h"
#include <string_view>

namespace compositor::editing {
enum class PixelEdit { Fill, Clear, Invert };
// Fill color is straight RGB (alpha ignored, matching the opaque source palette).
// For masks its red channel is gray. Clear requires a present selection and uses
// maskBackground on masks. Returns the original value when no pixels change.
// Fill can grow an image's source grid; callers must replace the complete Layer.
Layer editLayer(const Layer&, const Document&, PixelEdit, Pixel color = {0,0,0,255},
    bool targetMask = false, uint8_t maskBackground = 255);
enum class GradientShape { Linear, Radial };
enum class GradientStyle { ForegroundToBackground, ForegroundToTransparent };
struct GradientSettings {
    GradientShape shape{GradientShape::Linear};
    GradientStyle style{GradientStyle::ForegroundToTransparent};
    bool reversed{};
    double opacity{1};
};
// Preview every endpoint/palette change against the ORIGINAL Layer. This pure
// operation never accumulates prior previews; cancel discards the returned value.
// A line shorter than 0.5 document pixels leaves the original unchanged.
Layer gradientLayer(const Layer&, const Document&, Point start, Point end, GradientSettings,
    Pixel foreground = {0,0,0,255}, Pixel background = {255,255,255,255}, bool targetMask = false);
struct CopiedPixels { std::shared_ptr<const Raster> raster; Point origin; };
// null layerID means Copy Merged. Copy Current draws only the raster and transform,
// ignoring its masks, opacity, blend and visibility, matching SelectionClipboard.
// Optional outline supplies source vector bounds; otherwise coverage bounds apply.
std::optional<CopiedPixels> copyPixels(const Document&, std::optional<std::string_view> layerID,
    const IRasterBackend&, const SelectionOutline* outline = nullptr, bool targetMask = false);
}
