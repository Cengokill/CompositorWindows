#include "CameraRaw.h"
#include "CameraRawShadows.h"
#include "effects_tools/CurveMath.h"
#include "graphics/ParallelBatch.h"
extern "C" {
#include "graphics/upstream/AdjustPixels.h"
#include "graphics/upstream/CameraRawPixels.h"
}
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <stdexcept>

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
void warpGeometry(std::vector<std::uint8_t>& pixels, int w, int h, const CameraRawGeometrySettings& geometry, const std::function<bool()>& cancelled) {
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
    for (int y = 0; y < h; ++y) {
        if ((y & 31) == 0 && cancelled && cancelled()) throw std::runtime_error("Filter cancelled");
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
template<class Work>
void cameraRawRows(int width, int height, const std::function<bool()>& cancelled, const Work& work) {
    constexpr int bandHeight = 32;
    const std::uint32_t workers = std::uint64_t(width) * height < 128 * 1024 ? 1u : 0u;
    graphics::runParallelBatch(std::size_t((height + bandHeight - 1) / bandHeight), workers, [&](std::size_t band) {
        if (cancelled && cancelled()) throw std::runtime_error("Filter cancelled");
        const int first = int(band) * bandHeight;
        work(first, std::min(bandHeight, height - first));
    });
}
void blendSelection(std::vector<std::uint8_t>& result, const Raster& original, const GrayRaster* selection, const std::function<bool()>& cancelled) {
    if (!selection) return;
    const int w = selection->width, h = selection->height;
    cameraRawRows(w, h, cancelled, [&](int first, int count) {
        for (int y = first; y < first + count; ++y) for (int x = 0; x < w; ++x) {
            const unsigned coverage = selection->pixel(x, y);
            if (coverage == 255) continue;
            const auto p = original.pixel(x, y);
            const std::array<std::uint8_t, 4> before{p.r, p.g, p.b, p.a};
            auto* pixel = result.data() + (std::size_t(y) * w + x) * 4;
            for (int c = 0; c < 4; ++c)
                pixel[c] = std::uint8_t((pixel[c] * coverage + before[c] * (255 - coverage) + 127) / 255);
        }
    });
}
std::vector<std::uint8_t> paint(std::vector<std::uint8_t> pixels, int w, int h, const CameraRawSettings& settings, const CameraRawView& view,
                                const std::function<bool()>& cancelled, CameraRawShadowAnalysis* analysis = nullptr) {
    auto shouldStop = [&] { return cancelled && cancelled(); };
    auto stop = [&] { if (shouldStop()) throw std::runtime_error("Filter cancelled"); };
    const CameraRawCancel cancel{[](void* context) { return int((*static_cast<decltype(shouldStop)*>(context))()); }, &shouldStop};
    auto requirePass = [&](int success) {
        if (success) return;
        stop();
        throw std::runtime_error("Could not allocate Camera Raw working buffers");
    };
    const bool clip = view.clipping != 0;
    const bool mask = view.sharpenMask;
    const double scale = view.scale > 0 ? view.scale : 1;
    stop();
    if (!clip && !mask && view.visualizePointColor < 0 && settings.geometry.adjusts()) warpGeometry(pixels, w, h, settings.geometry, cancelled);
    stop();
    const auto stride = std::size_t(w) * 4;
    auto* bytes = pixels.data();
    stop();
    if (!clip && !mask && settings.calibration.adjusts()) {
        const auto& calibration = settings.calibration;
        cameraRawRows(w, h, cancelled, [&](int first, int count) {
            adjust_camera_raw_calibration(bytes + std::size_t(first) * stride, w, count, stride, calibration.shadowTint,
                                          calibration.redHue, calibration.redSaturation, calibration.greenHue, calibration.greenSaturation,
                                          calibration.blueHue, calibration.blueSaturation, int(calibration.process));
        });
    }
    stop();
    if (settings.adjustsLight() || settings.adjustsColor() || clip) {
        const auto gains = settings.gains();
        const bool adaptive = settings.shadows != 0;
        cameraRawRows(w, h, cancelled, [&](int first, int count) {
            adjust_camera_raw(bytes + std::size_t(first) * stride, w, count, stride, gains.red, gains.green, gains.blue,
                              settings.exposure, settings.contrast, settings.highlights, 0, adaptive ? 0 : settings.whites,
                              adaptive ? 0 : settings.blacks, adaptive ? 0 : settings.vibrance, adaptive ? 0 : settings.saturation,
                              adaptive ? 0 : view.clipping);
        });
        if (adaptive) {
            applyAdaptiveShadows(pixels, w, h, settings.shadows, scale, cancelled, analysis);
            if (settings.whites || settings.blacks || settings.vibrance || settings.saturation || view.clipping) {
                cameraRawRows(w, h, cancelled, [&](int first, int count) {
                    adjust_camera_raw(bytes + std::size_t(first) * stride, w, count, stride, 1, 1, 1, 0, 0, 0, 0,
                                      settings.whites, settings.blacks, settings.vibrance, settings.saturation, view.clipping);
                });
            }
        }
    }
    stop();
    const bool paintColor = !clip && !mask && (settings.curve.adjusts() || settings.mixer.adjusts() || settings.grading.adjusts() || view.visualizePointColor >= 0);
    if (paintColor) {
        const auto tone = settings.curve.toneTable();
        const auto red = settings.curve.channelTable(settings.curve.red);
        const auto green = settings.curve.channelTable(settings.curve.green);
        const auto blue = settings.curve.channelTable(settings.curve.blue);
        const auto mixer = settings.mixer.mixerFloats();
        const auto points = settings.mixer.pointFloats();
        const auto grade = settings.grading.gradeFloats();
        cameraRawRows(w, h, cancelled, [&](int first, int count) {
            adjust_camera_raw_curve_color(bytes + std::size_t(first) * stride, w, count, stride, tone.data(), red.data(), green.data(), blue.data(),
                                          settings.curve.refineSaturation / 100, mixer.data(), int(settings.mixer.points.size()),
                                          points.empty() ? nullptr : points.data(), grade.data(), settings.grading.blending / 100,
                                          settings.grading.balance / 100, view.visualizePointColor);
        });
    }
    stop();
    if (!clip && !mask && settings.adjustsEffects()) {
        if (settings.texture != 0 || settings.clarity != 0 || settings.dehaze != 0 || settings.glow != 0 || settings.vignetteAmount != 0)
            requirePass(adjust_camera_raw_effects(bytes, w, h, stride, settings.texture, settings.clarity, settings.dehaze, settings.glow, int(settings.glowStyle),
                                                 settings.glowRange, settings.glowSpread, settings.glowWarmth, settings.vignetteAmount, settings.vignetteMidpoint,
                                                 settings.vignetteRoundness, settings.vignetteFeather, settings.vignetteHighlights, int(settings.vignetteStyle), scale, cancel));
        if (settings.grainAmount > 0) {
            if (scale == 1) {
                cameraRawRows(w, h, cancelled, [&](int first, int count) {
                    adjust_grain(bytes + std::size_t(first) * stride, w, count, stride, settings.grainAmount, settings.grainKernelSize(),
                                 settings.grainRoughness, view.seed, 0, first, 1);
                });
            } else {
                adjust_grain(bytes, w, h, stride, settings.grainAmount, settings.grainKernelSize(), settings.grainRoughness, view.seed, 0, 0, 1 / scale);
            }
        }
    }
    stop();
    if (!clip && (settings.detail.adjusts() || settings.optics.adjusts() || mask)) {
        if (mask) {
            requirePass(adjust_camera_raw_sharpen_mask_overlay(bytes, w, h, stride, settings.detail.sharpenRadius,
                                                              settings.detail.sharpenDetail, settings.detail.sharpenMasking, scale, cancel));
            return pixels;
        }
        if (settings.optics.adjusts()) {
            const auto& optics = settings.optics;
            requirePass(adjust_camera_raw_optics(bytes, w, h, stride, optics.removeChromaticAberration ? 1 : 0, optics.enableLensProfile ? 1 : 0, optics.profileDistortion,
                                                 optics.profileVignetting, optics.distortionK(0.35), optics.purpleAmount, optics.purpleHueLow, optics.purpleHueHigh,
                                                 optics.greenAmount, optics.greenHueLow, optics.greenHueHigh, optics.vignetteAmount, optics.vignetteMidpoint, scale, cancel));
        }
        if (settings.detail.adjusts()) {
            const auto& detail = settings.detail;
            requirePass(adjust_camera_raw_detail(bytes, w, h, stride, detail.sharpenAmount, detail.sharpenRadius, detail.sharpenDetail, detail.sharpenMasking,
                                                 detail.noiseLuminance, detail.noiseLuminanceDetail, detail.noiseLuminanceContrast, detail.noiseColor,
                                                 detail.noiseColorDetail, detail.noiseColorSmoothness, scale, cancel));
        }
    }
    return pixels;
}
template<class Sample>
CameraRawScope scopeFrom(int width, int height, int limit, const std::function<bool()>& cancelled, const Sample& sample) {
    CameraRawScope scope;
    if (width < 1 || height < 1) return scope;
    const double factor = limit > 0 ? std::min(1., double(limit) / std::max(width, height)) : 1.;
    const int sw = std::max(1, int(std::lround(width * factor))), sh = std::max(1, int(std::lround(height * factor)));
    constexpr int side = 64;
    for (int y = 0; y < sh; ++y) {
        if (cancelled && cancelled()) throw std::runtime_error("Filter cancelled");
        const int sy = int((std::int64_t(y) * height) / sh);
        for (int x = 0; x < sw; ++x) {
            const auto p = sample(int((std::int64_t(x) * width) / sw), sy);
            if (!p.a) continue;
            const double alpha = p.a, weight = alpha / 255.;
            auto bin = [alpha](std::uint8_t channel) { return std::size_t(std::min(255., std::round(channel * 255. / alpha))); };
            scope.red[bin(p.r)] += weight;
            scope.green[bin(p.g)] += weight;
            scope.blue[bin(p.b)] += weight;
            const double r = std::min(1., p.r / alpha), g = std::min(1., p.g / alpha), b = std::min(1., p.b / alpha);
            const double maxChannel = std::max({r, g, b}), chroma = maxChannel - std::min({r, g, b});
            if (chroma <= 1e-4 || maxChannel <= 1e-4) continue;
            double hue = maxChannel == r ? (g - b) / chroma : maxChannel == g ? 2 + (b - r) / chroma : 4 + (r - g) / chroma;
            hue /= 6;
            if (hue < 0) hue += 1;
            const double angle = hue * 2 * std::numbers::pi;
            const double saturation = chroma / maxChannel;
            const int column = std::clamp(int((0.5 + std::cos(angle) * saturation * 0.48) * side), 0, side - 1);
            const int rowIndex = std::clamp(int((0.5 + std::sin(angle) * saturation * 0.48) * side), 0, side - 1);
            scope.vectorscope[std::size_t(rowIndex * side + column)] += weight;
        }
    }
    return scope;
}
Pixel packedPixel(const std::uint8_t* rgba, std::size_t stride, int x, int y) {
    const auto* p = rgba + std::size_t(y) * stride + std::size_t(x) * 4;
    return {p[0], p[1], p[2], p[3]};
}
template<class Sample>
std::optional<std::pair<double, double>> autoBalanceFrom(int width, int height, const std::function<bool()>& cancelled, const Sample& sample) {
    double red = 0, green = 0, blue = 0, count = 0;
    std::array<double, 256> opaque{};
    for (std::size_t i = 0; i < opaque.size(); ++i) opaque[i] = compositor::srgbToLinear(double(i) / 255);
    for (int y = 0; y < height; ++y) {
        if (cancelled && cancelled()) return {};
        for (int x = 0; x < width; ++x) {
            const auto p = sample(x, y);
            if (!p.a) continue;
            const double alpha = p.a;
            auto linear = [&](std::uint8_t value) {
                return p.a == 255 ? opaque[value] : compositor::srgbToLinear(std::min(1., value / alpha));
            };
            red += linear(p.r); green += linear(p.g); blue += linear(p.b); count += 1;
        }
    }
    if (count <= 0) return {};
    return CameraRawSettings::neutralizeLinear(red / count, green / count, blue / count);
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
    return autoBalanceFrom(width, height, {}, [&](int x, int y) { return packedPixel(rgba, std::size_t(stride), x, y); });
}
std::optional<std::pair<double, double>> CameraRawSettings::autoBalance(const Raster& source, const std::function<bool()>& cancelled) {
    return autoBalanceFrom(source.width, source.height, cancelled, [&](int x, int y) { return source.pixel(x, y); });
}
double CameraRawScope::peak() const { return std::max(displayScale(red), std::max(displayScale(green), displayScale(blue))); }
CameraRawScope CameraRawScope::make(const std::uint8_t* rgba, int width, int height) {
    if (!rgba) return {};
    return scopeFrom(width, height, 0, {}, [&](int x, int y) { return packedPixel(rgba, std::size_t(width) * 4, x, y); });
}
CameraRawScope CameraRawScope::make(const Raster& source, int limit, const std::function<bool()>& cancelled) {
    return scopeFrom(source.width, source.height, limit, cancelled, [&](int x, int y) { return source.pixel(x, y); });
}
CameraRawScope CameraRawScope::make(const DeepImage& image, int limit, const std::function<bool()>& cancelled) {
    if (!image.valid()) throw std::runtime_error("Invalid Camera Raw scope source");
    // Match normalizeDeep's SDR scope, including dither at the native coordinates,
    // without allocating a normalized full-resolution image.
    return scopeFrom(image.width, image.height, limit, cancelled, [&](int x, int y) {
        const auto p = image.pixel(x, y);
        if (image.isHdr() && std::any_of(p.begin(), p.end(), [](float sample) { return !std::isfinite(sample); }))
            throw std::runtime_error("Non-finite Camera Raw scope sample");
        if (!(p[3] > 0)) return Pixel{};
        const float scale = std::max({1.f, p[0] / p[3], p[1] / p[3], p[2] / p[3]});
        auto channel = [&](float value) {
            return ditherChannel(std::uint16_t(std::lround(std::clamp(value, 0.f, 1.f) * 65535)), x, y);
        };
        const auto alpha = channel(p[3]);
        return Pixel{std::min(alpha, channel(p[0] / scale)), std::min(alpha, channel(p[1] / scale)),
                     std::min(alpha, channel(p[2] / scale)), alpha};
    });
}
CameraRawRender renderCameraRaw(const Raster& source, const CameraRawSettings& incoming, const CameraRawView& view,
                                const GrayRaster* selection, const std::function<bool()>& cancelled, CameraRawShadowAnalysis* analysis) {
    const int w = source.width, h = source.height;
    const auto settings = incoming.normalized();
    if (cancelled && cancelled()) throw std::runtime_error("Filter cancelled");
    if (selection && (selection->width != w || selection->height != h || !selection->validStorage()))
        throw std::runtime_error("Camera Raw selection must match the source grid");
    CameraRawRender result;
    if (settings.isIdentity() && !view.paints()) {
        if (view.scopeLimit >= 0)
            result.scope = CameraRawScope::make(source, view.scopeLimit, cancelled);
        return result;
    }

    // The scope samples the clean grade. Alt and point-color displays are painted
    // separately only when required, and never used as the shadow-analysis input.
    const bool diagnostic = view.clipping || view.sharpenMask || view.visualizePointColor >= 0;
    std::vector<std::uint8_t> display;
    if (!diagnostic || view.scopeLimit >= 0) {
        CameraRawView clean;
        clean.scale = view.scale; clean.seed = view.seed;
        display = paint(source.rgba(cancelled), w, h, settings, clean, cancelled, analysis);
        blendSelection(display, source, selection, cancelled);
        if (view.scopeLimit >= 0)
            result.scope = scopeFrom(w, h, view.scopeLimit, cancelled,
                                     [&](int x, int y) { return packedPixel(display.data(), std::size_t(w) * 4, x, y); });
    }
    if (diagnostic) {
        std::vector<std::uint8_t>().swap(display);
        display = paint(source.rgba(cancelled), w, h, settings, view, cancelled);
        blendSelection(display, source, selection, cancelled);
    }
    if (!view.clipping && !view.sharpenMask && (view.shadowOverlay || view.highlightOverlay)) {
        const auto stride = std::size_t(w) * 4;
        cameraRawRows(w, h, cancelled, [&](int first, int count) {
            adjust_camera_raw_clip_overlay(display.data() + std::size_t(first) * stride, w, count, stride,
                                           view.shadowOverlay ? 1 : 0, view.highlightOverlay ? 1 : 0);
        });
    }
    result.raster = Raster::fromRgba(w, h, display.data(), std::size_t(w) * 4, cancelled);
    result.changed = true;
    return result;
}
bool cameraRawNeedsEightBit(const CameraRawSettings& incoming) {
    const auto settings = incoming.normalized();
    return settings.mixer.adjusts() || settings.grading.adjusts() || settings.adjustsEffects() || settings.detail.adjusts() || settings.optics.adjusts();
}
enum class GradeStage { All, BeforeShadows, AfterShadows };
struct DeepGrade {
    const CameraRawSettings& settings;
    CameraRawSettings::Gains gains = settings.gains();
    double light = std::exp2(settings.exposure);
    double contrastScale = 1 + settings.contrast / 100;
    bool curves = settings.curve.adjusts();
    std::vector<std::array<double, 3>> opaqueLight;
    double lightChannel(double value, double gain) const {
        const double linear = std::clamp(compositor::srgbToLinear(value) * gain * light, 0., 1.);
        return std::clamp(0.5 + (compositor::linearToSrgb(linear) - 0.5) * contrastScale, 0., 1.);
    }
};
void gradeStraight(double& r, double& g, double& b, const DeepGrade& grade, GradeStage stage, const std::uint16_t* opaque) {
    const auto& settings = grade.settings;
    if (stage != GradeStage::AfterShadows) {
        if (opaque) {
            r = grade.opaqueLight[opaque[0]][0];
            g = grade.opaqueLight[opaque[1]][1];
            b = grade.opaqueLight[opaque[2]][2];
        } else {
            r = grade.lightChannel(r, grade.gains.red);
            g = grade.lightChannel(g, grade.gains.green);
            b = grade.lightChannel(b, grade.gains.blue);
        }
    }
    auto rec = [&] { return 0.2126 * r + 0.7152 * g + 0.0722 * b; };
    auto scaleLum = [&](double target) {
        target = std::clamp(target, 0., 1.);
        const double y = rec();
        if (std::abs(target - y) < 1e-8) return;
        if (y < 1e-8) {
            if (target > y) r = g = b = target;
            return;
        }
        const double scale = target / y;
        r = std::clamp(r * scale, 0., 1.);
        g = std::clamp(g * scale, 0., 1.);
        b = std::clamp(b * scale, 0., 1.);
    };
    auto highlights = [&](double y, double amount) {
        const double t = std::clamp((y - 0.5) / 0.5, 0., 1.);
        const double weight = t * t;
        return amount >= 0 ? std::clamp(y + amount * weight * (1 - y), 0., 1.) : std::clamp(y + amount * weight * (y - 0.5), 0., 1.);
    };
    auto shadows = [&](double y, double amount) {
        const double t = std::clamp((0.5 - y) / 0.5, 0., 1.);
        const double weight = t * t;
        return amount >= 0 ? std::clamp(y + amount * weight * (0.5 - y), 0., 1.) : std::clamp(y + amount * weight * y, 0., 1.);
    };
    auto whites = [&](double y, double amount) { return y <= 0.75 ? y : std::clamp(0.75 + (y - 0.75) * (1 + amount), 0., 1.); };
    auto blacks = [&](double y, double amount) { return y >= 0.25 ? y : std::clamp(0.25 + (y - 0.25) * (1 - amount), 0., 1.); };
    if (stage != GradeStage::AfterShadows && settings.highlights) scaleLum(highlights(rec(), settings.highlights / 100));
    if (stage == GradeStage::All && settings.shadows) scaleLum(shadows(rec(), settings.shadows / 100));
    if (stage == GradeStage::BeforeShadows) return;
    if (settings.whites) scaleLum(whites(rec(), settings.whites / 100));
    if (settings.blacks) scaleLum(blacks(rec(), settings.blacks / 100));
    const double vibrance = settings.vibrance / 100, saturation = settings.saturation / 100;
    double lum = rec();
    double amount = 0;
    if (vibrance) {
        const double maxc = std::max({r, g, b}), minc = std::min({r, g, b}), chroma = maxc - minc;
        const double sat = maxc <= 1e-8 ? 0 : chroma / maxc;
        amount = vibrance * (1 - sat);
        if (vibrance > 0 && chroma > 1e-8) {
            double hue;
            if (r >= g && r >= b) hue = 60 * std::fmod((g - b) / chroma, 6.);
            else if (g >= r && g >= b) hue = 60 * ((b - r) / chroma + 2);
            else hue = 60 * ((r - g) / chroma + 4);
            if (hue < 0) hue += 360;
            if (hue >= 10 && hue <= 50) {
                const double skin = (hue <= 30 ? (hue - 10) / 20 : (50 - hue) / 20) * std::clamp((sat - 0.15) / 0.35, 0., 1.);
                amount *= 1 - 0.7 * skin;
            }
        }
    }
    double factor = 1 + amount;
    r = std::clamp(lum + (r - lum) * factor, 0., 1.);
    g = std::clamp(lum + (g - lum) * factor, 0., 1.);
    b = std::clamp(lum + (b - lum) * factor, 0., 1.);
    lum = rec();
    factor = 1 + saturation;
    r = std::clamp(lum + (r - lum) * factor, 0., 1.);
    g = std::clamp(lum + (g - lum) * factor, 0., 1.);
    b = std::clamp(lum + (b - lum) * factor, 0., 1.);
    if (grade.curves) {
        auto apply = [&](double tone, const std::vector<Point>& channel) { return curveAt(curveAt(settings.curve.parametric(tone), settings.curve.rgb), channel); };
        r = apply(r, settings.curve.red);
        g = apply(g, settings.curve.green);
        b = apply(b, settings.curve.blue);
    }
}
std::shared_ptr<const DeepImage> renderCameraRaw(const DeepImage& source, const CameraRawSettings& incoming, const CameraRawView& view,
                                               const GrayRaster* selection, const std::function<bool()>& cancelled, CameraRawShadowAnalysis* analysis) {
    if (!source.valid()) throw std::runtime_error("Invalid Camera Raw source");
    const auto settings = incoming.normalized();
    auto stop = [&] { if (cancelled && cancelled()) throw std::runtime_error("Filter cancelled"); };
    stop();
    if (source.isHdr() && (settings.geometry.adjusts() || cameraRawNeedsEightBit(settings)))
        throw std::runtime_error("Convert HDR to 16-Bit before spatial Camera Raw corrections");
    if (!source.isHdr() && cameraRawNeedsEightBit(settings))
        throw std::runtime_error("Convert this 16-bit layer to 8-bit before spatial Camera Raw corrections");
    if (settings.isIdentity() && !view.paints()) return std::make_shared<DeepImage>(source);
    if (selection && (selection->width != source.width || selection->height != source.height || !selection->validStorage()))
        throw std::runtime_error("Camera Raw selection must match the source grid");
    const int width = source.width, height = source.height;
    const bool geometry = settings.geometry.adjusts(), adaptive = settings.shadows != 0;
    const bool spatial = geometry || adaptive;
    const bool grade = settings.adjustsLight() || settings.adjustsColor() || settings.curve.adjusts() || view.clipping;
    std::vector<float> pixels;
    if (spatial) {
        pixels.resize(std::size_t(width) * height * 4);
        cameraRawRows(width, height, cancelled, [&](int first, int count) {
            for (int y = first; y < first + count; ++y) for (int x = 0; x < width; ++x) {
                const auto p = source.pixel(x, y);
                std::copy(p.begin(), p.end(), pixels.begin() + (std::size_t(y) * width + x) * 4);
            }
        });
    }
    if (geometry) {
        double vertical = settings.geometry.vertical, horizontal = settings.geometry.horizontal, rotate = settings.geometry.rotate;
        if (settings.geometry.upright == CameraRawUpright::Guided && !settings.geometry.guides.empty()) {
            const auto& first = settings.geometry.guides.front();
            const double dx = first.endX - first.startX, dy = first.endY - first.startY;
            if (std::hypot(dx, dy) > 1e-4) {
                double angle = std::atan2(dy, dx) * 180 / std::numbers::pi;
                double guided = -angle;
                if (guided > 45) guided -= 90;
                else if (guided < -45) guided += 90;
                rotate += guided;
            }
        }
        const int w = source.width, h = source.height;
        const double width = w, height = h;
        const double strength = settings.geometry.projection == CameraRawProjection::Perspective ? 1 : 0.55;
        const double v = vertical / 100 * width * 0.18 * strength;
        const double hz = horizontal / 100 * height * 0.18 * strength;
        const double aspectScale = 1 + settings.geometry.aspect / 200;
        const double zoom = 1 + settings.geometry.scale / 100;
        const double shiftX = settings.geometry.offsetX / 100 * width * 0.15;
        const double shiftY = settings.geometry.offsetY / 100 * height * 0.15;
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
        const auto inverse = Homography::fromCorners({down(topLeft), down(topRight), down(bottomRight), down(bottomLeft)}).inverse();
        if (inverse) {
            auto previous = pixels;
            for (int y = 0; y < h; ++y) {
                stop();
                for (int x = 0; x < w; ++x) {
                    const auto unit = inverse->map({x + .5, y + .5});
                    const double sx = unit.x * width - .5, sy = unit.y * height - .5;
                    auto i = (std::size_t(y) * w + x) * 4;
                    if (!std::isfinite(sx) || !std::isfinite(sy)) {
                        pixels[i] = pixels[i + 1] = pixels[i + 2] = pixels[i + 3] = 0;
                        continue;
                    }
                    const int ix = int(std::floor(sx)), iy = int(std::floor(sy));
                    const double fx = sx - ix, fy = sy - iy;
                    auto get = [&](int xx, int yy, int c) {
                        if (xx < 0 || yy < 0 || xx >= w || yy >= h) return 0.f;
                        return previous[(std::size_t(yy) * w + xx) * 4 + c];
                    };
                    for (int c = 0; c < 4; ++c)
                        pixels[i + c] = float((get(ix, iy, c) * (1 - fx) + get(ix + 1, iy, c) * fx) * (1 - fy) + (get(ix, iy + 1, c) * (1 - fx) + get(ix + 1, iy + 1, c) * fx) * fy);
                }
            }
        }
    }
    DeepGrade parameters{settings};
    if (grade && !source.isHdr() && !geometry && std::uint64_t(width) * height >= 256 * 1024) {
        parameters.opaqueLight.resize(65536);
        for (std::size_t i = 0; i < parameters.opaqueLight.size(); ++i) {
            const double value = float(i) / 65535.f;
            parameters.opaqueLight[i] = {parameters.lightChannel(value, parameters.gains.red),
                                         parameters.lightChannel(value, parameters.gains.green),
                                         parameters.lightChannel(value, parameters.gains.blue)};
        }
    }
    auto opaqueSource = [&](int x, int y, float alpha) {
        return alpha == 1 && !parameters.opaqueLight.empty() ? source.at(x, y) : nullptr;
    };
    // HDR normalization matches the grade path. Keep each pixel's original scale
    // while the spatial pass works on normalized values.
    std::vector<float> hdrScales;
    if (adaptive && source.isHdr()) hdrScales.resize(std::size_t(width) * height, 1.f);
    if (adaptive) {
        cameraRawRows(width, height, cancelled, [&](int first, int count) {
            for (int y = first; y < first + count; ++y) for (int x = 0; x < width; ++x) {
                const auto i = (std::size_t(y) * width + x) * 4;
                const float alpha = pixels[i + 3];
                if (!(alpha > 0)) continue;
                double r = pixels[i] / alpha, g = pixels[i + 1] / alpha, b = pixels[i + 2] / alpha;
                const double highlightScale = source.isHdr() ? std::max({1., r, g, b}) : 1.;
                if (!hdrScales.empty()) hdrScales[i / 4] = float(highlightScale);
                r /= highlightScale; g /= highlightScale; b /= highlightScale;
                gradeStraight(r, g, b, parameters, GradeStage::BeforeShadows, opaqueSource(x, y, alpha));
                pixels[i] = float(r * alpha); pixels[i + 1] = float(g * alpha); pixels[i + 2] = float(b * alpha);
            }
        });
        applyAdaptiveShadows(pixels, width, height, settings.shadows, view.scale, cancelled, analysis);
    }

    auto image = makeDeep(width, height, source.format);
    cameraRawRows(width, height, cancelled, [&](int first, int count) {
        for (int y = first; y < first + count; ++y) for (int x = 0; x < width; ++x) {
            const auto i = (std::size_t(y) * width + x) * 4;
            auto p = spatial ? WidePixel{pixels[i], pixels[i + 1], pixels[i + 2], pixels[i + 3]} : source.pixel(x, y);
            const float alpha = p[3];
            if (grade && alpha > 0) {
                double r = p[0] / alpha, g = p[1] / alpha, b = p[2] / alpha;
                const double highlightScale = adaptive ? (hdrScales.empty() ? 1. : hdrScales[i / 4])
                                                       : (source.isHdr() ? std::max({1., r, g, b}) : 1.);
                if (!adaptive) { r /= highlightScale; g /= highlightScale; b /= highlightScale; }
                gradeStraight(r, g, b, parameters, adaptive ? GradeStage::AfterShadows : GradeStage::All,
                              adaptive ? nullptr : opaqueSource(x, y, alpha));
                r *= highlightScale; g *= highlightScale; b *= highlightScale;
                if (view.clipping == 1) {
                    r = r >= 254.5 / 255 ? 1 : 0;
                    g = g >= 254.5 / 255 ? 1 : 0;
                    b = b >= 254.5 / 255 ? 1 : 0;
                } else if (view.clipping == 2) {
                    const int rc = r <= 0.5 / 255, gc = g <= 0.5 / 255, bc = b <= 0.5 / 255;
                    if (rc || gc || bc) {
                        r = rc ? 0 : 1; g = gc ? 0 : 1; b = bc ? 0 : 1;
                    } else r = g = b = 1;
                }
                p[0] = float(r * alpha); p[1] = float(g * alpha); p[2] = float(b * alpha);
            }
            p[3] = std::clamp(alpha, 0.f, 1.f);
            if (selection) {
                const float amount = selection->pixel(x, y) / 255.f;
                const auto old = source.pixel(x, y);
                for (int c = 0; c < 4; ++c) p[c] = old[c] + (p[c] - old[c]) * amount;
            }
            image->setPixel(x, y, p);
        }
    });
    return image;
}
}
