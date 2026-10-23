#include "raw_develop.h"
#include <libraw/libraw.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cwctype>
#include <memory>
#include <stdexcept>
#include <string>

namespace compositor::imaging {
namespace {
using Vec3 = std::array<double, 3>;
using Mat3 = std::array<Vec3, 3>;

// Nearest-neighbour keeps premultiplied pixels valid and is only used for on-screen previews.
RgbaImage downsample(const RgbaImage& source, std::uint32_t maxSide) {
    if (std::max(source.width, source.height) <= maxSide) return source;
    const double scale = double(maxSide) / std::max(source.width, source.height);
    RgbaImage out;
    out.width = std::max<std::uint32_t>(1, static_cast<std::uint32_t>(std::lround(source.width * scale)));
    out.height = std::max<std::uint32_t>(1, static_cast<std::uint32_t>(std::lround(source.height * scale)));
    out.stride = std::size_t(out.width) * 4;
    out.pixels.resize(out.stride * out.height);
    for (std::uint32_t y = 0; y < out.height; ++y) {
        const auto sy = std::min<std::uint32_t>(source.height - 1, static_cast<std::uint32_t>((y + 0.5) / scale));
        for (std::uint32_t x = 0; x < out.width; ++x) {
            const auto sx = std::min<std::uint32_t>(source.width - 1, static_cast<std::uint32_t>((x + 0.5) / scale));
            std::copy_n(source.pixels.data() + std::size_t(sy) * source.stride + std::size_t(sx) * 4, 4, out.pixels.data() + std::size_t(y) * out.stride + std::size_t(x) * 4);
        }
    }
    return out;
}

// LibRaw failures keep their meaning for the UI; cancellation uses the same wording as other import cancellations.
std::runtime_error librawError(const char* stage, int code) {
    switch (code) {
    case LIBRAW_CANCELLED_BY_CALLBACK: return std::runtime_error("Image operation cancelled");
    case LIBRAW_TOO_BIG:
    case LIBRAW_UNSUFFICIENT_MEMORY: return std::runtime_error("RAW image exceeds the import memory budget");
    case LIBRAW_FILE_UNSUPPORTED: return std::runtime_error("Unsupported RAW file: LibRaw does not recognise this format");
    case LIBRAW_IO_ERROR: return std::runtime_error("Cannot read RAW file");
    default: return std::runtime_error(std::string("Malformed RAW file (") + stage + ": " + libraw_strerror(code) + ")");
    }
}

int progressCallback(void* data, LibRaw_progress, int, int) {
    const auto& cancelled = *static_cast<const std::function<bool()>*>(data);
    return cancelled && cancelled() ? 1 : 0;
}

struct MemImageDeleter {
    void operator()(libraw_processed_image_t* image) const { LibRaw::dcraw_clear_mem(image); }
};

// Planckian chromaticity (Kim et al., 2002 approximation) for 2000..12000 K.
Vec3 planckianXyz(double kelvin) {
    const double t = kelvin, t2 = t * t, t3 = t2 * t;
    const double x = t <= 4000 ? -0.2661239e9 / t3 - 0.2343589e6 / t2 + 0.8776956e3 / t + 0.179910
                               : -3.0258469e9 / t3 + 2.1070379e6 / t2 + 0.2226347e3 / t + 0.240390;
    const double x2 = x * x, x3 = x2 * x;
    const double y = t <= 2222 ? -1.1063814 * x3 - 1.34811020 * x2 + 2.18555832 * x - 0.20219683
                   : t <= 4000 ? -0.9549476 * x3 - 1.37418593 * x2 + 2.09137015 * x - 0.16748867
                               : 3.0817580 * x3 - 5.87338670 * x2 + 3.75112997 * x - 0.37001483;
    return {x / y, 1.0, (1.0 - x - y) / y};
}

// Camera-channel multipliers, normalised to green = 1, that neutralise the Planckian illuminant at kelvin.
// LibRaw's cam_xyz has one row per camera channel and one column per XYZ component, so rgb = cam_xyz * XYZ.
// Throws when the matrix gives a non-positive response, which means the file has no usable colour data.
Vec3 multipliersFor(const Mat3& cameraXyz, double kelvin) {
    const auto xyz = planckianXyz(kelvin);
    Vec3 rgb{};
    for (int c = 0; c < 3; ++c) rgb[c] = cameraXyz[c][0] * xyz[0] + cameraXyz[c][1] * xyz[1] + cameraXyz[c][2] * xyz[2];
    if (!(rgb[0] > 0 && rgb[1] > 0 && rgb[2] > 0)) throw std::runtime_error("RAW camera colour data is unusable");
    return {rgb[1] / rgb[0], 1.0, rgb[1] / rgb[2]};
}

// Kelvin and tint in the dialog: tint scales the green multiplier; positive tint moves toward magenta.
Vec3 whiteBalanceMultipliers(const Mat3& cameraXyz, double kelvin, double tint) {
    auto m = multipliersFor(cameraXyz, kelvin);
    m[1] *= 1.0 - tint / 300.0;
    return m;
}

struct WhiteBalanceInfo {
    bool usable{};
    Mat3 cameraXyz{};
    std::uint32_t asShotKelvin{};
    double asShotTint{};
};

// Bisects the illuminant whose camera-space blue/red ratio matches the camera's as-shot multipliers, then reads tint from the residual green.
WhiteBalanceInfo estimateWhiteBalance(const LibRaw& raw) {
    WhiteBalanceInfo info;
    const auto& color = raw.imgdata.color;
    for (int c = 0; c < 3; ++c)
        for (int k = 0; k < 3; ++k) info.cameraXyz[c][k] = color.cam_xyz[c][k];
    const bool finiteMul = std::all_of(color.cam_mul, color.cam_mul + 3, [](float v) { return std::isfinite(v) && v > 0; });
    if (!finiteMul) return info;
    try {
        const double target = std::log(color.cam_mul[2] / color.cam_mul[0]);
        double low = kRawKelvinMin, high = kRawKelvinMax;
        for (int i = 0; i < 40; ++i) {
            const double mid = (low + high) / 2;
            const auto m = multipliersFor(info.cameraXyz, mid);
            // The ratio falls as the illuminant warms: a value above the target means the true illuminant is warmer than mid.
            if (std::log(m[2] / m[0]) > target) low = mid;
            else high = mid;
        }
        const double kelvin = (low + high) / 2;
        const auto m = multipliersFor(info.cameraXyz, kelvin);
        const double green = std::sqrt((m[0] * color.cam_mul[1] / color.cam_mul[0]) * (m[2] * color.cam_mul[1] / color.cam_mul[2]));
        info.asShotKelvin = static_cast<std::uint32_t>(std::lround(kelvin));
        info.asShotTint = std::clamp(300.0 * (1.0 - green), -kRawTintLimit, kRawTintLimit);
        info.usable = std::isfinite(info.asShotTint);
    } catch (const std::runtime_error&) {
        info.usable = false;
    }
    return info;
}

std::vector<std::uint8_t> srgbLookup(double gain) {
    std::vector<std::uint8_t> lookup(65536);
    for (std::size_t v = 0; v < lookup.size(); ++v) {
        const double linear = std::clamp(v / 65535.0 * gain, 0.0, 1.0);
        const double encoded = linear <= 0.0031308 ? 12.92 * linear : 1.055 * std::pow(linear, 1 / 2.4) - 0.055;
        lookup[v] = static_cast<std::uint8_t>(std::lround(std::clamp(encoded, 0.0, 1.0) * 255));
    }
    return lookup;
}

void validateSettings(const RawAdjustments& raw) {
    if ((raw.exposure && (!std::isfinite(*raw.exposure) || std::abs(*raw.exposure) > kRawExposureLimitEv)) ||
        (raw.kelvin && (*raw.kelvin < kRawKelvinMin || *raw.kelvin > kRawKelvinMax)) ||
        (raw.tint && (!std::isfinite(*raw.tint) || std::abs(*raw.tint) > kRawTintLimit)) ||
        (raw.noiseReduction && (!std::isfinite(*raw.noiseReduction) || *raw.noiseReduction < 0 || *raw.noiseReduction > kRawNoiseLimit)))
        throw std::runtime_error("Invalid RAW development settings");
}
}

bool isRawFileExtension(const std::filesystem::path& path) {
    auto extension = path.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return extension == L".cr2" || extension == L".nef" || extension == L".arw" || extension == L".dng" || extension == L".raw" || extension == L".rw2";
}

RawDevelopment developRaw(const std::filesystem::path& path, const ImportOptions& options, std::uint32_t previewMaxSide) {
    validateSettings(options.raw);
    checkCancelled(options);

    LibRaw raw;
    raw.set_progress_handler(progressCallback, const_cast<std::function<bool()>*>(&options.cancelled));
    raw.imgdata.rawparams.max_raw_memory_mb = static_cast<unsigned>(std::max<std::uint64_t>(1, options.maxWorkingBytes / (1024 * 1024)));
    if (int code = raw.open_file(path.c_str()); code != LIBRAW_SUCCESS) throw librawError("open", code);
    // Bounds the raw buffer before unpacking; the processed image is bounded again below.
    checkedBytes(raw.imgdata.sizes.raw_width, raw.imgdata.sizes.raw_height, 1, options, 2);
    if (int code = raw.unpack(); code != LIBRAW_SUCCESS) throw librawError("unpack", code);

    const auto whiteBalance = estimateWhiteBalance(raw);
    const auto& settings = options.raw;
    const bool adjustsWhiteBalance = settings.kelvin || settings.tint;
    if (adjustsWhiteBalance && !whiteBalance.usable) throw std::runtime_error("This RAW file cannot change white balance");

    auto& params = raw.imgdata.params;
    params.output_color = 1;   // sRGB primaries
    params.gamm[0] = 1.0;      // linear output, so exposure and the sRGB encoding happen here
    params.gamm[1] = 1.0;
    params.no_auto_bright = 1; // keeps brightness fixed so exposure is the only tonal shift
    params.output_bps = 16;
    params.user_flip = options.applyExifOrientation ? -1 : 0;
    params.threshold = static_cast<float>(settings.noiseReduction.value_or(0.0) * 5.0);
    params.use_camera_wb = adjustsWhiteBalance ? 0 : 1;
    params.use_auto_wb = 0;
    if (adjustsWhiteBalance) {
        const double kelvin = settings.kelvin ? double(*settings.kelvin) : double(whiteBalance.asShotKelvin);
        const double tint = settings.tint.value_or(whiteBalance.asShotTint);
        const auto m = whiteBalanceMultipliers(whiteBalance.cameraXyz, kelvin, tint);
        params.user_mul[0] = float(m[0]);
        params.user_mul[1] = float(m[1]);
        params.user_mul[2] = float(m[2]);
        params.user_mul[3] = float(m[1]);
    }

    if (int code = raw.dcraw_process(); code != LIBRAW_SUCCESS) throw librawError("process", code);
    int errorCode = 0;
    std::unique_ptr<libraw_processed_image_t, MemImageDeleter> processed(raw.dcraw_make_mem_image(&errorCode));
    if (!processed) throw librawError("memory image", errorCode);
    if (processed->colors != 3 || processed->bits != 16 || !processed->width || !processed->height)
        throw std::runtime_error("Unsupported RAW output layout");
    const auto width = processed->width, height = processed->height;
    checkedBytes(width, height, 3, options, 2);

    const auto lookup = srgbLookup(settings.exposure ? std::pow(2.0, *settings.exposure) : 1.0);
    const auto* samples = reinterpret_cast<const std::uint16_t*>(processed->data);
    DecodedImage decoded;
    decoded.image.width = width;
    decoded.image.height = height;
    decoded.image.stride = std::size_t(width) * 4;
    decoded.image.pixels.assign(decoded.image.stride * height, 255);
    for (std::uint32_t y = 0; y < height; ++y) {
        const auto* in = samples + std::size_t(y) * width * 3;
        auto* out = decoded.image.pixels.data() + std::size_t(y) * decoded.image.stride;
        for (std::uint32_t x = 0; x < width; ++x) {
            out[x * 4 + 0] = lookup[in[x * 3 + 0]];
            out[x * 4 + 1] = lookup[in[x * 3 + 1]];
            out[x * 4 + 2] = lookup[in[x * 3 + 2]];
        }
    }
    checkCancelled(options);
    decoded.metadata.originalOrientation = 1; // LibRaw already applied the EXIF rotation.
    decoded.metadata.profileStatus = ProfileStatus::DeclaredSrgb;
    decoded.metadata.decoder = "LibRaw";

    RawDevelopment developed;
    developed.capabilities.developable = true;
    developed.capabilities.exposure = true;
    developed.capabilities.temperature = whiteBalance.usable;
    developed.capabilities.tint = whiteBalance.usable;
    developed.capabilities.noiseReduction = true;
    developed.capabilities.minKelvin = kRawKelvinMin;
    developed.capabilities.maxKelvin = kRawKelvinMax;
    developed.asShot.exposure = 0;
    developed.asShot.kelvin = whiteBalance.asShotKelvin;
    developed.asShot.tint = whiteBalance.asShotTint;
    developed.asShot.noiseReduction = 0;
    developed.image = previewMaxSide ? DecodedImage{downsample(decoded.image, previewMaxSide), decoded.metadata} : std::move(decoded);
    return developed;
}

DecodedImage decodeRaw(const std::filesystem::path& path, const ImportOptions& options) {
    return developRaw(path, options).image;
}
}
