#pragma once
#include <QString>

namespace compositor::plugin {

// Runs a UXP plugin's JavaScript in an external JavaScript engine process.
//
// The engine is a separate process because the JavaScript engines that could be
// embedded here are not usable on this build: the Windows Chakra DLL lacks the JsRT
// entry points this build needs, and a vendored QuickJS still requires its code
// generation step. Running the script through a Node.js executable that is already
// on the machine keeps the plugin's own code, the photoshop shim and the recorded
// mutations identical, and a plugin crash cannot take the editor down.
//
// The host passes a document snapshot in and receives the plugin's recorded
// mutations out, so no callback crosses the process boundary.
class ScriptRuntime {
public:
    struct Result {
        bool ok{};
        QString mutationsJson;  // JSON array the plugin recorded.
        QString output;         // Engine stdout/stderr, for diagnostics.
        QString error;
    };

    // Locates the JavaScript engine. Empty when none is installed.
    static QString findEngine();

    // Runs one bundle: shim, snapshot, plugin source, entry invocation, then prints
    // the recorded mutations as the last stdout line.
    static Result runBundle(const QString& shimSource, const QString& snapshotJson,
                            const QString& pluginSource, const QString& entryPath,
                            int timeoutMs = 60000);
};

}  // namespace compositor::plugin
