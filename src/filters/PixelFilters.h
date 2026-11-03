#pragma once
#include "CameraRaw.h"
#include "core/Document.h"
#include <functional>
#include <optional>
#include <string>
namespace compositor::filters {
enum class Kind { GaussianBlur,MotionBlur,AddNoise,LensCorrection,ContentAwareFill,Vignette,Bloom,TonalContrast,Dither,Scanlines,CameraRaw,ProgressiveBlur,RgbSplit };
struct Settings {
    double radius{1},angle{0},distance{10},amount{10},distortion{0};
    bool gaussian{},monochromatic{};
    double vignette{},vignetteMidpoint{.5},vignetteFeather{.5};
    bool vignetteRound{true};
    double bloom{},bloomThreshold{.6},tonal{};
    // Bloom / Glow adjustment: Mac amount/radius. Distinct from the Windows Bloom
    // filter's strength/threshold pair, which keeps its previous pixel behavior.
    double bloomAmount{40},bloomRadius{24};
    bool bloomGlow{};
    int ditherLevels{},ditherStyle{};
    double scanline{},scanlineGlow{};
    double progressiveFrom{},progressiveTo{20};
    std::optional<Point> progressiveStart,progressiveEnd;
    bool progressiveRadial{},progressiveInverted{};
    double rgbSplitDistance{10},rgbSplitBlur{},rgbSplitAngle{};
    bool rgbSplitRadial{};
    std::optional<Point> rgbSplitCenter;
    double rgbSplitReach{1000};
    Settings normalized() const;
};
struct PixelRect {int x{},y{},width{},height{};bool operator==(const PixelRect&) const=default;};
// Layer pixel sides that sit on the canvas edge. Blurs carry those edge pixels
// onward instead of fading into a transparent exterior.
struct LayerSides {bool left{},right{},top{},bottom{};};
// Selection coverage is already mapped to original source pixels. Origin permits
// a selection to extend outside the original grid for Content-Aware Fill.
struct SourceSelection {std::shared_ptr<const GrayRaster> coverage;int originX{},originY{};};
struct Limits {
    // Covers a 100-megapixel Apply (linear intermediates plus one tiled Raster).
    std::uint64_t maxWorkingBytes{4000000000};
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
    std::shared_ptr<const DeepImage> deepSource;
    int canvasWidth{},canvasHeight{};
};
struct Result {
    std::shared_ptr<const Raster> raster;Transform transform;
    PixelRect sourceBounds;double previewScale{1};bool changed{};
    std::optional<CameraRawScope> cameraRawScope;
    std::string implementation;
    std::shared_ptr<const DeepImage> deep;
};
// Linear RGBA/float intermediates plus one tiled Raster. Distinct from the
// 1.2 GB import/merge working caps. Default Limits permit a 100-megapixel Apply.
std::uint64_t estimatedWorkingBytes(int width,int height,std::uint64_t bytesPerPixel=24);
double blurMargin(Kind,const Settings&);
bool supportsHdr(Kind);
LayerSides canvasTouchingSides(const Transform&,int sourceWidth,int sourceHeight,int canvasWidth,int canvasHeight);
PixelRect blurWorkingBounds(Kind,const Settings&,int width,int height,double retainedBlurMargin,LayerSides);
// The direct PixelFilter.run boundary: supplied image is already padded/scaled;
// optional coverage must match that grid. Output retains dimensions.
std::shared_ptr<const Raster> runPixels(Kind,const Raster&,const Settings&,double scale,std::uint32_t seed,const GrayRaster* selection=nullptr,const Limits& = {},LayerSides={});
std::shared_ptr<const DeepImage> runDeepPixels(Kind,const DeepImage&,const Settings&,double scale,std::uint32_t seed,const GrayRaster* selection=nullptr,const Limits& = {},const CameraRawSettings& = {},const CameraRawView& = {},LayerSides={});
// The FilterEdit boundary: grow, prepare preview, run, selection blend, final trim.
Result apply(const Request&);
// Reposition a cropped/expanded source grid without moving retained pixels.
Transform placedGrid(const Transform&,int originalWidth,int originalHeight,PixelRect);
}
