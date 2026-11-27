// Example plugin: prints document statistics and adds a layer.
//
// Build (MSVC):
//   cl /LD /I..\..\include example_stats.c /link /OUT:example_stats.compositor-plugin.dll
//
// The plugin demonstrates the whole v1 surface: document query, layer list,
// a mutation that goes through the editor's undo stack, and logging.

#include "compositor_plugin.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    const compositor_host_api* host;
} ExampleState;

static ExampleState g_state;

static compositor_result example_get_info(void* user_data, compositor_plugin_info* out) {
    (void)user_data;
    out->abi_version = COMPOSITOR_PLUGIN_ABI_VERSION;
    out->id = "com.example.stats";
    out->name = "示例：文档信息";
    out->version = "1.0.0";
    out->vendor = "Compositor Examples";
    out->description = "读取文档与图层信息，并新建一个图层作为演示。";
    return COMPOSITOR_OK;
}

static compositor_result example_create(void* user_data) {
    (void)user_data;
    if (g_state.host) g_state.host->log(g_state.host->host_data, "example_stats 已加载");
    return COMPOSITOR_OK;
}

static void example_destroy(void* user_data) { (void)user_data; }

static compositor_result example_run(void* user_data) {
    (void)user_data;
    if (!g_state.host) return COMPOSITOR_ERROR_UNKNOWN;
    const compositor_host_api* host = g_state.host;

    compositor_document_info info;
    compositor_result rc = host->get_document_info(host->host_data, &info);
    if (rc != COMPOSITOR_OK) {
        host->log(host->host_data, "没有打开的文档");
        return rc;
    }

    char line[256];
    snprintf(line, sizeof(line), "文档 %dx%d，%.1f DPI，共 %d 个图层",
             info.width, info.height, info.resolution, info.layer_count);
    host->log(host->host_data, line);

    const int32_t count = host->get_layer_count(host->host_data);
    for (int32_t i = 0; i < count; ++i) {
        compositor_layer_info layer;
        if (host->get_layer_info(host->host_data, i, &layer) == COMPOSITOR_OK) {
            snprintf(line, sizeof(line), "  图层 %d: %s (不透明度 %.2f)", (int)i, layer.name, layer.opacity);
            host->log(host->host_data, line);
        }
    }

    int32_t index = -1;
    if (host->create_layer(host->host_data, "示例图层", &index) == COMPOSITOR_OK) {
        snprintf(line, sizeof(line), "已新建图层 #%d", (int)index);
        host->log(host->host_data, line);
    }
    return COMPOSITOR_OK;
}

static const compositor_plugin_api g_api = {
    &g_state,
    example_get_info,
    example_create,
    example_destroy,
    example_run,
};

__declspec(dllexport) const compositor_plugin_api* CompositorPluginEntry(const compositor_host_api* host) {
    g_state.host = host;
    return &g_api;
}
