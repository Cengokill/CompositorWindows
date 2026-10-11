#include "PsdText.h"
#include "text/TextRaster.h"
#include "text/TextStyle.h"
#include <dwrite_3.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <optional>
#include <vector>

namespace compositor::imaging {
namespace {
using Microsoft::WRL::ComPtr;
struct Engine {
    enum class Kind { Number, Bool, String, Dict, Array } kind{Kind::Number};
    double number{};
    bool flag{};
    std::string text;
    std::map<std::string, Engine> items;
    std::vector<Engine> values;
};
struct Descriptor {
    enum class Kind { Text, Number, Enum, Data, Map, List } kind{Kind::Number};
    double number{};
    std::string text;
    std::vector<uint8_t> bytes;
    std::map<std::string, Descriptor> items;
    std::vector<Descriptor> values;
};
struct Cursor {
    const uint8_t* data{};
    size_t size{}, at{};
    bool ok{true};
    size_t remaining() const { return at > size ? 0 : size - at; }
    bool need(size_t n) { if (!ok || n > remaining()) { ok = false; return false; } return true; }
    uint8_t u8() { if (!need(1)) return 0; return data[at++]; }
    uint16_t u16() { if (!need(2)) return 0; uint16_t v = uint16_t(data[at] << 8 | data[at + 1]); at += 2; return v; }
    uint32_t u32() { if (!need(4)) return 0; uint32_t v = 0; for (int i = 0; i < 4; ++i) v = (v << 8) | data[at++]; return v; }
    int32_t i32() { return int32_t(u32()); }
    double f64() { if (!need(8)) return 0; uint64_t bits = 0; for (int i = 0; i < 8; ++i) bits = (bits << 8) | data[at++]; double value; std::memcpy(&value, &bits, 8); return value; }
    std::vector<uint8_t> bytes(size_t n) { if (!need(n)) return {}; std::vector<uint8_t> out(data + at, data + at + n); at += n; return out; }
    std::string ascii(size_t n) { auto raw = bytes(n); return {raw.begin(), raw.end()}; }
    std::string utf16() {
        auto count = u32(); if (!ok || count > 1000000) { ok = false; return {}; }
        auto raw = bytes(size_t(count) * 2); if (!ok) return {};
        std::string out;
        for (size_t i = 0; i + 1 < raw.size();) {
            uint32_t unit = uint32_t(raw[i] << 8 | raw[i + 1]); i += 2;
            if (unit >= 0xD800 && unit <= 0xDBFF && i + 1 < raw.size()) {
                uint32_t low = uint32_t(raw[i] << 8 | raw[i + 1]);
                if (low >= 0xDC00 && low <= 0xDFFF) { i += 2; unit = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00); }
            }
            if (unit < 0x80) out.push_back(char(unit));
            else if (unit < 0x800) { out.push_back(char(0xC0 | unit >> 6)); out.push_back(char(0x80 | (unit & 0x3F))); }
            else if (unit < 0x10000) { out.push_back(char(0xE0 | unit >> 12)); out.push_back(char(0x80 | ((unit >> 6) & 0x3F))); out.push_back(char(0x80 | (unit & 0x3F))); }
            else { out.push_back(char(0xF0 | unit >> 18)); out.push_back(char(0x80 | ((unit >> 12) & 0x3F))); out.push_back(char(0x80 | ((unit >> 6) & 0x3F))); out.push_back(char(0x80 | (unit & 0x3F))); }
        }
        return out;
    }
    std::string identifier() {
        auto length = u32(); if (!ok || length > 10000) { ok = false; return {}; }
        if (length == 0) return ascii(4);
        return ascii(size_t(length));
    }
};
bool delimiter(uint8_t byte) { return byte <= 0x20 || byte == '/' || byte == '<' || byte == '>' || byte == '[' || byte == ']' || byte == '(' || byte == ')'; }
struct EngineCursor {
    const std::vector<uint8_t>& bytes;
    size_t index{};
    std::optional<uint8_t> peek(int ahead = 0) const { size_t at = index + size_t(ahead); return at < bytes.size() ? std::optional<uint8_t>{bytes[at]} : std::nullopt; }
    void skip() {
        while (auto byte = peek()) {
            if (*byte == '%') { ++index; while (auto next = peek()) { if (*next == '\n' || *next == '\r') break; ++index; } }
            else if (*byte <= 0x20) ++index;
            else break;
        }
    }
    bool take(const char* token) {
        size_t n = std::strlen(token);
        if (index + n > bytes.size() || std::memcmp(bytes.data() + index, token, n) != 0) return false;
        index += n; return true;
    }
    bool takeWord(const char* token) {
        size_t n = std::strlen(token);
        if (index + n > bytes.size() || std::memcmp(bytes.data() + index, token, n) != 0) return false;
        if (index + n < bytes.size() && !delimiter(bytes[index + n])) return false;
        index += n; return true;
    }
    std::string token() { size_t start = index; while (auto byte = peek()) { if (delimiter(*byte)) break; ++index; } return {bytes.begin() + ptrdiff_t(start), bytes.begin() + ptrdiff_t(index)}; }
    std::optional<double> number() {
        size_t start = index;
        if (peek() && (*peek() == '+' || *peek() == '-')) ++index;
        while (auto byte = peek()) { if (*byte < '0' || *byte > '9') break; ++index; }
        if (peek() && *peek() == '.') { ++index; while (auto byte = peek()) { if (*byte < '0' || *byte > '9') break; ++index; } }
        if (peek() && (*peek() == 'e' || *peek() == 'E')) { ++index; if (peek() && (*peek() == '+' || *peek() == '-')) ++index; while (auto byte = peek()) { if (*byte < '0' || *byte > '9') break; ++index; } }
        if (index <= start) return std::nullopt;
        try { return std::stod(std::string(bytes.begin() + ptrdiff_t(start), bytes.begin() + ptrdiff_t(index))); } catch (...) { return std::nullopt; }
    }
    std::string decode(const std::vector<uint8_t>& raw) const {
        if (raw.size() >= 2 && raw[0] == 0xFE && raw[1] == 0xFF) {
            Cursor text{raw.data() + 2, raw.size() - 2}; auto count = (raw.size() - 2) / 2; text.size = raw.size() - 2; std::string units; units.reserve(count);
            Cursor wide{raw.data(), raw.size()}; wide.at = 2; wide.size = raw.size();
            std::string out; for (size_t i = 2; i + 1 < raw.size(); i += 2) { uint16_t unit = uint16_t(raw[i] << 8 | raw[i + 1]); if (unit < 0x80) out.push_back(char(unit)); else if (unit < 0x800) { out.push_back(char(0xC0 | unit >> 6)); out.push_back(char(0x80 | (unit & 0x3F))); } else { out.push_back(char(0xE0 | unit >> 12)); out.push_back(char(0x80 | ((unit >> 6) & 0x3F))); out.push_back(char(0x80 | (unit & 0x3F))); } }
            return out;
        }
        return {raw.begin(), raw.end()};
    }
    std::optional<Engine> parse();
    std::optional<Engine> dictionary() {
        if (!take("<<")) return std::nullopt;
        Engine dict; dict.kind = Engine::Kind::Dict;
        while (true) {
            skip(); if (!peek() || *peek() == '>') break;
            if (*peek() != '/') return std::nullopt;
            ++index; auto key = token(); auto value = parse(); if (!value) return std::nullopt;
            dict.items.emplace(std::move(key), std::move(*value));
        }
        if (!take(">>")) return std::nullopt;
        return dict;
    }
    std::optional<Engine> array() {
        if (!take("[")) return std::nullopt;
        Engine list; list.kind = Engine::Kind::Array;
        while (true) { skip(); if (!peek() || *peek() == ']') break; auto value = parse(); if (!value) return std::nullopt; list.values.push_back(std::move(*value)); }
        if (!take("]")) return std::nullopt;
        return list;
    }
    std::optional<Engine> string() {
        if (!take("(")) return std::nullopt;
        std::vector<uint8_t> raw;
        while (auto byte = peek()) {
            ++index;
            if (*byte == ')') break;
            if (*byte == '\\') {
                auto escaped = peek(); if (!escaped) return std::nullopt; ++index;
                if (*escaped == 'n') raw.push_back('\n');
                else if (*escaped == 'r') raw.push_back('\r');
                else if (*escaped == 't') raw.push_back('\t');
                else raw.push_back(*escaped);
            } else raw.push_back(*byte);
        }
        Engine value; value.kind = Engine::Kind::String; value.text = decode(raw); return value;
    }
};
std::optional<Engine> EngineCursor::parse() {
    skip(); auto byte = peek(); if (!byte) return std::nullopt;
    if (*byte == '<') { if (peek(1) && *peek(1) == '<') return dictionary(); ++index; std::vector<uint8_t> nibbles; while (auto next = peek()) { if (*next == '>') break; ++index; auto hex = [](uint8_t c)->int { if (c >= '0' && c <= '9') return c - '0'; if (c >= 'a' && c <= 'f') return c - 'a' + 10; if (c >= 'A' && c <= 'F') return c - 'A' + 10; return -1; }; int n = hex(*next); if (n >= 0) nibbles.push_back(uint8_t(n)); } if (!take(">")) return std::nullopt; std::vector<uint8_t> raw; for (size_t i = 0; i + 1 < nibbles.size(); i += 2) raw.push_back(uint8_t(nibbles[i] << 4 | nibbles[i + 1])); Engine value; value.kind = Engine::Kind::String; value.text = decode(raw); return value; }
    if (*byte == '[') return array();
    if (*byte == '(') return string();
    if (*byte == '/') { ++index; Engine value; value.kind = Engine::Kind::String; value.text = token(); return value; }
    if (*byte == '-' || *byte == '+' || *byte == '.' || (*byte >= '0' && *byte <= '9')) { auto parsed = number(); if (!parsed) return std::nullopt; Engine value; value.kind = Engine::Kind::Number; value.number = *parsed; return value; }
    if (takeWord("true")) { Engine value; value.kind = Engine::Kind::Bool; value.flag = true; return value; }
    if (takeWord("false")) { Engine value; value.kind = Engine::Kind::Bool; value.flag = false; return value; }
    if (takeWord("null")) { Engine value; value.kind = Engine::Kind::String; return value; }
    return std::nullopt;
}
const Engine* walk(const Engine* value, std::initializer_list<const char*> keys) {
    for (auto key : keys) { if (!value || value->kind != Engine::Kind::Dict) return nullptr; auto found = value->items.find(key); if (found == value->items.end()) return nullptr; value = &found->second; }
    return value;
}
std::optional<double> numberOf(const Engine* value) { return value && value->kind == Engine::Kind::Number ? std::optional<double>{value->number} : std::nullopt; }
std::optional<bool> boolOf(const Engine* value) { return value && value->kind == Engine::Kind::Bool ? std::optional<bool>{value->flag} : std::nullopt; }
const std::string* stringOf(const Engine* value) { return value && value->kind == Engine::Kind::String ? &value->text : nullptr; }
std::optional<Descriptor> descriptorValue(Cursor& in, const std::string& type);
std::optional<std::map<std::string, Descriptor>> descriptorMap(Cursor& in, bool versioned) {
    if (versioned && in.u32() != 16) return std::nullopt;
    if (!in.ok) return std::nullopt;
    in.utf16(); auto klass = in.identifier(); (void)klass; auto count = in.u32();
    if (!in.ok || count > 10000) return std::nullopt;
    std::map<std::string, Descriptor> items;
    for (uint32_t i = 0; i < count; ++i) {
        auto key = in.identifier(); auto type = in.ascii(4); auto value = descriptorValue(in, type);
        if (!in.ok || !value) return std::nullopt;
        items.emplace(std::move(key), std::move(*value));
    }
    return items;
}
bool skipReference(Cursor& in) {
    auto count = in.u32(); if (!in.ok || count > 10000) return false;
    for (uint32_t i = 0; i < count; ++i) {
        auto form = in.ascii(4); if (!in.ok) return false;
        if (form == "prop") { in.utf16(); in.identifier(); in.identifier(); }
        else if (form == "Clss") { in.utf16(); in.identifier(); }
        else if (form == "Enmr") { in.utf16(); in.identifier(); in.identifier(); in.identifier(); }
        else if (form == "rele") { in.utf16(); in.identifier(); in.i32(); }
        else if (form == "Idnt" || form == "indx") in.i32();
        else if (form == "name") in.utf16();
        else return false;
        if (!in.ok) return false;
    }
    return true;
}
std::optional<Descriptor> descriptorValue(Cursor& in, const std::string& type) {
    Descriptor value;
    if (type == "doub" || type == "UntF") { if (type == "UntF") in.ascii(4); value.kind = Descriptor::Kind::Number; value.number = in.f64(); }
    else if (type == "long") { value.kind = Descriptor::Kind::Number; value.number = in.i32(); }
    else if (type == "comp") { value.kind = Descriptor::Kind::Number; auto raw = in.bytes(8); if (raw.size() < 8) return std::nullopt; uint64_t bits = 0; for (auto byte : raw) bits = (bits << 8) | byte; value.number = double(int64_t(bits)); }
    else if (type == "bool") { in.u8(); value.kind = Descriptor::Kind::Number; }
    else if (type == "TEXT") { value.kind = Descriptor::Kind::Text; value.text = in.utf16(); }
    else if (type == "enum") { in.identifier(); value.kind = Descriptor::Kind::Enum; value.text = in.identifier(); }
    else if (type == "tdta") { auto length = in.u32(); if (length > 8000000) return std::nullopt; value.kind = Descriptor::Kind::Data; value.bytes = in.bytes(length); }
    else if (type == "Objc" || type == "GlbO") { auto nested = descriptorMap(in, false); if (!nested) return std::nullopt; value.kind = Descriptor::Kind::Map; value.items = std::move(*nested); }
    else if (type == "VlLs") {
        auto count = in.u32(); if (count > 10000) return std::nullopt; value.kind = Descriptor::Kind::List;
        for (uint32_t i = 0; i < count; ++i) { auto item = descriptorValue(in, in.ascii(4)); if (!item) return std::nullopt; value.values.push_back(std::move(*item)); }
    }
    else if (type == "alis") { auto length = in.u32(); if (length > 8000000) return std::nullopt; in.bytes(length); value.kind = Descriptor::Kind::Number; }
    else if (type == "obj ") { if (!skipReference(in)) return std::nullopt; value.kind = Descriptor::Kind::Number; }
    else if (type == "type" || type == "GlbC") { in.utf16(); in.identifier(); value.kind = Descriptor::Kind::Number; }
    else return std::nullopt;
    return in.ok ? std::optional<Descriptor>{std::move(value)} : std::nullopt;
}
const Descriptor* field(const std::map<std::string, Descriptor>& items, const char* key) { auto found = items.find(key); return found == items.end() ? nullptr : &found->second; }
std::optional<double> side(const Descriptor& box, const char* key) {
    auto found = box.items.find(key); if (found == box.items.end() || found->second.kind != Descriptor::Kind::Number || !std::isfinite(found->second.number)) return std::nullopt;
    return found->second.number;
}
std::string cleaned(std::string text) {
    while (!text.empty() && (text.front() == '\xEF' || text.front() == '\0')) {
        if (text.size() >= 3 && uint8_t(text[0]) == 0xEF && uint8_t(text[1]) == 0xBB && uint8_t(text[2]) == 0xBF) text.erase(0, 3);
        else if (text.front() == '\0') text.erase(text.begin());
        else break;
    }
    while (!text.empty() && (uint8_t(text.front()) == 0xFE || text.front() == '\0')) text.erase(text.begin());
    while (!text.empty() && text.back() == '\0') text.pop_back();
    std::string out; out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) { if (text[i] == '\r') { if (i + 1 < text.size() && text[i + 1] == '\n') ++i; out.push_back('\n'); } else out.push_back(text[i]); }
    return out;
}
std::optional<Engine> engineFrom(const std::vector<uint8_t>& bytes) {
    EngineCursor cursor{bytes}; auto parsed = cursor.parse();
    if (parsed && parsed->kind == Engine::Kind::Dict) return parsed;
    auto marker = std::search(bytes.begin(), bytes.end(), std::begin("<<"), std::end("<<") - 1);
    if (marker == bytes.end()) return std::nullopt;
    EngineCursor again{bytes}; again.index = size_t(marker - bytes.begin()); parsed = again.parse();
    return parsed && parsed->kind == Engine::Kind::Dict ? parsed : std::nullopt;
}
double unitColor(double value) { return value > 1 ? std::clamp(value, 0., 255.) / 255. : std::clamp(value, 0., 1.); }
std::wstring widen(const std::string& text) { if (text.empty()) return {}; int count = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0); if (count <= 0) return {}; std::wstring out(count, L'\0'); MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), out.data(), count); return out; }
std::string narrow(const std::wstring& text) { if (text.empty()) return {}; int count = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr); if (count <= 0) return {}; std::string out(size_t(count), '\0'); WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), out.data(), count, nullptr, nullptr); return out; }
bool familyExists(IDWriteFontCollection* fonts, const std::wstring& name) {
    UINT32 index = 0; BOOL exists = FALSE;
    return fonts && SUCCEEDED(fonts->FindFamilyName(name.c_str(), &index, &exists)) && exists;
}
std::optional<std::string> acceptedFamilyName(IDWriteFontCollection* fonts, IDWriteLocalizedStrings* names) {
    if (!fonts || !names) return std::nullopt;
    auto consider = [&](UINT32 index) -> std::optional<std::string> {
        UINT32 length = 0; if (FAILED(names->GetStringLength(index, &length)) || length == 0) return std::nullopt;
        std::wstring value(size_t(length) + 1, L'\0'); if (FAILED(names->GetString(index, value.data(), length + 1))) return std::nullopt;
        value.resize(length); if (!familyExists(fonts, value)) return std::nullopt; return narrow(value);
    };
    UINT32 preferred = 0; BOOL found = FALSE;
    if (SUCCEEDED(names->FindLocaleName(L"en-us", &preferred, &found)) && found) if (auto name = consider(preferred)) return name;
    for (UINT32 index = 0; index < names->GetCount(); ++index) if (auto name = consider(index)) return name;
    return std::nullopt;
}
// Photoshop FontSet names are often PostScript names (MongolianBaiti), not the DirectWrite family (Mongolian Baiti).
std::optional<std::string> installedFamily(const std::string& photoshopName) {
    const auto wideName = widen(photoshopName); if (wideName.empty()) return std::nullopt;
    ComPtr<IDWriteFactory> factory; if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(factory.GetAddressOf())))) return std::nullopt;
    ComPtr<IDWriteFontCollection> fonts; if (FAILED(factory->GetSystemFontCollection(fonts.GetAddressOf(), FALSE)) || !fonts) return std::nullopt;
    if (familyExists(fonts.Get(), wideName)) return photoshopName;
    ComPtr<IDWriteFactory3> factory3; if (FAILED(factory.As(&factory3)) || !factory3) return std::nullopt;
    ComPtr<IDWriteFontSet> set; if (FAILED(factory3->GetSystemFontSet(&set)) || !set) return std::nullopt;
    const DWRITE_FONT_PROPERTY property{DWRITE_FONT_PROPERTY_ID_POSTSCRIPT_NAME, wideName.c_str(), L""};
    ComPtr<IDWriteFontSet> matches; if (FAILED(set->GetMatchingFonts(&property, 1, &matches)) || !matches || matches->GetFontCount() == 0) return std::nullopt;
    ComPtr<IDWriteFontFaceReference> faceRef; if (FAILED(matches->GetFontFaceReference(0, &faceRef)) || !faceRef) return std::nullopt;
    ComPtr<IDWriteFontFace3> face; if (FAILED(faceRef->CreateFontFace(&face)) || !face) return std::nullopt;
    ComPtr<IDWriteLocalizedStrings> families; if (FAILED(face->GetFamilyNames(&families))) return std::nullopt;
    return acceptedFamilyName(fonts.Get(), families.Get());
}
const char* rasterNote = "Editable Photoshop text becomes pixels and can't be retyped.";
}
PhotoshopText readPhotoshopText(const uint8_t* data, size_t size) {
    PhotoshopText result; result.note = rasterNote;
    if (!data || !size || size > 8000000) return result;
    try {
        Cursor in{data, size}; if (in.u16() != 1) return result;
        const double xx = in.f64(), xy = in.f64(), yx = in.f64(), yy = in.f64(), tx = in.f64(), ty = in.f64();
        if (!in.ok || !std::isfinite(xx) || !std::isfinite(xy) || !std::isfinite(yx) || !std::isfinite(yy) || !std::isfinite(tx) || !std::isfinite(ty) || in.u16() != 50) return result;
        auto text = descriptorMap(in, true); if (!text) return result;
        if (auto orientation = field(*text, "Ornt"); orientation && orientation->kind == Descriptor::Kind::Enum && orientation->text == "Vrtc") { result.note = "Vertical Photoshop text was rasterized and can't be retyped."; return result; }
        const double scaleX = std::hypot(xx, yx); if (!(scaleX > 1e-6)) return result;
        const double cosR = xx / scaleX, sinR = yx / scaleX, localX = cosR * xy + sinR * yy, localY = -sinR * xy + cosR * yy, scaleY = std::abs(localY);
        const double largest = std::max(scaleX, scaleY);
        if (!(scaleY > 1e-6) || std::abs(localX) > 0.02 * largest || std::abs(scaleX - scaleY) > 0.02 * largest) return result;
        std::optional<Engine> engine; if (auto block = field(*text, "EngineData"); block && block->kind == Descriptor::Kind::Data) engine = engineFrom(block->bytes);
        std::string content;
        if (auto written = field(*text, "Txt "); written && written->kind == Descriptor::Kind::Text) content = cleaned(written->text);
        else if (auto writtenShort = field(*text, "Txt"); writtenShort && writtenShort->kind == Descriptor::Kind::Text) content = cleaned(writtenShort->text);
        else if (engine) if (auto engineText = stringOf(walk(&*engine, {"EngineDict", "Editor", "Text"}))) content = cleaned(*engineText);
        if (content.empty() || content.size() > 100000) return result;
        TextContent style; style.value = content; style.fontFamily = "Segoe UI"; style.fontSize = std::clamp(12 * scaleX, 1., 1000.);
        std::string notes;
        if (engine) {
            const Engine* runs = walk(&*engine, {"EngineDict", "StyleRun", "RunArray"});
            const Engine* first = runs && runs->kind == Engine::Kind::Array && !runs->values.empty() ? &runs->values.front() : &*engine;
            const Engine* dataNode = walk(first, {"StyleSheet", "StyleSheetData"}); if (!dataNode) dataNode = first;
            if (auto points = numberOf(walk(dataNode, {"FontSize"})); points && std::isfinite(*points) && *points > 0) style.fontSize = std::clamp(*points * scaleX, 1., 1000.);
            const Engine* fonts = walk(&*engine, {"ResourceDict", "FontSet"});
            int index = 0; if (auto font = numberOf(walk(dataNode, {"Font"}))) index = int(std::lround(*font));
            if (fonts && fonts->kind == Engine::Kind::Array && index >= 0 && index < int(fonts->values.size())) if (auto name = stringOf(walk(&fonts->values[size_t(index)], {"Name"})); name && !name->empty()) style.fontFamily = *name;
            if (auto values = walk(dataNode, {"FillColor", "Values"}); values && values->kind == Engine::Kind::Array) {
                std::vector<double> channels; for (const auto& item : values->values) if (auto channel = numberOf(&item)) channels.push_back(*channel);
                if (channels.size() >= 4) { style.red = unitColor(channels[1]); style.green = unitColor(channels[2]); style.blue = unitColor(channels[3]); }
                else if (channels.size() == 3) { style.red = unitColor(channels[0]); style.green = unitColor(channels[1]); style.blue = unitColor(channels[2]); }
                else if (!channels.empty()) style.red = style.green = style.blue = unitColor(channels[0]);
            }
            if (auto tracking = numberOf(walk(dataNode, {"Tracking"})); tracking && std::isfinite(*tracking)) style.tracking = std::clamp(*tracking * style.fontSize / 1000., -100., 1000.);
            const bool automatic = boolOf(walk(dataNode, {"AutoLeading"})).value_or(true);
            if (!automatic) if (auto leading = numberOf(walk(dataNode, {"Leading"})); leading && std::isfinite(*leading) && *leading > 0) style.leading = std::clamp(*leading * scaleX, 0., 5000.);
            const Engine* paragraphs = walk(&*engine, {"EngineDict", "ParagraphRun", "RunArray"});
            const Engine* paragraph = paragraphs && paragraphs->kind == Engine::Kind::Array && !paragraphs->values.empty() ? &paragraphs->values.front() : &*engine;
            int justification = 0; if (auto value = numberOf(walk(paragraph, {"ParagraphSheet", "Properties", "Justification"}))) justification = int(std::lround(*value));
            if (justification == 1) style.alignment = TextAlignment::Right;
            else if (justification == 2) style.alignment = TextAlignment::Center;
            else if (justification != 0) { style.alignment = TextAlignment::Left; notes += "Full justification was imported as left alignment.\n"; }
        }
        double originX = tx, originY = ty;
        if (auto bounds = field(*text, "bounds"); bounds && bounds->kind == Descriptor::Kind::Map) if (auto glyphs = field(*text, "boundingBox"); glyphs && glyphs->kind == Descriptor::Kind::Map) {
            auto left = side(*bounds, "Left"), top = side(*bounds, "Top "), right = side(*bounds, "Rght"), bottom = side(*bounds, "Btom");
            auto glyphLeft = side(*glyphs, "Left"), glyphTop = side(*glyphs, "Top "), glyphRight = side(*glyphs, "Rght"), glyphBottom = side(*glyphs, "Btom");
            if (left && top && right && bottom && glyphLeft && glyphTop && glyphRight && glyphBottom) {
                const double width = *right - *left, height = *bottom - *top, glyphWidth = *glyphRight - *glyphLeft, glyphHeight = *glyphBottom - *glyphTop;
                if (width > glyphWidth + 4 && height > glyphHeight + 4 && width > 1 && height > 1) {
                    style.boxWidth = std::clamp(width * scaleX + 24, 16., 30000.);
                    style.boxHeight = std::clamp(height * scaleX + 24, 16., 30000.);
                    const double ySign = localY < 0 ? -1. : 1.;
                    originX = cosR * scaleX * *left + (-sinR * scaleX * ySign) * *top + tx;
                    originY = sinR * scaleX * *left + (cosR * scaleX * ySign) * *top + ty;
                }
            }
        }
        if (auto resolved = installedFamily(style.fontFamily)) style.fontFamily = std::move(*resolved);
        else { notes += "The font \"" + style.fontFamily + "\" isn't installed, so the text was drawn with Segoe UI.\n"; style.fontFamily = "Segoe UI"; }
        if (!text::textRunsValid(style)) return result;
        result.editable = true; result.text = std::move(style); result.x = originX; result.y = originY; result.note = notes; return result;
    } catch (const std::exception&) { return result; }
}
}
