#pragma once
#include "CameraRaw.h"
#include "core/Document.h"
#include <functional>
#include <optional>
#include <string>
namespace compositor::filters {
enum class Kind { GaussianBlur,MotionBlur,AddNoise,LensCorrection,ContentAwareFill,Vignette,Bloom,TonalContrast,Dither,Scanlines,CameraRaw };
struct Settings {
    double radius{1},angle{0},distance{10},amount{10},distortion{0};
    bool gaussian{},monochromatic{};
    double vignette{},vignetteMidpoint{.5},vignetteFeather{.5};
    bool vignetteRound{true};
    double bloom{},bloomThreshold{.6},tonal{};
    int ditherLevels{},ditherStyle{};
    double scanline{},scanlineGlow{};
    Settings normalized() const;
};
struct PixelRect {int x{},y{},width{},height{};bool operator==(const PixelRect&) const=default;};
// Selection coverage is already mapped to original source pixels. Origin permits
// a selection to extend outside the original grid for Content-Aware Fill.
struct SourceSelection {std::shared_ptr<const GrayRaster> coverage;int originX{},originY{};};
struct Limits {
    std::uint64_t maxWorkingBytes{1200000000};
    std::function<bool()> cancelled;
};
struct Request {
    Kind kind{Kind::GaussianBlur};std::shared_ptr<const Raster> source;Transform transform;Settings settings;
    std::uint32_t seed{};std::optional<SourceSelection> selection;
    bool preview{};
    // Camera Raw interactive previews use this edge. Zero keeps the 2048 default.
    int previewMaxEdge{};
    // Preserve the largest padded margin while a preview panel remains open.
    double retainedBlurMargin{};
    CameraRawSettings cameraRaw;
    CameraRawView cameraRawView;
    Limits limits;
};
struct Result {
    std::shared_ptr<const Raster> raster;Transform transform;
    PixelRect sourceBounds;double previewScale{1};bool changed{};
    std::optional<CameraRawScope> cameraRawScope;
    std::string implementation;
};
double blurMargin(Kind,const Settings&);
// The direct PixelFilter.run boundary: supplied image is already padded/scaled;
// optional coverage must match that grid. Output retains dimensions.
std::shared_ptr<const Raster> runPixels(Kind,const Raster&,const Settings&,double scale,std::uint32_t seed,const GrayRaster* selection=nullptr,const Limits& = {});
// The FilterEdit boundary: grow, prepare preview, run, selection blend, final trim.
Result apply(const Request&);
// Reposition a cropped/expanded source grid without moving retained pixels.
Transform placedGrid(const Transform&,int originalWidth,int originalHeight,PixelRect);
}
