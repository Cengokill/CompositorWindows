#pragma once
// Native plugin SDK for Compositor Windows.
//
// A plugin is a dynamic library that exports CompositorPluginEntry and fills in
// the function table below. The host loads the library, hands it a PluginHost
// with the current document accessor, and calls create()/destroy().
//
// Design rules:
//   - Everything crosses the boundary as C structs and plain integers, so a
//     plugin does not depend on the host's C++ standard library ABI.
//   - The plugin never receives host object pointers it could keep; it asks for
//     data and issues commands through the host table.
//   - Every command reports a compositor_result; the host never throws across
//     the boundary.
//
// Version 1 covers the operations a typical UXP plugin performs on documents
// and layers. batchPlay descriptors and UXP HTML panels are NOT part of v1.

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define COMPOSITOR_PLUGIN_ABI_VERSION 1u

typedef enum compositor_result {
    COMPOSITOR_OK = 0,
    COMPOSITOR_ERROR_UNKNOWN = 1,
    COMPOSITOR_ERROR_INVALID_ARGUMENT = 2,
    COMPOSITOR_ERROR_NO_DOCUMENT = 3,
    COMPOSITOR_ERROR_NOT_FOUND = 4,
    COMPOSITOR_ERROR_NOT_SUPPORTED = 5,
    COMPOSITOR_ERROR_OUT_OF_MEMORY = 6,
    COMPOSITOR_ERROR_BUSY = 7,
} compositor_result;

typedef struct compositor_plugin_info {
    uint32_t abi_version;      // Must equal COMPOSITOR_PLUGIN_ABI_VERSION.
    const char* id;            // Stable plugin id, e.g. "com.example.watermark".
    const char* name;          // Display name.
    const char* version;       // Plugin version string.
    const char* vendor;
    const char* description;
} compositor_plugin_info;

// One layer as seen by a plugin. Pointers are owned by the host and stay valid
// only for the duration of the callback that produced them.
typedef struct compositor_layer_info {
    const char* id;
    const char* name;
    int visible;
    int is_group;
    int has_mask;
    int blend_mode;            // Index into the host's blend mode list.
    double opacity;            // 0..1
    int32_t parent_index;      // -1 for top level.
} compositor_layer_info;

// Document summary.
typedef struct compositor_document_info {
    int32_t width;
    int32_t height;
    double resolution;
    int32_t layer_count;
    int32_t active_layer_index; // -1 when there is no active layer.
    const char* title;
} compositor_document_info;

// RGBA colour in 0..255.
typedef struct compositor_color {
    uint8_t r, g, b, a;
} compositor_color;

// Exported function table the plugin fills in.
typedef struct compositor_plugin_api {
    void* user_data;

    // Fills in plugin metadata. Must set abi_version to COMPOSITOR_PLUGIN_ABI_VERSION.
    compositor_result (*get_info)(void* user_data, compositor_plugin_info* out);

    // Called once after the plugin is loaded.
    compositor_result (*create)(void* user_data);

    // Called before the library is unloaded. Release plugin-owned memory here.
    void (*destroy)(void* user_data);

    // Plugin entry point: invoked from the host's Plugins menu.
    compositor_result (*run)(void* user_data);
} compositor_plugin_api;

// Host services. The host owns this table and guarantees its lifetime spans the
// plugin's loaded period.
typedef struct compositor_host_api {
    void* host_data;
    uint32_t abi_version;

    // --- Document queries ---------------------------------------------------
    compositor_result (*get_document_info)(void* host_data, compositor_document_info* out);
    // Copies the layer at `index` into `out`. `out->id` and `out->name` point at
    // host-owned storage valid until the next host call.
    compositor_result (*get_layer_info)(void* host_data, int32_t index, compositor_layer_info* out);
    int32_t (*get_layer_count)(void* host_data);

    // --- Layer mutation -----------------------------------------------------
    compositor_result (*set_layer_opacity)(void* host_data, int32_t index, double opacity);
    compositor_result (*set_layer_visible)(void* host_data, int32_t index, int visible);
    compositor_result (*set_layer_blend_mode)(void* host_data, int32_t index, int blend_mode);
    // Adds an empty raster layer above the active one; `out_index` receives its index.
    compositor_result (*create_layer)(void* host_data, const char* name, int32_t* out_index);
    compositor_result (*delete_layer)(void* host_data, int32_t index);

    // --- Pixels -------------------------------------------------------------
    // Reads a layer's pixels as RGBA8. Caller frees `*out_pixels` with free_pixels.
    compositor_result (*read_layer_pixels)(void* host_data, int32_t index,
                                          uint8_t** out_pixels, int32_t* out_width, int32_t* out_height);
    // Writes RGBA8 pixels into a layer. Returns NOT_SUPPORTED for groups/masks.
    compositor_result (*write_layer_pixels)(void* host_data, int32_t index,
                                            const uint8_t* pixels, int32_t width, int32_t height);
    void (*free_pixels)(void* host_data, uint8_t* pixels);

    // --- Host commands ------------------------------------------------------
    // Runs a host command by its command catalog id (e.g. "edit.undo").
    compositor_result (*run_command)(void* host_data, const char* command_id);

    // --- Logging ------------------------------------------------------------
    void (*log)(void* host_data, const char* message);
} compositor_host_api;

// The single symbol every plugin library must export.
typedef const compositor_plugin_api* (*compositor_plugin_entry_fn)(const compositor_host_api* host);
void* COMPOSITOR_PLUGIN_ENTRY(void);  // Do not call; documented name only.

#define COMPOSITOR_PLUGIN_ENTRY_NAME "CompositorPluginEntry"

#ifdef __cplusplus
}
#endif
