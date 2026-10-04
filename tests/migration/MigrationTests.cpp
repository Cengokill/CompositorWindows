#include "persistence/ProjectStore.h"
#include "editing/DocumentGeometry.h"
#include "editing/SelectionExtras.h"
#include "editing/Shapes.h"
#include "effects/Adjustments.h"
#include "filters/PixelFilters.h"
#include "imaging/psd/PsdDocument.h"
#include "imaging/psd/PsdText.h"
#include "imaging/raw_develop.h"
#include "layers/LayerOperations.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <windows.h>
#include <objbase.h>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace compositor;
static void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
static int versionOf(const std::filesystem::path& path) {
    QFile file(QString::fromStdWString((path / L"manifest.json").wstring()));
    check(file.open(QIODevice::ReadOnly), "manifest missing");
    return QJsonDocument::fromJson(file.readAll()).object().value("version").toInt();
}
static Document base() { Document d; d.id = newId(); d.width = 8; d.height = 8; d.resolution = 72; return d; }
static Layer pixelLayer() { Layer l; l.id = newId(); l.name = "Paint"; l.transform = {0, 0, 4, 4}; l.raster = Raster::filled(4, 4, {255, 0, 0, 255}); return l; }
static void format_contract() {
    QTemporaryDir dir; check(dir.isValid(), "temp directory");
    ProjectStore store(makeWicProjectCodec());
    auto plain = base(); plain.layers = {pixelLayer()};
    auto plainPath = std::filesystem::path((dir.filePath("plain.comp")).toStdWString());
    store.save(plainPath, plain, plain.layers[0].id);
    check(versionOf(plainPath) == 7 && store.load(plainPath).readVersion == 7, "plain document must stay format v7");
    auto folder = base(); Layer group; group.id = newId(); group.name = "Folder"; group.group = true; group.opacity = .5; group.transform = {0, 0, 8, 8};
    auto child = pixelLayer(); child.parentId = group.id; folder.layers = {group, child};
    auto folderPath = std::filesystem::path(dir.filePath("folder.comp").toStdWString());
    store.save(folderPath, folder, child.id);
    check(versionOf(folderPath) == 8 && std::abs(store.load(folderPath).document.layers[0].opacity - .5) < 1e-9, "folder opacity is format v8");
    QFile manifest(QString::fromStdWString((folderPath / L"manifest.json").wstring())); check(manifest.open(QIODevice::ReadWrite), "manifest");
    auto json = QJsonDocument::fromJson(manifest.readAll()).object(); json["version"] = 7; manifest.resize(0); manifest.write(QJsonDocument(json).toJson()); manifest.close();
    bool rejected = false; try { store.load(folderPath); } catch (const std::exception&) { rejected = true; } check(rejected, "v7 must reject folder opacity");
    auto guided = plain; guided.guides.push_back({newId(), true, 3});
    auto guidePath = std::filesystem::path(dir.filePath("guides.comp").toStdWString());
    store.save(guidePath, guided, guided.layers[0].id);
    auto loadedGuides = store.load(guidePath); check(versionOf(guidePath) == 8 && loadedGuides.document.guides.size() == 1 && loadedGuides.document.guides[0].position == 3, "guides roundtrip");
    auto blur = base(); Layer adjustment; adjustment.id = newId(); adjustment.name = "Blur"; adjustment.transform = {0, 0, 8, 8}; adjustment.adjustmentJson = effects::defaultAdjustmentJson("Gaussian Blur");
    blur.layers = {adjustment}; auto blurPath = std::filesystem::path(dir.filePath("blur.comp").toStdWString());
    store.save(blurPath, blur, adjustment.id); check(versionOf(blurPath) == 9, "blur adjustment is format v9");
    auto typed = base(); auto textLayer = pixelLayer(); TextContent text; text.value = "Text"; text.fontFamily = "Segoe UI"; TextRun color; color.location = 0; color.length = 4; color.hasColor = true; color.red = 1; text.colorRuns = {color}; textLayer.text = text;
    typed.layers = {textLayer}; auto textPath = std::filesystem::path(dir.filePath("text.comp").toStdWString());
    store.save(textPath, typed, textLayer.id); check(versionOf(textPath) == 10 && store.load(textPath).document.layers[0].text->colorRuns[0].red == 1, "color runs are format v10");
    textLayer.text->fontRuns.push_back(TextRun{0, 4, 0, 0, 1, false, "Segoe UI", 24, true}); typed.layers = {textLayer};
    auto fontPath = std::filesystem::path(dir.filePath("font.comp").toStdWString()); store.save(fontPath, typed, textLayer.id);
    check(versionOf(fontPath) == 11 && store.load(fontPath).document.layers[0].text->fontRuns[0].fontSize == 24, "font runs are format v11");
    auto shaded = base(); auto fx = pixelLayer(); fx.effects.specified = true; fx.effects.dropShadow.enabled = true; fx.effects.dropShadow.distance = 6; shaded.layers = {fx};
    auto fxPath = std::filesystem::path(dir.filePath("fx.comp").toStdWString()); store.save(fxPath, shaded, fx.id);
    auto loadedFx = store.load(fxPath); check(versionOf(fxPath) == 7 && loadedFx.document.layers[0].effects.dropShadow.enabled && loadedFx.document.layers[0].effects.dropShadow.distance == 6, "effects stay additive at v7");
    auto paragraph = base(); auto boxed = pixelLayer(); TextContent box; box.value = "Paragraph"; box.fontFamily = "Segoe UI"; box.fontSize = 18; box.alignment = TextAlignment::Center; box.tracking = 15; box.leading = 40; box.boxWidth = 80; box.boxHeight = 48; box.fontStyle = "Bold"; boxed.text = box; paragraph.layers = {boxed};
    auto paragraphPath = std::filesystem::path(dir.filePath("paragraph.comp").toStdWString()); store.save(paragraphPath, paragraph, boxed.id);
    auto loadedParagraph = store.load(paragraphPath); const auto& round = *loadedParagraph.document.layers[0].text;
    check(versionOf(paragraphPath) == 12 && round.alignment == TextAlignment::Center && round.tracking == 15 && round.leading == 40 && round.boxWidth == 80 && round.boxHeight == 48 && round.fontStyle == "Bold", "paragraph text is format v12");
    QFile paragraphManifest(QString::fromStdWString((paragraphPath / L"manifest.json").wstring())); check(paragraphManifest.open(QIODevice::ReadWrite), "paragraph manifest");
    auto paragraphJson = QJsonDocument::fromJson(paragraphManifest.readAll()).object(); paragraphJson["version"] = 11; paragraphManifest.resize(0); paragraphManifest.write(QJsonDocument(paragraphJson).toJson()); paragraphManifest.close();
    bool paragraphRejected = false; try { store.load(paragraphPath); } catch (const std::exception&) { paragraphRejected = true; } check(paragraphRejected, "v11 must reject paragraph text");
}
static void behavior_contract() {
    editing::ShapeStyle line{editing::ShapeKind::Line, 0, 0, 0, 0, 3, 0, 0, 1, 1}; auto raster = editing::shapeRaster(line, 8, 8); check(raster->pixel(0, 0).a > 0 && raster->pixel(7, 7).a > 0, "line shape misses its endpoints");
    GrayRaster coverage; coverage.width = 5; coverage.height = 5; coverage.pixels.assign(25, 0); coverage.pixels[12] = 255; Selection selection{std::make_shared<GrayRaster>(coverage)};
    auto soft = editing::featherSelection(selection, 1); check(soft.coverage->pixel(12 % 5, 12 / 5) > 0 && soft.coverage->pixel(0, 0) < 255, "feather did not soften");
    Pixel red{255, 0, 0, 255}; auto source = Raster::filled(3, 1, {0, 0, 0, 255})->replacing(1, 0, 1, 1, &red, 1);
    auto range = editing::colorRangeMask(*source, 1, 0, 0); check(range->pixel(1, 0) == 255 && range->pixel(0, 0) == 0, "color range leaked");
    GrayRaster matte; matte.width = 4; matte.height = 1; matte.pixels = {255, 255, 0, 255}; auto component = editing::connectedComponent(matte, 0, 0, 128); check(component->pixel(1, 0) == 255 && component->pixel(3, 0) == 0, "component crossed a gap");
    auto document = base(); document.layers = {pixelLayer()}; document.guides.push_back({newId(), false, 2}); auto trimmed = editing::trimDocument(document); check(trimmed.width == 4 && trimmed.height == 4 && trimmed.guides[0].position == 2, "trim bounds or guide shift");
    filters::Settings vignette; vignette.vignette = 80; auto painted = filters::runPixels(filters::Kind::Vignette, *Raster::filled(4, 4), vignette, 1, 1, nullptr); check(painted->pixel(0, 0).a > 0 && painted->pixel(2, 2).a < painted->pixel(0, 0).a, "vignette missed an empty layer");
    auto grouped = base(); Layer folder; folder.id = newId(); folder.name = "Folder"; folder.group = true; folder.transform = {0, 0, 8, 8}; auto child = pixelLayer(); child.parentId = folder.id; grouped.layers = {folder, child};
    auto ungrouped = layers::ungroup(grouped, {{folder.id}, folder.id}); check(ungrouped.document.layers.size() == 1 && ungrouped.document.layers[0].parentId.empty(), "ungroup left the folder");
    QTemporaryDir rawDir; auto rawPath = std::filesystem::path(rawDir.filePath("missing.cr2").toStdWString()); QFile raw(rawDir.filePath("missing.cr2")); check(raw.open(QIODevice::WriteOnly), "raw fixture"); raw.write("not raw"); raw.close();
    bool rawRejected = false; try { imaging::decodeRaw(rawPath, {}); } catch (const std::exception& error) { rawRejected = std::string(error.what()).find("No RAW codec") != std::string::npos; } check(rawRejected, "missing RAW codec was not reported");
}
static void psd_contract() {
    const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    struct ReleaseCom { HRESULT hr; ~ReleaseCom() { if (SUCCEEDED(hr)) CoUninitialize(); } } release{apartment};
    std::vector<uint8_t> bytes; auto u16 = [&](int v) { bytes.push_back(uint8_t(v >> 8)); bytes.push_back(uint8_t(v)); }; auto u32 = [&](uint32_t v) { for (int i = 24; i >= 0; i -= 8) bytes.push_back(uint8_t(v >> i)); };
    auto ascii = [&](const char* s) { while (*s) bytes.push_back(uint8_t(*s++)); };
    ascii("8BPS"); u16(1); for (int i = 0; i < 6; ++i) bytes.push_back(0); u16(3); u32(2); u32(2); u16(8); u16(3); u32(0); u32(0);
    std::vector<uint8_t> info; auto i16 = [&](int v) { info.push_back(uint8_t(v >> 8)); info.push_back(uint8_t(v)); }; auto i32 = [&](uint32_t v) { for (int s = 24; s >= 0; s -= 8) info.push_back(uint8_t(v >> s)); }; auto infoAscii = [&](const char* s) { while (*s) info.push_back(uint8_t(*s++)); };
    i16(1); i32(0); i32(0); i32(2); i32(2); i16(4); for (int id : {0, 1, 2, -1}) { i16(id); i32(6); } infoAscii("8BIM"); infoAscii("norm"); info.push_back(255); info.push_back(0); info.push_back(0); info.push_back(0); i32(28); i32(0); i32(0); info.push_back(3); infoAscii("Red");
    infoAscii("8BIM"); infoAscii("xxxx"); i32(4); i32(0x01020304);
    for (int channel = 0; channel < 4; ++channel) { info.push_back(0); info.push_back(0); uint8_t value = channel == 1 || channel == 2 ? 0 : 255; for (int i = 0; i < 4; ++i) info.push_back(value); }
    u32(uint32_t(info.size() + 4)); u32(uint32_t(info.size())); bytes.insert(bytes.end(), info.begin(), info.end());
    auto imported = imaging::readPsd(bytes.data(), bytes.size()); check(imported.document.layers.size() == 1 && imported.document.layers[0].name == "Red" && imported.document.layers[0].raster->pixel(0, 0) == Pixel{255, 0, 0, 255}, "PSD layer was not imported");
    auto textFile = [&](const std::string& engine, bool vertical, const char* name) {
        std::vector<uint8_t> type; auto t16 = [&](int v) { type.push_back(uint8_t(v >> 8)); type.push_back(uint8_t(v)); }; auto t32 = [&](uint32_t v) { for (int s = 24; s >= 0; s -= 8) type.push_back(uint8_t(v >> s)); };
        auto f64 = [&](double v) { uint64_t bits = 0; std::memcpy(&bits, &v, 8); for (int s = 56; s >= 0; s -= 8) type.push_back(uint8_t(bits >> s)); }; auto raw = [&](const char* s) { while (*s) type.push_back(uint8_t(*s++)); };
        auto utf16 = [&](const char* s) { t32(uint32_t(std::strlen(s))); while (*s) { type.push_back(0); type.push_back(uint8_t(*s++)); } }; auto key = [&](const char* s) { t32(0); raw(s); };
        t16(1); f64(1); f64(0); f64(0); f64(1); f64(0); f64(0); t16(50); t32(16); t32(0); key("TxLr"); t32(vertical ? 2 : 2); key("Txt "); raw("TEXT"); utf16(vertical ? "Up" : "Hi");
        if (vertical) { key("Ornt"); raw("enum"); key("Ornt"); key("Vrtc"); }
        else { t32(10); raw("EngineData"); raw("tdta"); t32(uint32_t(engine.size())); type.insert(type.end(), engine.begin(), engine.end()); }
        auto parsed = imaging::readPhotoshopText(type.data(), type.size());
        if (!vertical && !parsed.editable) throw std::runtime_error(std::string("parser: ") + parsed.note + " bytes=" + std::to_string(type.size()));
        std::vector<uint8_t> file; auto p16 = [&](int v) { file.push_back(uint8_t(v >> 8)); file.push_back(uint8_t(v)); }; auto p32 = [&](uint32_t v) { for (int s = 24; s >= 0; s -= 8) file.push_back(uint8_t(v >> s)); }; auto pascii = [&](const char* s) { while (*s) file.push_back(uint8_t(*s++)); };
        pascii("8BPS"); p16(1); for (int i = 0; i < 6; ++i) file.push_back(0); p16(3); p32(64); p32(64); p16(8); p16(3); p32(0); p32(0);
        std::vector<uint8_t> info; auto i16 = [&](int v) { info.push_back(uint8_t(v >> 8)); info.push_back(uint8_t(v)); }; auto i32 = [&](uint32_t v) { for (int s = 24; s >= 0; s -= 8) info.push_back(uint8_t(v >> s)); }; auto iascii = [&](const char* s) { while (*s) info.push_back(uint8_t(*s++)); };
        i16(1); i32(0); i32(0); i32(2); i32(2); i16(4); for (int id : {0, 1, 2, -1}) { i16(id); i32(6); } iascii("8BIM"); iascii("norm"); info.push_back(255); info.push_back(0); info.push_back(0); info.push_back(0);
        std::vector<uint8_t> extra; auto e32 = [&](uint32_t v) { for (int s = 24; s >= 0; s -= 8) extra.push_back(uint8_t(v >> s)); }; auto eascii = [&](const char* s) { while (*s) extra.push_back(uint8_t(*s++)); };
        e32(0); e32(0); extra.push_back(uint8_t(std::strlen(name))); eascii(name); int padded = (1 + int(std::strlen(name)) + 3) & ~3; for (int i = 0; i < padded - 1 - int(std::strlen(name)); ++i) extra.push_back(0);
        eascii("8BIM"); eascii("TySh"); e32(uint32_t(type.size())); extra.insert(extra.end(), type.begin(), type.end()); if (type.size() & 1) extra.push_back(0);
        i32(uint32_t(extra.size())); info.insert(info.end(), extra.begin(), extra.end());
        for (int channel = 0; channel < 4; ++channel) { info.push_back(0); info.push_back(0); uint8_t value = channel == 1 || channel == 2 ? 0 : 255; for (int i = 0; i < 4; ++i) info.push_back(value); }
        p32(uint32_t(info.size() + 4)); p32(uint32_t(info.size())); file.insert(file.end(), info.begin(), info.end());
        return imaging::readPsd(file.data(), file.size());
    };
    const char* engine = "<< /EngineDict << /Editor << /Text (Hi) >> /StyleRun << /RunArray [ << /StyleSheet << /StyleSheetData << /FontSize 24 /Font 0 /FillColor << /Values [ 1 0.1 0.2 0.8 ] >> >> >> >> ] >> /ParagraphRun << /RunArray [ << /ParagraphSheet << /Properties << /Justification 2 >> >> >> ] >> >> /ResourceDict << /FontSet [ << /Name (Segoe UI) >> ] >> >>";
    auto horizontal = textFile(engine, false, "Title"); const auto& title = horizontal.document.layers.at(0);
    if (!title.text) throw std::runtime_error(std::string("horizontal PSD has no text: ") + horizontal.report);
    if (!(title.text->value == "Hi" && title.text->fontFamily == "Segoe UI" && std::abs(title.text->fontSize - 24) < 1e-6 && std::abs(title.text->red - 0.1) < 1e-6 && std::abs(title.text->green - 0.2) < 1e-6 && std::abs(title.text->blue - 0.8) < 1e-6 && title.text->alignment == TextAlignment::Center))
        throw std::runtime_error("horizontal style value=" + title.text->value + " family=" + title.text->fontFamily + " size=" + std::to_string(title.text->fontSize) + " rgb=" + std::to_string(title.text->red) + "," + std::to_string(title.text->green) + "," + std::to_string(title.text->blue) + " align=" + std::to_string(int(title.text->alignment)) + " report=" + horizontal.report);
    const char* justified = "<< /EngineDict << /Editor << /Text (Hi) >> /StyleRun << /RunArray [ << /StyleSheet << /StyleSheetData << /FontSize 24 /Font 0 >> >> >> ] >> /ParagraphRun << /RunArray [ << /ParagraphSheet << /Properties << /Justification 3 >> >> >> ] >> >> /ResourceDict << /FontSet [ << /Name (Segoe UI) >> ] >> >>";
    auto full = textFile(justified, false, "Justified");
    check(full.document.layers.at(0).text && full.document.layers.at(0).text->alignment == TextAlignment::Left && full.report.find("Full justification") != std::string::npos, "full justification was not reported as left");
    const char* missing = "<< /EngineDict << /Editor << /Text (Hi) >> /StyleRun << /RunArray [ << /StyleSheet << /StyleSheetData << /FontSize 18 /Font 0 >> >> >> ] >> >> /ResourceDict << /FontSet [ << /Name (NoSuchFontXYZ) >> ] >> >>";
    auto substituted = textFile(missing, false, "Missing");
    check(substituted.document.layers.at(0).text && substituted.document.layers.at(0).text->fontFamily == "Segoe UI" && substituted.report.find("NoSuchFontXYZ") != std::string::npos, "missing PSD font was not substituted");
    const char* segoePostScript = "<< /EngineDict << /Editor << /Text (Hi) >> /StyleRun << /RunArray [ << /StyleSheet << /StyleSheetData << /FontSize 18 /Font 0 >> >> >> ] >> >> /ResourceDict << /FontSet [ << /Name (SegoeUI) >> ] >> >>";
    auto segoe = textFile(segoePostScript, false, "Segoe");
    check(segoe.document.layers.at(0).text && segoe.document.layers.at(0).text->fontFamily == "Segoe UI" && segoe.report.find("isn't installed") == std::string::npos, "SegoeUI PostScript name was not resolved");
    wchar_t windowsDirectory[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"WINDIR", windowsDirectory, MAX_PATH) > 0 && std::filesystem::is_regular_file(std::filesystem::path(windowsDirectory) / L"Fonts" / L"monbaiti.ttf")) {
        const char* mongolian = "<< /EngineDict << /Editor << /Text (Hi) >> /StyleRun << /RunArray [ << /StyleSheet << /StyleSheetData << /FontSize 18 /Font 0 >> >> >> ] >> >> /ResourceDict << /FontSet [ << /Name (MongolianBaiti) >> ] >> >>";
        auto baiti = textFile(mongolian, false, "Baiti");
        check(baiti.document.layers.at(0).text && baiti.document.layers.at(0).text->fontFamily == "Mongolian Baiti" && baiti.report.find("isn't installed") == std::string::npos, "MongolianBaiti was treated as a missing font");
    }
    auto upright = textFile("", true, "Upright");
    check(!upright.document.layers.at(0).text && upright.report.find("Vertical") != std::string::npos, "vertical PSD text stayed editable");
    std::vector<uint8_t> cmyk; auto c16 = [&](int v) { cmyk.push_back(uint8_t(v >> 8)); cmyk.push_back(uint8_t(v)); }; auto c32 = [&](uint32_t v) { for (int s = 24; s >= 0; s -= 8) cmyk.push_back(uint8_t(v >> s)); }; auto cascii = [&](const char* s) { while (*s) cmyk.push_back(uint8_t(*s++)); };
    cascii("8BPS"); c16(1); for (int i = 0; i < 6; ++i) cmyk.push_back(0); c16(4); c32(2); c32(2); c16(8); c16(4);
    bool namedCmyk = false; try { imaging::readPsd(cmyk.data(), cmyk.size()); } catch (const std::exception& error) { namedCmyk = std::string(error.what()).find("CMYK") != std::string::npos; }
    check(namedCmyk, "CMYK PSD was not named in the error");
    std::vector<uint8_t> zip; auto z16 = [&](int v) { zip.push_back(uint8_t(v >> 8)); zip.push_back(uint8_t(v)); }; auto z32 = [&](uint32_t v) { for (int s = 24; s >= 0; s -= 8) zip.push_back(uint8_t(v >> s)); }; auto zascii = [&](const char* s) { while (*s) zip.push_back(uint8_t(*s++)); };
    zascii("8BPS"); z16(1); for (int i = 0; i < 6; ++i) zip.push_back(0); z16(3); z32(2); z32(2); z16(8); z16(3); z32(0); z32(0);
    std::vector<uint8_t> zipInfo; auto zi16 = [&](int v) { zipInfo.push_back(uint8_t(v >> 8)); zipInfo.push_back(uint8_t(v)); }; auto zi32 = [&](uint32_t v) { for (int s = 24; s >= 0; s -= 8) zipInfo.push_back(uint8_t(v >> s)); }; auto ziascii = [&](const char* s) { while (*s) zipInfo.push_back(uint8_t(*s++)); };
    zi16(1); zi32(0); zi32(0); zi32(2); zi32(2); zi16(4); for (int id : {0, 1, 2, -1}) { zi16(id); zi32(6); } ziascii("8BIM"); ziascii("norm"); zipInfo.push_back(255); zipInfo.push_back(0); zipInfo.push_back(0); zipInfo.push_back(0); zi32(12); zi32(0); zi32(0); zipInfo.push_back(3); ziascii("Red");
    for (int channel = 0; channel < 4; ++channel) { zipInfo.push_back(0); zipInfo.push_back(2); for (int i = 0; i < 4; ++i) zipInfo.push_back(0); }
    z32(uint32_t(zipInfo.size() + 4)); z32(uint32_t(zipInfo.size())); zip.insert(zip.end(), zipInfo.begin(), zipInfo.end());
    z16(0); for (int i = 0; i < 4; ++i) zip.push_back(0); for (int i = 0; i < 4; ++i) zip.push_back(255); for (int i = 0; i < 4; ++i) zip.push_back(0);
    auto zipImported = imaging::readPsd(zip.data(), zip.size());
    check(zipImported.document.layers.size() == 1 && zipImported.document.layers[0].raster && zipImported.document.layers[0].raster->pixel(0, 0) == Pixel{0, 255, 0, 255} && zipImported.report.find("merged") != std::string::npos, "ZIP-compressed layers did not fall back to the merged image");
    std::vector<uint8_t> deep; auto d16 = [&](int v) { deep.push_back(uint8_t(v >> 8)); deep.push_back(uint8_t(v)); }; auto d32 = [&](uint32_t v) { for (int s = 24; s >= 0; s -= 8) deep.push_back(uint8_t(v >> s)); }; auto dascii = [&](const char* s) { while (*s) deep.push_back(uint8_t(*s++)); };
    dascii("8BPS"); d16(1); for (int i = 0; i < 6; ++i) deep.push_back(0); d16(3); d32(2); d32(2); d16(16); d16(3); d32(0); d32(0); d32(0);
    d16(0);
    for (int i = 0; i < 4; ++i) { deep.push_back(255); deep.push_back(0); }
    for (int i = 0; i < 16; ++i) deep.push_back(0);
    auto deepImported = imaging::readPsd(deep.data(), deep.size());
    check(deepImported.document.layers.size() == 1 && deepImported.document.layers[0].raster && deepImported.document.layers[0].raster->pixel(0, 0) == Pixel{255, 0, 0, 255} && deepImported.report.find("16-bit") != std::string::npos, "16-bit PSD was not converted to 8-bit");
    std::vector<uint8_t> rle; auto r16 = [&](int v) { rle.push_back(uint8_t(v >> 8)); rle.push_back(uint8_t(v)); }; auto r32 = [&](uint32_t v) { for (int s = 24; s >= 0; s -= 8) rle.push_back(uint8_t(v >> s)); }; auto rascii = [&](const char* s) { while (*s) rle.push_back(uint8_t(*s++)); };
    rascii("8BPS"); r16(1); for (int i = 0; i < 6; ++i) rle.push_back(0); r16(3); r32(1); r32(2); r16(8); r16(3); r32(0); r32(0); r32(0);
    r16(1); r16(3); r16(3); r16(3);
    auto literal = [&](uint8_t value) { rle.push_back(1); rle.push_back(value); rle.push_back(value); };
    literal(255); literal(0); literal(128);
    auto rleImported = imaging::readPsd(rle.data(), rle.size());
    check(rleImported.document.layers.size() == 1 && rleImported.document.layers[0].raster && rleImported.document.layers[0].raster->pixel(0, 0) == Pixel{255, 0, 128, 255}, "merged RLE image was not decoded");
    std::vector<uint8_t> named; auto n16 = [&](int v) { named.push_back(uint8_t(v >> 8)); named.push_back(uint8_t(v)); }; auto n32 = [&](uint32_t v) { for (int s = 24; s >= 0; s -= 8) named.push_back(uint8_t(v >> s)); }; auto nascii = [&](const char* s) { while (*s) named.push_back(uint8_t(*s++)); };
    nascii("8BPS"); n16(1); for (int i = 0; i < 6; ++i) named.push_back(0); n16(3); n32(2); n32(2); n16(8); n16(3); n32(0); n32(0);
    std::vector<uint8_t> nameInfo; auto ni16 = [&](int v) { nameInfo.push_back(uint8_t(v >> 8)); nameInfo.push_back(uint8_t(v)); }; auto ni32 = [&](uint32_t v) { for (int s = 24; s >= 0; s -= 8) nameInfo.push_back(uint8_t(v >> s)); }; auto niascii = [&](const char* s) { while (*s) nameInfo.push_back(uint8_t(*s++)); };
    ni16(1); ni32(0); ni32(0); ni32(2); ni32(2); ni16(4); for (int id : {0, 1, 2, -1}) { ni16(id); ni32(6); } niascii("8BIM"); niascii("norm"); nameInfo.push_back(255); nameInfo.push_back(0); nameInfo.push_back(0); nameInfo.push_back(0);
    std::vector<uint8_t> nameExtra; auto ne32 = [&](uint32_t v) { for (int s = 24; s >= 0; s -= 8) nameExtra.push_back(uint8_t(v >> s)); };
    ne32(0); ne32(0); nameExtra.push_back(1); nameExtra.push_back(0x8E); nameExtra.push_back(0); nameExtra.push_back(0);
    nameExtra.insert(nameExtra.end(), {'8','B','I','M','l','u','n','i'}); ne32(12); ne32(4); nameExtra.insert(nameExtra.end(), {0,0x43,0,0x61,0,0x66,0,0xE9});
    ni32(uint32_t(nameExtra.size())); nameInfo.insert(nameInfo.end(), nameExtra.begin(), nameExtra.end());
    for (int channel = 0; channel < 4; ++channel) { nameInfo.push_back(0); nameInfo.push_back(0); uint8_t value = channel == 1 || channel == 2 ? 0 : 255; for (int i = 0; i < 4; ++i) nameInfo.push_back(value); }
    n32(uint32_t(nameInfo.size() + 4)); n32(uint32_t(nameInfo.size())); named.insert(named.end(), nameInfo.begin(), nameInfo.end());
    auto namedImported = imaging::readPsd(named.data(), named.size());
    check(namedImported.document.layers.size() == 1 && namedImported.document.layers[0].name == "Caf\xc3\xa9", "PSD Unicode layer name was not decoded");
    std::vector<uint8_t> terminated; auto t16 = [&](int v) { terminated.push_back(uint8_t(v >> 8)); terminated.push_back(uint8_t(v)); }; auto t32 = [&](uint32_t v) { for (int s = 24; s >= 0; s -= 8) terminated.push_back(uint8_t(v >> s)); }; auto tascii = [&](const char* s) { while (*s) terminated.push_back(uint8_t(*s++)); };
    tascii("8BPS"); t16(1); for (int i = 0; i < 6; ++i) terminated.push_back(0); t16(3); t32(2); t32(2); t16(8); t16(3); t32(0); t32(0);
    std::vector<uint8_t> termInfo; auto ti16 = [&](int v) { termInfo.push_back(uint8_t(v >> 8)); termInfo.push_back(uint8_t(v)); }; auto ti32 = [&](uint32_t v) { for (int s = 24; s >= 0; s -= 8) termInfo.push_back(uint8_t(v >> s)); }; auto tiascii = [&](const char* s) { while (*s) termInfo.push_back(uint8_t(*s++)); };
    ti16(1); ti32(0); ti32(0); ti32(2); ti32(2); ti16(4); for (int id : {0, 1, 2, -1}) { ti16(id); ti32(6); } tiascii("8BIM"); tiascii("norm"); termInfo.push_back(255); termInfo.push_back(0); termInfo.push_back(0); termInfo.push_back(0);
    std::vector<uint8_t> termExtra; auto te32 = [&](uint32_t v) { for (int s = 24; s >= 0; s -= 8) termExtra.push_back(uint8_t(v >> s)); };
    te32(0); te32(0); termExtra.push_back(1); termExtra.push_back(uint8_t('X')); termExtra.push_back(0); termExtra.push_back(0);
    termExtra.insert(termExtra.end(), {'8','B','I','M','l','u','n','i'}); te32(14); te32(5); termExtra.insert(termExtra.end(), {0,0x43,0,0x61,0,0x66,0,0xE9,0,0x00});
    ti32(uint32_t(termExtra.size())); termInfo.insert(termInfo.end(), termExtra.begin(), termExtra.end());
    for (int channel = 0; channel < 4; ++channel) { termInfo.push_back(0); termInfo.push_back(0); uint8_t value = channel == 1 || channel == 2 ? 0 : 255; for (int i = 0; i < 4; ++i) termInfo.push_back(value); }
    t32(uint32_t(termInfo.size() + 4)); t32(uint32_t(termInfo.size())); terminated.insert(terminated.end(), termInfo.begin(), termInfo.end());
    auto terminatedImported = imaging::readPsd(terminated.data(), terminated.size());
    const auto& terminatedName = terminatedImported.document.layers.at(0).name;
    check(terminatedImported.document.layers.size() == 1 && terminatedName == "Caf\xc3\xa9" && terminatedName.find('\0') == std::string::npos, "PSD luni name kept the terminating NUL");
    std::vector<uint8_t> blank; auto b16 = [&](int v) { blank.push_back(uint8_t(v >> 8)); blank.push_back(uint8_t(v)); }; auto b32 = [&](uint32_t v) { for (int s = 24; s >= 0; s -= 8) blank.push_back(uint8_t(v >> s)); }; auto bascii = [&](const char* s) { while (*s) blank.push_back(uint8_t(*s++)); };
    bascii("8BPS"); b16(1); for (int i = 0; i < 6; ++i) blank.push_back(0); b16(3); b32(2); b32(2); b16(8); b16(3); b32(0); b32(0);
    std::vector<uint8_t> blankInfo; auto bi16 = [&](int v) { blankInfo.push_back(uint8_t(v >> 8)); blankInfo.push_back(uint8_t(v)); }; auto bi32 = [&](uint32_t v) { for (int s = 24; s >= 0; s -= 8) blankInfo.push_back(uint8_t(v >> s)); }; auto biascii = [&](const char* s) { while (*s) blankInfo.push_back(uint8_t(*s++)); };
    bi16(1); bi32(0); bi32(0); bi32(2); bi32(2); bi16(4); for (int id : {0, 1, 2, -1}) { bi16(id); bi32(6); } biascii("8BIM"); biascii("norm"); blankInfo.push_back(255); blankInfo.push_back(0); blankInfo.push_back(0); blankInfo.push_back(0);
    std::vector<uint8_t> blankExtra; auto be32 = [&](uint32_t v) { for (int s = 24; s >= 0; s -= 8) blankExtra.push_back(uint8_t(v >> s)); };
    be32(0); be32(0); blankExtra.push_back(4); blankExtra.insert(blankExtra.end(), {'K','e','e','p'}); blankExtra.push_back(0); blankExtra.push_back(0); blankExtra.push_back(0);
    blankExtra.insert(blankExtra.end(), {'8','B','I','M','l','u','n','i'}); be32(6); be32(1); blankExtra.push_back(0); blankExtra.push_back(0);
    bi32(uint32_t(blankExtra.size())); blankInfo.insert(blankInfo.end(), blankExtra.begin(), blankExtra.end());
    for (int channel = 0; channel < 4; ++channel) { blankInfo.push_back(0); blankInfo.push_back(0); uint8_t value = channel == 1 || channel == 2 ? 0 : 255; for (int i = 0; i < 4; ++i) blankInfo.push_back(value); }
    b32(uint32_t(blankInfo.size() + 4)); b32(uint32_t(blankInfo.size())); blank.insert(blank.end(), blankInfo.begin(), blankInfo.end());
    auto blankImported = imaging::readPsd(blank.data(), blank.size());
    check(blankImported.document.layers.size() == 1 && blankImported.document.layers[0].name == "Keep", "PSD luni name of only NUL replaced the Pascal name");
    std::vector<uint8_t> wideChannels; auto w16 = [&](int v) { wideChannels.push_back(uint8_t(v >> 8)); wideChannels.push_back(uint8_t(v)); }; auto w32 = [&](uint32_t v) { for (int s = 24; s >= 0; s -= 8) wideChannels.push_back(uint8_t(v >> s)); }; auto wascii = [&](const char* s) { while (*s) wideChannels.push_back(uint8_t(*s++)); };
    wascii("8BPS"); w16(1); for (int i = 0; i < 6; ++i) wideChannels.push_back(0); w16(57); w32(1); w32(1); w16(8); w16(3);
    bool tooManyChannels = false; try { imaging::readPsd(wideChannels.data(), wideChannels.size()); } catch (const std::exception& error) { tooManyChannels = std::string(error.what()).find("exceeds 56") != std::string::npos; }
    check(tooManyChannels, "PSD header channel count above 56 was accepted");
    std::vector<uint8_t> shortRows; auto s16 = [&](int v) { shortRows.push_back(uint8_t(v >> 8)); shortRows.push_back(uint8_t(v)); }; auto s32 = [&](uint32_t v) { for (int s = 24; s >= 0; s -= 8) shortRows.push_back(uint8_t(v >> s)); }; auto sascii = [&](const char* s) { while (*s) shortRows.push_back(uint8_t(*s++)); };
    sascii("8BPS"); s16(1); for (int i = 0; i < 6; ++i) shortRows.push_back(0); s16(56); s32(100); s32(1); s16(8); s16(3); s32(0); s32(0); s32(0); s16(1);
    bool shortRowTable = false; try { imaging::readPsd(shortRows.data(), shortRows.size()); } catch (const std::exception& error) { shortRowTable = std::string(error.what()).find("ended early") != std::string::npos && std::string(error.what()).find("bad allocation") == std::string::npos; }
    check(shortRowTable, "Merged RLE row table larger than the file was not rejected");
    std::vector<uint8_t> wrapped; auto p16 = [&](int v) { wrapped.push_back(uint8_t(v >> 8)); wrapped.push_back(uint8_t(v)); }; auto p32 = [&](uint32_t v) { for (int s = 24; s >= 0; s -= 8) wrapped.push_back(uint8_t(v >> s)); }; auto p64 = [&](uint64_t v) { for (int s = 56; s >= 0; s -= 8) wrapped.push_back(uint8_t(v >> s)); }; auto pascii = [&](const char* s) { while (*s) wrapped.push_back(uint8_t(*s++)); };
    auto patch64 = [&](size_t at, uint64_t v) { for (int s = 56; s >= 0; s -= 8) wrapped[at++] = uint8_t(v >> s); };
    pascii("8BPS"); p16(2); for (int i = 0; i < 6; ++i) wrapped.push_back(0); p16(3); p32(1); p32(1); p16(8); p16(3); p32(0); p32(0);
    const size_t sectionAt = wrapped.size(); p64(0); const size_t infoAt = wrapped.size(); p64(0); p16(1);
    p32(0); p32(0); p32(1); p32(1); p16(3);
    const size_t redLengthAt = wrapped.size() + 2; p16(0); p64(0); p16(1); p64(3); p16(2); p64(3);
    pascii("8BIM"); pascii("norm"); wrapped.push_back(255); wrapped.push_back(0); wrapped.push_back(0); wrapped.push_back(0);
    const size_t extraAt = wrapped.size(); p32(0); p32(0); p32(0); wrapped.push_back(0); wrapped.push_back(0); wrapped.push_back(0); wrapped.push_back(0);
    const size_t decoyAt = wrapped.size(); wrapped.insert(wrapped.end(), {0, 0, 0, 0, 0, 0});
    const uint32_t extraSize = uint32_t(wrapped.size() - (extraAt + 4)); for (int s = 24; s >= 0; s -= 8) wrapped[extraAt + size_t(3 - s / 8)] = uint8_t(extraSize >> s);
    const size_t channelAt = wrapped.size(); wrapped.insert(wrapped.end(), {0, 0, 255});
    patch64(redLengthAt, uint64_t(decoyAt) - uint64_t(channelAt));
    const uint64_t infoSize = uint64_t(wrapped.size() - (infoAt + 8)); patch64(infoAt, infoSize);
    const uint64_t sectionSize = uint64_t(wrapped.size() - (sectionAt + 8)); patch64(sectionAt, sectionSize);
    bool wrappedRejected = false; std::string wrappedReason;
    try { auto imported = imaging::readPsd(wrapped.data(), wrapped.size()); wrappedRejected = imported.document.layers.empty(); }
    catch (const std::exception& error) { wrappedReason = error.what(); wrappedRejected = wrappedReason.find("channel could not be decoded") != std::string::npos || wrappedReason.find("no supported image") != std::string::npos; }
    check(wrappedRejected, wrappedReason.empty() ? "Wrapping PSB channel length imported rewound pixels" : wrappedReason.c_str());
    std::vector<uint8_t> opaque; auto o16 = [&](int v) { opaque.push_back(uint8_t(v >> 8)); opaque.push_back(uint8_t(v)); }; auto o32 = [&](uint32_t v) { for (int s = 24; s >= 0; s -= 8) opaque.push_back(uint8_t(v >> s)); }; auto oascii = [&](const char* s) { while (*s) opaque.push_back(uint8_t(*s++)); };
    oascii("8BPS"); o16(1); for (int i = 0; i < 6; ++i) opaque.push_back(0); o16(4); o32(1); o32(1); o16(8); o16(3); o32(0); o32(0); o32(0); o16(0); opaque.push_back(255); opaque.push_back(0); opaque.push_back(128);
    bool alphaRejected = false; std::string alphaReason;
    try { imaging::readPsd(opaque.data(), opaque.size()); }
    catch (const std::exception& error) { alphaReason = error.what(); alphaRejected = alphaReason.find("transparency") != std::string::npos && alphaReason.find("Imported the merged image") == std::string::npos; }
    check(alphaRejected, alphaReason.empty() ? "Truncated merged alpha imported an opaque image" : alphaReason.c_str());
}
static void loadPsdFile(const wchar_t* path) {
    const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    struct ReleaseCom { HRESULT hr; ~ReleaseCom() { if (SUCCEEDED(hr)) CoUninitialize(); } } release{apartment};
    std::wcout << path << L"\n" << std::flush;
    try {
        auto imported = imaging::readPsd(std::filesystem::path(path));
        int rasters = 0, texts = 0;
        for (const auto& layer : imported.document.layers) { if (layer.raster) ++rasters; if (layer.text) ++texts; }
        std::cout << "  OK " << imported.document.width << "x" << imported.document.height
                  << " layers=" << imported.document.layers.size() << " rasters=" << rasters << " texts=" << texts << "\n";
        std::cout << imported.report << std::flush;
    } catch (const std::exception& error) { std::cout << "  ERROR " << error.what() << "\n" << std::flush; }
}
static int guardPsd(const wchar_t* path) {
    __try { loadPsdFile(path); return 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return int(GetExceptionCode()); }
}
static int psd_folder(const std::filesystem::path& dir) {
    if (!std::filesystem::is_directory(dir)) { std::cerr << "PSD folder missing\n"; return 1; }
    int crashes = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        auto ext = entry.path().extension().wstring();
        for (auto& ch : ext) ch = wchar_t(towlower(ch));
        if (ext != L".psd" && ext != L".psb") continue;
        const int code = guardPsd(entry.path().c_str());
        if (code) { ++crashes; std::cout << "  CRASH 0x" << std::hex << code << std::dec << "\n" << std::flush; }
    }
    return crashes ? 1 : 0;
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc > 1 && std::string(argv[1]) == "psd-folder") {
        const auto dir = argc > 2 ? std::filesystem::path(argv[2]) : std::filesystem::path("psd-test");
        return psd_folder(dir);
    }
    try { format_contract(); behavior_contract(); psd_contract(); std::cout << "PASS migration.contract\n"; return 0; }
    catch (const std::exception& error) { std::cerr << "FAIL migration.contract: " << error.what() << '\n'; return 1; }
}
