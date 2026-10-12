#include "PsdDocument.h"
#include "PsdText.h"
#include "core/Document.h"
#include "text/TextRaster.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <vector>
#include <windows.h>
namespace compositor::imaging {
namespace {
struct Reader {
    const uint8_t* data; size_t size, at{};
    bool psb{};
    void need(size_t n) const { if (at > size || n > size - at) throw std::runtime_error("PSD ended early"); }
    uint8_t u8() { need(1); return data[at++]; }
    uint16_t u16() { need(2); uint16_t v = uint16_t(data[at] << 8 | data[at + 1]); at += 2; return v; }
    int16_t i16() { return int16_t(u16()); }
    uint32_t u32() { need(4); uint32_t v = 0; for (int i = 0; i < 4; ++i) v = (v << 8) | data[at++]; return v; }
    int32_t i32() { return int32_t(u32()); }
    uint64_t u64() { need(8); uint64_t v = 0; for (int i = 0; i < 8; ++i) v = (v << 8) | data[at++]; return v; }
    uint64_t wide() { return psb ? u64() : u32(); }
    void skip(uint64_t n) { if (n > size - at) throw std::runtime_error("PSD ended early"); at += size_t(n); }
    std::string ascii(size_t n) { need(n); std::string out(reinterpret_cast<const char*>(data + at), n); at += n; return out; }
};
Blend blendOf(const std::string& key, std::string& report) {
    if (key == "norm") return Blend::Normal; if (key == "mul ") return Blend::Multiply; if (key == "scrn") return Blend::Screen;
    if (key == "over") return Blend::Overlay; if (key == "dark") return Blend::Darken; if (key == "lite") return Blend::Lighten;
    if (key == "diff") return Blend::Difference; if (key == "div ") return Blend::ColorDodge; if (key == "idiv") return Blend::ColorBurn;
    if (key == "hue ") return Blend::Hue; if (key == "sat ") return Blend::Saturation; if (key == "colr") return Blend::Color;
    if (key == "lum ") return Blend::Luminosity; if (key == "lbrn") return Blend::LinearBurn; if (key == "lddg") return Blend::LinearDodge;
    if (key == "sLit") return Blend::SoftLight; if (key == "hLit") return Blend::HardLight; if (key == "vLit") return Blend::VividLight;
    if (key == "lLit") return Blend::LinearLight; if (key == "pLit") return Blend::PinLight; if (key == "hMix") return Blend::HardMix;
    if (key == "smud") return Blend::Exclusion; if (key == "fsub") return Blend::Subtract; if (key == "fdiv") return Blend::Divide;
    report += "Blend " + key + " became Normal.\n"; return Blend::Normal;
}
bool wideExtra(bool psb, const std::string& signature, const std::string& key) {
    if (signature == "8B64") return true;
    return psb && (key == "LMsk" || key == "Lr16" || key == "Lr32" || key == "Layr" || key == "Mt16" || key == "Mt32" || key == "Mtrn" || key == "Alph" || key == "FMsk" || key == "lnk2" || key == "FEid" || key == "FXid" || key == "PxSD");
}
void appendUtf8(std::string& out, uint32_t code) {
    if (code < 0x80) out.push_back(char(code));
    else if (code < 0x800) { out.push_back(char(0xC0 | code >> 6)); out.push_back(char(0x80 | (code & 0x3F))); }
    else if (code < 0x10000) { out.push_back(char(0xE0 | code >> 12)); out.push_back(char(0x80 | ((code >> 6) & 0x3F))); out.push_back(char(0x80 | (code & 0x3F))); }
    else { out.push_back(char(0xF0 | code >> 18)); out.push_back(char(0x80 | ((code >> 12) & 0x3F))); out.push_back(char(0x80 | ((code >> 6) & 0x3F))); out.push_back(char(0x80 | (code & 0x3F))); }
}
std::string utf8FromUtf16Be(const uint8_t* data, size_t size) {
    std::string out;
    for (size_t i = 0; i + 1 < size;) {
        uint32_t unit = uint32_t(data[i] << 8 | data[i + 1]); i += 2;
        if (unit >= 0xD800 && unit <= 0xDBFF && i + 1 < size) {
            uint32_t low = uint32_t(data[i] << 8 | data[i + 1]);
            if (low >= 0xDC00 && low <= 0xDFFF) { i += 2; unit = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00); }
        }
        if (unit >= 0xDC00 && unit <= 0xDFFF) continue;
        appendUtf8(out, unit);
    }
    return out;
}
std::string macRomanToUtf8(const std::string& raw) {
    if (raw.empty()) return {};
    const int wideCount = MultiByteToWideChar(10000, 0, raw.data(), int(raw.size()), nullptr, 0);
    if (wideCount <= 0) return raw;
    std::wstring wide(size_t(wideCount), L'\0');
    MultiByteToWideChar(10000, 0, raw.data(), int(raw.size()), wide.data(), wideCount);
    const int utf8Count = WideCharToMultiByte(CP_UTF8, 0, wide.data(), wideCount, nullptr, 0, nullptr, nullptr);
    if (utf8Count <= 0) return raw;
    std::string utf8(size_t(utf8Count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), wideCount, utf8.data(), utf8Count, nullptr, nullptr);
    while (!utf8.empty() && utf8.back() == '\0') utf8.pop_back();
    return utf8;
}
// ZIP prediction stores each row as deltas. 16-bit samples are big-endian.
void unpredict(std::vector<uint8_t>& raw, int width, int height, int depth) {
    const int sample = depth == 16 ? 2 : 1;
    for (int y = 0; y < height; ++y) {
        size_t row = size_t(y) * size_t(width) * sample;
        if (depth == 16) {
            uint32_t previous = 0;
            for (int x = 0; x < width; ++x) {
                uint32_t current = uint32_t(raw[row + size_t(x) * 2] << 8 | raw[row + size_t(x) * 2 + 1]);
                uint32_t value = (previous + current) & 0xFFFF;
                raw[row + size_t(x) * 2] = uint8_t(value >> 8);
                raw[row + size_t(x) * 2 + 1] = uint8_t(value);
                previous = value;
            }
        } else {
            for (int x = 1; x < width; ++x) raw[row + size_t(x)] = uint8_t(raw[row + size_t(x)] + raw[row + size_t(x) - 1]);
        }
    }
}
struct BitReader {
    const uint8_t* data; size_t size, at{}; uint32_t bits{}; int count{};
    uint32_t pull(int width) {
        while (count < width) { if (at >= size) throw std::runtime_error("Unsupported PSD compression"); bits |= uint32_t(data[at++]) << count; count += 8; }
        uint32_t value = bits & ((1u << width) - 1u); bits >>= width; count -= width; return value;
    }
};
struct Huffman {
    int count[16]{}, symbol[320]{}, max{};
    static Huffman build(const int* lengths, int symbols) {
        Huffman tree;
        for (int symbol = 0; symbol < symbols; ++symbol) if (lengths[symbol] > 15) throw std::runtime_error("Unsupported PSD compression"); else if (lengths[symbol]) { tree.count[lengths[symbol]]++; tree.max = std::max(tree.max, lengths[symbol]); }
        int offsets[16]{};
        for (int length = 1; length < 15; ++length) offsets[length + 1] = offsets[length] + tree.count[length];
        for (int symbol = 0; symbol < symbols; ++symbol) if (lengths[symbol]) { int slot = offsets[lengths[symbol]]++; if (slot < 0 || slot >= 320) throw std::runtime_error("Unsupported PSD compression"); tree.symbol[slot] = symbol; }
        return tree;
    }
    int decode(BitReader& reader) const {
        int code = 0, first = 0, index = 0;
        for (int length = 1; length <= max; ++length) {
            code |= int(reader.pull(1)); int countAt = count[length];
            if (code - countAt < first) return symbol[index + (code - first)];
            index += countAt; first = (first + countAt) << 1; code <<= 1;
        }
        throw std::runtime_error("Unsupported PSD compression");
    }
};
std::vector<uint8_t> inflateZlib(const uint8_t* data, size_t size, size_t expected) {
    if (size < 8 || (data[0] & 0x0F) != 8) throw std::runtime_error("Unsupported PSD compression");
    BitReader reader{data + 2, size - 6};
    std::vector<uint8_t> out(expected);
    size_t produced = 0;
    const int lengthBase[] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
    const int lengthExtra[] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
    const int distanceBase[] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
    const int distanceExtra[] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
    const int order[] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
    bool last = false;
    while (!last) {
        last = reader.pull(1) != 0; auto type = reader.pull(2);
        if (type == 0) {
            reader.bits = 0; reader.count = 0;
            if (reader.at + 4 > reader.size) throw std::runtime_error("Unsupported PSD compression");
            uint16_t length = uint16_t(reader.data[reader.at] | reader.data[reader.at + 1] << 8); reader.at += 4;
            if (produced + length > expected) throw std::runtime_error("Unsupported PSD compression");
            if (reader.at + length > reader.size) throw std::runtime_error("Unsupported PSD compression");
            std::memcpy(out.data() + produced, reader.data + reader.at, length); reader.at += length; produced += length;
        } else if (type == 1 || type == 2) {
            int literalLengths[288]{}, distanceLengths[32]{};
            if (type == 1) {
                for (int i = 0; i < 144; ++i) literalLengths[i] = 8;
                for (int i = 144; i < 256; ++i) literalLengths[i] = 9;
                for (int i = 256; i < 280; ++i) literalLengths[i] = 7;
                for (int i = 280; i < 288; ++i) literalLengths[i] = 8;
                for (int i = 0; i < 32; ++i) distanceLengths[i] = 5;
            } else {
                int literals = int(reader.pull(5)) + 257, distances = int(reader.pull(5)) + 1, codes = int(reader.pull(4)) + 4;
                if (literals > 286 || distances > 32) throw std::runtime_error("Unsupported PSD compression");
                int codeLengths[19]{};
                for (int i = 0; i < codes; ++i) codeLengths[order[i]] = int(reader.pull(3));
                Huffman codeTree = Huffman::build(codeLengths, 19);
                int lengths[318]{}, previous = 0;
                for (int i = 0; i < literals + distances;) {
                    int symbol = codeTree.decode(reader);
                    if (symbol < 16) lengths[i++] = previous = symbol;
                    else if (symbol == 16) { int repeat = int(reader.pull(2)) + 3; if (i + repeat > literals + distances) throw std::runtime_error("Unsupported PSD compression"); for (int n = 0; n < repeat; ++n) lengths[i++] = previous; }
                    else { int repeat = symbol == 17 ? int(reader.pull(3)) + 3 : int(reader.pull(7)) + 11; if (i + repeat > literals + distances) throw std::runtime_error("Unsupported PSD compression"); for (int n = 0; n < repeat; ++n) lengths[i++] = 0; previous = 0; }
                }
                std::memcpy(literalLengths, lengths, size_t(literals) * sizeof(int));
                std::memcpy(distanceLengths, lengths + literals, size_t(distances) * sizeof(int));
            }
            Huffman literals = Huffman::build(literalLengths, type == 1 ? 288 : 286);
            Huffman distances = Huffman::build(distanceLengths, type == 1 ? 32 : 32);
            for (;;) {
                int symbol = literals.decode(reader);
                if (symbol < 256) { if (produced >= expected) throw std::runtime_error("Unsupported PSD compression"); out[produced++] = uint8_t(symbol); }
                else if (symbol == 256) break;
                else {
                    int lengthIndex = symbol - 257; if (lengthIndex < 0 || lengthIndex > 28) throw std::runtime_error("Unsupported PSD compression");
                    int length = lengthBase[lengthIndex] + int(lengthExtra[lengthIndex] ? reader.pull(lengthExtra[lengthIndex]) : 0);
                    int distanceIndex = distances.decode(reader); if (distanceIndex < 0 || distanceIndex > 29) throw std::runtime_error("Unsupported PSD compression");
                    int distance = distanceBase[distanceIndex] + int(distanceExtra[distanceIndex] ? reader.pull(distanceExtra[distanceIndex]) : 0);
                    if (distance < 1 || size_t(distance) > produced || produced + size_t(length) > expected) throw std::runtime_error("Unsupported PSD compression");
                    for (int n = 0; n < length; ++n) out[produced] = out[produced - size_t(distance)], ++produced;
                }
            }
        } else throw std::runtime_error("Unsupported PSD compression");
    }
    if (produced != expected) throw std::runtime_error("Unsupported PSD compression");
    return out;
}
std::vector<uint8_t> decodeChannel(Reader& in, int width, int height, uint16_t compression, int depth, size_t packedBytes = 0, const std::vector<uint32_t>* rowTable = nullptr, int rowIndex = 0) {
    const int sample = depth == 16 ? 2 : 1;
    if (width < 1 || height < 1 || width > 30000 || height > 30000 || uint64_t(width) * uint64_t(height) > 100000000) throw std::runtime_error("Raster dimensions exceed supported budget");
    const size_t row = size_t(width) * sample;
    std::vector<uint8_t> raw(row * size_t(height));
    if (compression == 0) { for (auto& byte : raw) byte = in.u8(); }
    else if (compression == 2 || compression == 3) {
        if (packedBytes > in.size - in.at) throw std::runtime_error("PSD ended early");
        raw = inflateZlib(in.data + in.at, packedBytes, raw.size());
        if (compression == 3) unpredict(raw, width, height, depth);
    }
    else if (compression == 1) {
        std::vector<uint32_t> local;
        const std::vector<uint32_t>* rows = rowTable;
        if (!rows) {
            local.resize(size_t(height));
            for (int y = 0; y < height; ++y) local[y] = in.psb ? in.u32() : in.u16();
            rows = &local;
        } else if (rowIndex < 0 || size_t(rowIndex) + size_t(height) > rows->size()) throw std::runtime_error("PSD channel overrun");
        for (int y = 0; y < height; ++y) {
            const auto count = (*rows)[size_t(rowIndex) + y];
            if (count > in.size - in.at) throw std::runtime_error("PSD ended early");
            size_t written = 0; const size_t end = in.at + count;
            while (in.at < end && written < row) {
                int control = int8_t(in.u8());
                if (control >= 0) { int n = control + 1; for (int i = 0; i < n && written < row && in.at < end; ++i) raw[size_t(y) * row + written++] = in.u8(); }
                else if (control > -128) { if (in.at >= end) break; auto value = in.u8(); int n = 1 - control; for (int i = 0; i < n && written < row; ++i) raw[size_t(y) * row + written++] = value; }
            }
            in.at = end;
        }
    } else throw std::runtime_error("Unsupported PSD compression");
    if (depth == 8) return raw;
    std::vector<uint8_t> out(size_t(width) * height);
    for (size_t i = 0; i < out.size(); ++i) out[i] = raw[i * 2];
    return out;
}
std::shared_ptr<const Raster> compose(int width, int height, const std::vector<uint8_t>& r, const std::vector<uint8_t>& g, const std::vector<uint8_t>& b, const std::vector<uint8_t>& a) {
    std::vector<uint8_t> rgba(size_t(width) * height * 4);
    for (size_t i = 0; i < size_t(width) * height; ++i) { uint8_t alpha = a.size() == r.size() ? a[i] : 255; rgba[i * 4] = uint8_t(r[i] * alpha / 255); rgba[i * 4 + 1] = uint8_t(g[i] * alpha / 255); rgba[i * 4 + 2] = uint8_t(b[i] * alpha / 255); rgba[i * 4 + 3] = alpha; }
    return Raster::fromRgba(width, height, rgba.data(), size_t(width) * 4);
}
double beDouble(const uint8_t* data) {
    uint64_t bits = 0; for (int i = 0; i < 8; ++i) bits = (bits << 8) | data[i];
    double value; std::memcpy(&value, &bits, sizeof value); return value;
}
std::optional<size_t> descriptorValue(const std::vector<uint8_t>& blob, const char* key) {
    const size_t length = std::strlen(key);
    if (length < 1 || length > 64 || blob.size() < length + 8) return {};
    for (size_t at = 4; at + length + 4 <= blob.size(); ++at) {
        if (std::memcmp(blob.data() + at, key, length) != 0) continue;
        uint32_t stored = uint32_t(blob[at - 4] << 24 | blob[at - 3] << 16 | blob[at - 2] << 8 | blob[at - 1]);
        if (stored == length || (stored == 0 && length == 4)) return at + length;
    }
    return {};
}
std::optional<double> descriptorNumber(const std::vector<uint8_t>& blob, const char* key) {
    auto at = descriptorValue(blob, key);
    if (!at || *at + 4 > blob.size()) return {};
    if (std::memcmp(blob.data() + *at, "UntF", 4) == 0) {
        if (*at + 16 > blob.size()) return {};
        return beDouble(blob.data() + *at + 8);
    }
    if (std::memcmp(blob.data() + *at, "doub", 4) == 0) {
        if (*at + 12 > blob.size()) return {};
        return beDouble(blob.data() + *at + 4);
    }
    return {};
}
std::optional<bool> descriptorBool(const std::vector<uint8_t>& blob, const char* key) {
    auto at = descriptorValue(blob, key);
    if (!at || *at + 5 > blob.size() || std::memcmp(blob.data() + *at, "bool", 4) != 0) return {};
    return blob[*at + 4] != 0;
}
uint8_t byteChannel(double value) {
    if (!std::isfinite(value)) return 0;
    return uint8_t(std::clamp(std::lround(value), 0L, 255L));
}
struct ShapePaint { std::shared_ptr<const Raster> raster; Transform transform; };
std::optional<ShapePaint> rasterizeRoundedRect(const std::vector<uint8_t>& fill, const std::vector<uint8_t>& geometry, const std::vector<uint8_t>& stroke) {
    if (fill.size() < 4 || geometry.empty()) return {};
    auto top = descriptorNumber(geometry, "Top "); auto left = descriptorNumber(geometry, "Left");
    auto bottom = descriptorNumber(geometry, "Btom"); auto right = descriptorNumber(geometry, "Rght");
    auto radiusValue = descriptorNumber(geometry, "topLeft");
    auto red = descriptorNumber(fill, "Rd  "); auto green = descriptorNumber(fill, "Grn "); auto blue = descriptorNumber(fill, "Bl  ");
    if (!top || !left || !bottom || !right || !red || !green || !blue || *right <= *left || *bottom <= *top) return {};
    const bool gradient = std::memcmp(fill.data(), "GdFl", 4) == 0;
    double fillOpacity = 1;
    if (gradient) { if (auto opacity = descriptorNumber(fill, "Opct")) fillOpacity = std::clamp(*opacity / 100., 0., 1.); }
    const bool fillEnabled = descriptorBool(stroke, "fillEnabled").value_or(true);
    const bool strokeEnabled = descriptorBool(stroke, "strokeEnabled").value_or(false);
    double strokeWidth = descriptorNumber(stroke, "strokeStyleLineWidth").value_or(0);
    double strokeOpacity = 1;
    if (auto opacity = descriptorNumber(stroke, "strokeStyleOpacity")) strokeOpacity = std::clamp(*opacity / 100., 0., 1.);
    if (!strokeEnabled || !std::isfinite(strokeWidth) || strokeWidth <= 0) strokeWidth = 0;
    const double strokeRed = descriptorNumber(stroke, "Rd  ").value_or(0);
    const double strokeGreen = descriptorNumber(stroke, "Grn ").value_or(0);
    const double strokeBlue = descriptorNumber(stroke, "Bl  ").value_or(0);
    const double pad = strokeWidth * 0.5;
    const int x0 = int(std::floor(*left - pad)), y0 = int(std::floor(*top - pad));
    const int x1 = int(std::ceil(*right + pad)), y1 = int(std::ceil(*bottom + pad));
    const int width = x1 - x0, height = y1 - y0;
    if (width < 1 || height < 1 || width > 30000 || height > 30000 || uint64_t(width) * uint64_t(height) > 100000000) return {};
    const double pathWidth = *right - *left, pathHeight = *bottom - *top;
    double radius = std::clamp(radiusValue.value_or(0), 0., std::min(pathWidth, pathHeight) * 0.5);
    std::vector<uint8_t> rgba(size_t(width) * size_t(height) * 4);
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        const double px = x0 + x + 0.5 - *left, py = y0 + y + 0.5 - *top;
        const double halfWidth = pathWidth * 0.5 - radius, halfHeight = pathHeight * 0.5 - radius;
        const double dx = std::abs(px - pathWidth * 0.5) - halfWidth, dy = std::abs(py - pathHeight * 0.5) - halfHeight;
        const double sdf = std::hypot(std::max(dx, 0.), std::max(dy, 0.)) + std::min(std::max(dx, dy), 0.) - radius;
        const double fillCoverage = fillEnabled ? std::clamp(0.5 - sdf, 0., 1.) * fillOpacity : 0;
        const double strokeCoverage = strokeWidth > 0 ? std::clamp(strokeWidth * 0.5 + 0.5 - std::abs(sdf), 0., 1.) * strokeOpacity : 0;
        const double alpha = strokeCoverage + fillCoverage * (1 - strokeCoverage);
        if (alpha <= 0) continue;
        const double straightRed = (strokeRed * strokeCoverage + *red * fillCoverage * (1 - strokeCoverage)) / alpha;
        const double straightGreen = (strokeGreen * strokeCoverage + *green * fillCoverage * (1 - strokeCoverage)) / alpha;
        const double straightBlue = (strokeBlue * strokeCoverage + *blue * fillCoverage * (1 - strokeCoverage)) / alpha;
        const uint8_t storedAlpha = byteChannel(alpha * 255);
        auto pixel = rgba.data() + (size_t(y) * size_t(width) + size_t(x)) * 4;
        pixel[0] = byteChannel(straightRed * storedAlpha / 255.);
        pixel[1] = byteChannel(straightGreen * storedAlpha / 255.);
        pixel[2] = byteChannel(straightBlue * storedAlpha / 255.);
        pixel[3] = storedAlpha;
    }
    ShapePaint paint;
    paint.raster = Raster::fromRgba(width, height, rgba.data(), size_t(width) * 4);
    paint.transform = {double(x0), double(y0), double(width), double(height)};
    return paint;
}
bool readMerged(Reader& in, int width, int height, int depth, int channels, Layer& layer, std::string& report) {
    if (in.at + 2 > in.size) return false;
    auto compression = in.u16();
    if (compression > 1) throw std::runtime_error("Unsupported PSD compression");
    const int planes = std::max(int(channels), 3);
    std::vector<uint32_t> rows;
    if (compression == 1) {
        const uint64_t tableBytes = uint64_t(height) * uint64_t(planes) * (in.psb ? 4u : 2u);
        if (in.at > in.size || tableBytes > in.size - in.at) throw std::runtime_error("PSD ended early");
        rows.resize(size_t(height) * size_t(planes));
        for (auto& row : rows) row = in.psb ? in.u32() : in.u16();
    }
    auto plane = [&](int index) { return decodeChannel(in, width, height, compression, depth, 0, rows.empty() ? nullptr : &rows, index * height); };
    auto red = plane(0); auto green = plane(1); auto blue = plane(2);
    std::vector<uint8_t> alpha;
    if (channels >= 4) {
        try { alpha = plane(3); }
        catch (const std::exception&) {
            report += "Merged image transparency could not be read.\n";
            return false;
        }
    }
    layer.id = newId(); layer.name = "Background"; layer.transform = {0, 0, double(width), double(height)};
    layer.raster = compose(width, height, red, green, blue, alpha);
    return true;
}
}
PsdImport readPsd(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary); if (!file) throw std::runtime_error("Cannot open PSD");
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {}); return readPsd(bytes.data(), bytes.size());
}
PsdImport readPsd(const uint8_t* bytes, size_t size) {
    Reader in{bytes, size}; if (in.ascii(4) != "8BPS") throw std::runtime_error("Not a PSD");
    auto version = in.u16(); if (version != 1 && version != 2) throw std::runtime_error("Unsupported PSD version"); in.psb = version == 2; in.skip(6);
    auto channels = in.u16(); auto height = in.u32(); auto width = in.u32(); auto depth = in.u16(); auto mode = in.u16();
    if (mode == 4) throw std::runtime_error("CMYK PSD files are not supported");
    if (channels > 56) throw std::runtime_error("PSD channel count exceeds 56");
    if (channels < 3 || (depth != 8 && depth != 16) || mode != 3) throw std::runtime_error("Only 8-bit or 16-bit RGB PSD files are supported");
    if (!width || !height || width > 30000 || height > 30000 || uint64_t(width) * height > 100000000) throw std::runtime_error("PSD exceeds the canvas limit");
    in.skip(in.u32()); in.skip(in.u32());
    PsdImport result; result.document.width = int(width); result.document.height = int(height); result.document.resolution = 72; std::string report;
    if (depth == 16) report += "16-bit channels were converted to 8-bit.\n";
    auto section = in.wide(); auto sectionEnd = in.at + size_t(std::min<uint64_t>(section, in.size > in.at ? in.size - in.at : 0));
    if (section) {
        try {
            auto info = in.wide(); auto infoAt = in.at; auto infoEnd = infoAt + size_t(std::min<uint64_t>(info, in.size > infoAt ? in.size - infoAt : 0));
            bool depthLayers = false;
            // Photoshop stores 16-bit layer records in the Lr16 block when the standard layer info is empty.
            if (depth == 16) {
                Reader scan = in; scan.at = infoEnd;
                if (scan.at + 4 <= sectionEnd) {
                    auto global = scan.u32();
                    if (global <= sectionEnd - scan.at) {
                        scan.skip(global);
                        while (scan.at + 12 <= sectionEnd) {
                            auto signature = scan.ascii(4);
                            if (signature != "8BIM" && signature != "8B64") break;
                            auto key = scan.ascii(4);
                            auto length = wideExtra(scan.psb, signature, key) ? scan.u64() : scan.u32();
                            auto start = scan.at;
                            if ((key == "Lr16" || key == "Layr") && length >= 2 && start <= sectionEnd && length <= sectionEnd - start) { in.at = start; depthLayers = true; break; }
                            auto next = start + size_t(length + (length & 1));
                            if (next < start || next > sectionEnd) break;
                            scan.at = next;
                        }
                    }
                }
            }
            if (!depthLayers && info) in.at = infoAt;
            if (depthLayers || info) {
            auto count = std::abs(in.i16()); if (count > 256) throw std::runtime_error("Too many PSD layers");
            struct Record { int top, left, bottom, right; Blend blend; uint8_t opacity; std::string name; bool group{}, closer{}, visible{true}, hasMask{}; int maskTop{}, maskLeft{}, maskBottom{}, maskRight{}; uint8_t maskFlags{}; std::vector<std::pair<int16_t, uint64_t>> channels; std::vector<uint8_t> typeTool, fill, geometry, strokeData; };
            std::vector<Record> records;
            for (int i = 0; i < count; ++i) {
                Record record; record.top = in.i32(); record.left = in.i32(); record.bottom = in.i32(); record.right = in.i32();
                auto channelCount = in.u16(); if (channelCount > 16) throw std::runtime_error("Too many PSD channels");
                for (int c = 0; c < channelCount; ++c) { auto id = in.i16(); auto length = in.wide(); record.channels.push_back({id, length}); }
                if (in.ascii(4) != "8BIM") throw std::runtime_error("Invalid PSD blend signature"); record.blend = blendOf(in.ascii(4), report);
                record.opacity = in.u8(); in.u8(); record.visible = (in.u8() & 0x02) == 0; in.u8(); auto extra = in.u32(); auto extraEnd = in.at + extra;
                if (extraEnd > in.size) extraEnd = in.size;
                auto maskSize = in.u32(); auto maskAt = in.at;
                if (maskSize >= 20 && maskAt + maskSize <= extraEnd) {
                    record.maskTop = in.i32(); record.maskLeft = in.i32(); record.maskBottom = in.i32(); record.maskRight = in.i32(); in.u8(); record.maskFlags = in.u8(); record.hasMask = true;
                }
                if (maskAt <= extraEnd && maskSize <= extraEnd - maskAt) in.at = maskAt + size_t(maskSize);
                auto blendSize = in.at + 4 <= extraEnd ? in.u32() : 0; if (in.at <= extraEnd && blendSize <= extraEnd - in.at) in.skip(blendSize); else in.at = extraEnd;
                if (in.at < extraEnd) { auto nameLength = in.u8(); record.name = macRomanToUtf8(in.ascii(nameLength)); auto padded = (1 + nameLength + 3) & ~3; if (padded > 1 + nameLength && in.at + (padded - 1 - nameLength) <= extraEnd) in.skip(padded - 1 - nameLength); }
                while (in.at + 12 <= extraEnd) {
                    auto signature = in.ascii(4);
                    if (signature != "8BIM" && signature != "8B64") { in.at -= 4; break; }
                    auto key = in.ascii(4); auto length = wideExtra(in.psb, signature, key) ? in.u64() : in.u32(); auto start = in.at;
                    if (key == "lsct" || key == "lsdk") { auto kind = length >= 4 ? in.u32() : 0; record.group = kind == 1 || kind == 2; record.closer = kind == 3; }
                    else if (key == "luni" && length >= 4 && extraEnd >= in.at + 4) {
                        auto characters = in.u32();
                        auto encoded = std::min<uint64_t>(uint64_t(characters) * 2, extraEnd > in.at ? extraEnd - in.at : 0);
                        if (characters > 0 && characters < 100000 && in.at + encoded <= in.size) {
                            auto unicode = utf8FromUtf16Be(in.data + in.at, size_t(encoded));
                            while (!unicode.empty() && unicode.back() == '\0') unicode.pop_back();
                            if (!unicode.empty()) record.name = std::move(unicode);
                        }
                    }
                    else if ((key == "TySh" || key == "txt2") && record.typeTool.empty()) { auto stored = std::min(length, uint64_t(extraEnd > in.at ? extraEnd - in.at : 0)); record.typeTool.assign(in.data + in.at, in.data + in.at + size_t(stored)); }
                    else if (key == "vscg" || key == "vogk" || key == "vstk") { auto stored = std::min(length, uint64_t(extraEnd > in.at ? extraEnd - in.at : 0)); auto& dest = key == "vscg" ? record.fill : key == "vogk" ? record.geometry : record.strokeData; if (dest.empty()) dest.assign(in.data + in.at, in.data + in.at + size_t(stored)); }
                    auto next = start + size_t(length + (length & 1));
                    in.at = next > extraEnd ? extraEnd : next;
                }
                in.at = extraEnd; records.push_back(std::move(record));
            }
            std::vector<std::string> folders;
            for (auto& record : records) {
                int w = std::max(0, record.right - record.left), h = std::max(0, record.bottom - record.top);
                int maskWidth = std::max(0, record.maskRight - record.maskLeft), maskHeight = std::max(0, record.maskBottom - record.maskTop);
                const bool fits = w > 0 && h > 0 && w <= 30000 && h <= 30000 && uint64_t(w) * uint64_t(h) <= 100000000;
                const bool maskFits = record.hasMask && maskWidth > 0 && maskHeight > 0 && maskWidth <= 30000 && maskHeight <= 30000 && uint64_t(maskWidth) * uint64_t(maskHeight) <= 100000000;
                if (!record.group && !record.closer && (w > 30000 || h > 30000 || (w > 0 && h > 0 && uint64_t(w) * uint64_t(h) > 100000000)))
                    report += (record.name.empty() ? "Layer" : record.name) + " exceeds the canvas limit and was skipped.\n";
                std::vector<uint8_t> red, green, blue, alpha, maskPlane;
                for (auto [id, length] : record.channels) {
                    auto start = in.at;
                    try {
                        uint16_t compression = length >= 2 ? in.u16() : 0;
                        const size_t packed = length >= 2 ? size_t(length - 2) : 0;
                        if (!record.group && !record.closer && id == -2 && maskFits) maskPlane = decodeChannel(in, maskWidth, maskHeight, compression, int(depth), packed);
                        else if (fits && !record.group && !record.closer && (id == 0 || id == 1 || id == 2 || id == -1)) {
                            auto plane = decodeChannel(in, w, h, compression, int(depth), packed);
                            if (id == 0) red = std::move(plane); else if (id == 1) green = std::move(plane); else if (id == 2) blue = std::move(plane); else alpha = std::move(plane);
                        }
                    } catch (const std::exception&) { report += (record.name.empty() ? "Layer" : record.name) + ": a channel could not be decoded.\n"; }
                    const size_t end = start > in.size || length > in.size - start ? in.size : start + size_t(length);
                    in.at = end;
                }
                try {
                if (record.closer) { if (!folders.empty()) folders.pop_back(); continue; }
                Layer layer; layer.id = newId(); layer.name = record.name.empty() ? "Layer" : record.name; layer.opacity = record.opacity / 255.; layer.blend = record.group ? Blend::Normal : record.blend; layer.group = record.group; layer.visible = record.visible;
                layer.transform = {double(record.left), double(record.top), double(std::max(w, 1)), double(std::max(h, 1))}; layer.parentId = folders.empty() ? std::string{} : folders.back();
                if (record.group) folders.push_back(layer.id);
                else if (fits && red.size() == size_t(w) * size_t(h) && green.size() == red.size() && blue.size() == red.size()) layer.raster = compose(w, h, red, green, blue, alpha);
                if (!layer.raster && !record.group) { if (auto painted = rasterizeRoundedRect(record.fill, record.geometry, record.strokeData)) { layer.raster = std::move(painted->raster); layer.transform = painted->transform; } }
                if (maskFits && maskPlane.size() == size_t(maskWidth) * size_t(maskHeight)) {
                    if (record.maskFlags & 0x04) for (auto& value : maskPlane) value = uint8_t(255 - value);
                    auto gray = std::make_shared<GrayRaster>(); gray->width = maskWidth; gray->height = maskHeight; gray->pixels = std::move(maskPlane);
                    Mask mask; mask.raster = std::move(gray); mask.enabled = (record.maskFlags & 0x02) == 0; mask.linked = true;
                    if (record.maskLeft != record.left || record.maskTop != record.top || record.maskRight != record.right || record.maskBottom != record.bottom)
                        mask.placement = Transform{double(record.maskLeft), double(record.maskTop), double(maskWidth), double(maskHeight)};
                    layer.mask = std::move(mask);
                }
                if (!record.group && !record.typeTool.empty()) { auto imported = readPhotoshopText(record.typeTool.data(), record.typeTool.size()); if (!imported.note.empty()) { report += layer.name + ": " + imported.note; if (imported.note.back() != '\n') report.push_back('\n'); } if (imported.editable) { if (layer.raster) layer.text = imported.text; else { try { auto drawn = text::rasterize(imported.text); layer.text = imported.text; layer.raster = drawn.raster; layer.transform.x = imported.x; layer.transform.y = imported.y; layer.transform.width = drawn.width; layer.transform.height = drawn.height; } catch (const std::exception&) { layer.text.reset(); report += layer.name + ": Editable Photoshop text becomes pixels and can't be retyped.\n"; } } } }
                if (layer.group || layer.raster || layer.text || layer.mask) result.document.layers.push_back(std::move(layer));
                } catch (const std::exception& error) { report += (record.name.empty() ? "Layer" : record.name) + " (" + std::to_string(w) + "x" + std::to_string(h) + "): " + error.what() + "\n"; }
            }
            in.at = std::max(in.at, infoEnd);
            }
        } catch (const std::exception& error) {
            report += std::string("Layer records were skipped: ") + error.what() + "\n";
            result.document.layers.clear();
        }
    }
    in.at = std::max(in.at, sectionEnd);
    const bool hasImage = std::any_of(result.document.layers.begin(), result.document.layers.end(), [](const Layer& layer) { return layer.raster || layer.text; });
    if (!hasImage) {
        result.document.layers.clear();
        Layer merged;
        try { if (readMerged(in, int(width), int(height), int(depth), int(channels), merged, report) && merged.raster) { result.document.layers = {std::move(merged)}; report += "Imported the merged image.\n"; } }
        catch (const std::exception& error) { report += std::string("Merged image was skipped: ") + error.what() + "\n"; }
    }
    if (result.document.layers.empty()) throw std::runtime_error(report.empty() ? "PSD contains no supported image" : report);
    report += "Imported " + std::to_string(result.document.layers.size()) + " layer(s).\n"; result.report = report; validateDocument(result.document); return result;
}
}
