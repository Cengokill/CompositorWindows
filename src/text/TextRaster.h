#pragma once
#include "core/Document.h"
#include <memory>

namespace compositor::text {
struct RasterizedText {std::shared_ptr<const Raster> raster;int width{},height{};};
struct TextCaret { float x{},top{},height{}; int line{}; };
// One caret per UTF-16 boundary, including the position after the last unit.
// Coordinates match the raster: DirectWrite's origin plus the 12-pixel pad.
struct TextLayout {
    std::shared_ptr<const Raster> raster;
    int width{},height{};
    std::vector<TextCaret> carets;
};
RasterizedText rasterize(const TextContent& text);
// Empty text has a caret and no raster. Other invalid text throws, as rasterize does.
// Set includeRaster to false when only caret geometry is needed; this avoids
// creating a full WIC/D2D bitmap for selection and caret movement.
TextLayout layoutText(const TextContent& text,bool includeRaster=true);
}
