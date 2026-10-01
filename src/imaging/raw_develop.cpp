#include "raw_develop.h"
#include "wic_codec.h"
#include <cmath>
#include <algorithm>
namespace compositor::imaging {
namespace {
double linear(double encoded) { return encoded <= .04045 ? encoded / 12.92 : std::pow((encoded + .055) / 1.055, 2.4); }
double encoded(double value) { value = std::clamp(value, 0., 1.); return value <= .0031308 ? value * 12.92 : 1.055 * std::pow(value, 1 / 2.4) - .055; }
}
DecodedImage decodeRaw(const std::filesystem::path& path, RawDevelop develop, const ImportOptions& options) {
    DecodedImage decoded;
    try { decoded = WicCodec::decode(path, options); }
    catch (const std::exception&) {
        auto extension = path.extension().wstring(); for (auto& c : extension) c = towlower(c);
        if (extension == L".cr2" || extension == L".nef" || extension == L".arw" || extension == L".dng" || extension == L".raw" || extension == L".rw2")
            throw std::runtime_error("No RAW codec is installed for this file");
        throw;
    }
    if (develop.exposure == 0 && develop.temperature == 0) return decoded;
    if (!std::isfinite(develop.exposure) || !std::isfinite(develop.temperature) || std::abs(develop.exposure) > 5 || std::abs(develop.temperature) > 1)
        throw std::runtime_error("Invalid RAW development");
    double gain = std::pow(2., develop.exposure);
    for (uint32_t y = 0; y < decoded.image.height; ++y) for (uint32_t x = 0; x < decoded.image.width; ++x) {
        auto* p = decoded.image.pixels.data() + y * decoded.image.stride + x * 4; if (!p[3]) continue;
        double rgb[3]; for (int c = 0; c < 3; ++c) rgb[c] = linear(p[c] / double(p[3])) * gain;
        rgb[0] = std::clamp(rgb[0] * (1 + develop.temperature), 0., 1.); rgb[2] = std::clamp(rgb[2] * (1 - develop.temperature), 0., 1.);
        for (int c = 0; c < 3; ++c) p[c] = uint8_t(std::lround(encoded(rgb[c]) * p[3]));
    }
    decoded.metadata.decoder = "raw"; return decoded;
}
}
