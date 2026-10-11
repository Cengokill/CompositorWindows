#include "PsdDocument.h"
#include "PsdText.h"
#include "core/Document.h"
#include "text/TextRaster.h"
#include <algorithm>
#include <cstring>
#include <fstream>
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
std::vector<uint8_t> decodeChannel(Reader& in, int width, int height, uint16_t compression, int depth, const std::vector<uint32_t>* rowTable = nullptr, int rowIndex = 0) {
    const int sample = depth == 16 ? 2 : 1;
    if (width < 1 || height < 1 || width > 30000 || height > 30000 || uint64_t(width) * uint64_t(height) > 100000000) throw std::runtime_error("Raster dimensions exceed supported budget");
    const size_t row = size_t(width) * sample;
    std::vector<uint8_t> raw(row * size_t(height));
    if (compression == 0) { for (auto& byte : raw) byte = in.u8(); }
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
    auto plane = [&](int index) { return decodeChannel(in, width, height, compression, depth, rows.empty() ? nullptr : &rows, index * height); };
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
            auto info = in.wide(); auto infoEnd = in.at + size_t(std::min<uint64_t>(info, in.size > in.at ? in.size - in.at : 0)); auto count = std::abs(in.i16()); if (count > 256) throw std::runtime_error("Too many PSD layers");
            struct Record { int top, left, bottom, right; Blend blend; uint8_t opacity; std::string name; bool group{}, closer{}; std::vector<std::pair<int16_t, uint64_t>> channels; std::vector<uint8_t> typeTool; };
            std::vector<Record> records;
            for (int i = 0; i < count; ++i) {
                Record record; record.top = in.i32(); record.left = in.i32(); record.bottom = in.i32(); record.right = in.i32();
                auto channelCount = in.u16(); if (channelCount > 16) throw std::runtime_error("Too many PSD channels");
                for (int c = 0; c < channelCount; ++c) { auto id = in.i16(); auto length = in.wide(); record.channels.push_back({id, length}); }
                if (in.ascii(4) != "8BIM") throw std::runtime_error("Invalid PSD blend signature"); record.blend = blendOf(in.ascii(4), report);
                record.opacity = in.u8(); in.u8(); in.u8(); in.u8(); auto extra = in.u32(); auto extraEnd = in.at + extra;
                if (extraEnd > in.size) extraEnd = in.size;
                in.skip(in.u32()); in.skip(in.u32()); auto nameLength = in.u8(); record.name = macRomanToUtf8(in.ascii(nameLength)); auto padded = (1 + nameLength + 3) & ~3; in.skip(padded - 1 - nameLength);
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
                    auto next = start + size_t(length + (length & 1));
                    in.at = next > extraEnd ? extraEnd : next;
                }
                in.at = extraEnd; records.push_back(std::move(record));
            }
            std::vector<std::string> folders;
            for (auto& record : records) {
                int w = std::max(0, record.right - record.left), h = std::max(0, record.bottom - record.top);
                const bool fits = w > 0 && h > 0 && w <= 30000 && h <= 30000 && uint64_t(w) * uint64_t(h) <= 100000000;
                if (!record.group && !record.closer && (w > 30000 || h > 30000 || (w > 0 && h > 0 && uint64_t(w) * uint64_t(h) > 100000000)))
                    report += (record.name.empty() ? "Layer" : record.name) + " exceeds the canvas limit and was skipped.\n";
                std::vector<uint8_t> red, green, blue, alpha;
                for (auto [id, length] : record.channels) {
                    auto start = in.at;
                    try {
                        uint16_t compression = length >= 2 ? in.u16() : 0;
                        if (fits && !record.group && !record.closer && (id == 0 || id == 1 || id == 2 || id == -1)) {
                            auto plane = decodeChannel(in, w, h, compression, int(depth));
                            if (id == 0) red = std::move(plane); else if (id == 1) green = std::move(plane); else if (id == 2) blue = std::move(plane); else alpha = std::move(plane);
                        }
                    } catch (const std::exception&) { report += (record.name.empty() ? "Layer" : record.name) + ": a channel could not be decoded.\n"; }
                    const size_t end = start > in.size || length > in.size - start ? in.size : start + size_t(length);
                    in.at = end;
                }
                try {
                if (record.closer) { if (!folders.empty()) folders.pop_back(); continue; }
                Layer layer; layer.id = newId(); layer.name = record.name.empty() ? "Layer" : record.name; layer.opacity = record.opacity / 255.; layer.blend = record.group ? Blend::Normal : record.blend; layer.group = record.group;
                layer.transform = {double(record.left), double(record.top), double(std::max(w, 1)), double(std::max(h, 1))}; layer.parentId = folders.empty() ? std::string{} : folders.back();
                if (record.group) folders.push_back(layer.id);
                else if (fits && red.size() == size_t(w) * size_t(h) && green.size() == red.size() && blue.size() == red.size()) layer.raster = compose(w, h, red, green, blue, alpha);
                if (!record.group && !record.typeTool.empty()) { auto imported = readPhotoshopText(record.typeTool.data(), record.typeTool.size()); if (!imported.note.empty()) { report += layer.name + ": " + imported.note; if (imported.note.back() != '\n') report.push_back('\n'); } if (imported.editable) { try { auto drawn = text::rasterize(imported.text); layer.text = imported.text; layer.raster = drawn.raster; layer.transform.x = imported.x; layer.transform.y = imported.y; layer.transform.width = drawn.width; layer.transform.height = drawn.height; } catch (const std::exception&) { layer.text.reset(); report += layer.name + ": Editable Photoshop text becomes pixels and can't be retyped.\n"; } } }
                if (layer.group || layer.raster || layer.text) result.document.layers.push_back(std::move(layer));
                } catch (const std::exception& error) { report += (record.name.empty() ? "Layer" : record.name) + " (" + std::to_string(w) + "x" + std::to_string(h) + "): " + error.what() + "\n"; }
            }
            in.at = std::max(in.at, infoEnd);
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
