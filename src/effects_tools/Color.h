#pragma once
#include "core/Document.h"
#include <optional>
#include <string_view>

namespace compositor::effects_tools {
struct PaletteColor {
    double red{},green{},blue{};
    PaletteColor quantized() const;
    std::string hex() const;
    static std::optional<PaletteColor> fromHex(std::string_view);
    bool operator==(const PaletteColor&) const = default;
};
struct PickerHSB {
    double hue{},saturation{},brightness{};
    PaletteColor rgb() const;
    void setRGB(PaletteColor); // preserves hue for gray and saturation for black
    bool operator==(const PickerHSB&) const = default;
};
std::optional<PaletteColor> sampleCompositeColor(const Document&,Point,const IRasterBackend&);
struct SampleRingGeometry {
    Point origin;
    double width{116},height{116},pathInset{15},outlineWidth{24},colorWidth{16};
    double splitY{58}; // sampled color above; pre-drag original below
};
SampleRingGeometry sampleRingGeometry(Point viewPoint);
}
