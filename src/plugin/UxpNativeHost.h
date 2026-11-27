#pragma once
#include "compositor_plugin.h"
#include <QString>
#include <QStringList>
#include <memory>
#include <vector>

namespace compositor::plugin {

class PluginRuntime;

// One Photoshop UXP plugin loaded as-is (no translation).
struct UxpPlugin {
    QString root;
    QString id;
    QString name;
    QString version;
    QString description;
    QString entryPath;
    QString source;
};

// Hosts Photoshop UXP plugins natively.
//
// The plugin's JavaScript runs unmodified in the embedded engine. Because Chakra's
// native-callback ABI is not portable across engine builds, the host injects a
// document snapshot instead of calling back into C++: reads come from the
// snapshot, and the plugin's writes are recorded in JavaScript. After the script
// finishes, the host applies those recorded edits through the editor's undoable
// edit path. A plugin therefore sees a consistent snapshot at the moment it runs.
class UxpNativeHost {
public:
    explicit UxpNativeHost(PluginRuntime* runtime);
    ~UxpNativeHost();

    int scan(const QString& directory, QStringList* errors);
    static bool discover(const QString& directory, UxpPlugin& plugin, QString& error);

    compositor_result run(const UxpPlugin& plugin, QString& error);

    const std::vector<UxpPlugin>& plugins() const { return plugins_; }

    // JSON describing the current document, used for the injected snapshot.
    QString snapshotJson() const;
    // Applies the mutation list a finished script produced. Returns applied count.
    int applyMutations(const QString& mutationsJson, QString& error);

private:

    PluginRuntime* runtime_{};
    std::vector<UxpPlugin> plugins_;
};

// The JavaScript that implements the UXP surface over the injected snapshot.
QString uxpShimSource();
QString uxpSnapshotGlobal();
QString uxpMutationsGlobal();

}  // namespace compositor::plugin
