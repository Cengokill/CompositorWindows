#include "PsdDocument.h"
#include "imaging/cmyk.h"
#include "PsdText.h"
#include "core/DeepImage.h"
#include "core/Document.h"
#include "editing/Shapes.h"
#include "text/TextRaster.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
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
template<typename Sink>
void inflateZlib(const uint8_t* data, size_t size, uint64_t expected, Sink&& emit) {
    if (size < 8 || (data[0] & 0x0F) != 8) throw std::runtime_error("Unsupported PSD compression");
    BitReader reader{data + 2, size - 6};
    std::array<uint8_t, 32768> history{};
    uint64_t produced = 0;
    auto append = [&](uint8_t value) {
        if (produced >= expected) throw std::runtime_error("Unsupported PSD compression");
        history[size_t(produced & 32767)] = value;
        emit(value);
        ++produced;
    };
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
            if (uint64_t(length) > expected - produced) throw std::runtime_error("Unsupported PSD compression");
            if (reader.at + length > reader.size) throw std::runtime_error("Unsupported PSD compression");
            for (size_t i = 0; i < length; ++i) append(reader.data[reader.at++]);
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
                if (symbol < 256) append(uint8_t(symbol));
                else if (symbol == 256) break;
                else {
                    int lengthIndex = symbol - 257; if (lengthIndex < 0 || lengthIndex > 28) throw std::runtime_error("Unsupported PSD compression");
                    int length = lengthBase[lengthIndex] + int(lengthExtra[lengthIndex] ? reader.pull(lengthExtra[lengthIndex]) : 0);
                    int distanceIndex = distances.decode(reader); if (distanceIndex < 0 || distanceIndex > 29) throw std::runtime_error("Unsupported PSD compression");
                    int distance = distanceBase[distanceIndex] + int(distanceExtra[distanceIndex] ? reader.pull(distanceExtra[distanceIndex]) : 0);
                    if (distance < 1 || uint64_t(distance) > produced || uint64_t(length) > expected - produced) throw std::runtime_error("Unsupported PSD compression");
                    for (int n = 0; n < length; ++n) append(history[size_t((produced - size_t(distance)) & 32767)]);
                }
            }
        } else throw std::runtime_error("Unsupported PSD compression");
    }
    if (produced != expected) throw std::runtime_error("Unsupported PSD compression");
}
std::vector<uint8_t> decodeChannel(Reader& in, uint64_t width, uint64_t height, uint64_t cropLeft, uint64_t cropTop, int cropWidth, int cropHeight, uint16_t compression, int depth, size_t packedBytes = 0, const std::vector<uint32_t>* rowTable = nullptr, int rowIndex = 0, std::vector<uint16_t>* deep = nullptr) {
    const int sample = depth == 16 ? 2 : 1;
    if (!width || !height || cropWidth < 1 || cropHeight < 1 || cropWidth > 30000 || cropHeight > 30000 || cropLeft > width || cropTop > height || uint64_t(cropWidth) > width - cropLeft || uint64_t(cropHeight) > height - cropTop)
        throw std::runtime_error("Raster dimensions exceed supported budget");
    const uint64_t pixels = uint64_t(cropWidth) * uint64_t(cropHeight);
    if (pixels > 100000000) throw std::runtime_error("Raster dimensions exceed supported budget");
    if (in.at > in.size || packedBytes > in.size - in.at) throw std::runtime_error("PSD ended early");
    if (width > std::numeric_limits<uint64_t>::max() / uint64_t(sample) / height) throw std::runtime_error("PSD channel dimensions overflow");
    const uint64_t rowBytes = width * uint64_t(sample);
    const uint64_t expectedBytes = rowBytes * height;
    if (pixels > std::numeric_limits<size_t>::max() / size_t(sample)) throw std::runtime_error("Raster dimensions exceed supported budget");
    std::vector<uint8_t> raw(size_t(pixels) * size_t(sample));
    const uint64_t cropByteLeft = cropLeft * uint64_t(sample);
    const uint64_t cropByteCount = uint64_t(cropWidth) * uint64_t(sample);
    if (compression == 0) {
        if (expectedBytes > packedBytes) throw std::runtime_error("PSD channel is shorter than its dimensions");
        for (int y = 0; y < cropHeight; ++y) {
            const uint64_t sourceOffset = (cropTop + uint64_t(y)) * rowBytes + cropByteLeft;
            std::memcpy(raw.data() + size_t(y) * size_t(cropByteCount), in.data + in.at + size_t(sourceOffset), size_t(cropByteCount));
        }
        in.at += size_t(expectedBytes);
    }
    else if (compression == 2 || compression == 3) {
        uint64_t rawAt = 0;
        uint8_t previousByte = 0;
        uint8_t highByte = 0;
        uint16_t previousSample = 0;
        inflateZlib(in.data + in.at, packedBytes, expectedBytes, [&](uint8_t byte) {
            const uint64_t y = rawAt / rowBytes;
            const uint64_t byteX = rawAt % rowBytes;
            if (depth == 8) {
                uint8_t value = byte;
                if (compression == 3) value = uint8_t(value + previousByte);
                previousByte = value;
                const uint64_t x = byteX;
                if (y >= cropTop && y < cropTop + uint64_t(cropHeight) && x >= cropLeft && x < cropLeft + uint64_t(cropWidth))
                    raw[size_t(y - cropTop) * size_t(cropWidth) + size_t(x - cropLeft)] = value;
            } else if ((byteX & 1u) == 0) highByte = byte;
            else {
                uint16_t value = uint16_t(uint16_t(highByte) << 8 | byte);
                if (compression == 3) { value = uint16_t(value + previousSample); previousSample = value; }
                const uint64_t x = byteX / 2;
                if (y >= cropTop && y < cropTop + uint64_t(cropHeight) && x >= cropLeft && x < cropLeft + uint64_t(cropWidth))
                    raw[(size_t(y - cropTop) * size_t(cropWidth) + size_t(x - cropLeft)) * 2] = uint8_t(value >> 8), raw[(size_t(y - cropTop) * size_t(cropWidth) + size_t(x - cropLeft)) * 2 + 1] = uint8_t(value);
            }
            if (byteX + 1 == rowBytes) { previousByte = 0; previousSample = 0; }
            ++rawAt;
        });
        if (rawAt != expectedBytes) throw std::runtime_error("PSD channel is shorter than its dimensions");
    }
    else if (compression == 1) {
        const std::vector<uint32_t>* rows = rowTable;
        uint64_t dataAt = in.at;
        const uint64_t channelEnd = in.at + packedBytes;
        uint64_t tableAt = 0;
        const uint64_t entryBytes = in.psb ? 4u : 2u;
        if (!rows) {
            if (height > packedBytes / entryBytes) throw std::runtime_error("PSD channel row table is truncated");
            tableAt = in.at;
            dataAt += height * entryBytes;
        } else if (rowIndex < 0 || uint64_t(rowIndex) + height > rows->size()) throw std::runtime_error("PSD channel overrun");
        in.at = size_t(dataAt);
        if (in.at > channelEnd) throw std::runtime_error("PSD channel row table is truncated");
        for (uint64_t y = 0; y < height; ++y) {
            uint64_t count = 0;
            if (rows) count = (*rows)[size_t(rowIndex) + size_t(y)];
            else { Reader table = in; table.at = size_t(tableAt + y * entryBytes); count = in.psb ? table.u32() : table.u16(); }
            if (count > channelEnd - in.at) throw std::runtime_error("PSD channel row exceeds channel data");
            size_t written = 0; const size_t end = in.at + size_t(count);
            const bool keepRow = y >= cropTop && y < cropTop + uint64_t(cropHeight);
            if (!keepRow) { in.at = end; continue; }
            while (in.at < end && written < rowBytes) {
                int control = int8_t(in.u8());
                if (control >= 0) {
                    const size_t n = size_t(control + 1);
                    if (n > rowBytes - written || n > end - in.at) throw std::runtime_error("Invalid PSD PackBits row");
                    const uint64_t runBegin = written, runEnd = runBegin + n;
                    const uint64_t keepBegin = std::max(runBegin, cropByteLeft);
                    const uint64_t keepEnd = std::min(runEnd, cropByteLeft + cropByteCount);
                    if (keepRow && keepBegin < keepEnd) std::memcpy(raw.data() + size_t(y - cropTop) * size_t(cropByteCount) + size_t(keepBegin - cropByteLeft), in.data + in.at + size_t(keepBegin - runBegin), size_t(keepEnd - keepBegin));
                    in.at += n;
                    written += n;
                }
                else if (control > -128) {
                    if (in.at >= end) throw std::runtime_error("Invalid PSD PackBits row");
                    const auto value = in.u8(); const size_t n = size_t(1 - control);
                    if (n > rowBytes - written) throw std::runtime_error("Invalid PSD PackBits row");
                    const uint64_t runBegin = written, runEnd = runBegin + n;
                    const uint64_t keepBegin = std::max(runBegin, cropByteLeft);
                    const uint64_t keepEnd = std::min(runEnd, cropByteLeft + cropByteCount);
                    if (keepRow && keepBegin < keepEnd) std::fill_n(raw.data() + size_t(y - cropTop) * size_t(cropByteCount) + size_t(keepBegin - cropByteLeft), size_t(keepEnd - keepBegin), value);
                    written += n;
                }
            }
            if (written != rowBytes) throw std::runtime_error("Invalid PSD PackBits row");
            in.at = end;
        }
    } else throw std::runtime_error("Unsupported PSD compression");
    if (depth == 8) return raw;
    std::vector<uint8_t> out(static_cast<size_t>(pixels));
    if (deep) deep->assign(size_t(pixels), 0);
    for (int y = 0; y < cropHeight; ++y) for (int x = 0; x < cropWidth; ++x) {
        const size_t i = size_t(y) * size_t(cropWidth) + size_t(x);
        const uint16_t scaled = scalePhotoshop16(uint16_t(uint16_t(raw[i * 2]) << 8 | raw[i * 2 + 1]));
        if (deep) (*deep)[i] = scaled;
        out[i] = ditherChannel(scaled, int((cropLeft + uint64_t(x)) & 3), int((cropTop + uint64_t(y)) & 3));
    }
    return out;
}
std::shared_ptr<const Raster> compose(int width, int height, const std::vector<uint8_t>& r, const std::vector<uint8_t>& g, const std::vector<uint8_t>& b, const std::vector<uint8_t>& a);
void installImage(Layer& layer, int width, int height, const std::vector<uint8_t>& r, const std::vector<uint8_t>& g, const std::vector<uint8_t>& b, const std::vector<uint8_t>& a, const std::vector<uint16_t>& r16, const std::vector<uint16_t>& g16, const std::vector<uint16_t>& b16, const std::vector<uint16_t>& a16) {
    const size_t count = size_t(width) * size_t(height);
    if (!r16.empty() && r16.size() == count && g16.size() == count && b16.size() == count) {
        layer.deep = deepFromPlanes(width, height, r16.data(), g16.data(), b16.data(), a16.size() == count ? a16.data() : nullptr);
        layer.raster = rasterFromDeep(*layer.deep);
        return;
    }
    layer.deep.reset();
    layer.raster = compose(width, height, r, g, b, a);
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
    return uint8_t(std::lround(std::clamp(value, 0., 255.)));
}
bool inIntRange(double value) {
    return std::isfinite(value)
        && value >= static_cast<double>(std::numeric_limits<int>::min())
        && value <= static_cast<double>(std::numeric_limits<int>::max());
}
struct ShapePaint { std::shared_ptr<const Raster> raster; Transform transform; };
std::optional<ShapePaint> rasterizeRoundedRect(const std::vector<uint8_t>& fill, const std::vector<uint8_t>& geometry, const std::vector<uint8_t>& stroke) {
    if (fill.size() < 4 || geometry.empty()) return {};
    auto top = descriptorNumber(geometry, "Top "); auto left = descriptorNumber(geometry, "Left");
    auto bottom = descriptorNumber(geometry, "Btom"); auto right = descriptorNumber(geometry, "Rght");
    auto radiusValue = descriptorNumber(geometry, "topLeft");
    auto red = descriptorNumber(fill, "Rd  "); auto green = descriptorNumber(fill, "Grn "); auto blue = descriptorNumber(fill, "Bl  ");
    if (!top || !left || !bottom || !right || !red || !green || !blue) return {};
    if (!std::isfinite(*top) || !std::isfinite(*left) || !std::isfinite(*bottom) || !std::isfinite(*right) || !std::isfinite(*red) || !std::isfinite(*green) || !std::isfinite(*blue)) return {};
    if (*right <= *left || *bottom <= *top) return {};
    if (radiusValue && !std::isfinite(*radiusValue)) return {};
    const bool gradient = std::memcmp(fill.data(), "GdFl", 4) == 0;
    double fillOpacity = 1;
    if (gradient) { if (auto opacity = descriptorNumber(fill, "Opct"); opacity && std::isfinite(*opacity)) fillOpacity = std::clamp(*opacity / 100., 0., 1.); }
    const bool fillEnabled = descriptorBool(stroke, "fillEnabled").value_or(true);
    const bool strokeEnabled = descriptorBool(stroke, "strokeEnabled").value_or(false);
    double strokeWidth = descriptorNumber(stroke, "strokeStyleLineWidth").value_or(0);
    double strokeOpacity = 1;
    if (auto opacity = descriptorNumber(stroke, "strokeStyleOpacity"); opacity && std::isfinite(*opacity)) strokeOpacity = std::clamp(*opacity / 100., 0., 1.);
    if (!strokeEnabled || !std::isfinite(strokeWidth) || strokeWidth <= 0) strokeWidth = 0;
    const double strokeRed = descriptorNumber(stroke, "Rd  ").value_or(0);
    const double strokeGreen = descriptorNumber(stroke, "Grn ").value_or(0);
    const double strokeBlue = descriptorNumber(stroke, "Bl  ").value_or(0);
    const double pad = strokeWidth * 0.5;
    const double rawLeft = *left - pad, rawTop = *top - pad, rawRight = *right + pad, rawBottom = *bottom + pad;
    if (!inIntRange(std::floor(rawLeft)) || !inIntRange(std::floor(rawTop)) || !inIntRange(std::ceil(rawRight)) || !inIntRange(std::ceil(rawBottom))) return {};
    const int x0 = static_cast<int>(std::floor(rawLeft)), y0 = static_cast<int>(std::floor(rawTop));
    const int x1 = static_cast<int>(std::ceil(rawRight)), y1 = static_cast<int>(std::ceil(rawBottom));
    const auto width64 = std::int64_t(x1) - std::int64_t(x0), height64 = std::int64_t(y1) - std::int64_t(y0);
    if (width64 < 1 || height64 < 1 || width64 > 30000 || height64 > 30000 || uint64_t(width64) * uint64_t(height64) > 100000000) return {};
    const int width = static_cast<int>(width64), height = static_cast<int>(height64);
    const double pathWidth = *right - *left, pathHeight = *bottom - *top;
    if (!std::isfinite(pathWidth) || !std::isfinite(pathHeight)) return {};
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
// Shape layers keep origination (`vogk`), path (`vsms`/`vmsk`), and fill (`vscg`) separately.
// A layer becomes a live shape only when its path agrees with the origination box; anything else stays pixels.
constexpr double kShapeTolerance = 0.5;
constexpr double kMaxShapeSide = 30000;
constexpr double kMaxShapePixels = 100000000;

struct Point2 { double x{}, y{}; };
struct PathKnot { Point2 in, anchor, out; };
struct VectorShape { editing::ShapeStyle style; editing::Rect bounds; };

int32_t readI32(const uint8_t* p) { return int32_t(uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | uint32_t(p[3])); }

std::optional<int32_t> descriptorLong(const std::vector<uint8_t>& blob, const char* key) {
    auto at = descriptorValue(blob, key);
    if (!at || *at + 8 > blob.size() || std::memcmp(blob.data() + *at, "long", 4) != 0) return {};
    return readI32(blob.data() + *at + 4);
}

// Searches from a key so that a repeated short key (for example "Top ") is read from the right descriptor.
std::vector<uint8_t> tailFrom(const std::vector<uint8_t>& blob, const char* key) {
    const size_t length = std::strlen(key);
    for (size_t at = 0; at + length <= blob.size(); ++at)
        if (std::memcmp(blob.data() + at, key, length) == 0) return {blob.begin() + at, blob.end()};
    return {};
}

// Returns the knots of the only subpath, which must be closed and must consume every knot record.
// Photoshop stores each knot as y then x, as 8.24 fixed-point fractions of the canvas size.
std::optional<std::vector<PathKnot>> closedSubpath(const std::vector<uint8_t>& data, double width, double height) {
    if (data.size() < 8) return {};
    auto point = [&](size_t at) { return Point2{double(readI32(data.data() + at + 4)) / 16777216.0 * width, double(readI32(data.data() + at)) / 16777216.0 * height}; };
    std::vector<PathKnot> knots;
    int subpaths = 0, remaining = 0;
    bool closed = false;
    for (size_t record = 8; record + 26 <= data.size(); record += 26) {
        const int type = int16_t(uint16_t(data[record]) << 8 | data[record + 1]);
        const size_t body = record + 2;
        if (type == 0 || type == 3) {
            const int count = int16_t(uint16_t(data[body]) << 8 | data[body + 1]);
            if (remaining != 0 || ++subpaths > 1 || count < 0) return {};
            remaining = count;
            closed = type == 0;
        } else if (type == 1 || type == 2 || type == 4 || type == 5) {
            if (remaining <= 0) return {};
            --remaining;
            knots.push_back({point(body), point(body + 8), point(body + 16)});
        }
    }
    if (remaining != 0 || subpaths != 1 || !closed || knots.size() < 4) return {};
    return knots;
}

bool nearPoint(Point2 a, Point2 b) { return std::abs(a.x - b.x) <= kShapeTolerance && std::abs(a.y - b.y) <= kShapeTolerance; }

// Each knot must sit on a distinct expected anchor. Sharp corners need zero handles; curved rectangle and
// ellipse handles must be horizontal or vertical, which rejects rotated or skewed outlines.
bool matchesAnchors(const std::vector<PathKnot>& knots, const std::vector<Point2>& expected, bool sharp) {
    if (knots.size() != expected.size()) return false;
    std::vector<bool> used(expected.size());
    for (const auto& knot : knots) {
        size_t match = expected.size();
        for (size_t i = 0; i < expected.size() && match == expected.size(); ++i)
            if (!used[i] && nearPoint(knot.anchor, expected[i])) match = i;
        if (match == expected.size()) return false;
        used[match] = true;
        for (const Point2 handle : {knot.in, knot.out}) {
            const bool aligned = std::abs(handle.x - knot.anchor.x) <= kShapeTolerance || std::abs(handle.y - knot.anchor.y) <= kShapeTolerance;
            if (sharp ? !nearPoint(handle, knot.anchor) : !aligned) return false;
        }
    }
    return true;
}

bool containsText(const std::vector<uint8_t>& blob, const char* text) {
    const size_t length = std::strlen(text);
    return std::search(blob.begin(), blob.end(), text, text + length) != blob.end();
}

// Reads the stroke into style and returns how far the path must grow outward to become the editable shape's bounds.
// The editable stroke is drawn inside those bounds, so a centred stroke grows the bounds by half its width.
// The width is read as document pixels, as in the rounded-rectangle fallback.
std::optional<double> strokeOffset(const std::vector<uint8_t>& stroke, editing::ShapeStyle& style, std::string& reason) {
    if (!descriptorBool(stroke, "strokeEnabled").value_or(false)) return 0.;
    const auto width = descriptorNumber(stroke, "strokeStyleLineWidth");
    if (!width || !std::isfinite(*width) || *width <= 0 || *width > 500) { reason = "the stroke width could not be read or is too wide"; return {}; }
    if (const auto opacity = descriptorNumber(stroke, "strokeStyleOpacity"); opacity && (!std::isfinite(*opacity) || *opacity < 100)) { reason = "semi-transparent strokes are kept as pixels"; return {}; }
    if (const auto dashes = descriptorValue(stroke, "strokeStyleLineDashSet"); dashes && *dashes + 8 <= stroke.size() && std::memcmp(stroke.data() + *dashes, "VlLs", 4) == 0 && readI32(stroke.data() + *dashes + 4) > 0) { reason = "dashed strokes are kept as pixels"; return {}; }
    if (!containsText(stroke, "solidColorLayer")) { reason = "only solid colour strokes become editable"; return {}; }
    const auto red = descriptorNumber(stroke, "Rd  "), green = descriptorNumber(stroke, "Grn "), blue = descriptorNumber(stroke, "Bl  ");
    if (!red || !green || !blue || !std::isfinite(*red) || !std::isfinite(*green) || !std::isfinite(*blue)) { reason = "the stroke colour could not be read"; return {}; }
    auto channel = [](double value) { return std::clamp(value / 255., 0., 1.); };
    style.strokeEnabled = true;
    style.strokeWidth = *width;
    style.strokeRed = channel(*red);
    style.strokeGreen = channel(*green);
    style.strokeBlue = channel(*blue);
    if (containsText(stroke, "strokeStyleAlignInside")) return 0.;
    if (containsText(stroke, "strokeStyleAlignOutside")) return *width;
    return *width / 2;
}

std::optional<VectorShape> vectorShape(const std::vector<uint8_t>& fill, const std::vector<uint8_t>& origination, const std::vector<uint8_t>& path, const std::vector<uint8_t>& stroke, double width, double height, std::string& reason) {
    const bool solid = fill.size() >= 4 && std::memcmp(fill.data(), "SoCo", 4) == 0;
    if (!solid) {
        reason = fill.size() >= 4 && std::memcmp(fill.data(), "GdFl", 4) == 0 ? "gradient fills are kept as pixels" : "only solid colour fills become editable shapes";
        return {};
    }
    if (!descriptorBool(stroke, "fillEnabled").value_or(true)) { reason = "a shape without fill is kept as pixels"; return {}; }
    const auto red = descriptorNumber(fill, "Rd  "), green = descriptorNumber(fill, "Grn "), blue = descriptorNumber(fill, "Bl  ");
    if (!red || !green || !blue || !std::isfinite(*red) || !std::isfinite(*green) || !std::isfinite(*blue)) { reason = "the fill colour could not be read"; return {}; }
    auto channel = [](double value) { return std::clamp(value / 255., 0., 1.); };
    editing::ShapeStyle style{editing::ShapeKind::Rectangle, channel(*red), channel(*green), channel(*blue), 0};
    const auto offset = strokeOffset(stroke, style, reason);
    if (!offset) return {};

    auto outline = closedSubpath(path, width, height);
    if (!outline) { reason = "the vector path is compound, open, or truncated"; return {}; }
    const auto& knots = *outline;
    editing::Rect bounds{};
    if (origination.empty()) {
        // Without origination metadata, only a sharp axis-aligned rectangle is accepted; its box comes from the path.
        double left = knots[0].anchor.x, right = left, top = knots[0].anchor.y, bottom = top;
        for (const auto& knot : knots) {
            left = std::min(left, knot.anchor.x); right = std::max(right, knot.anchor.x);
            top = std::min(top, knot.anchor.y); bottom = std::max(bottom, knot.anchor.y);
        }
        bounds = {left, top, right - left, bottom - top};
        const std::vector<Point2> corners{{left, top}, {right, top}, {right, bottom}, {left, bottom}};
        if (!matchesAnchors(knots, corners, true)) { reason = "the path is not an axis-aligned rectangle"; return {}; }
    } else {
        const auto type = descriptorLong(origination, "keyOriginType");
        if (!type || (*type != 1 && *type != 2 && *type != 5)) { reason = "only rectangle and ellipse shapes become editable"; return {}; }
        const auto box = tailFrom(origination, "keyOriginShapeBBox");
        const auto top = descriptorNumber(box, "Top "), left = descriptorNumber(box, "Left"), bottom = descriptorNumber(box, "Btom"), right = descriptorNumber(box, "Rght");
        if (!top || !left || !bottom || !right || !std::isfinite(*top) || !std::isfinite(*left) || !std::isfinite(*bottom) || !std::isfinite(*right) || *right <= *left || *bottom <= *top) {
            reason = "the shape bounds could not be read";
            return {};
        }
        bounds = {*left, *top, *right - *left, *bottom - *top};
        const double cx = (*left + *right) / 2, cy = (*top + *bottom) / 2;
        if (*type == 5) {
            style.kind = editing::ShapeKind::Ellipse;
            if (!matchesAnchors(knots, {{cx, *top}, {*right, cy}, {cx, *bottom}, {*left, cy}}, false)) { reason = "the path does not match the ellipse bounds"; return {}; }
        } else {
            double radius = 0;
            if (*type == 2) {
                const auto radii = tailFrom(origination, "keyOriginRRectRadii");
                const auto values = {descriptorNumber(radii, "topLeft"), descriptorNumber(radii, "topRight"), descriptorNumber(radii, "bottomRight"), descriptorNumber(radii, "bottomLeft")};
                double lo = std::numeric_limits<double>::infinity(), hi = -lo;
                for (const auto& value : values) {
                    if (!value || !std::isfinite(*value)) { reason = "the corner radii could not be read"; return {}; }
                    lo = std::min(lo, *value); hi = std::max(hi, *value);
                }
                if (hi - lo > kShapeTolerance) { reason = "unequal corner radii are kept as pixels"; return {}; }
                radius = std::clamp(hi, 0., std::min(*right - *left, *bottom - *top) / 2);
            }
            style.cornerRadius = radius;
            if (radius <= kShapeTolerance) {
                const std::vector<Point2> corners{{*left, *top}, {*right, *top}, {*right, *bottom}, {*left, *bottom}};
                if (!matchesAnchors(knots, corners, true)) { reason = "the path does not match the rectangle bounds"; return {}; }
            } else {
                const double r = radius, l = *left, t = *top, rt = *right, b = *bottom;
                const std::vector<Point2> anchors{{l + r, t}, {rt - r, t}, {rt, t + r}, {rt, b - r}, {rt - r, b}, {l + r, b}, {l, b - r}, {l, t + r}};
                if (!matchesAnchors(knots, anchors, false)) { reason = "the path does not match the rounded rectangle"; return {}; }
            }
        }
    }
    if (*offset > 0) {
        bounds = {bounds.x - *offset, bounds.y - *offset, bounds.width + 2 * *offset, bounds.height + 2 * *offset};
        if (style.cornerRadius > kShapeTolerance) style.cornerRadius += *offset;
    }
    // Live shapes use whole-pixel rasters; the sub-pixel edge is rounded once, which is disclosed in the user guide.
    const double pixelWidth = std::round(bounds.width), pixelHeight = std::round(bounds.height);
    if (pixelWidth < 1 || pixelHeight < 1 || pixelWidth > kMaxShapeSide || pixelHeight > kMaxShapeSide || pixelWidth * pixelHeight > kMaxShapePixels) {
        reason = "the shape is too large to edit";
        return {};
    }
    bounds.width = pixelWidth;
    bounds.height = pixelHeight;
    return VectorShape{style, bounds};
}

// Returns an empty string when the layer became an editable shape, or the reason it stays pixels.
// The layer keeps its identity, mask, opacity, blend, and visibility; only pixels and shape metadata change.
std::string convertVectorShape(Layer& layer, const std::vector<uint8_t>& fill, const std::vector<uint8_t>& origination, const std::vector<uint8_t>& path, const std::vector<uint8_t>& stroke, double width, double height) {
    std::string reason;
    auto shape = vectorShape(fill, origination, path, stroke, width, height, reason);
    if (!shape) return reason;
    try {
        auto made = editing::createShapeLayer(shape->bounds, shape->style, layer.name);
        if (!made) return "the shape is empty";
        layer.raster = made->raster;
        layer.shapeJson = std::move(made->shapeJson);
        layer.transform = made->transform;
        layer.deep.reset();
    } catch (const std::exception& error) {
        return error.what();
    }
    return {};
}

uint64_t cmykRasterBytes(int width,int height){
    return uint64_t((width+Raster::tileSide-1)/Raster::tileSide)*uint64_t((height+Raster::tileSide-1)/Raster::tileSide)*sizeof(Raster::Tile);
}
void convertInks(CmykTransform& color,int width,std::vector<uint8_t>& c,std::vector<uint8_t>& m,std::vector<uint8_t>& y,const std::vector<uint8_t>& k,
                 std::vector<uint16_t>& c16,std::vector<uint16_t>& m16,std::vector<uint16_t>& y16,const std::vector<uint16_t>& k16){
    const auto count=c.size();if(!count||m.size()!=count||y.size()!=count||k.size()!=count)throw std::runtime_error("Incomplete CMYK PSD channels");
    const bool deep=!c16.empty();if(deep&&(c16.size()!=count||m16.size()!=count||y16.size()!=count||k16.size()!=count))throw std::runtime_error("Incomplete 16-bit CMYK PSD channels");
    std::array<uint16_t,4096> ink{};std::array<uint16_t,3072> rgb{};
    for(size_t first=0;first<count;first+=1024){const auto n=std::min(size_t(1024),count-first);
        for(size_t i=0;i<n;++i){const auto p=first+i;ink[i*4]=uint16_t(65535-(deep?c16[p]:unsigned(c[p])*257));ink[i*4+1]=uint16_t(65535-(deep?m16[p]:unsigned(m[p])*257));ink[i*4+2]=uint16_t(65535-(deep?y16[p]:unsigned(y[p])*257));ink[i*4+3]=uint16_t(65535-(deep?k16[p]:unsigned(k[p])*257));}
        color.toRgb(std::span(ink).first(n*4),std::span(rgb).first(n*3));
        for(size_t i=0;i<n;++i){const auto p=first+i;const int x=int(p%size_t(width)),row=int(p/size_t(width));
            if(deep){c16[p]=rgb[i*3];m16[p]=rgb[i*3+1];y16[p]=rgb[i*3+2];}
            c[p]=ditherChannel(rgb[i*3],x,row);m[p]=ditherChannel(rgb[i*3+1],x,row);y[p]=ditherChannel(rgb[i*3+2],x,row);
        }
    }
}

bool readMerged(Reader& in, int width, int height, int depth, int channels, Layer& layer, std::string& report,CmykTransform* color,bool mergedTransparency) {
    if (in.at + 2 > in.size) return false;
    if (color && uint64_t(width)*height*(depth==16?27u:9u)+cmykRasterBytes(width,height)+sizeof(Raster::Tile)>1200000000ULL) throw std::runtime_error("CMYK PSD exceeds the decode memory budget");
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
    std::vector<uint16_t> red16, green16, blue16, alpha16;
    auto plane = [&](int index, std::vector<uint16_t>* samples) { return decodeChannel(in, uint64_t(width), uint64_t(height), 0, 0, width, height, compression, depth, in.size - in.at, rows.empty() ? nullptr : &rows, index * height, samples); };
    auto red = plane(0, depth == 16 ? &red16 : nullptr); auto green = plane(1, depth == 16 ? &green16 : nullptr); auto blue = plane(2, depth == 16 ? &blue16 : nullptr);
    std::vector<uint8_t> alpha;
    if(color){std::vector<uint16_t> key16;auto key=plane(3,depth==16?&key16:nullptr);convertInks(*color,width,red,green,blue,key,red16,green16,blue16,key16);}
    if ((!color&&channels>=4)||(color&&mergedTransparency&&channels>=5)) {
        try { alpha = plane(color?4:3, depth == 16 ? &alpha16 : nullptr); }
        catch (const std::exception&) {
            report += "Merged image transparency could not be read.\n";
            return false;
        }
    }
    layer.id = newId(); layer.name = "Background"; layer.transform = {0, 0, double(width), double(height)};
    installImage(layer, width, height, red, green, blue, alpha, red16, green16, blue16, alpha16);
    return true;
}
}
PsdImport readPsd(const std::filesystem::path& path,CmykProfilePtr fallback) {
    std::ifstream file(path, std::ios::binary); if (!file) throw std::runtime_error("Cannot open PSD");
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {}); return readPsd(bytes.data(), bytes.size(),std::move(fallback));
}
PsdImport readPsd(const uint8_t* bytes, size_t size,CmykProfilePtr fallback) {
    Reader in{bytes, size}; if (in.ascii(4) != "8BPS") throw std::runtime_error("Not a PSD");
    auto version = in.u16(); if (version != 1 && version != 2) throw std::runtime_error("Unsupported PSD version"); in.psb = version == 2; in.skip(6);
    auto channels = in.u16(); auto height = in.u32(); auto width = in.u32(); auto depth = in.u16(); auto mode = in.u16();
    const bool cmyk=mode==4;
    if (channels > 56) throw std::runtime_error("PSD channel count exceeds 56");
    if (channels < (cmyk?4:3) || (depth != 8 && depth != 16) || (mode != 3 && !cmyk)) throw std::runtime_error("Only 8-bit or 16-bit RGB/CMYK PSD files are supported");
    if (!width || !height || width > 30000 || height > 30000 || uint64_t(width) * height > 100000000) throw std::runtime_error("PSD exceeds the canvas limit");
    in.skip(in.u32());
    const auto resourceLength=in.u32();in.need(resourceLength);
    Reader resources{in.data+in.at,resourceLength};in.skip(resourceLength);
    CmykProfilePtr profile;
    if(cmyk){while(resources.at<resources.size){
        if(resources.ascii(4)!="8BIM")throw std::runtime_error("Invalid PSD resource signature");
        const auto id=resources.u16();const auto name=resources.u8();resources.skip(name);if((name+1)%2)resources.skip(1);
        const auto length=resources.u32();resources.need(length);
        if(id==1039){if(profile)throw std::runtime_error("Duplicate PSD ICC profile");profile=makeCmykProfile(std::span(resources.data+resources.at,length),"Embedded CMYK profile");}
        resources.skip(length);if(length%2)resources.skip(1);
    }if(!profile)profile=std::move(fallback);if(!profile)throw MissingCmykProfile();}
    std::unique_ptr<CmykTransform> color;if(profile)color=std::make_unique<CmykTransform>(profile);
    PsdImport result; result.document.cmykProfile=profile; result.document.id = newId(); result.document.width = int(width); result.document.height = int(height); result.document.resolution = 72; std::string report; std::vector<std::string> warnings;
    if (depth == 16) report += "16-bit channels were preserved.\n";
    bool mergedTransparency=false;
    auto section = in.wide();if(cmyk&&section>in.size-in.at)throw std::runtime_error("Truncated CMYK layer section"); auto sectionEnd = in.at + size_t(std::min<uint64_t>(section, in.size > in.at ? in.size - in.at : 0));
    if (section) {
        try {
            auto info = in.wide();if(cmyk&&(in.at>sectionEnd||info>sectionEnd-in.at))throw std::runtime_error("Invalid CMYK layer info length"); auto infoAt = in.at; auto infoEnd = infoAt + size_t(std::min<uint64_t>(info, in.size > infoAt ? in.size - infoAt : 0));
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
            const auto signedCount=in.i16();mergedTransparency=signedCount<0;auto count = std::abs(signedCount); if (count > 256) throw std::runtime_error("Too many PSD layers");
            struct Record { int top, left, bottom, right; Blend blend; uint8_t opacity; std::string name; bool group{}, closer{}, visible{true}, hasMask{}; int maskTop{}, maskLeft{}, maskBottom{}, maskRight{}; uint8_t maskFlags{}; std::vector<std::pair<int16_t, uint64_t>> channels; std::vector<uint8_t> typeTool, fill, solidFill, geometry, strokeData, vectorPath; };
            std::vector<Record> records;
            for (int i = 0; i < count; ++i) {
                Record record; record.top = in.i32(); record.left = in.i32(); record.bottom = in.i32(); record.right = in.i32();
                auto channelCount = in.u16(); if (channelCount > 16) throw std::runtime_error("Too many PSD channels");
                for (int c = 0; c < channelCount; ++c) { auto id = in.i16(); auto length = in.wide();if(cmyk&&std::any_of(record.channels.begin(),record.channels.end(),[id](const auto& channel){return channel.first==id;}))throw std::runtime_error("Duplicate CMYK PSD channel"); record.channels.push_back({id, length}); }
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
                    else if (key == "SoCo" && record.solidFill.empty()) { auto stored = std::min(length, uint64_t(extraEnd > in.at ? extraEnd - in.at : 0)); record.solidFill.assign(in.data + in.at, in.data + in.at + size_t(stored)); }
                    else if (key == "vscg" || key == "vogk" || key == "vstk") { auto stored = std::min(length, uint64_t(extraEnd > in.at ? extraEnd - in.at : 0)); auto& dest = key == "vscg" ? record.fill : key == "vogk" ? record.geometry : record.strokeData; if (dest.empty()) dest.assign(in.data + in.at, in.data + in.at + size_t(stored)); }
                    else if ((key == "vmsk" || key == "vsms") && record.vectorPath.empty()) { auto stored = std::min(length, uint64_t(extraEnd > in.at ? extraEnd - in.at : 0)); record.vectorPath.assign(in.data + in.at, in.data + in.at + size_t(stored)); }
                    auto next = start + size_t(length + (length & 1));
                    in.at = next > extraEnd ? extraEnd : next;
                }
                in.at = extraEnd; records.push_back(std::move(record));
            }
            std::vector<std::string> folders;
            uint64_t retainedPixels=0,retainedMaskPixels=0,retainedBytes=0;
            for (auto& record : records) {
                // A shape's solid fill may be stored as a SoCo block, whose payload lacks the "SoCo" prefix that the vscg form carries.
                // Only a layer with vector geometry is a shape, so a plain solid-colour fill layer keeps its existing handling.
                if (record.fill.empty() && !record.geometry.empty() && !record.solidFill.empty()) {
                    const std::string tag = "SoCo";
                    record.fill.assign(tag.begin(), tag.end());
                    record.fill.insert(record.fill.end(), record.solidFill.begin(), record.solidFill.end());
                }
                const int64_t sourceWidth = int64_t(record.right) - record.left;
                const int64_t sourceHeight = int64_t(record.bottom) - record.top;
                const int64_t visibleLeft = std::max<int64_t>(record.left, 0);
                const int64_t visibleTop = std::max<int64_t>(record.top, 0);
                const int64_t visibleRight = std::min<int64_t>(record.right, result.document.width);
                const int64_t visibleBottom = std::min<int64_t>(record.bottom, result.document.height);
                const int cropWidth = visibleRight > visibleLeft ? int(visibleRight - visibleLeft) : 0;
                const int cropHeight = visibleBottom > visibleTop ? int(visibleBottom - visibleTop) : 0;
                const uint64_t cropLeft = visibleRight > visibleLeft ? uint64_t(visibleLeft - record.left) : 0;
                const uint64_t cropTop = visibleBottom > visibleTop ? uint64_t(visibleTop - record.top) : 0;
                const uint64_t cropPixels = uint64_t(cropWidth) * uint64_t(cropHeight);
                const bool hasVisibleLayer = cropWidth > 0 && cropHeight > 0;
                const bool sourceFits = sourceWidth > 0 && sourceHeight > 0 && sourceWidth <= 30000 && sourceHeight <= 30000 && uint64_t(sourceWidth) * uint64_t(sourceHeight) <= 100000000;
                const bool cropFits = hasVisibleLayer && cropWidth <= 30000 && cropHeight <= 30000 && cropPixels <= 100000000;
                const bool cropOversizedSource = !sourceFits;
                const int decodeWidth = sourceFits ? int(sourceWidth) : cropWidth;
                const int decodeHeight = sourceFits ? int(sourceHeight) : cropHeight;
                const uint64_t decodeLeft = sourceFits ? 0 : cropLeft;
                const uint64_t decodeTop = sourceFits ? 0 : cropTop;
                const uint64_t decodedPixels = uint64_t(decodeWidth) * uint64_t(decodeHeight);

                const int64_t maskSourceWidth = int64_t(record.maskRight) - record.maskLeft;
                const int64_t maskSourceHeight = int64_t(record.maskBottom) - record.maskTop;
                const int64_t maskVisibleLeft = std::max<int64_t>(record.maskLeft, 0);
                const int64_t maskVisibleTop = std::max<int64_t>(record.maskTop, 0);
                const int64_t maskVisibleRight = std::min<int64_t>(record.maskRight, result.document.width);
                const int64_t maskVisibleBottom = std::min<int64_t>(record.maskBottom, result.document.height);
                const int maskCropWidth = maskVisibleRight > maskVisibleLeft ? int(maskVisibleRight - maskVisibleLeft) : 0;
                const int maskCropHeight = maskVisibleBottom > maskVisibleTop ? int(maskVisibleBottom - maskVisibleTop) : 0;
                const uint64_t maskCropLeft = maskVisibleRight > maskVisibleLeft ? uint64_t(maskVisibleLeft - record.maskLeft) : 0;
                const uint64_t maskCropTop = maskVisibleBottom > maskVisibleTop ? uint64_t(maskVisibleTop - record.maskTop) : 0;
                const bool maskSourceFits = record.hasMask && maskSourceWidth > 0 && maskSourceHeight > 0 && maskSourceWidth <= 30000 && maskSourceHeight <= 30000 && uint64_t(maskSourceWidth) * uint64_t(maskSourceHeight) <= 100000000;
                const bool maskCropFits = record.hasMask && maskSourceWidth > 0 && maskSourceHeight > 0 && maskCropWidth > 0 && maskCropHeight > 0 && maskCropWidth <= 30000 && maskCropHeight <= 30000 && uint64_t(maskCropWidth) * uint64_t(maskCropHeight) <= 100000000;
                const bool cropMask = !maskSourceFits;
                const int decodeMaskWidth = cropMask ? maskCropWidth : int(maskSourceWidth);
                const int decodeMaskHeight = cropMask ? maskCropHeight : int(maskSourceHeight);
                const uint64_t decodeMaskLeft = cropMask ? maskCropLeft : 0;
                const uint64_t decodeMaskTop = cropMask ? maskCropTop : 0;
                const bool maskFits = hasVisibleLayer && (maskSourceFits || maskCropFits);
                const bool hasVectorData = !record.fill.empty() || !record.geometry.empty() || !record.strokeData.empty();
                if (!record.group && !record.closer && !hasVisibleLayer && !hasVectorData && record.typeTool.empty())
                    report += (record.name.empty() ? "Layer" : record.name) + " is outside the document canvas and was skipped.\n";
                else if (!record.group && !record.closer && !sourceFits && !cropFits && !hasVectorData && record.typeTool.empty())
                    report += (record.name.empty() ? "Layer" : record.name) + " has no visible pixels within the supported limits and was skipped.\n";
                if(cmyk&&!record.group&&!record.closer&&hasVisibleLayer&&(sourceFits||cropFits)) {
                    const uint64_t maskPixels=maskFits?uint64_t(decodeMaskWidth)*uint64_t(decodeMaskHeight):0;
                    if(decodedPixels>100000000-retainedPixels||maskPixels>100000000-retainedMaskPixels)
                        throw std::runtime_error("CMYK PSD exceeds the project pixel budget");
                    // Four inks plus alpha, optional deep planes and the installed RGB image coexist.
                    const auto tileBytes=cmykRasterBytes(decodeWidth,decodeHeight);
                    const uint64_t working=retainedBytes+decodedPixels*(depth==16?27u:9u)+tileBytes+sizeof(Raster::Tile)+maskPixels*2;
                    if(working>1200000000ULL)throw std::runtime_error("CMYK PSD exceeds the decode memory budget");
                    retainedPixels+=decodedPixels;retainedMaskPixels+=maskPixels;retainedBytes+=tileBytes+decodedPixels*(depth==16?8u:0u)+maskPixels;
                }
                std::vector<uint8_t> red, green, blue, alpha, maskPlane,key;
                std::vector<uint16_t> red16, green16, blue16, alpha16,key16;
                for (auto [id, length] : record.channels) {
                    auto start = in.at;
                    if(cmyk&&(length<2||start>sectionEnd||length>sectionEnd-start))throw std::runtime_error("Invalid CMYK PSD channel length");
                    try {
                        uint16_t compression = length >= 2 ? in.u16() : 0;
                        const size_t packed = length >= 2 ? size_t(length - 2) : 0;
                        if (!record.group && !record.closer && maskFits && id == -2)
                            maskPlane = decodeChannel(in, uint64_t(maskSourceWidth), uint64_t(maskSourceHeight), decodeMaskLeft, decodeMaskTop, decodeMaskWidth, decodeMaskHeight, compression, int(depth), packed);
                        else if ((sourceFits || cropFits) && hasVisibleLayer && !record.group && !record.closer && (id == 0 || id == 1 || id == 2 || id == -1 || (cmyk && id == 3))) {
                            std::vector<uint16_t> samples;
                            auto plane = decodeChannel(in, uint64_t(sourceWidth), uint64_t(sourceHeight), decodeLeft, decodeTop, decodeWidth, decodeHeight, compression, int(depth), packed, nullptr, 0, depth == 16 ? &samples : nullptr);
                            if (id == 0) { red = std::move(plane); red16 = std::move(samples); }
                            else if (id == 1) { green = std::move(plane); green16 = std::move(samples); }
                            else if (id == 2) { blue = std::move(plane); blue16 = std::move(samples); }
                            else if(id==3){key=std::move(plane);key16=std::move(samples);}
                            else { alpha = std::move(plane); alpha16 = std::move(samples); }
                        }
                    } catch (const std::exception& error) { if(cmyk)throw; report += (record.name.empty() ? "Layer" : record.name) + ": a channel could not be decoded (" + error.what() + ").\n"; }
                    const size_t end = start > in.size || length > in.size - start ? in.size : start + size_t(length);
                    in.at = end;
                }
                try {
                if (record.closer) { if (!folders.empty()) folders.pop_back(); continue; }
                if (!record.group && !hasVisibleLayer && !hasVectorData && record.typeTool.empty()) continue;
                if (!record.group && !sourceFits && !cropFits && !hasVectorData && record.typeTool.empty()) continue;
                if(color&&!record.group&&!record.closer&&hasVisibleLayer)convertInks(*color,decodeWidth,red,green,blue,key,red16,green16,blue16,key16);
                Layer layer; layer.id = newId(); layer.name = record.name.empty() ? "Layer" : record.name; layer.opacity = record.opacity / 255.; layer.blend = record.group ? Blend::Normal : record.blend; layer.group = record.group; layer.visible = record.visible;
                layer.transform = record.group
                    ? Transform{double(record.left), double(record.top), double(std::max<int64_t>(sourceWidth, 1)), double(std::max<int64_t>(sourceHeight, 1))}
                    : cropOversizedSource && cropFits
                        ? Transform{double(visibleLeft), double(visibleTop), double(cropWidth), double(cropHeight)}
                        : Transform{double(record.left), double(record.top), double(std::max<int64_t>(sourceWidth, 1)), double(std::max<int64_t>(sourceHeight, 1))};
                layer.parentId = folders.empty() ? std::string{} : folders.back();
                if (record.group) folders.push_back(layer.id);
                else if ((sourceFits || cropFits) && red.size() == size_t(decodedPixels) && green.size() == red.size() && blue.size() == red.size()) installImage(layer, decodeWidth, decodeHeight, red, green, blue, alpha, red16, green16, blue16, alpha16);
                std::string shapeReason;
                if (!cmyk && !record.group && !record.closer && !record.fill.empty()) shapeReason = convertVectorShape(layer, record.fill, record.geometry, record.vectorPath, record.strokeData, result.document.width, result.document.height);
                if (!cmyk && !layer.raster && !record.group) { if (auto painted = rasterizeRoundedRect(record.fill, record.geometry, record.strokeData)) { layer.raster = std::move(painted->raster); layer.transform = painted->transform; } }
                if (!shapeReason.empty()) warnings.push_back(layer.name + (layer.raster ? " stays as pixels: " : " could not be imported: ") + shapeReason + ".");
                const size_t decodedMaskPixels = size_t(decodeMaskWidth) * size_t(decodeMaskHeight);
                if (maskFits && maskPlane.size() == decodedMaskPixels) {
                    if (record.maskFlags & 0x04) for (auto& value : maskPlane) value = uint8_t(255 - value);
                    auto gray = std::make_shared<GrayRaster>(); gray->width = decodeMaskWidth; gray->height = decodeMaskHeight; gray->pixels = std::move(maskPlane);
                    Mask mask; mask.raster = std::move(gray); mask.enabled = (record.maskFlags & 0x02) == 0; mask.linked = true;
                    if (record.maskLeft != record.left || record.maskTop != record.top || record.maskRight != record.right || record.maskBottom != record.bottom)
                        mask.placement = cropMask
                            ? Transform{double(maskVisibleLeft), double(maskVisibleTop), double(maskCropWidth), double(maskCropHeight)}
                            : Transform{double(record.maskLeft), double(record.maskTop), double(maskSourceWidth), double(maskSourceHeight)};
                    layer.mask = std::move(mask);
                }
                if(cmyk&&!record.group&&(hasVectorData||!record.typeTool.empty()))warnings.push_back(layer.name+": CMYK text/vector content remains rasterized.");
                if (!cmyk && !record.group && !record.typeTool.empty()) { auto imported = readPhotoshopText(record.typeTool.data(), record.typeTool.size()); if (!imported.note.empty()) { report += layer.name + ": " + imported.note; if (imported.note.back() != '\n') report.push_back('\n'); } if (imported.editable) { if (layer.raster) layer.text = imported.text; else { try { auto drawn = text::rasterize(imported.text); layer.text = imported.text; layer.raster = drawn.raster; layer.transform.x = imported.x; layer.transform.y = imported.y; layer.transform.width = drawn.width; layer.transform.height = drawn.height; } catch (const std::exception&) { layer.text.reset(); report += layer.name + ": Editable Photoshop text becomes pixels and can't be retyped.\n"; } } } }
                if (layer.group || layer.raster || layer.text || layer.mask) result.document.layers.push_back(std::move(layer));
                } catch (const std::exception& error) { if(cmyk)throw; report += (record.name.empty() ? "Layer" : record.name) + " (" + std::to_string(sourceWidth) + "x" + std::to_string(sourceHeight) + "): " + error.what() + "\n"; }
            }
            in.at = std::max(in.at, infoEnd);
            }
        } catch (const std::exception& error) {
            if(cmyk)throw;
            report += std::string("Layer records were skipped: ") + error.what() + "\n";
            result.document.layers.clear();
            warnings.clear();
        }
    }
    in.at = std::max(in.at, sectionEnd);
    const bool hasImage = std::any_of(result.document.layers.begin(), result.document.layers.end(), [](const Layer& layer) { return layer.raster || layer.text; });
    if (!hasImage) {
        result.document.layers.clear();
        Layer merged;
        try { if (readMerged(in, int(width), int(height), int(depth), int(channels), merged, report,color.get(),mergedTransparency) && merged.raster) { result.document.layers = {std::move(merged)}; report += "Imported the merged image.\n"; } }
        catch (const std::exception& error) { report += std::string("Merged image was skipped: ") + error.what() + "\n"; }
    }
    if (result.document.layers.empty()) throw std::runtime_error(report.empty() ? "PSD contains no supported image" : report);
    report += "Imported " + std::to_string(result.document.layers.size()) + " layer(s).\n"; result.report = report; result.warnings = std::move(warnings); validateDocument(result.document); return result;
}
}
