#pragma once

#include "core/Document.h"
#include <array>
#include <cstdint>
#include <functional>
#include <memory>

namespace compositor::filters {
struct RimLight {
    double angle{135};
    std::array<double, 3> color{1, .86, .7};
    double strength{50}, width{30}, choke{};
};
struct RimLightSettings {
    std::array<RimLight, 3> lights{{RimLight{},RimLight{-45,{.7,.84,1},50,30,0},RimLight{135,{1,1,1},50,30,0}}};
    int count{1};
    double glow{10};
    RimLightSettings normalized() const;
    bool isIdentity() const;
};

// Source and coverage share a pixel grid. Source uses premultiplied RGBA.
std::shared_ptr<const Raster> renderRimLight(const Raster&, const GrayRaster&,
    const RimLightSettings&, std::uint64_t maxWorkingBytes,
    const std::function<bool()>& cancelled = {});
}
