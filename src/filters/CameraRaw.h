#pragma once
#include "core/Document.h"
#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace compositor::filters {
enum class CameraRawWhiteBalance { Custom, Auto };
enum class CameraRawGlowStyle { Diffusion, Bloom, Halation };
enum class CameraRawVignetteStyle { HighlightPriority, ColorPriority, PaintOverlay };
enum class CameraRawUpright { Off, Guided };
enum class CameraRawProjection { Perspective, Rectilinear };
enum class CameraRawProcess { Version1 = 1, Version2, Version3, Version4, Version5, Version6 };
enum class CameraRawCurvePage { Parametric, Point };
enum class CameraRawPointChannel { Rgb, Red, Green, Blue };
enum class CameraRawMixerPage { Hsl, Color, Point };
enum class CameraRawMixerTab { Hue, Saturation, Luminance };
enum class CameraRawGradePage { ThreeWay, Shadows, Midtones, Highlights, Global };

struct CameraRawPointColor {
    double hue{}, saturation{}, luminance{};
    double hueShift{}, saturationShift{}, luminanceShift{};
    double hueRange{30}, saturationRange{0.4}, luminanceRange{0.4};
    bool visualize{};
    CameraRawPointColor normalized() const;
    bool operator==(const CameraRawPointColor&) const = default;
};

struct CameraRawCurveSettings {
    double shadows{}, darks{}, lights{}, highlights{};
    double shadowSplit{25}, darkSplit{50}, lightSplit{75};
    std::vector<Point> rgb{{0, 0}, {1, 1}};
    std::vector<Point> red{{0, 0}, {1, 1}};
    std::vector<Point> green{{0, 0}, {1, 1}};
    std::vector<Point> blue{{0, 0}, {1, 1}};
    double refineSaturation{};
    static std::vector<Point> linear();
    static std::vector<Point> mediumContrast();
    static std::vector<Point> strongContrast();
    static bool isLinear(const std::vector<Point>&);
    bool adjusts() const;
    double parametric(double tone) const;
    std::array<float, 256> toneTable() const;
    std::array<float, 256> channelTable(const std::vector<Point>&) const;
    int region(double tone) const;
    CameraRawCurveSettings nudged(CameraRawPointChannel channel, double tone, double delta) const;
    CameraRawCurveSettings normalized() const;
    bool operator==(const CameraRawCurveSettings&) const = default;
};

struct CameraRawMixerSettings {
    static constexpr std::array<const char*, 8> names{"Reds", "Oranges", "Yellows", "Greens", "Aquas", "Blues", "Purples", "Magentas"};
    static constexpr std::array<double, 8> centers{0, 30, 60, 120, 180, 240, 270, 300};
    std::array<double, 8> hue{}, saturation{}, luminance{};
    std::vector<CameraRawPointColor> points;
    bool adjusts() const;
    static std::array<double, 8> weights(double degrees);
    std::array<float, 24> mixerFloats() const;
    std::vector<float> pointFloats() const;
    CameraRawMixerSettings normalized() const;
    bool operator==(const CameraRawMixerSettings&) const = default;
};

struct CameraRawGradeWheel {
    double hue{}, saturation{}, luminance{};
    CameraRawGradeWheel normalized() const;
    bool operator==(const CameraRawGradeWheel&) const = default;
};

struct CameraRawGradingSettings {
    CameraRawGradeWheel shadows, midtones, highlights, global;
    double blending{50}, balance{};
    bool adjusts() const;
    std::array<float, 12> gradeFloats() const;
    CameraRawGradingSettings normalized() const;
    bool operator==(const CameraRawGradingSettings&) const = default;
};

struct CameraRawDetailSettings {
    double sharpenAmount{}, sharpenRadius{10}, sharpenDetail{25}, sharpenMasking{};
    double noiseLuminance{}, noiseLuminanceDetail{50}, noiseLuminanceContrast{};
    double noiseColor{}, noiseColorDetail{50}, noiseColorSmoothness{50};
    bool adjusts() const;
    CameraRawDetailSettings normalized() const;
    bool operator==(const CameraRawDetailSettings&) const = default;
};

struct CameraRawOpticsSettings {
    bool removeChromaticAberration{}, enableLensProfile{};
    double profileDistortion{100}, profileVignetting{100}, distortion{};
    double purpleAmount{}, purpleHueLow{270}, purpleHueHigh{310};
    double greenAmount{}, greenHueLow{60}, greenHueHigh{120};
    double vignetteAmount{}, vignetteMidpoint{50};
    bool adjusts() const;
    double distortionK(double profileStrength) const;
    CameraRawOpticsSettings normalized() const;
    bool operator==(const CameraRawOpticsSettings&) const = default;
};

