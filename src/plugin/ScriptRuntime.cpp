#include "ScriptRuntime.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

namespace compositor::plugin {
namespace {

// The engine is a plain JavaScript program: the shim, the snapshot, the plugin and a
// small epilogue that prints what the plugin recorded.
QString buildProgram(const QString& shim, const QString& snapshot, const QString& plugin,
                     const QString& entryPath) {
    QString program;
    program += QStringLiteral("var __compositorSnapshot = (%1);\n")
                   .arg(snapshot.isEmpty() ? QStringLiteral("{}") : snapshot);
    program += QStringLiteral("var __compositorMutations = [];\n");
    // The plugin's console output is discarded: it cannot be shown while the editor
    // is running another process.
    program += QStringLiteral(
        "var console = { log: function(){}, info: function(){}, warn: function(){}, error: function(){} };\n");
    // Node's CommonJS wrapper binds a local `require` that hides the shim's version,
    // so route lookups through the global the shim installs.
    program += QStringLiteral("var require = function(name){ return globalThis.require ? globalThis.require(name) : {}; };\n");    program += shim;
    // The plugin is evaluated at module scope so its top-level declarations, such as
    // `function run()`, stay visible to the entry call below.
    program += QStringLiteral("\n");
    program += plugin;
    program += QStringLiteral("\n");
    // Entry points: a global run(), an exported run, or onLoad().
    program += QStringLiteral(
        "try{ if(typeof run==='function'){ run(); }"
        " else if(typeof module!=='undefined'&&module.exports&&typeof module.exports.run==='function'){ module.exports.run(); }"
        " else if(typeof onLoad==='function'){ onLoad(); } }"
        "catch(e){ process.stderr.write(String(e && e.stack || e) + '\\n'); process.exit(3); }\n");
    program += QStringLiteral("process.stdout.write('__MUTATIONS__'+JSON.stringify(__compositorMutations)+'\\n');\n");
    Q_UNUSED(entryPath);
    return program;
}

}  // namespace

QString ScriptRuntime::findEngine() {
    const QString fromPath = QStandardPaths::findExecutable(QStringLiteral("node"));
    if (!fromPath.isEmpty()) return fromPath;
    const QStringList candidates{
        QStringLiteral("C:/Program Files/nodejs/node.exe"),
        QStringLiteral("C:/Program Files (x86)/nodejs/node.exe"),
    };
    for (const auto& candidate : candidates) {
        if (QFileInfo::exists(candidate)) return candidate;
    }
    return {};
}

ScriptRuntime::Result ScriptRuntime::runBundle(const QString& shimSource, const QString& snapshotJson,
                                               const QString& pluginSource, const QString& entryPath,
                                               int timeoutMs) {
    Result result;
    const QString engine = findEngine();
    if (engine.isEmpty()) {
        result.error = QStringLiteral(
            "未找到可用的 JavaScript 引擎（node.exe），无法运行 UXP 插件。原生 C 插件不受影响。");
        return result;
    }

    QTemporaryDir workspace;
    if (!workspace.isValid()) {
        result.error = QStringLiteral("无法创建临时目录。");
        return result;
    }
    const QString scriptPath = QDir(workspace.path()).filePath(QStringLiteral("plugin-run.js"));
    QFile script(scriptPath);
    if (!script.open(QIODevice::WriteOnly | QIODevice::Text)) {
        result.error = QStringLiteral("无法写入运行脚本。");
        return result;
    }
    script.write(buildProgram(shimSource, snapshotJson, pluginSource, entryPath).toUtf8());
    script.close();

    QProcess process;
    process.setWorkingDirectory(workspace.path());
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(engine, {QStringLiteral("--no-warnings"), scriptPath});
    if (!process.waitForStarted(10000)) {
        result.error = QStringLiteral("无法启动 JavaScript 引擎：%1").arg(engine);
        return result;
    }
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(2000);
        result.error = QStringLiteral("插件执行超时（%1 秒）。").arg(timeoutMs / 1000);
        return result;
    }
    result.output = QString::fromUtf8(process.readAll());
    if (process.exitCode() != 0) {
        result.error = result.output.trimmed().isEmpty()
                           ? QStringLiteral("插件执行失败（退出码 %1）。").arg(process.exitCode())
                           : QStringLiteral("插件执行失败：%1").arg(result.output.trimmed());
        return result;
    }
    // The epilogue prints one marker line; everything before it is plugin output.
    const int marker = result.output.lastIndexOf(QStringLiteral("__MUTATIONS__"));
    if (marker < 0) {
        result.error = QStringLiteral("插件未返回变更记录。");
        return result;
    }
    const QString tail = result.output.mid(marker + int(QStringLiteral("__MUTATIONS__").size()));
    result.mutationsJson = tail.section(QLatin1Char('\n'), 0, 0).trimmed();
    if (result.mutationsJson.isEmpty()) result.mutationsJson = QStringLiteral("[]");
    result.ok = true;
    return result;
}

}  // namespace compositor::plugin
