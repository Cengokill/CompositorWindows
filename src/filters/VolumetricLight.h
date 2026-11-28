#pragma once

#include "core/Document.h"
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace compositor::filters {
// Filters > Volumetric Light. `center` is the light, in document coordinates;
// the panel opens with it on the layer's brightest area. The rest are 0-100.
struct VolumetricLightSettings {
    std::optional<Point> center;
    std::array<double,3> color{1,.86,.68};
    double intensity{30},length{0},haze{60},size{20},shafts{0},distance{30},depthContrast{0};
    bool keepsSubjectClear{true};
    VolumetricLightSettings normalized() const;
    bool isIdentity() const;
    bool operator==(const VolumetricLightSettings&) const = default;
};
// Relative depth, rows top-down: 1 nearest, 0 farthest.
struct DepthMap {
    int width{},height{};
    std::vector<float> values;
    // Each value moved `amount` (0-1) of the way to its rank, keeping the order.
    DepthMap equalized(float amount) const;
};
// What the light and its haze go by, found once per source image. Masks and
// depth may have any size (an analysis copy); they are resampled to the grid.
// A missing subject means nothing blocks the light; a missing depth falls back
// to an estimate from the pixel's height in the frame.
struct VolumetricLightInputs {
    // The subject refined onto the photo's edges: it blocks the beams and thins the haze in front.
    std::shared_ptr<const GrayRaster> subject;
    // The model's own mask, firmed for the depth's edge so it does not follow busy backdrops.
    std::shared_ptr<const GrayRaster> outline;
    std::shared_ptr<const DepthMap> depth;
};
// Reused while one panel previews one source: the scene (image and subject),
// the composed depth (Depth Contrast, Keep Subject Clear) and the beams (light,
// Size, Length, Shafts). Haze, Distance, Intensity and the color only rerun the
// final combination. A different source or grid empties every level.
struct VolumetricLightCache {
    struct State;
    std::shared_ptr<State> state;
    int sceneBuilds{},depthBuilds{},beamBuilds{};
};

// Where the light starts: the brightest pixel of the largest bright area of a
// 64-pixel copy, in `image` pixel coordinates (rows top-down).
Point volumetricBrightestPoint(const Raster& image);
// Where the haze begins and where it is full, as distance (0 near, 1 far).
// `distances` are the photo's distances sorted ascending, or empty without depth.
std::pair<float,float> volumetricRamp(double distance,const std::vector<float>& distances);
// The depth the haze uses at width x height: white near, black far.
std::vector<float> composedDepth(const DepthMap& depth,const GrayRaster* outline,int width,int height,bool keepsSubjectClear,
                                 const std::function<bool()>& cancelled={},std::uint32_t workers=0);
// Opaque gray layer pixels for Extract Depth, at width x height.
std::shared_ptr<const Raster> volumetricDepthLayer(const VolumetricLightInputs&,const VolumetricLightSettings&,int width,int height,
                                                   std::uint64_t maxWorkingBytes,const std::function<bool()>& cancelled={});
// Premultiplied encoded sRGB in and out (the light color alone is linearized,
// as on the Mac). `light` is in `source` pixel coordinates (rows top-down).
// The cache is used only with an `identity`, which names the image `source`
// was made from; the cache holds it so another image cannot take its place.
std::shared_ptr<const Raster> renderVolumetricLight(const Raster& source,const VolumetricLightInputs&,Point light,
    const VolumetricLightSettings&,std::uint64_t maxWorkingBytes,const std::function<bool()>& cancelled={},
    VolumetricLightCache* cache=nullptr,const std::shared_ptr<const void>& identity={},std::uint32_t workers=0);
}
