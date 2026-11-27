#pragma once
#include <QString>
#include <QStringList>
#include <memory>

namespace compositor::plugin {

// Wrapper over the JavaScript engine that ships with Windows (Chakra).
//
// Design note: the host never registers a native callback. Chakra's native
// function ABI differs between engine builds and a wrong guess crashes the
// process, so all host data is injected as JSON globals before the script runs
// and the script records its edits in a JSON array the host reads afterwards.
class ScriptRuntime {
public:
    ScriptRuntime();
    ~ScriptRuntime();
    ScriptRuntime(const ScriptRuntime&) = delete;
    ScriptRuntime& operator=(const ScriptRuntime&) = delete;

    // Creates the engine context on first use.
    bool available(QString& error);

    // Sets a global whose value is the given JSON text, parsed by the engine.
    bool setGlobalJson(const QString& name, const QString& json, QString& error);

    // Evaluates a script. `sourceName` appears in engine error messages.
    bool evaluate(const QString& source, const QString& sourceName, QString& error);

    // Evaluates an expression and returns its string form.
    bool evaluateToString(const QString& expression, QString& out, QString& error);

    // True when the global exists and is a function.
    bool hasGlobalFunction(const QString& name);
    bool callGlobal(const QString& functionName, QString& error);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace compositor::plugin
