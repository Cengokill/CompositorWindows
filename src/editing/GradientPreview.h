#pragma once
#include "PixelEdits.h"

namespace compositor::editing {
// Immutable analytic view of BrushStroke.paintCanvas. Construction validates
// allocated bounds and aggregate source budgets but allocates no image pixels.
// Each endpoint/settings update starts from the original Layer and selection.
class GradientPreview {
public:
    GradientPreview(Layer original, const Document&, Point start, Point end,
        GradientSettings, Pixel foreground = {0,0,0,255},
        Pixel background = {255,255,255,255}, bool targetMask = false);
    std::shared_ptr<const LayerRenderPreview> renderPreview() const;
    // Apply only: assembles source pixels, trims image alpha and preserves mask
    // placement. No document or history state changes if assembly throws.
    Layer materializeLayer() const;
    Rect allocatedBounds() const;
    uint64_t allocatedPixelCount() const;
    uint64_t previewOwnedPixelBytes() const; // excludes shared immutable inputs
private:
    struct Impl;
    std::shared_ptr<const Impl> impl_;
};
}
