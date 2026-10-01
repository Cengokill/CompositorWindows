#include "PsdDocument.h"
#include "core/Document.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>
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
std::vector<uint8_t> decodeChannel(Reader& in, int width, int height, uint16_t compression) {
    std::vector<uint8_t> out(size_t(width) * height);
    if (compression == 0) { for (auto& byte : out) byte = in.u8(); return out; }
    if (compression != 1) throw std::runtime_error("Unsupported PSD compression");
    std::vector<uint32_t> rows(height); for (int y = 0; y < height; ++y) rows[y] = in.psb ? uint32_t(in.u32()) : in.u16();
    for (int y = 0; y < height; ++y) {
        size_t written = 0; auto end = in.at + rows[y];
        while (in.at < end && written < size_t(width)) {
            int control = int8_t(in.u8());
            if (control >= 0) { int count = control + 1; for (int i = 0; i < count && written < size_t(width); ++i) out[size_t(y) * width + written++] = in.u8(); }
            else if (control > -128) { auto value = in.u8(); int count = 1 - control; for (int i = 0; i < count && written < size_t(width); ++i) out[size_t(y) * width + written++] = value; }
        }
        if (in.at > end) throw std::runtime_error("PSD channel overrun"); in.at = end;
    }
    return out;
}
std::shared_ptr<const Raster> compose(int width, int height, const std::vector<uint8_t>& r, const std::vector<uint8_t>& g, const std::vector<uint8_t>& b, const std::vector<uint8_t>& a) {
    std::vector<uint8_t> rgba(size_t(width) * height * 4);
    for (size_t i = 0; i < size_t(width) * height; ++i) { uint8_t alpha = a.empty() ? 255 : a[i]; rgba[i * 4] = uint8_t(r[i] * alpha / 255); rgba[i * 4 + 1] = uint8_t(g[i] * alpha / 255); rgba[i * 4 + 2] = uint8_t(b[i] * alpha / 255); rgba[i * 4 + 3] = alpha; }
    return Raster::fromRgba(width, height, rgba.data(), size_t(width) * 4);
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
    if (channels < 3 || depth != 8 || mode != 3) throw std::runtime_error("Only 8-bit RGB PSD files are supported");
    if (!width || !height || width > 30000 || height > 30000 || uint64_t(width) * height > 100000000) throw std::runtime_error("PSD exceeds the canvas limit");
    in.skip(in.u32()); in.skip(in.u32());
    PsdImport result; result.document.width = int(width); result.document.height = int(height); result.document.resolution = 72; std::string report;
    auto section = in.wide(); auto sectionEnd = in.at + size_t(section);
    if (section) {
        auto info = in.wide(); auto infoEnd = in.at + size_t(info); auto count = std::abs(in.i16()); if (count > 256) throw std::runtime_error("Too many PSD layers");
        struct Record { int top, left, bottom, right; Blend blend; uint8_t opacity; std::string name; bool group{}, closer{}; std::vector<std::pair<int16_t, uint64_t>> channels; std::string text; };
        std::vector<Record> records;
        for (int i = 0; i < count; ++i) {
            Record record; record.top = in.i32(); record.left = in.i32(); record.bottom = in.i32(); record.right = in.i32();
            auto channelCount = in.u16(); if (channelCount > 16) throw std::runtime_error("Too many PSD channels");
            for (int c = 0; c < channelCount; ++c) { auto id = in.i16(); auto length = in.wide(); record.channels.push_back({id, length}); }
            if (in.ascii(4) != "8BIM") throw std::runtime_error("Invalid PSD blend signature"); record.blend = blendOf(in.ascii(4), report);
            record.opacity = in.u8(); in.u8(); in.u8(); in.u8(); auto extra = in.u32(); auto extraEnd = in.at + extra;
            in.skip(in.u32()); in.skip(in.u32()); auto nameLength = in.u8(); record.name = in.ascii(nameLength); auto padded = (1 + nameLength + 3) & ~3; in.skip(padded - 1 - nameLength);
            while (in.at + 12 <= extraEnd) {
                if (in.ascii(4) != "8BIM") break; auto key = in.ascii(4); auto length = in.psb && (key == "LMsk" || key == "Lr16" || key == "Lr32" || key == "Layr" || key == "Mt16" || key == "Mt32" || key == "Mtrn" || key == "Alph" || key == "FMsk" || key == "lnk2" || key == "FEid" || key == "FXid" || key == "PxSD") ? in.u64() : in.u32(); auto start = in.at;
                if (key == "lsct" || key == "lsdk") { auto kind = length >= 4 ? in.u32() : 0; record.group = kind == 1 || kind == 2; record.closer = kind == 3; }
                else if (key == "TySh" || key == "txt2") { auto block = std::string(reinterpret_cast<const char*>(in.data + in.at), std::min(length, uint64_t(extraEnd - in.at))); auto marker = block.find("Txt "); if (marker != std::string::npos && marker + 8 < block.size()) { uint32_t chars = uint8_t(block[marker + 4]) << 24 | uint8_t(block[marker + 5]) << 16 | uint8_t(block[marker + 6]) << 8 | uint8_t(block[marker + 7]); size_t encoded = std::min(size_t(chars) * 2, block.size() - (marker + 8)); for (size_t n = 0; n + 1 < encoded; n += 2) { char16_t unit = char16_t(uint8_t(block[marker + 8 + n]) | uint8_t(block[marker + 8 + n + 1]) << 8); if (unit < 0x80) record.text.push_back(char(unit)); else if (unit < 0x800) { record.text.push_back(char(0xC0 | unit >> 6)); record.text.push_back(char(0x80 | (unit & 0x3F))); } else { record.text.push_back(char(0xE0 | unit >> 12)); record.text.push_back(char(0x80 | ((unit >> 6) & 0x3F))); record.text.push_back(char(0x80 | (unit & 0x3F))); } } } }
                in.at = start + size_t(length + (length & 1)); if (in.at > extraEnd) throw std::runtime_error("PSD extra data overrun");
            }
            in.at = extraEnd; records.push_back(std::move(record));
        }
        std::vector<std::string> folders;
        for (auto& record : records) {
            int w = std::max(0, record.right - record.left), h = std::max(0, record.bottom - record.top);
            std::vector<uint8_t> red, green, blue, alpha;
            for (auto [id, length] : record.channels) {
                auto start = in.at; uint16_t compression = length >= 2 ? in.u16() : 0;
                if (!record.group && !record.closer && w > 0 && h > 0 && (id == 0 || id == 1 || id == 2 || id == -1)) { auto plane = decodeChannel(in, w, h, compression); if (id == 0) red = std::move(plane); else if (id == 1) green = std::move(plane); else if (id == 2) blue = std::move(plane); else alpha = std::move(plane); }
                if (in.at < start + length) in.at = start + size_t(length); else if (in.at > start + length) throw std::runtime_error("PSD channel overrun");
            }
            if (record.closer) { if (!folders.empty()) folders.pop_back(); continue; }
            Layer layer; layer.id = newId(); layer.name = record.name.empty() ? "Layer" : record.name; layer.opacity = record.opacity / 255.; layer.blend = record.group ? Blend::Normal : record.blend; layer.group = record.group;
            layer.transform = {double(record.left), double(record.top), double(std::max(w, 1)), double(std::max(h, 1))}; layer.parentId = folders.empty() ? std::string{} : folders.back();
            if (record.group) folders.push_back(layer.id);
            else if (red.size() == size_t(w) * h && green.size() == red.size() && blue.size() == red.size()) layer.raster = compose(w, h, red, green, blue, alpha);
            if (!record.group && !record.text.empty()) { TextContent text; text.value = record.text; text.fontFamily = "Segoe UI"; layer.text = text; }
            result.document.layers.push_back(std::move(layer));
        }
        in.at = std::max(in.at, infoEnd);
    }
    in.at = std::max(in.at, sectionEnd);
    if (result.document.layers.empty() && in.at + 2 <= in.size) {
        auto compression = in.u16(); auto red = decodeChannel(in, int(width), int(height), compression); auto green = decodeChannel(in, int(width), int(height), compression); auto blue = decodeChannel(in, int(width), int(height), compression);
        Layer layer; layer.id = newId(); layer.name = "Background"; layer.transform = {0, 0, double(width), double(height)}; layer.raster = compose(int(width), int(height), red, green, blue, {}); result.document.layers.push_back(std::move(layer)); report += "Imported the merged image.\n";
    }
    if (result.document.layers.empty()) throw std::runtime_error("PSD contains no supported image");
    report += "Imported " + std::to_string(result.document.layers.size()) + " layer(s).\n"; result.report = report; validateDocument(result.document); return result;
}
}
