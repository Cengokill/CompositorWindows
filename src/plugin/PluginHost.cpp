#include "PluginHost.h"
#include "PluginRuntime.h"
#include "UxpNativeHost.h"
#include "core/Document.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace compositor::plugin {
namespace {

#ifdef _WIN32
void* openLibrary(const QString& path) {
    return reinterpret_cast<void*>(::LoadLibraryW(reinterpret_cast<LPCWSTR>(path.utf16())));
}
void closeLibrary(void* library) {
    if (library) ::FreeLibrary(reinterpret_cast<HMODULE>(library));
}
compositor_plugin_entry_fn resolveEntry(void* library) {
    if (!library) return nullptr;
    return reinterpret_cast<compositor_plugin_entry_fn>(
        ::GetProcAddress(reinterpret_cast<HMODULE>(library), COMPOSITOR_PLUGIN_ENTRY_NAME));
}
#else
void* openLibrary(const QString&) { return nullptr; }
void closeLibrary(void*) {}
compositor_plugin_entry_fn resolveEntry(void*) { return nullptr; }
#endif

}  // namespace

struct PluginHost::HostState {
    compositor_host_api api{};
    PluginRuntime runtime;
    std::unique_ptr<UxpNativeHost> uxp;
};

PluginHost::PluginHost() : state_(std::make_unique<HostState>()) {
    installHostApi(&state_->api, &state_->runtime);
    state_->uxp = std::make_unique<UxpNativeHost>(&state_->runtime);
}
PluginHost::~PluginHost() {
    for (auto& plugin : plugins_) {
        if (plugin.api && plugin.api->destroy) plugin.api->destroy(plugin.api->user_data);
        closeLibrary(plugin.library);
    }
    plugins_.clear();
}

PluginRuntime& PluginHost::runtime() { return state_->runtime; }

UxpNativeHost& PluginHost::uxp() { return *state_->uxp; }

int PluginHost::loadUxpDirectory(const QString& directory, QStringList* errors) {
    return state_->uxp->scan(directory, errors);
}

QString defaultPluginDirectory() {
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("plugins"));
}

int PluginHost::scanDirectories(const QStringList& directories) {
    int loaded = 0;
    for (const auto& directory : directories) {
        QDir dir(directory);
        if (!dir.exists()) continue;
        const auto entries = dir.entryInfoList(QStringList{QStringLiteral("*.dll")}, QDir::Files);
        for (const auto& entry : entries) {
            if (loadLibrary(entry.absoluteFilePath())) ++loaded;
        }
    }
    return loaded;
}

bool PluginHost::loadLibrary(const QString& path) {
    lastError_.clear();
    void* library = openLibrary(path);
    if (!library) {
        lastError_ = QStringLiteral("无法加载 %1").arg(path);
        return false;
    }
    const auto entry = resolveEntry(library);
    if (!entry) {
        lastError_ = QStringLiteral("%1 未导出 %2")
                         .arg(path, QString::fromLatin1(COMPOSITOR_PLUGIN_ENTRY_NAME));
        closeLibrary(library);
        return false;
    }
    const compositor_plugin_api* api = entry(&state_->api);
    if (!api) {
        lastError_ = QStringLiteral("%1 未返回插件表").arg(path);
        closeLibrary(library);
        return false;
    }
    compositor_plugin_info info{};
    if (api->get_info && api->get_info(api->user_data, &info) == COMPOSITOR_OK) {
        if (info.abi_version != COMPOSITOR_PLUGIN_ABI_VERSION) {
            lastError_ = QStringLiteral("%1 需要 ABI %2，宿主为 %3")
                             .arg(path)
                             .arg(info.abi_version)
                             .arg(COMPOSITOR_PLUGIN_ABI_VERSION);
            closeLibrary(library);
            return false;
        }
    }
    LoadedPlugin plugin;
    plugin.path = path;
    plugin.id = QString::fromUtf8(info.id ? info.id : "");
    plugin.name = QString::fromUtf8(info.name ? info.name : "");
    plugin.version = QString::fromUtf8(info.version ? info.version : "");
    plugin.vendor = QString::fromUtf8(info.vendor ? info.vendor : "");
    plugin.description = QString::fromUtf8(info.description ? info.description : "");
    plugin.library = library;
    plugin.api = api;
    if (api->create) api->create(api->user_data);
    plugins_.push_back(std::move(plugin));
    return true;
}

compositor_result PluginHost::run(const QString& id) {
    for (const auto& plugin : plugins_) {
        if (plugin.id == id) {
            if (!plugin.api || !plugin.api->run) return COMPOSITOR_ERROR_NOT_SUPPORTED;
            // Interned strings handed to the plugin belong to one call.
            state_->runtime.resetInterns();
            const auto result = plugin.api->run(plugin.api->user_data);
            state_->runtime.resetInterns();
            return result;
        }
    }
    lastError_ = QStringLiteral("未找到插件 %1").arg(id);
    return COMPOSITOR_ERROR_NOT_FOUND;
}

}  // namespace compositor::plugin
