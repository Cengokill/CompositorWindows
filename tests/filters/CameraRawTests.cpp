#include "filters/CameraRaw.h"
#include "filters/PixelFilters.h"
#include "core/DeepImage.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <limits>
#include <stdexcept>
using namespace compositor;
using namespace compositor::filters;
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
std::shared_ptr<const Raster> image(int w, int h, const std::function<Pixel(int, int)>& get) {
    std::vector<std::uint8_t> pixels(std::size_t(w) * h * 4);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const auto pixel = get(x, y);
        const auto i = (std::size_t(y) * w + x) * 4;
        pixels[i] = pixel.r; pixels[i + 1] = pixel.g; pixels[i + 2] = pixel.b; pixels[i + 3] = pixel.a;
    }
    return Raster::fromRgba(w, h, pixels.data(), std::size_t(w) * 4);
}
std::shared_ptr<const Raster> solid(int w, int h, Pixel pixel) { return image(w, h, [pixel](int, int) { return pixel; }); }
__declspec(noinline) Pixel rendered(const Raster& source, CameraRawSettings settings, CameraRawView view = {}) {
    auto result = renderCameraRaw(source, settings, view, nullptr);
    return result.raster ? result.raster->pixel(0, 0) : source.pixel(0, 0);
}
__declspec(noinline) std::shared_ptr<const Raster> paint(const std::shared_ptr<const Raster>& source, CameraRawSettings settings, CameraRawView view = {}) {
    auto result = renderCameraRaw(*source, settings, view, nullptr);
    return result.raster ? result.raster : source;
}
__declspec(noinline) bool didChange(const std::shared_ptr<const Raster>& source, CameraRawSettings settings, CameraRawView view = {}) {
    return renderCameraRaw(*source, settings, view, nullptr).changed;
}
int chroma(Pixel pixel) { return std::max(pixel.r, std::max(pixel.g, pixel.b)) - std::min(pixel.r, std::min(pixel.g, pixel.b)); }
bool gray(Pixel pixel) { return pixel.r == pixel.g && pixel.g == pixel.b; }
// Reproduces the deterministic seven-octave landscape from upstream
// CompositorTests/CameraRawTests.swift at 735c2cc23c0efe7c2c2f85a66741b253e053cf2b.
std::vector<double> shadowField(int w, int h, std::uint64_t seed) {
    std::vector<double> out(std::size_t(w) * h); double amplitude = 1, total = 0;
    for (int octave = 0; octave < 7; ++octave) {
        const int cell = std::max(1, std::max(w, h) >> (octave + 2));
        const int gw = w / cell + 2, gh = h / cell + 2;
        std::vector<double> grid(std::size_t(gw) * gh);
        for (double& v : grid) { seed = seed * 6364136223846793005ULL + 1442695040888963407ULL; v = double(seed >> 11) / double(1ULL << 53); }
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
            const double fx = double(x) / cell, fy = double(y) / cell;
            const int xx = int(fx), yy = int(fy); const double tx = fx - xx, ty = fy - yy;
            const double top = grid[yy * gw + xx] * (1 - tx) + grid[yy * gw + xx + 1] * tx;
            const double bottom = grid[(yy + 1) * gw + xx] * (1 - tx) + grid[(yy + 1) * gw + xx + 1] * tx;
            out[std::size_t(y) * w + x] += amplitude * (top * (1 - ty) + bottom * ty);
        }
        total += amplitude; amplitude *= .6;
    }
    for (double& v : out) v /= total;
    return out;
}
std::shared_ptr<const Raster> shadowLandscape(int w) {
    const int h = w * 2 / 3; const auto clouds = shadowField(w, h, 1), lay = shadowField(w, h, 2);
    return image(w, h, [&](int x, int y) {
        const auto i = std::size_t(y) * w + x;
        const double horizon = h * (.5 + .25 * (lay[x] - .5));
        const double sky = 140 + 80 * (1 - y / horizon) + 60 * (clouds[i] - .5);
        const double ground = 4 + 70 * std::pow(clouds[i], 2.2) + 20 * (lay[i] - .5);
        const auto v = std::uint8_t(std::clamp(std::lround(y < horizon ? sky : ground), 0L, 255L));
        return Pixel{v, v, v, 255};
    });
}
double shadowBandMean(const Raster& raster, bool top) {
    double sum = 0; const int rows = raster.height / 5;
    for (int y = 0; y < rows; ++y) for (int x = 0; x < raster.width; ++x)
        sum += raster.pixel(x, top ? y : raster.height - 1 - y).g;
    return sum / (rows * raster.width);
}

}
void run() {
        auto gray128 = solid(4, 4, {128, 128, 128, 200});
        require(rendered(*gray128, {}).a == 200 && rendered(*gray128, {}) == Pixel{128, 128, 128, 200}, "identity keeps color and alpha");
        CameraRawSettings invalid;
        invalid.exposure = std::numeric_limits<double>::quiet_NaN();
        invalid.temperature = 400;
        require(!invalid.isValid(), "NaN exposure and temperature 400 are invalid");
        auto normalized = invalid.normalized();
        require(normalized.exposure == 0 && normalized.temperature == 100, "normalization clamps non-finite exposure and temperature 400");

        CameraRawSettings exposure;
        exposure.exposure = 1;
        auto plus = rendered(*gray128, exposure);
        require(plus.r > 165 && plus.r < 190 && gray(plus) && plus.a == 200, "one stop on mid gray lands near 176 and keeps alpha");
        auto dark = solid(2, 2, {64, 64, 64, 255});
        auto bright = solid(2, 2, {192, 192, 192, 255});
        CameraRawSettings contrast;
        contrast.contrast = 100;
        require(rendered(*dark, contrast).r < 64 && rendered(*bright, contrast).r > 192, "contrast +100 drives dark and bright apart");
        contrast.contrast = -100;
        auto crushedDark = rendered(*dark, contrast).r, crushedBright = rendered(*bright, contrast).r;
        require(std::abs(crushedDark - 128) < 8 && std::abs(crushedBright - 128) < 8, "contrast -100 meets near mid gray");

        auto mid = solid(2, 2, {128, 128, 128, 255});
        CameraRawSettings tonal;
        tonal.highlights = -100;
        require(rendered(*bright, tonal).r < 192 && rendered(*mid, tonal).r == 128, "highlights -100 darkens a bright tone and leaves mid gray");
        tonal = {};
        tonal.whites = 100;
        require(rendered(*bright, tonal).r > 192 && rendered(*mid, tonal).r == 128 && rendered(*solid(2, 2, {250, 250, 250, 255}), tonal).r == 255, "whites +100 lifts a bright tone, clips near white, and leaves mid gray");
        CameraRawView clips;
        clips.clipping = 1;
        tonal = {};
        require(paint(solid(2, 2, {255, 255, 255, 255}), {}, clips)->pixel(0, 0) == Pixel{255, 255, 255, 255}, "highlight clip view is white on a clipped pixel");
        require(paint(mid, {}, clips)->pixel(0, 0) == Pixel{0, 0, 0, 255}, "highlight clip view is black on mid gray");
        auto shadowTone = solid(2, 2, {40, 40, 40, 255});
        tonal = {};
        tonal.shadows = 100;
        require(rendered(*shadowTone, tonal).r > 40, "shadows +100 opens a dark tone");
        tonal = {};
        tonal.blacks = -100;
        require(rendered(*shadowTone, tonal).r < 40, "blacks -100 crushes a dark tone");
        clips.clipping = 2;
        require(paint(solid(2, 2, {0, 0, 0, 255}), {}, clips)->pixel(0, 0) == Pixel{0, 0, 0, 255}, "shadow clip view is black on a crushed pixel");
        require(paint(mid, {}, clips)->pixel(0, 0) == Pixel{255, 255, 255, 255}, "shadow clip view is white on mid gray");

        // Reference means and tolerances are from the upstream Photoshop fixture,
        // not a claim that Photoshop was run in this test environment.
        for (int width : {600, 1200}) {
            auto landscape = shadowLandscape(width);
            for (double amount : {100., 50., -100.}) {
                CameraRawSettings local; local.shadows = amount;
                auto out = paint(landscape, local);
                const double target = width == 1200 ? (amount == 100 ? 59.0 : amount == 50 ? 34.9 : 4.2)
                                                    : (amount == 100 ? 51.0 : amount == 50 ? 32.2 : 5.5);
                const double tolerance = width == 600 && amount == 100 ? 10 : 4;
                require(std::abs(shadowBandMean(*out, false) - target) <= tolerance, "adaptive shadows diverge from upstream ground fixture");
                const double sky = amount == 100 ? 205.9 : amount == 50 ? 205.3 : 203.6;
                require(std::abs(shadowBandMean(*out, true) - sky) <= 2, "adaptive shadows diverge from upstream sky fixture");
            }
        }
        auto landscape = shadowLandscape(600);
        CameraRawSettings adaptive; adaptive.shadows = 100;
        auto openedLandscape = paint(landscape, adaptive);
        auto flatDark = solid(600, 400, {20, 20, 20, 255});
        for (double amount : {-100., 100.}) {
            CameraRawSettings settings; settings.shadows = amount;
            const auto baseline = paint(flatDark, settings);
            for (std::uint8_t alpha : {std::uint8_t(1), std::uint8_t(255)}) {
                auto speck = image(600, 400, [alpha](int x, int y) {
                    return x == 300 && y == 200 ? Pixel{alpha, alpha, alpha, alpha} : Pixel{20, 20, 20, 255};
                });
                const auto adjusted = paint(speck, settings);
                require(std::abs(int(adjusted->pixel(0, 0).r) - int(baseline->pixel(0, 0).r)) <= 1,
                        "an isolated bright pixel must not determine the image-wide shadow adjustment");
            }
        }
        require(shadowBandMean(*openedLandscape, false) - shadowBandMean(*landscape, false) >
                rendered(*flatDark, adaptive).r - 20 + 10, "surrounding sky must affect the shadow lift");
        CameraRawView reduced; reduced.scale = .5;
        auto scaledSmall = paint(landscape, adaptive, reduced);
        auto ordinarySmall = paint(landscape, adaptive);
        require(std::abs(shadowBandMean(*scaledSmall, false) - shadowBandMean(*ordinarySmall, false)) > 1,
                "preview must select the coefficient set for the original layer size");
        Request previewRequest; previewRequest.kind = Kind::CameraRaw;
        previewRequest.source = shadowLandscape(1200); previewRequest.cameraRaw = adaptive;
        previewRequest.preview = true; previewRequest.previewMaxEdge = 600;
        auto reducedResult = apply(previewRequest);
        previewRequest.preview = false; auto fullResult = apply(previewRequest);
        require(reducedResult.raster->width == 600 && fullResult.raster->width == 1200 &&
                std::abs(shadowBandMean(*reducedResult.raster, false) - shadowBandMean(*fullResult.raster, false)) < 4,
                "production preview and full render must use the original layer size");
        auto selectedSource = shadowLandscape(600);
        GrayRaster selection{600, 400, std::vector<std::uint8_t>(600 * 400)};
        for (int y = 200; y < 400; ++y) for (int x = 0; x < 600; ++x) selection.pixels[std::size_t(y) * 600 + x] = 255;
        auto selected = renderCameraRaw(*selectedSource, adaptive, {}, &selection);
        require(selected.raster->pixel(50, 20) == selectedSource->pixel(50, 20) &&
                selected.raster->pixel(50, 350) != selectedSource->pixel(50, 350), "adaptive shadows must honor selection coverage");
        bool cancelled = false;
        int polls = 0;
        try { renderCameraRaw(*selectedSource, adaptive, {}, nullptr, [&] { return ++polls > 8; }); }
        catch (const std::exception&) { cancelled = true; }
        require(cancelled, "adaptive shadows must stop a cancelled render");
        auto ramp = image(256, 1, [](int x, int) { const auto v = std::uint8_t(x); return Pixel{v, v, v, 255}; });
        for (double amount : {-100., 100.}) {
            CameraRawSettings rampSettings; rampSettings.shadows = amount;
            auto adjusted = paint(ramp, rampSettings);
            for (int x = 1; x < 256; ++x)
                require(adjusted->pixel(x, 0).r >= adjusted->pixel(x - 1, 0).r, "adaptive shadows must preserve ramp order");
            require(adjusted->pixel(0, 0).r <= 2 && adjusted->pixel(255, 0).r >= 253, "adaptive shadows must preserve ramp endpoints");
        }
        auto translucent = solid(4, 4, {20, 20, 20, 100});
        auto translucentOut = paint(translucent, adaptive);
        require(translucentOut->pixel(0, 0).a == 100 && translucentOut->pixel(0, 0).r <= 100, "adaptive shadows preserve premultiplied alpha");

        CameraRawSettings color;
        color.temperature = 100;
        auto warmed = rendered(*mid, color);
        require(warmed.r > 128 && warmed.b < 128, "temperature +100 raises red and lowers blue");
        color = {};
        color.tint = 100;
        auto tinted = rendered(*mid, color);
        require(tinted.g < tinted.r && tinted.g < tinted.b, "tint +100 lowers green below red and blue");
        auto dull = solid(2, 2, {180, 220, 180, 255});
        auto saturated = solid(2, 2, {0, 255, 0, 255});
        auto skin = solid(2, 2, {200, 180, 160, 255});
        color = {};
        color.vibrance = 100;
        const int dullMove = chroma(rendered(*dull, color)) - chroma(dull->pixel(0, 0));
        const int saturatedMove = chroma(rendered(*saturated, color)) - chroma(saturated->pixel(0, 0));
        const int skinMove = chroma(rendered(*skin, color)) - chroma(skin->pixel(0, 0));
        require(dullMove > saturatedMove && dullMove > skinMove && chroma(dull->pixel(0, 0)) == chroma(skin->pixel(0, 0)), "vibrance moves dull green more than saturated green or skin at the same chroma");
        color = {};
        color.saturation = 100;
        auto muted = solid(2, 2, {140, 100, 100, 255});
        require(chroma(rendered(*muted, color)) > chroma(muted->pixel(0, 0)) * 3 / 2, "saturation +100 increases chroma");
        auto cast = Pixel{180, 140, 100, 255};
        auto balance = CameraRawSettings::neutralizeStraight(cast.r / 255., cast.g / 255., cast.b / 255.);
        require(balance.has_value(), "neutralize returns temperature and tint");
        color = {};
        color.temperature = balance->first;
        color.tint = balance->second;
        require(chroma(rendered(*solid(2, 2, cast), color)) * 2 < chroma(cast), "neutralize cuts chroma by more than half");
        auto scanned = CameraRawSettings::autoBalance(solid(2, 2, cast)->rgba().data(), 2, 2, 8);
        require(scanned.has_value(), "auto balance scans opaque pixels");
        color.temperature = scanned->first;
        color.tint = scanned->second;
        require(chroma(rendered(*solid(2, 2, cast), color)) * 2 < chroma(cast), "auto balance cuts chroma by more than half");
        auto clear = solid(2, 2, {180, 140, 100, 0});
        require(!CameraRawSettings::autoBalance(clear->rgba().data(), 2, 2, 8), "a fully transparent layer does not produce a balance");

        CameraRawGroupEyes eyes;
        eyes.light = false;
        eyes.color = false;
        CameraRawSettings hidden;
        hidden.exposure = 1;
        hidden.temperature = 40;
        hidden.texture = 80;
        hidden.grainAmount = 40;
        eyes.effects = false;
        require(hidden.applying(eyes).isIdentity(), "hidden eyes produce an identity grade");
        require(!didChange(mid, hidden.applying(eyes)), "rendering hidden eyes leaves the source");

        auto step = image(16, 4, [](int x, int) { return x < 8 ? Pixel{32, 32, 32, 255} : Pixel{220, 220, 220, 255}; });
        CameraRawSettings texture;
        texture.texture = 100;
        auto textured = paint(step, texture);
        require(didChange(step, texture) && textured->pixel(7, 1) != step->pixel(7, 1), "texture changes the pixel next to a step");
        require(textured->pixel(0, 1) == step->pixel(0, 1), "texture leaves a pixel eight columns from the step");
        texture = {};
        texture.clarity = 100;
        auto clarified = paint(step, texture);
        require(textured->pixel(4, 1) == step->pixel(4, 1) && clarified->pixel(4, 1) != step->pixel(4, 1), "clarity reaches a pixel texture leaves alone");
        CameraRawView scaled;
        scaled.scale = 8;
        require(paint(step, texture, scaled)->pixel(0, 1) != clarified->pixel(0, 1), "preview scale changes the clarity radius");

        auto grainField = solid(8, 8, {128, 128, 128, 255});
        CameraRawSettings grain;
        grain.grainAmount = 80;
        CameraRawView seeded;
        seeded.seed = 17;
        auto first = paint(grainField, grain, seeded);
        auto second = paint(grainField, grain, seeded);
        require(first->pixel(3, 3) == second->pixel(3, 3), "the same grain seed is stable");
        require(gray(first->pixel(3, 3)) && first->pixel(3, 3) != grainField->pixel(3, 3), "grain keeps the channels equal");
        auto empty = solid(4, 4, {128, 128, 128, 0});
        require(paint(empty, grain, seeded)->pixel(1, 1).a == 0, "grain leaves a transparent pixel transparent");
        eyes = {};
        eyes.effects = false;
        grain.texture = 100;
        require(grain.applying(eyes).texture == 0 && grain.applying(eyes).grainAmount == 0, "the effects eye drops texture and grain together");

        auto red = solid(8, 8, {255, 0, 0, 255});
        auto scope = CameraRawScope::make(red->rgba().data(), 8, 8);
        int peak = 0;
        double peakValue = -1;
        for (int i = 0; i < 64 * 64; ++i) if (scope.vectorscope[size_t(i)] > peakValue) { peakValue = scope.vectorscope[size_t(i)]; peak = i; }
        require(peak % 64 >= 32, "pure red vectorscope peak is on the right half");
        require(scope.red[255] > 0 && scope.green[0] > 0 && scope.blue[0] > 0, "histogram counts the red pixel");
        CameraRawView overlay;
        overlay.highlightOverlay = true;
        require(didChange(bright, {}, overlay), "clip indicators are preview paint");

        auto low = solid(2, 2, {30, 30, 30, 255});
        auto high = solid(2, 2, {210, 210, 210, 255});
        CameraRawSettings curve;
        curve.curve.shadows = 100;
        require(rendered(*low, curve).r - 30 > rendered(*high, curve).r - 210, "parametric shadows lift a dark tone more than a light one");
        curve = {};
        curve.curve.rgb = CameraRawCurveSettings::strongContrast();
        require(rendered(*solid(2, 2, {64, 64, 64, 255}), curve).r < 55, "strong contrast pulls 0.25 below 55");
        curve = {};
        curve.mixer.hue[0] = 100;
        auto mixed = rendered(*red, curve);
        require(mixed.g > mixed.b && mixed.r > 0, "reds hue +100 moves red toward orange");
        curve = {};
        curve.grading.shadows.hue = 30;
        curve.grading.shadows.saturation = 80;
        auto gradedDark = rendered(*low, curve);
        auto gradedWhite = rendered(*solid(2, 2, {255, 255, 255, 255}), curve);
        require(chroma(gradedDark) > 4 && chroma(gradedWhite) == 0, "shadow grading tints a dark pixel and leaves white");
        curve.grading.balance = 100;
        require(chroma(rendered(*low, curve)) < chroma(gradedDark), "balance +100 weakens the shadow tint");
        eyes = {};
        eyes.curve = false;
        curve = {};
        curve.curve.shadows = 100;
        require(!curve.applying(eyes).curve.adjusts() && !didChange(low, curve.applying(eyes)), "the curve eye off is identity");

        CameraRawSettings detail;
        detail.detail.sharpenAmount = 150;
        require(paint(step, detail)->pixel(7, 1) != step->pixel(7, 1), "sharpen amount 150 changes a step");
        detail = {};
        detail.detail.noiseLuminance = 80;
        require(paint(mid, detail)->pixel(0, 0) == mid->pixel(0, 0), "luminance noise reduction leaves a flat field");
        CameraRawView mask;
        mask.sharpenMask = true;
        detail = {};
        detail.detail.sharpenAmount = 150;
        detail.detail.sharpenMasking = 50;
        require(gray(paint(step, detail, mask)->pixel(1, 1)), "sharpen-mask preview is grayscale");
        eyes = {};
        eyes.detail = false;
        require(!detail.applying(eyes).detail.adjusts(), "the detail eye drops sharpening");

        auto checks = image(12, 12, [](int x, int y) { return ((x / 3) + (y / 3)) % 2 ? Pixel{240, 240, 240, 255} : Pixel{20, 20, 20, 255}; });
        CameraRawSettings optics;
        optics.optics.distortion = 100;
        require(paint(checks, optics)->pixel(0, 5) != checks->pixel(0, 5), "distortion +100 changes a checkerboard edge");
        auto purple = solid(2, 2, {180, 40, 220, 255});
        optics = {};
        optics.optics.purpleAmount = 100;
        optics.optics.purpleHueLow = 250;
        optics.optics.purpleHueHigh = 320;
        require(chroma(rendered(*purple, optics)) < chroma(purple->pixel(0, 0)), "purple amount inside 250 to 320 lowers chroma");
        optics.optics.removeChromaticAberration = true;
        require(optics.optics.adjusts(), "the chromatic-aberration toggle counts as an optics adjustment");

        CameraRawSettings geometry;
        geometry.geometry.vertical = 40;
        require(paint(checks, geometry)->pixel(2, 2) != checks->pixel(2, 2), "vertical 40 changes a checkerboard");
        geometry = {};
        geometry.geometry.upright = CameraRawUpright::Guided;
        require(!didChange(checks, geometry), "guided with no line is identity");
        geometry.geometry.guides.push_back({0.1, 0.2, 0.8, 0.9});
        auto guided = paint(checks, geometry);
        auto other = image(12, 12, [](int x, int y) { return Pixel{std::uint8_t(x * 20), std::uint8_t(y * 18), 40, 255}; });
        auto guidedOther = paint(other, geometry);
        require(guided->pixel(4, 4) != checks->pixel(4, 4), "one diagonal guide warps the checkerboard");
        require(guidedOther->pixel(4, 4) != other->pixel(4, 4) && guided->pixel(4, 4) != guidedOther->pixel(4, 4), "the same guide warps a second image differently");

        CameraRawSettings calibration;
        calibration.calibration.process = CameraRawProcess::Version1;
        require(!calibration.calibration.adjusts() && !didChange(red, calibration), "process version alone does not change pixels");
        calibration.calibration.redHue = 80;
        auto calibrated = rendered(*red, calibration);
        require(calibrated != red->pixel(0, 0), "red hue 80 changes pure red");
        eyes = {};
        eyes.calibration = false;
        require(!didChange(red, calibration.applying(eyes)), "the calibration eye off does not change pixels");

        CameraRawSettings dehaze;
        dehaze.dehaze = 50;
        require(didChange(muted, dehaze), "dehaze changes a colored field");
        CameraRawSettings glow;
        glow.glow = 40;
        glow.glowStyle = CameraRawGlowStyle::Halation;
        require(didChange(bright, glow), "halation glow changes a bright field");
        CameraRawSettings vignette;
        vignette.vignetteAmount = -60;
        auto vignetted = paint(image(16, 16, [](int, int) { return Pixel{180, 180, 180, 255}; }), vignette);
        require(vignetted->pixel(0, 0).r < vignetted->pixel(8, 8).r, "vignette darkens the corner more than the center");

        Request request;
        request.kind = Kind::CameraRaw;
        request.source = mid;
        request.cameraRaw.exposure = 1;
        request.seed = 9;
        auto applied = apply(request);
        require(applied.implementation == "camera raw" && applied.changed && applied.cameraRawScope && applied.raster->pixel(0, 0).r > 165, "apply runs the camera raw grade and returns a scope");
        auto darkRamp = makeDeep(1024, 8, DeepImage::Format::Unorm16);
        for (int y = 0; y < darkRamp->height; ++y) for (int x = 0; x < darkRamp->width; ++x) {
            for (int c = 0; c < 3; ++c) darkRamp->at(x, y)[c] = std::uint16_t(x);
            darkRamp->at(x, y)[3] = 65535;
        }
        for (double amount : {-100., 100.}) {
            CameraRawSettings rampSettings; rampSettings.shadows = amount;
            auto adjusted = renderCameraRaw(*darkRamp, rampSettings, {}, nullptr);
            for (int x = 1; x < darkRamp->width; ++x) {
                const int step = int(adjusted->at(x, 4)[0]) - int(adjusted->at(x - 1, 4)[0]);
                require(step >= 0 && step <= 4, "adaptive shadows must preserve a smooth, ordered dark 16-bit ramp");
            }
            require(adjusted->at(0, 4)[0] == 0, "adaptive shadows must keep black black");
        }
        auto deep = makeDeep(4, 1, DeepImage::Format::Unorm16);
        deep->at(0, 0)[0] = 10001; deep->at(1, 0)[0] = 10031; deep->at(2, 0)[0] = 10001; deep->at(3, 0)[0] = 50021;
        for (int x = 0; x < 4; ++x) { deep->at(x, 0)[1] = 20003; deep->at(x, 0)[2] = 30007; deep->at(x, 0)[3] = 65535; }
        CameraRawSettings stop; stop.exposure = 1;
        auto graded = renderCameraRaw(*deep, stop, {}, nullptr);
        require(graded && graded->format == DeepImage::Format::Unorm16 && graded->at(0, 0)[0] != graded->at(1, 0)[0], "Camera Raw exposure collapsed 16-bit neighbors");
        require(graded->at(0, 0)[0] > 10001, "Camera Raw exposure ignored retained samples");
        auto hdr = makeDeep(1, 1, DeepImage::Format::HalfFloat);
        hdr->setPixel(0, 0, {2.f, 1.f, .5f, 1.f});
        CameraRawSettings hdrColor; hdrColor.saturation = -25;
        auto hdrGraded = renderCameraRaw(*hdr, hdrColor, {}, nullptr);
        require(hdrGraded->pixel(0, 0)[0] > 1.f, "Camera Raw clipped HDR highlights during color grading");
        CameraRawSettings adaptiveDeep; adaptiveDeep.shadows = 100;
        auto deepShadows = renderCameraRaw(*deep, adaptiveDeep, {}, nullptr);
        require(deepShadows && deepShadows->format == DeepImage::Format::Unorm16 &&
                deepShadows->at(0, 0)[0] != deepShadows->at(1, 0)[0], "adaptive shadows preserve distinct 16-bit samples");
        auto hdrShadows = renderCameraRaw(*hdr, adaptiveDeep, {}, nullptr);
        require(hdrShadows && hdrShadows->pixel(0, 0)[0] > 1.f, "adaptive shadows preserve HDR highlight range");
        Request shadowRequest; shadowRequest.kind = Kind::CameraRaw; shadowRequest.source = rasterFromDeep(*deep);
        shadowRequest.deepSource = deep; shadowRequest.cameraRaw = adaptiveDeep;
        require(apply(shadowRequest).deep != nullptr, "adaptive shadows apply retains 16-bit data");
        shadowRequest.limits.maxWorkingBytes = 1;
        bool budgetRejected = false; try { apply(shadowRequest); } catch (const std::exception&) { budgetRejected = true; }
        require(budgetRejected, "adaptive shadows must respect the working memory budget");
        CameraRawSettings mixer; mixer.mixer.saturation[0] = 40;
        require(cameraRawNeedsEightBit(mixer), "mixer must be classified as 8-bit");
        bool refused = false; try { renderCameraRaw(*deep, mixer, {}, nullptr); } catch (const std::exception&) { refused = true; }
        require(refused, "16-bit mixer Camera Raw did not require conversion");
        Request eight; eight.kind = Kind::CameraRaw; eight.source = rasterFromDeep(*deep); eight.deepSource = deep; eight.cameraRaw = mixer; eight.preview = true;
        require(apply(eight).changed && !apply(eight).deep, "mixer preview must not claim 16-bit precision");
        eight.preview = false; refused = false; try { apply(eight); } catch (const std::exception&) { refused = true; }
        require(refused, "mixer apply kept a silent 16-bit container");
        std::cout << "PASS camera raw\n";
}
int main() {
    try { run(); return 0; }
    catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
