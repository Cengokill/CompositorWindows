#pragma once
#include "compositor_plugin.h"
#include <QString>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace compositor { struct Document; struct Layer; }

namespace compositor::plugin {

// Bridges the C plugin API to the live editor.
//
// The editor installs callbacks here once. Every plugin mutation goes through the
// editor's own edit path, so plugin changes are undoable and refresh the UI the
// same way a menu command does.
class PluginRuntime {
public:
    using EditOperation = std::function<void(compositor::Document&)>;
    using EditInvoker = std::function<bool(const char* name, const EditOperation&)>;
    using DocumentProvider = std::function<compositor::Document*()>;
    using ActiveLayerProvider = std::function<int32_t()>;
    using CommandInvoker = std::function<compositor_result(const QString& commandId)>;
    using LogSink = std::function<void(const QString& message)>;

    void setDocumentProvider(DocumentProvider provider) { document_ = std::move(provider); }
    void setActiveLayerProvider(ActiveLayerProvider provider) { activeLayer_ = std::move(provider); }
    void setEditInvoker(EditInvoker invoker) { edit_ = std::move(invoker); }
    void setCommandInvoker(CommandInvoker invoker) { runCommand_ = std::move(invoker); }
    void setLogSink(LogSink sink) { log_ = std::move(sink); }
    void setTitleProvider(std::function<QString()> provider) { title_ = std::move(provider); }

    compositor::Document* document() const { return document_ ? document_() : nullptr; }
    int32_t activeLayerIndex() const { return activeLayer_ ? activeLayer_() : -1; }
    std::string documentTitle() { return title_ ? title_().toStdString() : std::string("Compositor"); }
    void log(const QString& message) { if (log_) log_(message); }
    compositor_result runCommand(const QString& id) {
        return runCommand_ ? runCommand_(id) : COMPOSITOR_ERROR_NOT_SUPPORTED;
    }

    // Interned UTF-8 strings handed to plugins as const char*. They stay valid until
    // the plugin's current call returns, which is the documented contract.
    const char* intern(const std::string& value) {
        strings_.push_back(value);
        return strings_.back().c_str();
    }
    void resetInterns() { strings_.clear(); }

    compositor_result editLayer(const char* name, int32_t index,
                                const std::function<void(compositor::Layer&)>& mutate);
    compositor_result createLayer(const QString& name, int32_t* outIndex);
    compositor_result deleteLayer(int32_t index);
    compositor_result readLayerPixels(int32_t index, uint8_t** pixels, int32_t* width, int32_t* height);
    compositor_result writeLayerPixels(int32_t index, const uint8_t* pixels, int32_t width, int32_t height);

private:
    DocumentProvider document_;
    ActiveLayerProvider activeLayer_;
    EditInvoker edit_;
    CommandInvoker runCommand_;
    LogSink log_;
    std::function<QString()> title_;
    std::vector<std::string> strings_;
};

// Fills a host table that points at `runtime`.
void installHostApi(compositor_host_api* api, PluginRuntime* runtime);

}  // namespace compositor::plugin
