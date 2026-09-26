#pragma once
#include "core/Document.h"
#include "effects_tools/Levels.h"
#include <QImage>
#include <QSize>

namespace compositor {
// Display pixels only; source images and output documents keep their own grids.
QImage fittedDocumentPreview(const Document&,QSize maximum);
struct DocumentHistogramStats {
    uint64_t renderedPixels{},renderedTiles{},skippedTiles{},maxTilePixels{};
};
// Full document-pixel histogram, in the same tile/row order as levelsHistogram.
// Transparent tiles outside every raster's reduced coverage contribute zero.
effects_tools::LevelsHistogram documentLevelsHistogram(const Document&,
    const std::function<bool()>& cancelled={},DocumentHistogramStats* stats=nullptr);
struct DocumentPreviewHistogramStats {
    int width{},height{};
    uint64_t logicalPreviewPixels{},renderedPixels{},renderedRegions{},skippedRows{},skippedStrips{};
    uint64_t maxRegionPixels{},maxOutputTilePixels{},maxRetainedTilePixels{};
};
// Live Levels: compose canonical document pixels, then nearest-reduce to the
// source8000-side preview and accumulate in global row order. One256-row band
// of canonical tiles is retained; larger sparse canvases are a native extension.
effects_tools::LevelsHistogram documentLevelsPreviewHistogram(const Document&,
    const std::function<bool()>& cancelled={},DocumentPreviewHistogramStats* stats=nullptr);
std::optional<std::array<double,3>> sampleDocumentOriginalRGB(const Document&,Point);
}
