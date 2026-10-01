#include "persistence/ProjectStore.h"
#include "editing/DocumentGeometry.h"
#include "editing/SelectionExtras.h"
#include "editing/Shapes.h"
#include "effects/Adjustments.h"
#include "filters/PixelFilters.h"
#include "imaging/psd/PsdDocument.h"
#include "imaging/raw_develop.h"
#include "layers/LayerOperations.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
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
    std::vector<uint8_t> bytes; auto u16 = [&](int v) { bytes.push_back(uint8_t(v >> 8)); bytes.push_back(uint8_t(v)); }; auto u32 = [&](uint32_t v) { for (int i = 24; i >= 0; i -= 8) bytes.push_back(uint8_t(v >> i)); };
    auto ascii = [&](const char* s) { while (*s) bytes.push_back(uint8_t(*s++)); };
    ascii("8BPS"); u16(1); for (int i = 0; i < 6; ++i) bytes.push_back(0); u16(3); u32(2); u32(2); u16(8); u16(3); u32(0); u32(0);
    std::vector<uint8_t> info; auto i16 = [&](int v) { info.push_back(uint8_t(v >> 8)); info.push_back(uint8_t(v)); }; auto i32 = [&](uint32_t v) { for (int s = 24; s >= 0; s -= 8) info.push_back(uint8_t(v >> s)); }; auto infoAscii = [&](const char* s) { while (*s) info.push_back(uint8_t(*s++)); };
    i16(1); i32(0); i32(0); i32(2); i32(2); i16(4); for (int id : {0, 1, 2, -1}) { i16(id); i32(6); } infoAscii("8BIM"); infoAscii("norm"); info.push_back(255); info.push_back(0); info.push_back(0); info.push_back(0); i32(12); i32(0); i32(0); info.push_back(3); infoAscii("Red");
    for (int channel = 0; channel < 4; ++channel) { info.push_back(0); info.push_back(0); uint8_t value = channel == 1 || channel == 2 ? 0 : 255; for (int i = 0; i < 4; ++i) info.push_back(value); }
    u32(uint32_t(info.size() + 4)); u32(uint32_t(info.size())); bytes.insert(bytes.end(), info.begin(), info.end());
    auto imported = imaging::readPsd(bytes.data(), bytes.size()); check(imported.document.layers.size() == 1 && imported.document.layers[0].name == "Red" && imported.document.layers[0].raster->pixel(0, 0) == Pixel{255, 0, 0, 255}, "PSD layer was not imported");
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try { format_contract(); behavior_contract(); psd_contract(); std::cout << "PASS migration.contract\n"; return 0; }
    catch (const std::exception& error) { std::cerr << "FAIL migration.contract: " << error.what() << '\n'; return 1; }
}