struct CameraRawGeometryGuide {
    double startX{}, startY{}, endX{}, endY{};
    bool operator==(const CameraRawGeometryGuide&) const = default;
};

struct CameraRawGeometrySettings {
    CameraRawUpright upright{CameraRawUpright::Off};
    CameraRawProjection projection{CameraRawProjection::Perspective};
    double vertical{}, horizontal{}, rotate{}, aspect{}, scale{}, offsetX{}, offsetY{};
    bool constrainCrop{};
    std::vector<CameraRawGeometryGuide> guides;
    bool adjusts() const;
    CameraRawGeometrySettings normalized() const;
    bool operator==(const CameraRawGeometrySettings&) const = default;
};

struct CameraRawCalibrationSettings {
    CameraRawProcess process{CameraRawProcess::Version6};
    double shadowTint{}, redHue{}, redSaturation{}, greenHue{}, greenSaturation{}, blueHue{}, blueSaturation{};
    bool adjusts() const;
    static const char* summary(CameraRawProcess);
    CameraRawCalibrationSettings normalized() const;
    bool operator==(const CameraRawCalibrationSettings&) const = default;
};

struct CameraRawGroupEyes {
    bool light{true}, color{true}, effects{true}, curve{true}, mixer{true}, grading{true};
    bool detail{true}, optics{true}, geometry{true}, calibration{true};
    bool operator==(const CameraRawGroupEyes&) const = default;
};

// Preview-only paint. OK leaves every field at these defaults.
struct CameraRawView {
    int clipping{};
    double scale{1};
    std::uint32_t seed{};
    int visualizePointColor{-1};
    bool sharpenMask{};
    bool shadowOverlay{};
    bool highlightOverlay{};
};

struct CameraRawScope {
    std::array<double, 256> red{}, green{}, blue{};
    std::array<double, 64 * 64> vectorscope{};
    double peak() const;
    static CameraRawScope make(const std::uint8_t* rgba, int width, int height);
};

struct CameraRawSettings {
    static constexpr double temperatureGain = 0.35;
    static constexpr double tintRedBlue = 0.15;
    static constexpr double tintGreen = 0.30;
    CameraRawWhiteBalance whiteBalance{CameraRawWhiteBalance::Custom};
    double temperature{}, tint{}, exposure{}, contrast{}, highlights{}, shadows{}, whites{}, blacks{};
    double vibrance{}, saturation{}, texture{}, clarity{}, dehaze{};
    double glow{}, glowRange{}, glowSpread{}, glowWarmth{};
    CameraRawGlowStyle glowStyle{CameraRawGlowStyle::Diffusion};
    double vignetteAmount{}, vignetteMidpoint{50}, vignetteRoundness{}, vignetteFeather{50}, vignetteHighlights{};
    CameraRawVignetteStyle vignetteStyle{CameraRawVignetteStyle::HighlightPriority};
    double grainAmount{}, grainSize{25}, grainRoughness{50};
    CameraRawCurveSettings curve;
    CameraRawMixerSettings mixer;
    CameraRawGradingSettings grading;
    CameraRawDetailSettings detail;
    CameraRawOpticsSettings optics;
    CameraRawGeometrySettings geometry;
    CameraRawCalibrationSettings calibration;
    bool adjustsLight() const;
    bool adjustsColor() const;
    bool adjustsEffects() const;
    bool isIdentity() const;
    bool isValid() const;
    double grainKernelSize() const;
    struct Gains { double red{1}, green{1}, blue{1}; };
    Gains gains() const;
    CameraRawSettings normalized() const;
    CameraRawSettings applying(const CameraRawGroupEyes&) const;
    static std::optional<std::pair<double, double>> neutralizeLinear(double red, double green, double blue);
    static std::optional<std::pair<double, double>> neutralizeStraight(double red, double green, double blue);
    static std::optional<std::pair<double, double>> autoBalance(const std::uint8_t* rgba, int width, int height, int stride);
    bool operator==(const CameraRawSettings&) const = default;
};

struct CameraRawRender {
    std::shared_ptr<const Raster> raster;
    CameraRawScope scope;
    bool changed{};
};
// Grade, then selection blend, then preview-only paint. Scope is the grade before that paint.
CameraRawRender renderCameraRaw(const Raster& source, const CameraRawSettings& settings, const CameraRawView& view, const GrayRaster* selection);
}
