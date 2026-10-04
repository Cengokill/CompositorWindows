#include "CameraRaw.h"
#include "effects_tools/CurveMath.h"
extern "C" {
#include "graphics/upstream/AdjustPixels.h"
#include "graphics/upstream/CameraRawPixels.h"
#include "graphics/upstream/LevelsPixels.h"
}
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>

namespace compositor::filters {
namespace {
double clampTo(double value, double low, double high, double fallback) {
    return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
}
double curveAt(double x, const std::vector<Point>& points) {
    if (points.size() < 2) return x;
    std::vector<Point> curve;
    curve.reserve(points.size());
    for (const auto& point : points) curve.push_back({point.x * 255, point.y * 255});
    return effects_tools::curveValueUnchecked(curve, x * 255) / 255;
}
double bend(double tone, double lower, double low, double upper, double high) {
    constexpr double strength = 1.66;
    if (tone < lower && lower > 0) return lower * std::pow(tone / lower, std::pow(2, -low / 100 * strength));
    if (tone > upper && upper < 1) {
        const double rest = 1 - upper;
        return 1 - rest * std::pow((1 - tone) / rest, std::pow(2, high / 100 * strength));
    }
    return tone;
}
std::vector<Point> repair(std::vector<Point> points) {
    std::erase_if(points, [](const Point& point) { return !std::isfinite(point.x) || !std::isfinite(point.y); });
    std::sort(points.begin(), points.end(), [](const Point& a, const Point& b) { return a.x < b.x; });
    if (points.size() < 2) return CameraRawCurveSettings::linear();
    points.front() = {0, std::clamp(points.front().y, 0., 1.)};
    points.back() = {1, std::clamp(points.back().y, 0., 1.)};
    std::vector<Point> kept{points.front()};
    for (size_t i = 1; i + 1 < points.size(); ++i) {
        const double x = std::clamp(points[i].x, 0.01, 0.99);
        if (x <= kept.back().x + 0.01) continue;
        kept.push_back({x, std::clamp(points[i].y, 0., 1.)});
    }
    kept.push_back(points.back());
    return kept;
}
std::vector<Point>& channel(CameraRawCurveSettings& curve, CameraRawPointChannel which) {
    switch (which) {
    case CameraRawPointChannel::Rgb: return curve.rgb;
    case CameraRawPointChannel::Red: return curve.red;
    case CameraRawPointChannel::Green: return curve.green;
    case CameraRawPointChannel::Blue: return curve.blue;
    }
    return curve.rgb;
}
struct Homography {
    std::array<double, 9> matrix{1, 0, 0, 0, 1, 0, 0, 0, 1};
    Point map(Point p) const {
        const auto& m = matrix;
        const double w = m[6] * p.x + m[7] * p.y + m[8];
        if (std::abs(w) < 1e-14) return {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN()};
        return {(m[0] * p.x + m[1] * p.y + m[2]) / w, (m[3] * p.x + m[4] * p.y + m[5]) / w};
    }
    std::optional<Homography> inverse() const {
        const auto& m = matrix;
        const double a = m[4] * m[8] - m[5] * m[7], b = m[2] * m[7] - m[1] * m[8], c = m[1] * m[5] - m[2] * m[4];
        const double d = m[5] * m[6] - m[3] * m[8], e = m[0] * m[8] - m[2] * m[6], f = m[2] * m[3] - m[0] * m[5];
        const double g = m[3] * m[7] - m[4] * m[6], h = m[1] * m[6] - m[0] * m[7], i = m[0] * m[4] - m[1] * m[3];
        const double determinant = m[0] * a + m[1] * d + m[2] * g;
        if (!std::isfinite(determinant) || std::abs(determinant) < 1e-14) return {};
        Homography out{{a, b, c, d, e, f, g, h, i}};
        for (auto& value : out.matrix) value /= determinant;
        return out;
    }
    // Same unit-square mapping as editing_transform::Homography::fromCorners.
    static Homography fromCorners(const std::array<Point, 4>& corners) {
        const auto& c = corners;
        const double sx = c[0].x - c[1].x + c[2].x - c[3].x, sy = c[0].y - c[1].y + c[2].y - c[3].y;
        double g = 0, h = 0;
        if (std::abs(sx) > 1e-9 || std::abs(sy) > 1e-9) {
            const double dx1 = c[1].x - c[2].x, dx2 = c[3].x - c[2].x, dy1 = c[1].y - c[2].y, dy2 = c[3].y - c[2].y;
            const double denominator = dx1 * dy2 - dx2 * dy1;
            if (std::abs(denominator) > 1e-12) {
                g = (sx * dy2 - dx2 * sy) / denominator;
                h = (dx1 * sy - sx * dy1) / denominator;
            }
        }
        Homography out{{c[1].x - c[0].x + g * c[1].x, c[3].x - c[0].x + h * c[3].x, c[0].x,
                        c[1].y - c[0].y + g * c[1].y, c[3].y - c[0].y + h * c[3].y, c[0].y, g, h, 1}};
        return out;
    }
};
bool usable(const std::array<Point, 4>& corners) {
    double sign = 0;
    for (auto p : corners)
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || std::abs(p.x) > 1000000 || std::abs(p.y) > 1000000) return false;
    for (size_t i = 0; i < 4; ++i) {
        const auto a = corners[i], b = corners[(i + 1) % 4], d = corners[(i + 2) % 4];
        const double cross = (b.x - a.x) * (d.y - b.y) - (b.y - a.y) * (d.x - b.x);
        if (std::abs(cross) <= .01) return false;
        if (sign == 0) sign = cross < 0 ? -1 : 1;
        else if ((cross < 0) != (sign < 0)) return false;
    }
    return true;
}
double sampleChannel(const std::vector<std::uint8_t>& pixels, int w, int h, double x, double y, int channel) {
    if (x <= -1 || y <= -1 || x >= w || y >= h) return 0;
    const int ix = int(std::floor(x)), iy = int(std::floor(y));
    const double fx = x - ix, fy = y - iy;
    auto get = [&](int xx, int yy) {
        if (xx < 0 || yy < 0 || xx >= w || yy >= h) return 0.;
        return double(pixels[(std::size_t(yy) * w + xx) * 4 + channel]);
    };
    return (get(ix, iy) * (1 - fx) + get(ix + 1, iy) * fx) * (1 - fy) + (get(ix, iy + 1) * (1 - fx) + get(ix + 1, iy + 1) * fx) * fy;
}
std::uint8_t byteOf(double value) { return std::uint8_t(std::clamp(std::floor(value + .5), 0., 255.)); }
bool usesGuides(const CameraRawGeometrySettings& geometry) {
    return geometry.upright == CameraRawUpright::Guided && std::any_of(geometry.guides.begin(), geometry.guides.end(), [](const CameraRawGeometryGuide& guide) {
        return std::hypot(guide.endX - guide.startX, guide.endY - guide.startY) > 0.01;
    });
}
void warpGeometry(std::vector<std::uint8_t>& pixels, int w, int h, const CameraRawGeometrySettings& geometry) {
    if (w < 1 || h < 1 || !geometry.adjusts()) return;
    double vertical = geometry.vertical, horizontal = geometry.horizontal, rotate = geometry.rotate;
    if (geometry.upright == CameraRawUpright::Guided && !geometry.guides.empty()) {
        const auto& first = geometry.guides.front();
        const double dx = first.endX - first.startX, dy = first.endY - first.startY;
        if (std::hypot(dx, dy) > 1e-4) {
            double angle = std::atan2(dy, dx) * 180 / std::numbers::pi;
            double guided = -angle;
            if (guided > 45) guided -= 90;
            else if (guided < -45) guided += 90;
            rotate += guided;
            if (geometry.guides.size() > 1) {
                const auto& second = geometry.guides[1];
                const double sx = second.endX - second.startX, sy = second.endY - second.startY;
                if (std::hypot(sx, sy) > 1e-4) {
                    const double a2 = std::atan2(sy, sx) * 180 / std::numbers::pi;
                    vertical += std::abs(a2) > 45 ? (a2 > 0 ? 25 : -25) : 0;
                    horizontal += std::abs(a2) <= 45 ? (a2 > 0 ? 25 : -25) : 0;
                }
            }
        }
    }
    const double width = w, height = h;
    const double strength = geometry.projection == CameraRawProjection::Perspective ? 1 : 0.55;
    const double v = vertical / 100 * width * 0.18 * strength;
    const double hz = horizontal / 100 * height * 0.18 * strength;
    const double aspectScale = 1 + geometry.aspect / 200;
    const double zoom = 1 + geometry.scale / 100;
    const double shiftX = geometry.offsetX / 100 * width * 0.15;
    const double shiftY = geometry.offsetY / 100 * height * 0.15;
    Point topLeft{-v + shiftX, height + shiftY};
    Point topRight{width + v + shiftX, height + shiftY};
    Point bottomRight{width + hz + shiftX, -shiftY};
    Point bottomLeft{-hz + shiftX, -shiftY};
    const Point center{width / 2 + shiftX, height / 2 + shiftY};
    const double radians = rotate * std::numbers::pi / 180, cosine = std::cos(radians), sine = std::sin(radians);
    auto rotatePoint = [&](Point point) {
        const double dx = point.x - center.x, dy = point.y - center.y;
        return Point{center.x + dx * cosine - dy * sine, center.y + dx * sine + dy * cosine};
    };
    topLeft = rotatePoint(topLeft);
    topRight = rotatePoint(topRight);
    bottomRight = rotatePoint(bottomRight);
    bottomLeft = rotatePoint(bottomLeft);
    if (aspectScale != 1) {
        auto scaled = [&](Point point) { return Point{center.x + (point.x - center.x) * aspectScale, center.y + (point.y - center.y) / aspectScale}; };
        topLeft = scaled(topLeft);
        topRight = scaled(topRight);
        bottomRight = scaled(bottomRight);
        bottomLeft = scaled(bottomLeft);
    }
    if (zoom != 1) {
        auto zoomed = [&](Point point) { return Point{center.x + (point.x - center.x) * zoom, center.y + (point.y - center.y) * zoom}; };
        topLeft = zoomed(topLeft);
        topRight = zoomed(topRight);
        bottomRight = zoomed(bottomRight);
        bottomLeft = zoomed(bottomLeft);
    }
    auto down = [&](Point point) { return Point{point.x, height - point.y}; };
    const std::array<Point, 4> corners{down(topLeft), down(topRight), down(bottomRight), down(bottomLeft)};
    if (!usable(corners)) return;
    const auto inverse = Homography::fromCorners(corners).inverse();
    if (!inverse) return;
    const auto source = pixels;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const auto unit = inverse->map({x + .5, y + .5});
            const double sx = unit.x * width - .5, sy = unit.y * height - .5;
            auto i = (std::size_t(y) * w + x) * 4;
            if (!std::isfinite(sx) || !std::isfinite(sy)) {
                pixels[i] = pixels[i + 1] = pixels[i + 2] = pixels[i + 3] = 0;
                continue;
            }
            for (int c = 0; c < 4; ++c) pixels[i + c] = byteOf(sampleChannel(source, w, h, sx, sy, c));
            const unsigned alpha = pixels[i + 3];
            for (int c = 0; c < 3; ++c)
                if (pixels[i + c] > alpha) pixels[i + c] = std::uint8_t(alpha);
        }
    if (!geometry.constrainCrop) return;
    int x0 = w, y0 = h, x1 = 0, y1 = 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (pixels[(std::size_t(y) * w + x) * 4 + 3]) {
                x0 = std::min(x0, x);
                y0 = std::min(y0, y);
                x1 = std::max(x1, x + 1);
                y1 = std::max(y1, y + 1);
            }
    if (x1 <= x0 || y1 <= y0 || (x0 == 0 && y0 == 0 && x1 == w && y1 == h)) return;
    const int cropW = x1 - x0, cropH = y1 - y0;
    const double fit = std::min(double(w) / cropW, double(h) / cropH);
    const double drawW = cropW * fit, drawH = cropH * fit;
    const double originX = (w - drawW) / 2, originY = (h - drawH) / 2;
    const auto cropped = pixels;
    std::fill(pixels.begin(), pixels.end(), std::uint8_t{0});
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const double fx = (x + .5 - originX) / fit + x0 - .5;
            const double fy = (y + .5 - originY) / fit + y0 - .5;
            if (fx < x0 - 1 || fy < y0 - 1 || fx >= x1 || fy >= y1) continue;
            auto i = (std::size_t(y) * w + x) * 4;
            for (int c = 0; c < 4; ++c) pixels[i + c] = byteOf(sampleChannel(cropped, w, h, fx, fy, c));
        }
}
double displayScale(const std::array<double, 256>& bins) {
    double peak = 0;
    for (double value : bins)
        if (std::isfinite(value) && value > 0) peak = std::max(peak, value);
    if (peak <= 0) return 0;
    std::vector<double> interior;
    for (size_t i = 1; i + 1 < bins.size(); ++i)
        if (std::isfinite(bins[i]) && bins[i] > 0) interior.push_back(bins[i]);
    if (interior.empty()) return peak;
    std::sort(interior.begin(), interior.end());
    return std::min(peak, interior[size_t(double(interior.size() - 1) * .95)] * 4);
}
void blendSelection(std::vector<std::uint8_t>& result, const std::vector<std::uint8_t>& original, const GrayRaster* selection) {
    if (!selection) return;
    const int w = selection->width, h = selection->height;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const auto i = std::size_t(y) * w + x;
            const unsigned coverage = selection->pixels[i];
            for (int c = 0; c < 4; ++c)
                result[i * 4 + c] = std::uint8_t((unsigned(result[i * 4 + c]) * coverage + unsigned(original[i * 4 + c]) * (255 - coverage) + 127) / 255);
        }
}
bool viewPaints(const CameraRawView& view) {
    return view.clipping || view.visualizePointColor >= 0 || view.sharpenMask || view.shadowOverlay || view.highlightOverlay;
}
std::vector<std::uint8_t> paint(std::vector<std::uint8_t> pixels, int w, int h, const CameraRawSettings& settings, const CameraRawView& view) {
    const bool clip = view.clipping != 0;
    const bool mask = view.sharpenMask;
    const double scale = view.scale > 0 ? view.scale : 1;
    if (!clip && !mask && view.visualizePointColor < 0 && settings.geometry.adjusts()) warpGeometry(pixels, w, h, settings.geometry);
    const auto stride = std::size_t(w) * 4;
    auto* bytes = pixels.data();
    if (!clip && !mask && settings.calibration.adjusts()) {
        const auto& calibration = settings.calibration;
        adjust_camera_raw_calibration(bytes, w, h, stride, calibration.shadowTint, calibration.redHue, calibration.redSaturation,
                                      calibration.greenHue, calibration.greenSaturation, calibration.blueHue, calibration.blueSaturation, int(calibration.process));
    }
    if (settings.adjustsLight() || settings.adjustsColor() || clip) {
        const auto gains = settings.gains();
        adjust_camera_raw(bytes, w, h, stride, gains.red, gains.green, gains.blue, settings.exposure, settings.contrast, settings.highlights,
                          settings.shadows, settings.whites, settings.blacks, settings.vibrance, settings.saturation, view.clipping);
    }
    const bool paintColor = !clip && !mask && (settings.curve.adjusts() || settings.mixer.adjusts() || settings.grading.adjusts() || view.visualizePointColor >= 0);
    if (paintColor) {
        const auto tone = settings.curve.toneTable();
        const auto red = settings.curve.channelTable(settings.curve.red);
        const auto green = settings.curve.channelTable(settings.curve.green);
        const auto blue = settings.curve.channelTable(settings.curve.blue);
        const auto mixer = settings.mixer.mixerFloats();
        const auto points = settings.mixer.pointFloats();
        const auto grade = settings.grading.gradeFloats();
        adjust_camera_raw_curve_color(bytes, w, h, stride, tone.data(), red.data(), green.data(), blue.data(), settings.curve.refineSaturation / 100,
                                      mixer.data(), int(settings.mixer.points.size()), points.empty() ? nullptr : points.data(), grade.data(),
                                      settings.grading.blending / 100, settings.grading.balance / 100, view.visualizePointColor);
    }
    if (!clip && !mask && settings.adjustsEffects()) {
        if (settings.texture != 0 || settings.clarity != 0 || settings.dehaze != 0 || settings.glow != 0 || settings.vignetteAmount != 0)
            adjust_camera_raw_effects(bytes, w, h, stride, settings.texture, settings.clarity, settings.dehaze, settings.glow, int(settings.glowStyle),
                                      settings.glowRange, settings.glowSpread, settings.glowWarmth, settings.vignetteAmount, settings.vignetteMidpoint,
                                      settings.vignetteRoundness, settings.vignetteFeather, settings.vignetteHighlights, int(settings.vignetteStyle), scale);
        if (settings.grainAmount > 0)
            adjust_grain(bytes, w, h, stride, settings.grainAmount, settings.grainKernelSize(), settings.grainRoughness, view.seed, 0, 0, 1 / scale);
    }
    if (!clip && (settings.detail.adjusts() || settings.optics.adjusts() || mask)) {
        if (mask) {
            adjust_camera_raw_sharpen_mask_overlay(bytes, w, h, stride, settings.detail.sharpenRadius, settings.detail.sharpenDetail, settings.detail.sharpenMasking, scale);
            return pixels;
        }
        if (settings.optics.adjusts()) {
            const auto& optics = settings.optics;
            adjust_camera_raw_optics(bytes, w, h, stride, optics.removeChromaticAberration ? 1 : 0, optics.enableLensProfile ? 1 : 0, optics.profileDistortion,
                                     optics.profileVignetting, optics.distortionK(0.35), optics.purpleAmount, optics.purpleHueLow, optics.purpleHueHigh,
                                     optics.greenAmount, optics.greenHueLow, optics.greenHueHigh, optics.vignetteAmount, optics.vignetteMidpoint, scale);
        }
        if (settings.detail.adjusts()) {
            const auto& detail = settings.detail;
            adjust_camera_raw_detail(bytes, w, h, stride, detail.sharpenAmount, detail.sharpenRadius, detail.sharpenDetail, detail.sharpenMasking,
                                     detail.noiseLuminance, detail.noiseLuminanceDetail, detail.noiseLuminanceContrast, detail.noiseColor,
                                     detail.noiseColorDetail, detail.noiseColorSmoothness, scale);
        }
    }
    return pixels;
}
}
std::vector<Point> CameraRawCurveSettings::linear() { return {{0, 0}, {1, 1}}; }
std::vector<Point> CameraRawCurveSettings::mediumContrast() { return {{0, 0}, {0.25, 0.18}, {0.75, 0.82}, {1, 1}}; }
std::vector<Point> CameraRawCurveSettings::strongContrast() { return {{0, 0}, {0.25, 0.10}, {0.75, 0.90}, {1, 1}}; }
bool CameraRawCurveSettings::isLinear(const std::vector<Point>& points) {
    return points.size() == 2 && points[0] == Point{0, 0} && points[1] == Point{1, 1};
}
bool CameraRawCurveSettings::adjusts() const {
    return shadows != 0 || darks != 0 || lights != 0 || highlights != 0 || refineSaturation != 0 || !isLinear(rgb) || !isLinear(red) || !isLinear(green) || !isLinear(blue);
}
double CameraRawCurveSettings::parametric(double tone) const {
    if (shadows == 0 && darks == 0 && lights == 0 && highlights == 0) return tone;
    std::vector<Point> anchors;
    anchors.reserve(33);
    for (int index = 0; index <= 32; ++index) {
        const double x = index / 32.;
        anchors.push_back({x, bend(bend(x, shadowSplit / 100, shadows, lightSplit / 100, highlights), darkSplit / 100, darks, darkSplit / 100, lights)});
    }
    return curveAt(tone, anchors);
}
std::array<float, 256> CameraRawCurveSettings::toneTable() const {
    std::array<float, 256> table{};
    for (int i = 0; i < 256; ++i) table[size_t(i)] = float(curveAt(parametric(i / 255.), rgb));
    return table;
}
std::array<float, 256> CameraRawCurveSettings::channelTable(const std::vector<Point>& points) const {
    std::array<float, 256> table{};
    for (int i = 0; i < 256; ++i) table[size_t(i)] = float(curveAt(i / 255., points));
    return table;
}
int CameraRawCurveSettings::region(double tone) const {
    if (tone < shadowSplit / 100) return 0;
    if (tone < darkSplit / 100) return 1;
    if (tone < lightSplit / 100) return 2;
    return 3;
}
CameraRawCurveSettings CameraRawCurveSettings::nudged(CameraRawPointChannel which, double tone, double delta) const {
    auto result = *this;
    auto& points = channel(result, which);
    if (points.empty()) return result;
    size_t index = 0;
    for (size_t i = 1; i < points.size(); ++i)
        if (std::abs(points[i].x - tone) < std::abs(points[index].x - tone)) index = i;
    points[index].y = std::clamp(points[index].y + delta, 0., 1.);
    return result;
}
CameraRawCurveSettings CameraRawCurveSettings::normalized() const {
    auto result = *this;
    result.shadows = clampTo(shadows, -100, 100, 0);
    result.darks = clampTo(darks, -100, 100, 0);
    result.lights = clampTo(lights, -100, 100, 0);
    result.highlights = clampTo(highlights, -100, 100, 0);
    result.refineSaturation = clampTo(refineSaturation, -100, 100, 0);
    result.shadowSplit = clampTo(shadowSplit, 5, 90, 25);
    result.darkSplit = clampTo(darkSplit, result.shadowSplit + 2, 95, 50);
    result.lightSplit = clampTo(lightSplit, result.darkSplit + 2, 98, 75);
    result.rgb = repair(rgb);
    result.red = repair(red);
    result.green = repair(green);
    result.blue = repair(blue);
    return result;
}
bool CameraRawMixerSettings::adjusts() const {
    auto moved = [](double value) { return value != 0; };
    return std::any_of(hue.begin(), hue.end(), moved) || std::any_of(saturation.begin(), saturation.end(), moved) || std::any_of(luminance.begin(), luminance.end(), moved)
        || std::any_of(points.begin(), points.end(), [](const CameraRawPointColor& point) { return point.hueShift != 0 || point.saturationShift != 0 || point.luminanceShift != 0; });
}
std::array<double, 8> CameraRawMixerSettings::weights(double degrees) {
    std::array<double, 8> result{};
    for (size_t i = 0; i < centers.size(); ++i) {
        double distance = std::abs(degrees - centers[i]);
        if (distance > 180) distance = 360 - distance;
        result[i] = std::max(0., 1 - distance / 40);
    }
    return result;
}
std::array<float, 24> CameraRawMixerSettings::mixerFloats() const {
    std::array<float, 24> result{};
    for (int i = 0; i < 8; ++i) {
        result[size_t(i)] = float(hue[size_t(i)] / 100);
        result[size_t(8 + i)] = float(saturation[size_t(i)] / 100);
        result[size_t(16 + i)] = float(luminance[size_t(i)] / 100);
    }
    return result;
}
std::vector<float> CameraRawMixerSettings::pointFloats() const {
    std::vector<float> result;
    result.reserve(points.size() * 9);
    for (const auto& point : points) {
        const float values[]{float(point.hue / 360), float(point.saturation), float(point.luminance), float(point.hueShift / 100), float(point.saturationShift / 100),
                             float(point.luminanceShift / 100), float(point.hueRange / 360), float(point.saturationRange), float(point.luminanceRange)};
        result.insert(result.end(), std::begin(values), std::end(values));
    }
    return result;
}
CameraRawPointColor CameraRawPointColor::normalized() const {
    CameraRawPointColor result = *this;
    result.hue = clampTo(hue, 0, 360, 0);
    result.saturation = clampTo(saturation, 0, 1, 0);
    result.luminance = clampTo(luminance, 0, 1, 0);
    result.hueShift = clampTo(hueShift, -100, 100, 0);
    result.saturationShift = clampTo(saturationShift, -100, 100, 0);
    result.luminanceShift = clampTo(luminanceShift, -100, 100, 0);
    result.hueRange = clampTo(hueRange, 5, 180, 30);
    result.saturationRange = clampTo(saturationRange, 0.05, 1, 0.4);
    result.luminanceRange = clampTo(luminanceRange, 0.05, 1, 0.4);
    return result;
}
CameraRawMixerSettings CameraRawMixerSettings::normalized() const {
    auto result = *this;
    for (auto& value : result.hue) value = clampTo(value, -100, 100, 0);
    for (auto& value : result.saturation) value = clampTo(value, -100, 100, 0);
    for (auto& value : result.luminance) value = clampTo(value, -100, 100, 0);
    if (result.points.size() > 8) result.points.resize(8);
    for (auto& point : result.points) point = point.normalized();
    return result;
}
CameraRawGradeWheel CameraRawGradeWheel::normalized() const {
    return {clampTo(hue, 0, 360, 0), clampTo(saturation, 0, 100, 0), clampTo(luminance, -100, 100, 0)};
}
bool CameraRawGradingSettings::adjusts() const {
    for (const auto& wheel : {shadows, midtones, highlights, global})
        if (wheel.saturation != 0 || wheel.luminance != 0) return true;
    return false;
}
std::array<float, 12> CameraRawGradingSettings::gradeFloats() const {
    std::array<float, 12> result{};
    const CameraRawGradeWheel wheels[]{shadows, midtones, highlights, global};
    for (int i = 0; i < 4; ++i) {
        result[size_t(i * 3)] = float(wheels[i].hue / 360);
        result[size_t(i * 3 + 1)] = float(wheels[i].saturation / 100);
        result[size_t(i * 3 + 2)] = float(wheels[i].luminance / 100);
    }
    return result;
}
CameraRawGradingSettings CameraRawGradingSettings::normalized() const {
    auto result = *this;
    result.shadows = shadows.normalized();
    result.midtones = midtones.normalized();
    result.highlights = highlights.normalized();
    result.global = global.normalized();
    result.blending = clampTo(blending, 0, 100, 50);
    result.balance = clampTo(balance, -100, 100, 0);
    return result;
}
bool CameraRawDetailSettings::adjusts() const { return sharpenAmount != 0 || noiseLuminance != 0 || noiseColor != 0; }
CameraRawDetailSettings CameraRawDetailSettings::normalized() const {
    auto result = *this;
    result.sharpenAmount = clampTo(sharpenAmount, 0, 150, 0);
    result.sharpenRadius = clampTo(sharpenRadius, 0, 100, 10);
    result.sharpenDetail = clampTo(sharpenDetail, 0, 100, 25);
    result.sharpenMasking = clampTo(sharpenMasking, 0, 100, 0);
    result.noiseLuminance = clampTo(noiseLuminance, 0, 100, 0);
    result.noiseLuminanceDetail = clampTo(noiseLuminanceDetail, 0, 100, 50);
    result.noiseLuminanceContrast = clampTo(noiseLuminanceContrast, 0, 100, 0);
    result.noiseColor = clampTo(noiseColor, 0, 100, 0);
    result.noiseColorDetail = clampTo(noiseColorDetail, 0, 100, 50);
    result.noiseColorSmoothness = clampTo(noiseColorSmoothness, 0, 100, 50);
    return result;
}
bool CameraRawOpticsSettings::adjusts() const {
    return removeChromaticAberration || enableLensProfile || distortion != 0 || purpleAmount != 0 || greenAmount != 0 || vignetteAmount != 0;
}
double CameraRawOpticsSettings::distortionK(double profileStrength) const {
    return distortion / 100 * profileStrength + (enableLensProfile ? profileDistortion / 100 * profileStrength : 0);
}
CameraRawOpticsSettings CameraRawOpticsSettings::normalized() const {
    auto result = *this;
    result.profileDistortion = clampTo(profileDistortion, 0, 100, 100);
    result.profileVignetting = clampTo(profileVignetting, 0, 100, 100);
    result.distortion = clampTo(distortion, -100, 100, 0);
    result.purpleAmount = clampTo(purpleAmount, 0, 100, 0);
    result.greenAmount = clampTo(greenAmount, 0, 100, 0);
    result.vignetteAmount = clampTo(vignetteAmount, -100, 100, 0);
    result.vignetteMidpoint = clampTo(vignetteMidpoint, 0, 100, 50);
    result.purpleHueLow = clampTo(purpleHueLow, 0, 360, 270);
    result.purpleHueHigh = clampTo(purpleHueHigh, 0, 360, 310);
    result.greenHueLow = clampTo(greenHueLow, 0, 360, 60);
    result.greenHueHigh = clampTo(greenHueHigh, 0, 360, 120);
    if (result.purpleHueLow > result.purpleHueHigh) std::swap(result.purpleHueLow, result.purpleHueHigh);
    if (result.greenHueLow > result.greenHueHigh) std::swap(result.greenHueLow, result.greenHueHigh);
    return result;
}
bool CameraRawGeometrySettings::adjusts() const {
    return usesGuides(*this) || vertical != 0 || horizontal != 0 || rotate != 0 || aspect != 0 || scale != 0 || offsetX != 0 || offsetY != 0;
}
CameraRawGeometrySettings CameraRawGeometrySettings::normalized() const {
    auto result = *this;
    result.vertical = clampTo(vertical, -100, 100, 0);
    result.horizontal = clampTo(horizontal, -100, 100, 0);
    result.rotate = clampTo(rotate, -45, 45, 0);
    result.aspect = clampTo(aspect, -100, 100, 0);
    result.scale = clampTo(scale, -100, 100, 0);
    result.offsetX = clampTo(offsetX, -100, 100, 0);
    result.offsetY = clampTo(offsetY, -100, 100, 0);
    std::erase_if(result.guides, [](const CameraRawGeometryGuide& guide) { return std::hypot(guide.endX - guide.startX, guide.endY - guide.startY) <= 0.01; });
    return result;
}
bool CameraRawCalibrationSettings::adjusts() const {
    return shadowTint != 0 || redHue != 0 || redSaturation != 0 || greenHue != 0 || greenSaturation != 0 || blueHue != 0 || blueSaturation != 0;
}
const char* CameraRawCalibrationSettings::summary(CameraRawProcess process) {
    switch (process) {
    case CameraRawProcess::Version1: return "Earliest response. Hue, saturation, and shadow tint move about half as far as Version 6.";
    case CameraRawProcess::Version2: return "A little stronger than Version 1. The sliders below still fall well short of the current look.";
    case CameraRawProcess::Version3: return "Firmer color than Version 2. Primary shifts stay gentler than the current process.";
    case CameraRawProcess::Version4: return "The 2012 response. Calibration reaches most of the strength used by Version 6.";
    case CameraRawProcess::Version5: return "Close to the current process, with slightly softer primary and shadow shifts.";
    case CameraRawProcess::Version6: return "Current default. The calibration sliders below apply at full strength.";
    }
    return "";
}
CameraRawCalibrationSettings CameraRawCalibrationSettings::normalized() const {
    auto result = *this;
    result.shadowTint = clampTo(shadowTint, -100, 100, 0);
    result.redHue = clampTo(redHue, -100, 100, 0);
    result.redSaturation = clampTo(redSaturation, -100, 100, 0);
    result.greenHue = clampTo(greenHue, -100, 100, 0);
    result.greenSaturation = clampTo(greenSaturation, -100, 100, 0);
    result.blueHue = clampTo(blueHue, -100, 100, 0);
    result.blueSaturation = clampTo(blueSaturation, -100, 100, 0);
    return result;
}
bool CameraRawSettings::adjustsLight() const { return exposure != 0 || contrast != 0 || highlights != 0 || shadows != 0 || whites != 0 || blacks != 0; }
bool CameraRawSettings::adjustsColor() const { return temperature != 0 || tint != 0 || vibrance != 0 || saturation != 0; }
bool CameraRawSettings::adjustsEffects() const { return texture != 0 || clarity != 0 || dehaze != 0 || glow != 0 || vignetteAmount != 0 || grainAmount != 0; }
bool CameraRawSettings::isIdentity() const {
    return !adjustsLight() && !adjustsColor() && !adjustsEffects() && !curve.adjusts() && !mixer.adjusts() && !grading.adjusts() && !detail.adjusts() && !optics.adjusts()
        && !geometry.adjusts() && !calibration.adjusts();
}
bool CameraRawSettings::isValid() const {
    auto tone = [](double value) { return std::isfinite(value) && value >= -100 && value <= 100; };
    auto unit = [](double value) { return std::isfinite(value) && value >= 0 && value <= 100; };
    return std::isfinite(exposure) && exposure >= -5 && exposure <= 5
        && tone(contrast) && tone(highlights) && tone(shadows) && tone(whites) && tone(blacks) && tone(temperature) && tone(tint) && tone(vibrance) && tone(saturation)
        && tone(texture) && tone(clarity) && tone(dehaze) && tone(glowRange) && tone(glowSpread) && tone(glowWarmth) && tone(vignetteAmount) && tone(vignetteRoundness)
        && unit(glow) && unit(vignetteMidpoint) && unit(vignetteFeather) && unit(vignetteHighlights) && unit(grainAmount) && unit(grainSize) && unit(grainRoughness);
}
double CameraRawSettings::grainKernelSize() const { return 0.5 + (grainSize / 100) * 19.5; }
CameraRawSettings::Gains CameraRawSettings::gains() const {
    const double warm = temperature / 100, magenta = tint / 100;
    return {1 + temperatureGain * warm + tintRedBlue * magenta, 1 - tintGreen * magenta, 1 - temperatureGain * warm + tintRedBlue * magenta};
}
CameraRawSettings CameraRawSettings::normalized() const {
    auto result = *this;
    result.exposure = clampTo(exposure, -5, 5, 0);
    result.contrast = clampTo(contrast, -100, 100, 0);
    result.highlights = clampTo(highlights, -100, 100, 0);
    result.shadows = clampTo(shadows, -100, 100, 0);
    result.whites = clampTo(whites, -100, 100, 0);
    result.blacks = clampTo(blacks, -100, 100, 0);
    result.temperature = clampTo(temperature, -100, 100, 0);
    result.tint = clampTo(tint, -100, 100, 0);
    result.vibrance = clampTo(vibrance, -100, 100, 0);
    result.saturation = clampTo(saturation, -100, 100, 0);
    result.texture = clampTo(texture, -100, 100, 0);
    result.clarity = clampTo(clarity, -100, 100, 0);
    result.dehaze = clampTo(dehaze, -100, 100, 0);
    result.glow = clampTo(glow, 0, 100, 0);
    result.glowRange = clampTo(glowRange, -100, 100, 0);
    result.glowSpread = clampTo(glowSpread, -100, 100, 0);
    result.glowWarmth = clampTo(glowWarmth, -100, 100, 0);
    result.vignetteAmount = clampTo(vignetteAmount, -100, 100, 0);
    result.vignetteMidpoint = clampTo(vignetteMidpoint, 0, 100, 50);
    result.vignetteRoundness = clampTo(vignetteRoundness, -100, 100, 0);
    result.vignetteFeather = clampTo(vignetteFeather, 0, 100, 50);
    result.vignetteHighlights = clampTo(vignetteHighlights, 0, 100, 0);
    result.grainAmount = clampTo(grainAmount, 0, 100, 0);
    result.grainSize = clampTo(grainSize, 0, 100, 25);
    result.grainRoughness = clampTo(grainRoughness, 0, 100, 50);
    result.curve = curve.normalized();
    result.mixer = mixer.normalized();
    result.grading = grading.normalized();
    result.detail = detail.normalized();
    result.optics = optics.normalized();
    result.geometry = geometry.normalized();
    result.calibration = calibration.normalized();
    return result;
}
CameraRawSettings CameraRawSettings::applying(const CameraRawGroupEyes& eyes) const {
    auto result = *this;
    if (!eyes.light) result.exposure = result.contrast = result.highlights = result.shadows = result.whites = result.blacks = 0;
    if (!eyes.color) result.temperature = result.tint = result.vibrance = result.saturation = 0;
    if (!eyes.effects) result.texture = result.clarity = result.dehaze = result.glow = result.vignetteAmount = result.grainAmount = 0;
    if (!eyes.curve) result.curve = {};
    if (!eyes.mixer) result.mixer = {};
    if (!eyes.grading) result.grading = {};
    if (!eyes.detail) result.detail = {};
    if (!eyes.optics) result.optics = {};
    if (!eyes.geometry) result.geometry = {};
    if (!eyes.calibration) result.calibration = {};
    return result;
}
std::optional<std::pair<double, double>> CameraRawSettings::neutralizeLinear(double red, double green, double blue) {
    if (!(red > 1e-4 && green > 1e-4 && blue > 1e-4)) return {};
    const double a1 = temperatureGain * red;
    const double b1 = tintRedBlue * red + tintGreen * green;
    const double c1 = green - red;
    const double a2 = -temperatureGain * blue;
    const double b2 = tintRedBlue * blue + tintGreen * green;
    const double c2 = green - blue;
    const double determinant = a1 * b2 - a2 * b1;
    if (std::abs(determinant) <= 1e-8) return {};
    const double warm = (c1 * b2 - c2 * b1) / determinant;
    const double magenta = (a1 * c2 - a2 * c1) / determinant;
    if (!std::isfinite(warm) || !std::isfinite(magenta)) return {};
    return std::pair{warm * 100, magenta * 100};
}
std::optional<std::pair<double, double>> CameraRawSettings::neutralizeStraight(double red, double green, double blue) {
    auto decode = [](double encoded) { return encoded <= 0.04045 ? encoded / 12.92 : std::pow((encoded + 0.055) / 1.055, 2.4); };
    return neutralizeLinear(decode(red), decode(green), decode(blue));
}
std::optional<std::pair<double, double>> CameraRawSettings::autoBalance(const std::uint8_t* rgba, int width, int height, int stride) {
    if (!rgba || width < 1 || height < 1 || stride < width * 4) return {};
    double red = 0, green = 0, blue = 0, count = 0;
    auto decode = [](double encoded) { return encoded <= 0.04045 ? encoded / 12.92 : std::pow((encoded + 0.055) / 1.055, 2.4); };
    for (int y = 0; y < height; ++y) {
        const auto* row = rgba + std::size_t(y) * stride;
        for (int x = 0; x < width; ++x) {
            const auto* pixel = row + x * 4;
            if (!pixel[3]) continue;
            const double alpha = pixel[3];
            red += decode(std::min(1., pixel[0] / alpha));
            green += decode(std::min(1., pixel[1] / alpha));
            blue += decode(std::min(1., pixel[2] / alpha));
            count += 1;
        }
    }
    if (count <= 0) return {};
    return neutralizeLinear(red / count, green / count, blue / count);
}
double CameraRawScope::peak() const { return std::max(displayScale(red), std::max(displayScale(green), displayScale(blue))); }
CameraRawScope CameraRawScope::make(const std::uint8_t* rgba, int width, int height) {
    CameraRawScope scope;
    if (!rgba || width < 1 || height < 1) return scope;
    std::array<double, 1024> bins{};
    levels_histogram(rgba, nullptr, std::size_t(width) * height, bins.data());
    std::copy_n(bins.begin() + 256, 256, scope.red.begin());
    std::copy_n(bins.begin() + 512, 256, scope.green.begin());
    std::copy_n(bins.begin() + 768, 256, scope.blue.begin());
    constexpr int side = 64;
    for (int y = 0; y < height; ++y) {
        const auto* row = rgba + std::size_t(y) * width * 4;
        for (int x = 0; x < width; ++x) {
            const auto* pixel = row + x * 4;
            const double alpha = pixel[3];
            if (!alpha) continue;
            const double r = std::min(1., pixel[0] / alpha), g = std::min(1., pixel[1] / alpha), b = std::min(1., pixel[2] / alpha);
            const double maxChannel = std::max(r, std::max(g, b)), minChannel = std::min(r, std::min(g, b));
            const double chroma = maxChannel - minChannel;
            if (chroma <= 1e-4 || maxChannel <= 1e-4) continue;
            double hue = maxChannel == r ? (g - b) / chroma : maxChannel == g ? 2 + (b - r) / chroma : 4 + (r - g) / chroma;
            hue /= 6;
            if (hue < 0) hue += 1;
            const double angle = hue * 2 * std::numbers::pi;
            const double saturation = chroma / maxChannel;
            const double plotX = 0.5 + std::cos(angle) * saturation * 0.48;
            const double plotY = 0.5 + std::sin(angle) * saturation * 0.48;
            const int column = std::clamp(int(plotX * side), 0, side - 1);
            const int rowIndex = std::clamp(int(plotY * side), 0, side - 1);
            scope.vectorscope[size_t(rowIndex * side + column)] += alpha / 255;
        }
    }
    return scope;
}
CameraRawRender renderCameraRaw(const Raster& source, const CameraRawSettings& incoming, const CameraRawView& view, const GrayRaster* selection) {
    const int w = source.width, h = source.height;
    const auto settings = incoming.normalized();
    auto original = source.rgba();
    CameraRawView clean = view;
    clean.clipping = 0;
    clean.visualizePointColor = -1;
    clean.sharpenMask = false;
    clean.shadowOverlay = false;
    clean.highlightOverlay = false;
    const bool early = settings.isIdentity() && !viewPaints(view);
    std::vector<std::uint8_t> grade = early ? original : paint(original, w, h, settings, clean);
    blendSelection(grade, original, selection);
    CameraRawRender result;
    result.scope = CameraRawScope::make(grade.data(), w, h);
    result.changed = !early;
    std::vector<std::uint8_t> display = grade;
    if (view.clipping || view.sharpenMask || view.visualizePointColor >= 0) {
        auto shown = view;
        shown.shadowOverlay = false;
        shown.highlightOverlay = false;
        display = paint(original, w, h, settings, shown);
        blendSelection(display, original, selection);
        result.changed = true;
    }
    if (!view.clipping && !view.sharpenMask && (view.shadowOverlay || view.highlightOverlay)) {
        adjust_camera_raw_clip_overlay(display.data(), w, h, std::size_t(w) * 4, view.shadowOverlay ? 1 : 0, view.highlightOverlay ? 1 : 0);
        result.changed = true;
    }
    result.raster = result.changed ? Raster::fromRgba(w, h, display.data(), std::size_t(w) * 4) : nullptr;
    return result;
}
}
