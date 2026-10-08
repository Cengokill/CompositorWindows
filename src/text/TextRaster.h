#pragma once
#include "core/Document.h"
#include <memory>

namespace compositor::text {
struct RasterizedText {std::shared_ptr<const Raster> raster;int width{},height{};};
struct TextCaret { float x{},top{},height{}; int line{}; };
struct TextCluster { float x{},top{},width{},height{}; int start{},length{},line{}; };
// One caret per UTF-16 boundary, including the position after the last unit.
// Coordinates match the raster: DirectWrite's origin plus the 12-pixel pad.
struct TextLayout {
    std::shared_ptr<const Raster> raster;
    int width{},height{};
    std::vector<TextCaret> carets;
    std::vector<TextCluster> clusters;
};
RasterizedText rasterize(const TextContent& text);
// Empty text has a caret and no raster. Other invalid text throws, as rasterize does.
TextLayout layoutText(const TextContent& text);
}
