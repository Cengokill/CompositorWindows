#include "PsdDocument.h"
#include "PsdText.h"
#include "core/Document.h"
#include "text/TextRaster.h"
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
void appendUtf8(std::string& out,uint32_t codePoint){
    if(codePoint<=0x7f)out.push_back(char(codePoint));
    else if(codePoint<=0x7ff){out.push_back(char(0xc0|(codePoint>>6)));out.push_back(char(0x80|(codePoint&0x3f)));}
    else if(codePoint<=0xffff){out.push_back(char(0xe0|(codePoint>>12)));out.push_back(char(0x80|((codePoint>>6)&0x3f)));out.push_back(char(0x80|(codePoint&0x3f)));}
    else{out.push_back(char(0xf0|(codePoint>>18)));out.push_back(char(0x80|((codePoint>>12)&0x3f)));out.push_back(char(0x80|((codePoint>>6)&0x3f)));out.push_back(char(0x80|(codePoint&0x3f)));}
}
std::string unicodeLayerName(Reader& in,uint64_t length){
    if(length<4)return {};
    const auto count=in.u32();if(uint64_t(count)>((length-4)/2))throw std::runtime_error("PSD Unicode layer name overrun");
    std::string result;
    for(uint32_t index=0;index<count;++index){
        const uint32_t first=in.u16();
        if(first>=0xd800&&first<=0xdbff&&index+1<count){
            const uint32_t second=in.u16();
            if(second>=0xdc00&&second<=0xdfff){appendUtf8(result,0x10000+((first-0xd800)<<10)+(second-0xdc00));++index;continue;}
            appendUtf8(result,0xfffd);
            appendUtf8(result,second);
            continue;
        }
        appendUtf8(result,(first>=0xdc00&&first<=0xdfff)?0xfffd:first);
    }
    return result;
}
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
        if(rows[y]>in.size-in.at)throw std::runtime_error("PSD PackBits row ends early");
        size_t written = 0; const auto end = in.at + rows[y];
        while (in.at < end) {
            int control = int8_t(in.u8());
            if (control >= 0) {
                const auto count = size_t(control + 1);
                if(count>end-in.at||count>size_t(width)-written)throw std::runtime_error("PSD PackBits literal row overrun");
                for (size_t i = 0; i < count; ++i) out[size_t(y) * width + written++] = in.u8();
            }else if (control > -128) {
                const auto count = size_t(1-control);
                if(in.at==end||count>size_t(width)-written)throw std::runtime_error("PSD PackBits repeat row overrun");
                const auto value = in.u8();
                for (size_t i = 0; i < count; ++i) out[size_t(y) * width + written++] = value;
            }
        }
        if(written!=size_t(width))throw std::runtime_error("PSD PackBits row is incomplete");
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
    if (mode == 4) throw std::runtime_error("CMYK PSD files are not supported");
    if (channels < 3 || depth != 8 || mode != 3) throw std::runtime_error("Only 8-bit RGB PSD files are supported");
    if (!width || !height || width > 30000 || height > 30000 || uint64_t(width) * height > 100000000) throw std::runtime_error("PSD exceeds the canvas limit");
    in.skip(in.u32()); in.skip(in.u32());
    PsdImport result; result.document.width = int(width); result.document.height = int(height); result.document.resolution = 72; std::string report;
    auto section = in.wide(); auto sectionEnd = in.at + size_t(section);
    if (section) {
        auto info = in.wide(); auto infoEnd = in.at + size_t(info); auto count = std::abs(in.i16()); if (count > 256) throw std::runtime_error("Too many PSD layers");
        struct Record { int top, left, bottom, right; Blend blend; uint8_t opacity; std::string name; bool group{}, closer{}; std::vector<std::pair<int16_t, uint64_t>> channels; std::vector<uint8_t> typeTool; };
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
                else if (key == "luni") record.name = unicodeLayerName(in,length);
                else if ((key == "TySh" || key == "txt2") && record.typeTool.empty()) { auto stored = std::min(length, uint64_t(extraEnd - in.at)); record.typeTool.assign(in.data + in.at, in.data + in.at + size_t(stored)); }
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
            if (!record.group && !record.typeTool.empty()) { auto imported = readPhotoshopText(record.typeTool.data(), record.typeTool.size()); if (!imported.note.empty()) { report += layer.name + ": " + imported.note; if (imported.note.back() != '\n') report.push_back('\n'); } if (imported.editable) { try { auto drawn = text::rasterize(imported.text); layer.text = imported.text; layer.raster = drawn.raster; layer.transform.x = imported.x; layer.transform.y = imported.y; layer.transform.width = drawn.width; layer.transform.height = drawn.height; } catch (const std::exception&) { layer.text.reset(); report += layer.name + ": Editable Photoshop text becomes pixels and can't be retyped.\n"; } } }
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
