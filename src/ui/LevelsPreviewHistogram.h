#pragma once
#include "effects_tools/Levels.h"
namespace compositor {
struct LevelsPreviewHistogramResult {
    effects_tools::LevelsHistogram bins{};
    int width{},height{};
};
// Levels.swift135–151 + BrushRaster.draw(.none): cap the original source at
// 8000 on its longer side, then accumulate contiguous row-major RGBA samples.
LevelsPreviewHistogramResult levelsPreviewHistogram(const Raster&,const Transform&,
    const Document&,const std::function<bool()>& cancelled={});
}
