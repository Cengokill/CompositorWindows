#include "persistence/ProjectStore.h"
#include "core/DeepImage.h"
#include "core/DocumentExport.h"
#include "editing/DocumentGeometry.h"
#include "editing/PixelEdits.h"
#include "editing/SelectionExtras.h"
#include "editing/Shapes.h"
#include "effects/Adjustments.h"
#include "filters/PixelFilters.h"
#include "imaging/psd/PsdDocument.h"
#include "imaging/wic_codec.h"
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
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <utility>
#include <stdexcept>
#include <string>
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
// LibRaw failure classes reported by raw_develop.cpp for a file it cannot open or decode.
static bool isRawRejection(const std::string& message) {
    return message.find("Unsupported RAW file") != std::string::npos || message.find("Malformed RAW file") != std::string::npos || message.find("Cannot read RAW file") != std::string::npos;
}
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
    bool rawRejected = false; try { imaging::decodeRaw(rawPath, {}); } catch (const std::exception& error) { rawRejected = isRawRejection(error.what()); } check(rawRejected, "malformed RAW was not reported");
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
    check(imported.document.id.size() == 36 && imported.document.id[8] == '-' && imported.document.id[13] == '-' && imported.document.id[18] == '-' && imported.document.id[23] == '-', "PSD import left the document without a UUID");
    {
        QTemporaryDir saved; check(saved.isValid(), "PSD save fixture directory");
        const auto path = std::filesystem::path(saved.path().toStdWString()) / L"Imported.comp";
        ProjectStore store(makeWicProjectCodec());
        store.save(path, imported.document, imported.document.layers[0].id);
        auto loaded = store.load(path);
        check(loaded.document.id == imported.document.id && loaded.document.layers.size() == 1, "Imported PSD could not be saved as a project");
    }
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
    const char* boldMissing = "<< /EngineDict << /Editor << /Text (Hi) >> /StyleRun << /RunArray [ << /StyleSheet << /StyleSheetData << /FontSize 18 /Font 0 >> >> >> ] >> >> /ResourceDict << /FontSet [ << /Name (NoSuchFont-Bold) >> ] >> >>";
    auto boldFace = textFile(boldMissing, false, "BoldMissing");
    check(boldFace.document.layers.at(0).text && boldFace.document.layers.at(0).text->fontFamily == "Segoe UI" && boldFace.document.layers.at(0).text->fontStyle == "Bold", "missing bold PostScript name lost its weight");
    const char* segoePostScript = "<< /EngineDict << /Editor << /Text (Hi) >> /StyleRun << /RunArray [ << /StyleSheet << /StyleSheetData << /FontSize 18 /Font 0 >> >> >> ] >> >> /ResourceDict << /FontSet [ << /Name (SegoeUI) >> ] >> >>";
    auto segoe = textFile(segoePostScript, false, "Segoe");
    check(segoe.document.layers.at(0).text && segoe.document.layers.at(0).text->fontFamily == "Segoe UI" && segoe.report.find("isn't installed") == std::string::npos, "SegoeUI PostScript name was not resolved");
    wchar_t windowsDirectory[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"WINDIR", windowsDirectory, MAX_PATH) > 0 && std::filesystem::is_regular_file(std::filesystem::path(windowsDirectory) / L"Fonts" / L"monbaiti.ttf")) {
        const char* mongolian = "<< /EngineDict << /Editor << /Text (Hi) >> /StyleRun << /RunArray [ << /StyleSheet << /StyleSheetData << /FontSize 18 /Font 0 >> >> >> ] >> >> /ResourceDict << /FontSet [ << /Name (MongolianBaiti) >> ] >> >>";
        auto baiti = textFile(mongolian, false, "Baiti");
        check(baiti.document.layers.at(0).text && baiti.document.layers.at(0).text->fontFamily == "Mongolian Baiti" && baiti.report.find("isn't installed") == std::string::npos, "MongolianBaiti was treated as a missing font");
    }
    if (GetEnvironmentVariableW(L"WINDIR", windowsDirectory, MAX_PATH) > 0 && std::filesystem::is_regular_file(std::filesystem::path(windowsDirectory) / L"Fonts" / L"calibri.ttf")) {
        const char* mixed = "<< /EngineDict << /Editor << /Text (Hi) >> /StyleRun << /RunArray [ << /StyleSheet << /StyleSheetData << /FontSize 18 /Font 0 >> >> >> << /StyleSheet << /StyleSheetData << /FontSize 18 /Font 1 >> >> >> ] /RunLengthArray [ 1 1 ] >> >> /ResourceDict << /FontSet [ << /Name (NoSuchFontXYZ) >> << /Name (Calibri) >> ] >> >>";
        auto runs = textFile(mixed, false, "Runs");
        const auto* typed = runs.document.layers.at(0).text ? &*runs.document.layers.at(0).text : nullptr;
        check(typed && typed->fontFamily == "Segoe UI" && typed->fontRuns.size() == 1 && typed->fontRuns[0].location == 1 && typed->fontRuns[0].length == 1 && typed->fontRuns[0].fontFamily == "Calibri" && typed->fontRuns[0].hasFont, "a later installed PSD font was replaced with the missing face");
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
    check(deepImported.document.layers.size() == 1 && deepImported.document.layers[0].raster && deepImported.document.layers[0].raster->pixel(0, 0) == Pixel{255, 0, 0, 255} && deepImported.document.layers[0].deep && deepImported.document.layers[0].deep->at(0, 0)[0] == 65535 && deepImported.report.find("converted") == std::string::npos, "16-bit PSD white was not preserved");
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
    auto descriptor = [](std::initializer_list<std::pair<const char*, double>> numbers, std::initializer_list<std::pair<const char*, bool>> flags = {}) {
        std::vector<uint8_t> blob;
        auto b32 = [&](uint32_t v) { for (int s = 24; s >= 0; s -= 8) blob.push_back(uint8_t(v >> s)); };
        auto b64 = [&](double v) { uint64_t bits = 0; std::memcpy(&bits, &v, 8); for (int s = 56; s >= 0; s -= 8) blob.push_back(uint8_t(bits >> s)); };
        auto raw = [&](const char* s) { while (*s) blob.push_back(uint8_t(*s++)); };
        auto key = [&](const char* name) { auto n = uint32_t(std::strlen(name)); b32(n == 4 ? 0 : n); raw(name); };
        for (const auto& item : numbers) { key(item.first); raw("doub"); b64(item.second); }
        for (const auto& item : flags) { key(item.first); raw("bool"); blob.push_back(item.second ? 1 : 0); }
        return blob;
    };
    auto shapeFile = [&](const std::vector<uint8_t>& fill, const std::vector<uint8_t>& geometry, const std::vector<uint8_t>& stroke) {
        std::vector<uint8_t> file;
        auto p16 = [&](int v) { file.push_back(uint8_t(v >> 8)); file.push_back(uint8_t(v)); };
        auto p32 = [&](uint32_t v) { for (int s = 24; s >= 0; s -= 8) file.push_back(uint8_t(v >> s)); };
        auto pascii = [&](const char* s) { while (*s) file.push_back(uint8_t(*s++)); };
        pascii("8BPS"); p16(1); for (int i = 0; i < 6; ++i) file.push_back(0); p16(3); p32(1); p32(1); p16(8); p16(3); p32(0); p32(0);
        std::vector<uint8_t> info;
        auto i16 = [&](int v) { info.push_back(uint8_t(v >> 8)); info.push_back(uint8_t(v)); };
        auto i32 = [&](uint32_t v) { for (int s = 24; s >= 0; s -= 8) info.push_back(uint8_t(v >> s)); };
        auto iascii = [&](const char* s) { while (*s) info.push_back(uint8_t(*s++)); };
        i16(1); i32(0); i32(0); i32(0); i32(0); i16(4); for (int id : {0, 1, 2, -1}) { i16(id); i32(0); }
        iascii("8BIM"); iascii("norm"); info.push_back(255); info.push_back(0); info.push_back(0); info.push_back(0);
        std::vector<uint8_t> extra;
        auto e32 = [&](uint32_t v) { for (int s = 24; s >= 0; s -= 8) extra.push_back(uint8_t(v >> s)); };
        auto eascii = [&](const char* s) { while (*s) extra.push_back(uint8_t(*s++)); };
        auto extraBlock = [&](const char* key, const std::vector<uint8_t>& data) {
            eascii("8BIM"); eascii(key); e32(uint32_t(data.size())); extra.insert(extra.end(), data.begin(), data.end()); if (data.size() & 1) extra.push_back(0);
        };
        e32(0); e32(0); extra.push_back(5); eascii("Shape"); extra.push_back(0); extra.push_back(0);
        extraBlock("vscg", fill); extraBlock("vogk", geometry); if (!stroke.empty()) extraBlock("vstk", stroke);
        i32(uint32_t(extra.size())); info.insert(info.end(), extra.begin(), extra.end());
        p32(uint32_t(info.size() + 4)); p32(uint32_t(info.size())); file.insert(file.end(), info.begin(), info.end());
        p16(0); file.push_back(0); file.push_back(255); file.push_back(0);
        return imaging::readPsd(file.data(), file.size());
    };
    const auto solid = descriptor({{"Rd  ", 255}, {"Grn ", 0}, {"Bl  ", 0}});
    const auto rounded = descriptor({{"Top ", 1}, {"Left", 2}, {"Btom", 9}, {"Rght", 12}, {"topLeft", 2}});
    auto painted = shapeFile(solid, rounded, {});
    check(painted.document.layers.size() == 1 && painted.document.layers[0].name == "Shape" && painted.document.layers[0].raster
        && painted.document.layers[0].raster->width == 10 && painted.document.layers[0].raster->height == 8
        && painted.document.layers[0].transform.x == 2 && painted.document.layers[0].transform.y == 1
        && painted.document.layers[0].raster->pixel(5, 4) == Pixel{255, 0, 0, 255}, "rounded rectangle descriptor was not rasterized");
    const auto stroked = descriptor({{"strokeStyleLineWidth", 2}, {"Rd  ", 0}, {"Grn ", 0}, {"Bl  ", 255}}, {{"strokeEnabled", true}});
    auto outlined = shapeFile(solid, descriptor({{"Top ", 0}, {"Left", 0}, {"Btom", 8}, {"Rght", 8}, {"topLeft", 0}}), stroked);
    check(outlined.document.layers.size() == 1 && outlined.document.layers[0].raster && outlined.document.layers[0].raster->width == 10 && outlined.document.layers[0].raster->height == 10, "finite stroke bounds were rejected");
    const double nan = std::numeric_limits<double>::quiet_NaN();
    auto mergedOnly = [&](const std::vector<uint8_t>& geometry, const std::vector<uint8_t>& stroke, const char* message) {
        auto imported = shapeFile(solid, geometry, stroke);
        check(imported.document.layers.size() == 1 && imported.document.layers[0].name == "Background" && imported.document.layers[0].raster
            && imported.document.layers[0].raster->width == 1 && imported.document.layers[0].raster->pixel(0, 0) == Pixel{0, 255, 0, 255}
            && imported.report.find("merged") != std::string::npos, message);
    };
    mergedOnly(descriptor({{"Top ", nan}, {"Left", 0}, {"Btom", 10}, {"Rght", 10}}), {}, "NaN shape coordinates were rasterized");
    mergedOnly(descriptor({{"Top ", 0}, {"Left", 0}, {"Btom", 10}, {"Rght", 10}, {"topLeft", nan}}), {}, "NaN corner radius was rasterized");
    mergedOnly(descriptor({{"Top ", 0}, {"Left", 0}, {"Btom", 1e300}, {"Rght", 1e300}}), {}, "huge shape coordinates were rasterized");
    mergedOnly(descriptor({{"Top ", 0}, {"Left", -1e300}, {"Btom", 10}, {"Rght", 10}}), {}, "shape origin outside the integer range was rasterized");
    mergedOnly(descriptor({{"Top ", 0}, {"Left", 0}, {"Btom", std::numeric_limits<double>::infinity()}, {"Rght", 10}}), {}, "infinite shape coordinates were rasterized");
    mergedOnly(descriptor({{"Top ", 0}, {"Left", 0}, {"Btom", 8}, {"Rght", 8}}), descriptor({{"strokeStyleLineWidth", 1e300}}, {{"strokeEnabled", true}}), "huge stroke width expanded the raster");
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
static std::vector<uint8_t> mergedRgb(const std::filesystem::path& path, int width, int height) {
    QFile file(QString::fromStdWString(path.wstring()));
    check(file.open(QIODevice::ReadOnly), "Could not reopen the Instagram PSD");
    const auto bytes = file.readAll();
    auto need = [&](qint64 at, qint64 count) { check(at >= 0 && count >= 0 && at + count <= bytes.size(), "Merged image ended early"); };
    auto u16 = [&](qint64& at) { need(at, 2); int v = int(uint8_t(bytes[at]) << 8 | uint8_t(bytes[at + 1])); at += 2; return v; };
    auto u32 = [&](qint64& at) { need(at, 4); uint32_t v = 0; for (int i = 0; i < 4; ++i) v = (v << 8) | uint8_t(bytes[at++]); return v; };
    qint64 at = 26; at += qint64(u32(at)); at += qint64(u32(at)); at += qint64(u32(at));
    check(u16(at) == 0, "Merged image is not uncompressed");
    std::vector<uint8_t> rgb(size_t(width) * size_t(height) * 3);
    for (int channel = 0; channel < 3; ++channel) for (int i = 0; i < width * height; ++i) {
        need(at, 2);
        const auto sample = scalePhotoshop16(uint16_t(uint16_t(uint8_t(bytes[at])) << 8 | uint8_t(bytes[at + 1])));
        rgb[size_t(i) * 3 + channel] = uint8_t((int(sample) * 255 + 32767) / 65535);
        at += 2;
    }
    return rgb;
}
static void instagram_layers() {
    const auto path = std::filesystem::path(COMPOSITOR_SOURCE_ROOT) / L"psd-test" / L"Mise \u00e0 jour Instagram.psd";
    if (!std::filesystem::is_regular_file(path)) {
        std::cout << "SKIP Instagram PSD fixture; psd-test/Mise a jour Instagram.psd is not in this checkout\n";
        return;
    }
    auto imported = imaging::readPsd(path);
    const auto& document = imported.document;
    check(document.width == 1080 && document.height == 1350, "Instagram PSD canvas");
    if (document.layers.size() != 19) throw std::runtime_error("Instagram PSD layer count " + std::to_string(document.layers.size()) + "\n" + imported.report);
    check(imported.report.find("Imported the merged image") == std::string::npos, "Instagram PSD used the merged image");
    check(document.layers[0].name == "Arri\xc3\xa8re-plan" && document.layers[0].visible && document.layers[0].transform.x == 0 && document.layers[0].transform.y == 0, "background layer");
    const auto& photo = document.layers[1];
    check(photo.name == "Calque 1" && photo.visible && photo.raster && photo.transform.x == -3 && photo.transform.y == -445, "Calque 1 position");
    int hidden = 0; const Layer* masked = nullptr; const Layer* rectangle = nullptr; const Layer* title = nullptr; const Layer* page = nullptr; const Layer* details = nullptr;
    for (const auto& layer : document.layers) {
        if (!layer.visible) ++hidden;
        if (layer.name == "Calque 5 copie") masked = &layer;
        if (layer.name == "Rectangle 1") rectangle = &layer;
        if (layer.name == "a Fresh New Look") title = &layer;
        if (layer.name == "num\xc3\xa9ro courant") page = &layer;
        if (layer.name.rfind("Find details", 0) == 0) details = &layer;
    }
    check(hidden == 9, "hidden layer count");
    check(masked && !masked->visible && masked->mask && masked->mask->enabled && masked->mask->raster && masked->mask->raster->width == masked->raster->width, "hidden layer mask");
    check(rectangle && rectangle->visible && rectangle->raster && rectangle->transform.x > 20 && rectangle->transform.y > 250, "rounded rectangle");
    check(title && title->visible && title->text && title->text->value.find("Fresh") != std::string::npos && title->raster && title->transform.x == 81 && title->transform.y == 100 && title->transform.width == 930 && title->transform.height == 86, "text layer keeps Photoshop pixels");
    bool bodyFont = false;
    std::string seen;
    if (details && details->text) {
        seen = details->text->fontFamily + "/" + details->text->fontStyle + " runs=" + std::to_string(details->text->fontRuns.size());
        for (const auto& run : details->text->fontRuns) {
            seen += " [" + std::to_string(run.location) + "+" + std::to_string(run.length) + " " + run.fontFamily + " " + run.fontStyle + "]";
            if (run.hasFont && run.fontFamily == "Javanese Text") bodyFont = true;
        }
    }
    if (!(details && details->text && details->text->fontStyle == "Bold" && bodyFont)) throw std::runtime_error("Find details font " + seen);
    check(page && page->visible && page->text && page->name == "num\xc3\xa9ro courant", "unicode text layer");
    auto reference = mergedRgb(path, document.width, document.height);
    auto rendered = SoftwareRenderer{}.render(document, 0, 0, document.width, document.height);
    check(rendered && rendered->width == document.width && rendered->height == document.height, "composite render");
    uint64_t error = 0; int maxError = 0;
    for (int y = 0; y < document.height; ++y) for (int x = 0; x < document.width; ++x) {
        auto pixel = rendered->pixel(x, y); size_t i = (size_t(y) * document.width + x) * 3;
        for (int channel = 0; channel < 3; ++channel) {
            int delta = std::abs(int((&pixel.r)[channel]) - int(reference[i + channel]));
            error += uint64_t(delta); maxError = std::max(maxError, delta);
        }
    }
    const double mean = double(error) / (double(document.width) * document.height * 3);
    // 16-bit layers are previewed through a dithered 8-bit copy, then composited with
    // 8-bit shapes and text. That preview matches the scaled merged image in the large,
    // and a broken scale (for example keeping only the high byte) pushes the mean far past this.
    if (mean > 40) throw std::runtime_error("composite drift mean " + std::to_string(mean) + " max " + std::to_string(maxError));
    check(document.layers[0].deep && document.layers[0].deep->at(0, 0)[0] == 65535 && document.layers[0].raster->pixel(0, 0).r == 255, "16-bit background was not scaled to full range");
}
static int pngBitDepth(const std::filesystem::path& path) {
    QFile file(QString::fromStdWString(path.wstring()));
    check(file.open(QIODevice::ReadOnly), "PNG header missing");
    const auto header = file.read(25);
    check(header.size() == 25, "PNG header short");
    return int(uint8_t(header[24]));
}
static void deep_contract() {
    auto sample = [](int version, int width, uint16_t first, uint16_t second) {
        std::vector<uint8_t> file;
        auto u16 = [&](int v) { file.push_back(uint8_t(v >> 8)); file.push_back(uint8_t(v)); };
        auto u32 = [&](uint32_t v) { for (int s = 24; s >= 0; s -= 8) file.push_back(uint8_t(v >> s)); };
        auto u64 = [&](uint64_t v) { for (int s = 56; s >= 0; s -= 8) file.push_back(uint8_t(v >> s)); };
        auto ascii = [&](const char* s) { while (*s) file.push_back(uint8_t(*s++)); };
        auto pair = [&](uint16_t v) { file.push_back(uint8_t(v >> 8)); file.push_back(uint8_t(v)); };
        ascii("8BPS"); u16(version); for (int i = 0; i < 6; ++i) file.push_back(0); u16(3); u32(1); u32(uint32_t(width)); u16(16); u16(3); u32(0); u32(0);
        if (version == 2) u64(0); else u32(0);
        u16(0);
        pair(first); if (width > 1) pair(second);
        for (int channel = 0; channel < 2; ++channel) for (int x = 0; x < width; ++x) pair(0);
        return imaging::readPsd(file.data(), file.size());
    };
    auto distinct = sample(1, 2, 0x0100, 0x0180);
    check(distinct.document.layers.size() == 1 && distinct.document.layers[0].deep && distinct.document.layers[0].deep->at(0, 0)[0] == 512 && distinct.document.layers[0].deep->at(1, 0)[0] == 768, "16-bit PSD samples that share a high byte collapsed");
    auto white = sample(2, 1, 0x8000, 0);
    check(white.document.layers[0].deep && white.document.layers[0].deep->at(0, 0)[0] == 65535 && white.document.layers[0].raster->pixel(0, 0).r == 255, "Photoshop 16-bit white was not scaled to full range");
    QTemporaryDir directory; check(directory.isValid(), "16-bit project directory");
    const auto path = std::filesystem::path(directory.path().toStdWString()) / L"Deep.comp";
    ProjectStore store(makeWicProjectCodec());
    store.save(path, distinct.document, distinct.document.layers[0].id);
    check(versionOf(path) == 13, "16-bit project did not use format v13");
    auto image = path / L"images" / std::filesystem::path(QString::fromStdString(distinct.document.layers[0].id + ".png").toStdWString());
    check(pngBitDepth(image) == 16, "16-bit project PNG was stored at 8 bits");
    auto reopened = store.load(path);
    check(reopened.document.layers[0].deep && reopened.document.layers[0].deep->samples == distinct.document.layers[0].deep->samples, "16-bit project pixels changed after reopen");
    QFile manifest(QString::fromStdWString((path / L"manifest.json").wstring()));
    check(manifest.open(QIODevice::ReadOnly), "manifest missing");
    auto object = QJsonDocument::fromJson(manifest.readAll()).object(); manifest.close();
    object["version"] = 7;
    check(manifest.open(QIODevice::WriteOnly | QIODevice::Truncate), "manifest rewrite failed");
    manifest.write(QJsonDocument(object).toJson()); manifest.close();
    bool rejected = false; try { store.load(path); } catch (const std::exception&) { rejected = true; }
    check(rejected, "Version 7 accepted a 16-bit project PNG");
    Document eight = base(); eight.layers.push_back(pixelLayer());
    const auto eightPath = std::filesystem::path(directory.path().toStdWString()) / L"Eight.comp";
    store.save(eightPath, eight, eight.layers[0].id);
    check(versionOf(eightPath) < 13 && pngBitDepth(eightPath / L"images" / std::filesystem::path(QString::fromStdString(eight.layers[0].id + ".png").toStdWString())) == 8, "8-bit project was raised to 16-bit");
    Document gradientDoc; gradientDoc.id = newId(); gradientDoc.width = 8; gradientDoc.height = 1; gradientDoc.resolution = 72;
    Layer gradient = pixelLayer(); gradient.transform = {0, 0, 8, 1};
    auto source = std::make_shared<DeepImage>(); source->width = 8; source->height = 1; source->samples.assign(32, 0);
    for (int x = 0; x < 8; ++x) source->at(x, 0)[3] = 65535;
    gradient.deep = source; gradient.raster = rasterFromDeep(*source);
    gradientDoc.layers = {gradient};
    editing::GradientSettings settings; settings.style = editing::GradientStyle::ForegroundToBackground;
    auto graded = editing::gradientLayer(gradient, gradientDoc, {0, 0.5}, {8, 0.5}, settings, {0, 0, 0, 255}, {255, 255, 255, 255});
    check(graded.deep && graded.deep->width == graded.raster->width, "Gradient dropped 16-bit pixels");
    const auto middle = graded.deep->at(graded.deep->width / 2, 0)[0];
    check(middle % 257 != 0, "Gradient intermediates collapsed to 8-bit steps");
    auto filled = editing::editLayer(gradient, gradientDoc, editing::PixelEdit::Fill, {255, 0, 0, 255});
    check(filled.deep && filled.deep->at(0, 0)[0] == 65535 && filled.deep->at(0, 0)[1] == 0 && filled.deep->at(0, 0)[3] == 65535, "Fill did not keep 16-bit color");
    const auto png = std::filesystem::path(directory.path().toStdWString()) / L"flat.png";
    const auto jpeg = std::filesystem::path(directory.path().toStdWString()) / L"flat.jpg";
    imaging::ExportOptions pngOptions; pngOptions.format = imaging::ImageFormat::Png;
    exportDocumentAtomic(distinct.document, png, pngOptions);
    check(pngBitDepth(png) == 16, "PNG export of a 16-bit layer was 8-bit");
    auto exported = imaging::WicCodec::decodeProjectDeepPng(png);
    check(exported && exported->at(0, 0)[0] == distinct.document.layers[0].deep->at(0, 0)[0] && exported->at(1, 0)[0] == distinct.document.layers[0].deep->at(1, 0)[0], "Exported 16-bit pixels changed");
    imaging::ExportOptions jpegOptions; jpegOptions.format = imaging::ImageFormat::Jpeg;
    exportDocumentAtomic(distinct.document, jpeg, jpegOptions);
    QFile jpegFile(QString::fromStdWString(jpeg.wstring())); check(jpegFile.open(QIODevice::ReadOnly), "JPEG export missing");
    const auto signature = jpegFile.read(2); check(signature.size() == 2 && uint8_t(signature[0]) == 0xFF && uint8_t(signature[1]) == 0xD8, "JPEG export was not an 8-bit JPEG");
    auto deepLayer = [](int width, int height, Pixel color) {
        Layer layer; layer.id = newId(); layer.name = "Deep"; layer.transform = {0, 0, double(width), double(height)};
        auto image = std::make_shared<DeepImage>(); image->width = width; image->height = height; image->samples.assign(std::size_t(width) * std::size_t(height) * 4, 0);
        for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
            auto* sample = image->at(x, y);
            sample[0] = promote8(color.r); sample[1] = promote8(color.g); sample[2] = promote8(color.b); sample[3] = promote8(color.a);
        }
        layer.deep = image; layer.raster = rasterFromDeep(*image); return layer;
    };
    Document hidden; hidden.id = newId(); hidden.width = 2; hidden.height = 1; hidden.resolution = 72;
    Layer folder; folder.id = newId(); folder.name = "Folder"; folder.group = true; folder.visible = false; folder.transform = {0, 0, 2, 1};
    auto child = deepLayer(2, 1, {255, 0, 0, 255}); child.parentId = folder.id; hidden.layers = {folder, child};
    const auto hiddenPng = std::filesystem::path(directory.path().toStdWString()) / L"hidden.png";
    exportDocumentAtomic(hidden, hiddenPng, pngOptions);
    check(pngBitDepth(hiddenPng) == 8, "Hidden 16-bit folder exported as 16-bit PNG");
    auto hiddenImage = imaging::WicCodec::decode(hiddenPng).image;
    check(hiddenImage.width == 2 && hiddenImage.height == 1, "Hidden folder export size");
    check(std::all_of(hiddenImage.pixels.begin(), hiddenImage.pixels.end(), [](std::uint8_t value) { return value == 0; }), "Hidden folder exported its 16-bit child");
    Document clipped; clipped.id = newId(); clipped.width = 2; clipped.height = 1; clipped.resolution = 72;
    Layer baseLayer; baseLayer.id = newId(); baseLayer.name = "Base"; baseLayer.transform = {0, 0, 2, 1};
    const std::uint8_t coverage[]{255, 255, 255, 255, 0, 0, 0, 0};
    baseLayer.raster = Raster::fromRgba(2, 1, coverage, 8);
    auto overlay = deepLayer(2, 1, {255, 0, 0, 255}); overlay.maskSourceId = baseLayer.id; clipped.layers = {baseLayer, overlay};
    const auto clippedPng = std::filesystem::path(directory.path().toStdWString()) / L"clipped.png";
    exportDocumentAtomic(clipped, clippedPng, pngOptions);
    auto clippedImage = imaging::WicCodec::decode(clippedPng).image;
    auto rendered = SoftwareRenderer{}.render(clipped, 0, 0, clipped.width, clipped.height);
    check(clippedImage.width == 2 && clippedImage.height == 1 && rendered, "Clipped 16-bit export size");
    for (int x = 0; x < 2; ++x) {
        const auto pixel = rendered->pixel(x, 0);
        const auto* exportedPixel = clippedImage.pixels.data() + std::size_t(x) * 4;
        check(exportedPixel[0] == pixel.r && exportedPixel[1] == pixel.g && exportedPixel[2] == pixel.b && exportedPixel[3] == pixel.a, "Clipped 16-bit PNG did not match the canvas composite");
    }
    imaging::ImportOptions byteBudget; byteBudget.maxWorkingBytes = 8;
    bool rejectedBudget = false; try { imaging::WicCodec::decodeProjectDeepPng(png, byteBudget); } catch (const std::exception&) { rejectedBudget = true; }
    check(rejectedBudget, "16-bit PNG decode accepted a 4-byte-per-pixel budget");
    imaging::ImportOptions deepOnly; deepOnly.maxWorkingBytes = 16;
    bool rejectedRaster = false; try { imaging::WicCodec::decodeProjectDeepPng(png, deepOnly); } catch (const std::exception&) { rejectedRaster = true; }
    check(rejectedRaster, "16-bit PNG decode ignored the dithered raster");
    imaging::ImportOptions enough; enough.maxWorkingBytes = 16 + 8 + sizeof(Raster::Tile);
    check(static_cast<bool>(imaging::WicCodec::decodeProjectDeepPng(png, enough)), "16-bit PNG decode rejected a budget that fits the deep buffer and raster");
    QByteArray animated;
    const unsigned char pngSignature[]{137, 80, 78, 71, 13, 10, 26, 10};
    animated.append(reinterpret_cast<const char*>(pngSignature), 8);
    auto be = [&](std::uint32_t value) { for (int shift = 24; shift >= 0; shift -= 8) animated.append(char(value >> shift)); };
    be(13); animated.append("IHDR"); be(1); be(1); animated.append("\x10\x06\x00\x00\x00", 5); be(0);
    be(8); animated.append("acTL"); be(1); be(1); be(0);
    const auto animatedPath = std::filesystem::path(directory.path().toStdWString()) / L"animated-deep.png";
    QFile animatedFile(QString::fromStdWString(animatedPath.wstring()));
    check(animatedFile.open(QIODevice::WriteOnly) && animatedFile.write(animated) == animated.size(), "animated 16-bit PNG fixture");
    animatedFile.close();
    std::string animatedReason;
    try { imaging::WicCodec::decodeProjectDeepPng(animatedPath); } catch (const std::exception& error) { animatedReason = error.what(); }
    check(animatedReason.find("Animated") != std::string::npos, "Animated 16-bit project PNG was not rejected");
}
// Returns the exception text, or an empty string when the call succeeds.
template <typename Call>
static std::string errorOf(Call&& call) {
    try { call(); } catch (const std::exception& error) { return error.what(); }
    return {};
}
static double meanLuma(const imaging::RgbaImage& image) {
    double sum = 0;
    for (std::uint32_t y = 0; y < image.height; ++y) for (std::uint32_t x = 0; x < image.width; ++x) {
        const auto* p = &image.pixels[y * image.stride + x * 4];
        sum += 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2];
    }
    return sum / (double(image.width) * image.height);
}
static void raw_development_contract() {
    QTemporaryDir directory;
    // Every RAW rejection names the LibRaw failure class; a silent as-shot fallback would not match.
    check(directory.isValid(), "RAW temporary directory");
    const auto garbage = std::filesystem::path(directory.filePath("garbage.dng").toStdWString());
    QFile garbageFile(directory.filePath("garbage.dng"));
    check(garbageFile.open(QIODevice::WriteOnly) && garbageFile.write("not raw") == 7, "RAW garbage fixture");
    garbageFile.close();
    // Settings are validated before any codec work, so these hold on every host.
    imaging::ImportOptions options;
    options.raw.exposure = std::numeric_limits<double>::quiet_NaN();
    check(errorOf([&] { imaging::developRaw(garbage, options); }).find("Invalid RAW development settings") != std::string::npos, "non-finite exposure was not rejected");
    options.raw.exposure = 6.0;
    check(errorOf([&] { imaging::developRaw(garbage, options); }).find("Invalid RAW development settings") != std::string::npos, "exposure beyond 5 EV was not rejected");
    options = {};
    options.cancelled = [] { return true; };
    check(errorOf([&] { imaging::developRaw(garbage, options); }).find("cancelled") != std::string::npos, "cancelled RAW development was not reported");
    check(imaging::isRawFileExtension("IMG_0001.CR2") && imaging::isRawFileExtension("a.dng") && !imaging::isRawFileExtension("a.png"), "RAW extension routing");
    // A corrupt file must report a rejection, never a silent as-shot import.
    const auto malformed = errorOf([&] { imaging::decodeRaw(garbage, {}); });
    check(isRawRejection(malformed), "corrupt RAW error is not distinct");

    // Codec-dependent checks run only with a real RAW file; see docs/user-guide.md for the fixture provenance.
    const char* fixture = std::getenv("COMPOSITOR_RAW_FIXTURE");
    if (!fixture) { std::cout << "SKIP raw_development codec checks: COMPOSITOR_RAW_FIXTURE is not set\n"; return; }
    const auto raw = std::filesystem::path(QString::fromUtf8(fixture).toStdWString());
    const auto asShot = imaging::developRaw(raw, {});
    const auto& caps = asShot.capabilities;
    std::cout << "RAW caps temperature=" << caps.temperature << " tint=" << caps.tint << " kelvin=" << caps.minKelvin << ".." << caps.maxKelvin
              << " asShot K=" << asShot.asShot.kelvin << " tint=" << asShot.asShot.tint << '\n';
    check(caps.developable && caps.minKelvin == imaging::kRawKelvinMin && caps.maxKelvin == imaging::kRawKelvinMax, "RAW capabilities");

    // Preview is a downscale of the same development result, never a separate pipeline.
    const auto preview = imaging::developRaw(raw, {}, 256);
    const double fullRatio = double(asShot.image.image.width) / asShot.image.image.height;
    const double previewRatio = double(preview.image.image.width) / preview.image.image.height;
    check(std::max(preview.image.image.width, preview.image.image.height) <= 256 && std::abs(previewRatio - fullRatio) / fullRatio < 0.01, "preview bound or aspect ratio");
    // Exposure 0 EV is the identity: explicit settings must not change the default development.
    imaging::ImportOptions zeroEv;
    zeroEv.raw.exposure = 0.0;
    check(imaging::developRaw(raw, zeroEv).image.image.pixels == asShot.image.image.pixels, "0 EV changed the pixels");
    // Exposure is applied to the linear output before the sRGB encoding: +1 EV brightens the frame.
    imaging::ImportOptions brighter;
    brighter.raw.exposure = 1.0;
    check(meanLuma(imaging::developRaw(raw, brighter).image.image) > meanLuma(asShot.image.image) + 1.0, "+1 EV did not brighten the developed frame");
    // Out-of-range settings are rejected before LibRaw runs, not clamped silently.
    imaging::ImportOptions outside;
    outside.raw.kelvin = imaging::kRawKelvinMax + 1;
    check(errorOf([&] { imaging::developRaw(raw, outside); }).find("Invalid RAW development settings") != std::string::npos, "out-of-range temperature was not rejected");
    // Export and reopen: the 8-bit pixels written to PNG are read back within rounding.
    const auto exported = std::filesystem::path(directory.filePath("developed.png").toStdWString());
    imaging::ExportOptions png;
    png.format = imaging::ImageFormat::Png;
    imaging::WicCodec::encode(exported, asShot.image.image, png);
    const auto reopened = imaging::WicCodec::decode(exported).image;
    check(reopened.width == asShot.image.image.width && reopened.height == asShot.image.image.height, "exported RAW dimensions");
    int worst = 0;
    for (std::size_t i = 0; i < reopened.pixels.size(); ++i) worst = std::max(worst, std::abs(int(reopened.pixels[i]) - int(asShot.image.image.pixels[i])));
    check(worst <= 2, "exported RAW pixels differ beyond rounding");
    // A truncated RAW reports an explicit error instead of a partial image.
    const auto truncated = std::filesystem::path(directory.filePath("truncated.dng").toStdWString());
    QFile source(QString::fromStdWString(raw.wstring()));
    check(source.open(QIODevice::ReadOnly), "RAW fixture read");
    const auto head = source.read(4096);
    source.close();
    QFile truncatedFile(QString::fromStdWString(truncated.wstring()));
    check(truncatedFile.open(QIODevice::WriteOnly) && truncatedFile.write(head) == head.size(), "truncated RAW fixture");
    truncatedFile.close();
    check(!errorOf([&] { imaging::developRaw(truncated, {}); }).empty(), "truncated RAW was accepted");
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc > 1 && std::string(argv[1]) == "psd-folder") {
        const auto dir = argc > 2 ? std::filesystem::path(argv[2]) : std::filesystem::path("psd-test");
        return psd_folder(dir);
    }
    try { format_contract(); behavior_contract(); psd_contract(); deep_contract(); instagram_layers(); raw_development_contract(); std::cout << "PASS migration.contract\n"; return 0; }
    catch (const std::exception& error) { std::cerr << "FAIL migration.contract: " << error.what() << '\n'; return 1; }
}
