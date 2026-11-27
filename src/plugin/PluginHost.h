#pragma once
#include "compositor_plugin.h"
#include <QString>
#include <QStringList>
#include <memory>
#include <vector>

namespace compositor::plugin {

class PluginRuntime;
class UxpNativeHost;
struct UxpPlugin;

// One loaded plugin library.
struct LoadedPlugin {
    QString path;
    QString id;
    QString name;
    QString version;
    QString vendor;
    QString description;
    void* library{};
    const compositor_plugin_api* api{};
};

// Loads plugin libraries from the given directories and keeps them alive.
//
// A plugin directory holds one shared library named `*.compositor-plugin.dll`
// (or `plugin.dll`). Loading never throws: a broken library is recorded in
// `lastError` and skipped so one bad plugin cannot prevent startup.
class PluginHost {
public:
    PluginHost();
    ~PluginHost();
    PluginHost(const PluginHost&) = delete;
    PluginHost& operator=(const PluginHost&) = delete;

    // Scans each directory for plugin libraries and returns how many loaded.
    int scanDirectories(const QStringList& directories);

    // Loads a single library. Returns false and sets lastError() on failure.
    bool loadLibrary(const QString& path);

    const std::vector<LoadedPlugin>& plugins() const { return plugins_; }
    QString lastError() const { return lastError_; }

    // The runtime the editor binds its document and edit callbacks to.
    PluginRuntime& runtime();

    // Native UXP hosting: plugins run unmodified in the embedded JavaScript engine.
    UxpNativeHost& uxp();
    int loadUxpDirectory(const QString& directory, QStringList* errors);

    // Runs a plugin's entry point by id. The host guarantees a document is open.
    compositor_result run(const QString& id);

private:
    // Installs the host callbacks the loaded plugin will use.
    void bindDocumentAccessor();

    std::vector<LoadedPlugin> plugins_;
    QString lastError_;
    struct HostState;
    std::unique_ptr<HostState> state_;
};

// Directory next to the executable that plugins are discovered in.
QString defaultPluginDirectory();

}  // namespace compositor::plugin
