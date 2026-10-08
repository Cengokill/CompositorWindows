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
#include <objbase.h>
#include <cstring>
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
    i16(1); i32(0); i32(0); i32(2); i32(2); i16(4); for (int id : {0, 1, 2, -1}) { i16(id); i32(6); } infoAscii("8BIM"); infoAscii("norm"); info.push_back(255); info.push_back(0); info.push_back(0); info.push_back(0); i32(48); i32(0); i32(0); info.push_back(3); infoAscii("Red");
    infoAscii("8BIM"); infoAscii("luni"); i32(24); i32(10); for (uint16_t codePoint : {uint16_t('C'), uint16_t('a'), uint16_t('f'), uint16_t(0x00e9), uint16_t(' '), uint16_t(0x2013), uint16_t(' '), uint16_t(0x65e5), uint16_t(0x672c), uint16_t(0x8a9e)}) i16(codePoint);
    for (int channel = 0; channel < 4; ++channel) { info.push_back(0); info.push_back(0); uint8_t value = channel == 1 || channel == 2 ? 0 : 255; for (int i = 0; i < 4; ++i) info.push_back(value); }
    u32(uint32_t(info.size() + 4)); u32(uint32_t(info.size())); bytes.insert(bytes.end(), info.begin(), info.end());
    auto imported = imaging::readPsd(bytes.data(), bytes.size()); check(imported.document.layers.size() == 1 && imported.document.layers[0].name == "Café – 日本語" && imported.document.layers[0].raster->pixel(0, 0) == Pixel{255, 0, 0, 255}, "PSD Unicode layer name was not imported");
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
    auto upright = textFile("", true, "Upright");
    check(!upright.document.layers.at(0).text && upright.report.find("Vertical") != std::string::npos, "vertical PSD text stayed editable");
    std::vector<uint8_t> cmyk; auto c16 = [&](int v) { cmyk.push_back(uint8_t(v >> 8)); cmyk.push_back(uint8_t(v)); }; auto c32 = [&](uint32_t v) { for (int s = 24; s >= 0; s -= 8) cmyk.push_back(uint8_t(v >> s)); }; auto cascii = [&](const char* s) { while (*s) cmyk.push_back(uint8_t(*s++)); };
    cascii("8BPS"); c16(1); for (int i = 0; i < 6; ++i) cmyk.push_back(0); c16(4); c32(2); c32(2); c16(8); c16(4);
    bool namedCmyk = false; try { imaging::readPsd(cmyk.data(), cmyk.size()); } catch (const std::exception& error) { namedCmyk = std::string(error.what()).find("CMYK") != std::string::npos; }
    check(namedCmyk, "CMYK PSD was not named in the error");
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try { format_contract(); behavior_contract(); psd_contract(); std::cout << "PASS migration.contract\n"; return 0; }
    catch (const std::exception& error) { std::cerr << "FAIL migration.contract: " << error.what() << '\n'; return 1; }
}
