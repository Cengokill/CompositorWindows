#include "PluginHost.h"
#include "PluginRuntime.h"
#include "core/Document.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace compositor::plugin {
namespace {

// The runtime hands the host API a live document plus the active layer id. These
// helpers translate between the plugin's index-based view and the document's
// bottom-to-top layer vector.

compositor::Document* documentOf(void* hostData) {
    auto* runtime = static_cast<PluginRuntime*>(hostData);
    return runtime ? runtime->document() : nullptr;
}

compositor_result ok(bool condition) {
    return condition ? COMPOSITOR_OK : COMPOSITOR_ERROR_INVALID_ARGUMENT;
}

}  // namespace

void installHostApi(compositor_host_api* api, PluginRuntime* runtime) {
    if (!api || !runtime) return;
    api->host_data = runtime;
    api->abi_version = COMPOSITOR_PLUGIN_ABI_VERSION;

    api->get_document_info = [](void* hostData, compositor_document_info* out) -> compositor_result {
        if (!out) return COMPOSITOR_ERROR_INVALID_ARGUMENT;
        auto* runtime = static_cast<PluginRuntime*>(hostData);
        auto* doc = runtime ? runtime->document() : nullptr;
        if (!doc) return COMPOSITOR_ERROR_NO_DOCUMENT;
        out->width = doc->width;
        out->height = doc->height;
        out->resolution = doc->resolution;
        out->layer_count = static_cast<int32_t>(doc->layers.size());
        out->active_layer_index = runtime->activeLayerIndex();
        out->title = runtime->documentTitle().c_str();
        return COMPOSITOR_OK;
    };

    api->get_layer_count = [](void* hostData) -> int32_t {
        auto* doc = documentOf(hostData);
        return doc ? static_cast<int32_t>(doc->layers.size()) : 0;
    };

    api->get_layer_info = [](void* hostData, int32_t index, compositor_layer_info* out) -> compositor_result {
        if (!out) return COMPOSITOR_ERROR_INVALID_ARGUMENT;
        auto* runtime = static_cast<PluginRuntime*>(hostData);
        auto* doc = runtime ? runtime->document() : nullptr;
        if (!doc) return COMPOSITOR_ERROR_NO_DOCUMENT;
        if (index < 0 || size_t(index) >= doc->layers.size()) return COMPOSITOR_ERROR_NOT_FOUND;
        const auto& layer = doc->layers[size_t(index)];
        out->id = runtime->intern(layer.id);
        out->name = runtime->intern(layer.name);
        out->visible = layer.visible ? 1 : 0;
        out->is_group = layer.group ? 1 : 0;
        out->has_mask = layer.mask.has_value() ? 1 : 0;
        out->blend_mode = int(layer.blend);
        out->opacity = layer.opacity;
        out->parent_index = -1;
        if (!layer.parentId.empty()) {
            for (size_t i = 0; i < doc->layers.size(); ++i) {
                if (doc->layers[i].id == layer.parentId) { out->parent_index = int32_t(i); break; }
            }
        }
        return COMPOSITOR_OK;
    };

    api->set_layer_opacity = [](void* hostData, int32_t index, double opacity) -> compositor_result {
        auto* runtime = static_cast<PluginRuntime*>(hostData);
        if (!runtime) return COMPOSITOR_ERROR_INVALID_ARGUMENT;
        return runtime->editLayer("Set Layer Opacity", index, [opacity](compositor::Layer& layer) {
            layer.opacity = std::clamp(opacity, 0.0, 1.0);
        });
    };

    api->set_layer_visible = [](void* hostData, int32_t index, int visible) -> compositor_result {
        auto* runtime = static_cast<PluginRuntime*>(hostData);
        if (!runtime) return COMPOSITOR_ERROR_INVALID_ARGUMENT;
        return runtime->editLayer("Set Layer Visibility", index, [visible](compositor::Layer& layer) {
            layer.visible = visible != 0;
        });
    };

    api->set_layer_blend_mode = [](void* hostData, int32_t index, int mode) -> compositor_result {
        auto* runtime = static_cast<PluginRuntime*>(hostData);
        if (!runtime) return COMPOSITOR_ERROR_INVALID_ARGUMENT;
        if (mode < 0 || mode >= int(blendNames.size())) return COMPOSITOR_ERROR_INVALID_ARGUMENT;
        return runtime->editLayer("Set Layer Blend Mode", index, [mode](compositor::Layer& layer) {
            layer.blend = compositor::Blend(mode);
        });
    };

    api->create_layer = [](void* hostData, const char* name, int32_t* outIndex) -> compositor_result {
        auto* runtime = static_cast<PluginRuntime*>(hostData);
        if (!runtime) return COMPOSITOR_ERROR_INVALID_ARGUMENT;
        return runtime->createLayer(QString::fromUtf8(name ? name : "Layer"), outIndex);
    };

    api->delete_layer = [](void* hostData, int32_t index) -> compositor_result {
        auto* runtime = static_cast<PluginRuntime*>(hostData);
        if (!runtime) return COMPOSITOR_ERROR_INVALID_ARGUMENT;
        return runtime->deleteLayer(index);
    };

    api->read_layer_pixels = [](void* hostData, int32_t index, uint8_t** outPixels,
                                int32_t* outWidth, int32_t* outHeight) -> compositor_result {
        auto* runtime = static_cast<PluginRuntime*>(hostData);
        if (!runtime || !outPixels || !outWidth || !outHeight) return COMPOSITOR_ERROR_INVALID_ARGUMENT;
        return runtime->readLayerPixels(index, outPixels, outWidth, outHeight);
    };

    api->write_layer_pixels = [](void* hostData, int32_t index, const uint8_t* pixels,
                                 int32_t width, int32_t height) -> compositor_result {
        auto* runtime = static_cast<PluginRuntime*>(hostData);
        if (!runtime) return COMPOSITOR_ERROR_INVALID_ARGUMENT;
        return runtime->writeLayerPixels(index, pixels, width, height);
    };

    api->free_pixels = [](void*, uint8_t* pixels) { std::free(pixels); };

    api->run_command = [](void* hostData, const char* commandId) -> compositor_result {
        auto* runtime = static_cast<PluginRuntime*>(hostData);
        if (!runtime || !commandId) return COMPOSITOR_ERROR_INVALID_ARGUMENT;
        return runtime->runCommand(QString::fromUtf8(commandId));
    };

    api->log = [](void* hostData, const char* message) {
        auto* runtime = static_cast<PluginRuntime*>(hostData);
        if (runtime && message) runtime->log(QString::fromUtf8(message));
    };
}

}  // namespace compositor::plugin
