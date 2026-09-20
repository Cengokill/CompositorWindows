#pragma once
#include "core/Document.h"
#include <array>
#include <optional>
#include <span>
#include <string_view>

namespace compositor::effects_tools {
enum class LevelsChannel { RGB, Red, Green, Blue };
enum class LevelsAuto { Contrast, Color, Neutral };
enum class LevelsSample { Black, Gray, White };
struct LevelRange {
    double black{},gamma{1},white{255},outputBlack{},outputWhite{255};
    LevelRange normalized() const;
    double apply(double value) const;
    bool operator==(const LevelRange&) const = default;
};
struct LevelsSettings {
    LevelsChannel channel{LevelsChannel::RGB};
    std::array<LevelRange,4> ranges;
    bool identity() const;
    double apply(double value,LevelsChannel) const;
    bool operator==(const LevelsSettings&) const = default;
};
using LevelsHistogram=std::array<std::array<double,256>,4>;
LevelsHistogram levelsHistogram(const Raster&,const GrayRaster* mappedSelection = nullptr);
double histogramDisplayScale(std::span<const double> bins);
LevelsSettings autoLevels(const LevelsHistogram&,LevelsAuto);
LevelsSettings sampleLevels(const LevelsSettings&,std::array<double,3> originalStraightRGB,LevelsSample);
std::optional<std::array<double,3>> sampleOriginalRGB(const Raster&,const Transform&,
    Point documentPoint,int canvasWidth,int canvasHeight);
std::array<double,3> levelsInputHandles(LevelRange);
LevelRange moveLevelsHandle(LevelRange,int index,bool output,double position0To255);
LevelsSettings levelsFromAdjustmentJson(std::string_view);
std::string withLevelsSettings(std::string_view fullAdjustmentJson,const LevelsSettings&);
}
