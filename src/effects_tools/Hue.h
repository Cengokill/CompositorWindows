#pragma once
#include "Color.h"
#include <array>
#include <optional>
#include <string_view>

namespace compositor::effects_tools {
enum class ColorRange { Master,Reds,Yellows,Greens,Cyans,Blues,Magentas };
enum class HueSample { Replace,Add,Remove };
struct HueBand {
    double falloffStart{},rangeStart{},rangeEnd{},falloffEnd{};
    static double forward(double from,double to);
    double weight(double hue) const;
    HueBand centered(double hue) const;
    void include(double hue);
    void exclude(double hue);
    void setHandle(int index,double degrees);
    std::array<double,4> handles()const{return {falloffStart,rangeStart,rangeEnd,falloffEnd};}
    bool operator==(const HueBand&)const=default;
private:
    void normalize();
};
struct RangeAdjustment {double hue{},saturation{},lightness{};bool operator==(const RangeAdjustment&)const=default;};
struct HueSettings {
    ColorRange range{ColorRange::Master};
    bool colorize{},invertRange{};
    std::array<RangeAdjustment,7> adjustments{};
    std::array<HueBand,7> bands;
    HueSettings();
    double weight(ColorRange,double hue) const;
    double shiftedHue(double hue) const;
    bool identity() const;
    static HueSettings colorizeStart();
    bool operator==(const HueSettings&)const=default;
};
HueBand defaultHueBand(ColorRange);
std::optional<double> sampledHue(PaletteColor);
HueSettings sampleHueRange(const HueSettings&,double hue,HueSample);
struct HueTargetDrag {ColorRange range;double hue,saturation;};
std::optional<HueTargetDrag> beginHueTargeting(HueSettings&,PaletteColor);
HueSettings dragHueTargeting(const HueSettings&,HueTargetDrag,double viewDelta,bool adjustsHue);
int nearestHueHandle(HueBand,double degrees);
HueSettings hueFromAdjustmentJson(std::string_view);
std::string withHueSettings(std::string_view fullAdjustmentJson,const HueSettings&);
}
