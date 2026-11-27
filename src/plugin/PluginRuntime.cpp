#include "PluginRuntime.h"
#include "core/Document.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace compositor::plugin {

compositor_result PluginRuntime::editLayer(const char* name, int32_t index,
                                           const std::function<void(compositor::Layer&)>& mutate) {
    auto* doc = document();
    if (!doc) return COMPOSITOR_ERROR_NO_DOCUMENT;
    if (index < 0 || size_t(index) >= doc->layers.size()) return COMPOSITOR_ERROR_NOT_FOUND;
    if (!edit_) return COMPOSITOR_ERROR_NOT_SUPPORTED;
    const size_t target = size_t(index);
    const bool applied = edit_(name, [target, &mutate](compositor::Document& document) {
        if (target < document.layers.size()) mutate(document.layers[target]);
    });
    return applied ? COMPOSITOR_OK : COMPOSITOR_ERROR_BUSY;
}

compositor_result PluginRuntime::createLayer(const QString& name, int32_t* outIndex) {
    auto* doc = document();
    if (!doc) return COMPOSITOR_ERROR_NO_DOCUMENT;
    if (!edit_) return COMPOSITOR_ERROR_NOT_SUPPORTED;
    const int created = int32_t(doc->layers.size());
    const std::string layerName = name.toStdString();
    const bool applied = edit_("New Layer", [created, layerName](compositor::Document& document) {
        compositor::Layer layer;
        layer.id = "plugin-layer-" + std::to_string(created);
        layer.name = layerName;
        layer.raster = compositor::Raster::filled(document.width, document.height, {0, 0, 0, 0});
        document.layers.push_back(std::move(layer));
    });
    if (!applied) return COMPOSITOR_ERROR_BUSY;
    if (outIndex) *outIndex = created;
    return COMPOSITOR_OK;
}

compositor_result PluginRuntime::deleteLayer(int32_t index) {
    auto* doc = document();
    if (!doc) return COMPOSITOR_ERROR_NO_DOCUMENT;
    if (index < 0 || size_t(index) >= doc->layers.size()) return COMPOSITOR_ERROR_NOT_FOUND;
    if (!edit_) return COMPOSITOR_ERROR_NOT_SUPPORTED;
    const size_t target = size_t(index);
    const bool applied = edit_("Delete Layer", [target](compositor::Document& document) {
        if (target < document.layers.size()) document.layers.erase(document.layers.begin() + target);
    });
    return applied ? COMPOSITOR_OK : COMPOSITOR_ERROR_BUSY;
}

compositor_result PluginRuntime::readLayerPixels(int32_t index, uint8_t** pixels,
                                                 int32_t* width, int32_t* height) {
    auto* doc = document();
    if (!doc) return COMPOSITOR_ERROR_NO_DOCUMENT;
    if (index < 0 || size_t(index) >= doc->layers.size()) return COMPOSITOR_ERROR_NOT_FOUND;
    const auto& layer = doc->layers[size_t(index)];
    if (!layer.raster) return COMPOSITOR_ERROR_NOT_SUPPORTED;
    auto rgba = layer.raster->rgba();
    const size_t bytes = rgba.size();
    auto* buffer = static_cast<uint8_t*>(std::malloc(bytes ? bytes : 1));
    if (!buffer) return COMPOSITOR_ERROR_OUT_OF_MEMORY;
    if (bytes) std::memcpy(buffer, rgba.data(), bytes);
    *pixels = buffer;
    *width = layer.raster->width;
    *height = layer.raster->height;
    return COMPOSITOR_OK;
}

compositor_result PluginRuntime::writeLayerPixels(int32_t index, const uint8_t* pixels,
                                                  int32_t width, int32_t height) {
    auto* doc = document();
    if (!doc) return COMPOSITOR_ERROR_NO_DOCUMENT;
    if (!pixels || width <= 0 || height <= 0) return COMPOSITOR_ERROR_INVALID_ARGUMENT;
    if (index < 0 || size_t(index) >= doc->layers.size()) return COMPOSITOR_ERROR_NOT_FOUND;
    if (!edit_) return COMPOSITOR_ERROR_NOT_SUPPORTED;
    const size_t target = size_t(index);
    auto replacement = compositor::Raster::fromRgba(width, height, pixels, size_t(width) * 4);
    if (!replacement) return COMPOSITOR_ERROR_OUT_OF_MEMORY;
    const bool applied = edit_("Write Layer Pixels", [target, replacement](compositor::Document& document) {
        if (target < document.layers.size()) document.layers[target].raster = replacement;
    });
    return applied ? COMPOSITOR_OK : COMPOSITOR_ERROR_BUSY;
}

}  // namespace compositor::plugin
