#include "UxpTranslator.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryFile>

namespace compositor::plugin {
namespace {

struct ApiMapping {
    const char* pattern;
    const char* sdkCall;
};

// Mirrors tools/uxp-translate/uxp_translate.py so the in-app importer and the
// standalone script report the same results.
constexpr ApiMapping kMappings[] = {
    {R"(app\.activeDocument\b)", "get_document_info()"},
    {R"(\.width\b)", "document_info.width"},
    {R"(\.height\b)", "document_info.height"},
    {R"(\.resolution\b)", "document_info.resolution"},
    {R"(\.layers\.length\b)", "host->get_layer_count(host->host_data)"},
    {R"(app\.activeDocument\.activeLayers\b)", "active layer index + get_layer_info()"},
    {R"(\.opacity\b)", "layer.opacity"},
    {R"(\.visible\b)", "layer.visible"},
    {R"(\.blendMode\b)", "layer.blend_mode"},
    {R"(\.name\b)", "layer.name"},
    {R"(\bcreateLayer\b|\baddLayer\b)", "host->create_layer()"},
    {R"(\bdeleteLayer\b|\bremoveLayer\b)", "host->delete_layer()"},
    {R"(\bexecuteAsModal\b)", "ignored: native edits are already synchronous"},
    {R"(\bconsole\.(log|warn|error)\b)", "host->log()"},
};

struct UnsupportedPattern {
    const char* pattern;
    const char* description;
};

constexpr UnsupportedPattern kUnsupported[] = {
    {R"(\bbatchPlay\b)", "batchPlay 动作描述符系统（PS 专有，数百条 descriptor）"},
    {R"(\brequire\(\s*['"]photoshop['"]\s*\))", "photoshop 宿主模块（UXP API 整体）"},
    {R"(\brequire\(\s*['"]uxp['"]\s*\))", "uxp 模块（宿主 UI / 存储 / 网络）"},
    {R"(\bentrypoints\.[\w.]+\b)", "UXP 面板入口（HTML/CSS 界面）"},
    {R"(\bselection\b)", "选区 API（v1 SDK 未暴露）"},
    {R"(\bsmartObject\b)", "智能对象"},
    {R"(\btimeline\b|\bvideo\b)", "视频与时间轴"},
};

QString cIdentifier(const QString& value) {
    QString cleaned;
    for (const QChar ch : value) {
        cleaned.append(ch.isLetterOrNumber() || ch == QLatin1Char('_') ? ch : QLatin1Char('_'));
    }
    if (cleaned.isEmpty() || cleaned.at(0).isDigit()) cleaned.prepend(QStringLiteral("p_"));
    return cleaned;
}

QString readText(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(file.readAll());
}

}  // namespace

bool UxpTranslator::analyse(const QString& pluginDirectory, UxpImportResult& result, QString& error) {
    result = UxpImportResult{};
    QDir dir(pluginDirectory);
    if (!dir.exists()) {
        error = QStringLiteral("目录不存在：%1").arg(pluginDirectory);
        return false;
    }

    // A user often picks the wrapper folder that holds the real plugin, so look for
    // the manifest at a shallow depth and analyse from wherever it lives.
    if (!QFileInfo::exists(dir.filePath(QStringLiteral("manifest.json")))) {
        QDirIterator it(pluginDirectory, QStringList{QStringLiteral("manifest.json")}, QDir::Files,
                        QDirIterator::Subdirectories);
        QString best;
        int bestDepth = 99;
        while (it.hasNext()) {
            const QString candidate = it.next();
            const int depth = QDir(pluginDirectory).relativeFilePath(QFileInfo(candidate).absolutePath())
                                  .count(QLatin1Char('/')) + 1;
            if (depth < bestDepth) {
                best = candidate;
                bestDepth = depth;
            }
        }
        if (!best.isEmpty() && bestDepth <= 3) {
            dir.setPath(QFileInfo(best).absolutePath());
        }
    }

    QJsonObject manifest;
    const QString manifestPath = dir.filePath(QStringLiteral("manifest.json"));
    if (QFileInfo::exists(manifestPath)) {
        QJsonParseError parseError{};
        const auto document = QJsonDocument::fromJson(readText(manifestPath).toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            result.manifestNotes.append(QStringLiteral("manifest.json 解析失败：%1").arg(parseError.errorString()));
        } else {
            manifest = document.object();
        }
    } else {
        result.manifestNotes.append(QStringLiteral("未找到 manifest.json，按无清单处理。"));
    }

    result.name = manifest.value(QStringLiteral("name")).toString(dir.dirName());
    result.version = manifest.value(QStringLiteral("version")).toString(QStringLiteral("1.0.0"));
    QString rawId = manifest.value(QStringLiteral("id")).toString(dir.dirName());
    result.id = cIdentifier(rawId);

    // Host check: a plugin built for InDesign will not map cleanly.
    const auto hosts = manifest.value(QStringLiteral("host")).toArray();
    QStringList hostNames;
    for (const auto& entry : hosts) {
        hostNames.append(entry.toObject().value(QStringLiteral("app")).toString());
    }
    if (!hostNames.isEmpty() && !hostNames.contains(QStringLiteral("PS"))) {
        result.manifestNotes.append(
            QStringLiteral("目标宿主为 %1，不是 Photoshop，转译结果仅供参考。").arg(hostNames.join(QLatin1Char(','))));
    }

    // Entry point: manifest.main may be a script or an HTML panel that loads one.
    QString entry = manifest.value(QStringLiteral("main")).toString();
    if (!entry.isEmpty() && !QFileInfo::exists(dir.filePath(entry))) entry.clear();

    // A UXP panel declares its script in the HTML; follow that reference first.
    if (!entry.isEmpty() && entry.endsWith(QStringLiteral(".html"), Qt::CaseInsensitive)) {
        const QString html = readText(dir.filePath(entry));
        const QRegularExpression scriptRef(QStringLiteral(R"(<script[^>]+src\s*=\s*["']([^"']+)["'])"),
                                           QRegularExpression::CaseInsensitiveOption);
        const auto match = scriptRef.match(html);
        if (match.hasMatch()) {
            const QString referenced = QFileInfo(dir.filePath(entry)).absoluteDir().filePath(match.captured(1));
            if (QFileInfo::exists(referenced)) {
                entry = dir.relativeFilePath(referenced);
            } else {
                entry.clear();
            }
        } else {
            entry.clear();
        }
    }

    if (entry.isEmpty()) {
        for (const char* name : {"index.js", "main.js", "plugin.js"}) {
            if (QFileInfo::exists(dir.filePath(QString::fromLatin1(name)))) { entry = QString::fromLatin1(name); break; }
        }
    }
    if (entry.isEmpty()) {
        // Search the tree, shallowest first, skipping vendored dependencies.
        QDirIterator it(dir.absolutePath(), QStringList{QStringLiteral("*.js")}, QDir::Files,
                        QDirIterator::Subdirectories);
        QString best;
        int bestDepth = 99;
        while (it.hasNext()) {
            const QString candidate = it.next();
            if (candidate.contains(QStringLiteral("/node_modules/"))) continue;
            const int depth = dir.relativeFilePath(candidate).count(QLatin1Char('/'));
            if (depth < bestDepth) { best = candidate; bestDepth = depth; }
        }
        if (!best.isEmpty()) entry = dir.relativeFilePath(best);
    }
    if (entry.isEmpty()) {
        result.manifestNotes.append(QStringLiteral("未找到 JavaScript 入口，只生成空骨架。"));
    }
    result.entryFile = entry;
    const QString source = entry.isEmpty() ? QString() : readText(dir.filePath(entry));

    for (const auto& mapping : kMappings) {
        const QRegularExpression expression(QString::fromLatin1(mapping.pattern));
        const auto match = expression.match(source);
        if (match.hasMatch()) {
            result.mapped.append({match.captured(0), QString::fromLatin1(mapping.sdkCall)});
        }
    }
    for (const auto& entry : kUnsupported) {
        if (QRegularExpression(QString::fromLatin1(entry.pattern)).match(source).hasMatch()) {
            const QString description = QString::fromUtf8(entry.description);
            if (!result.unsupported.contains(description)) result.unsupported.append(description);
        }
    }

    const QString safeName = result.name.isEmpty() ? result.id : result.name;
    QString notes;
    for (const auto& item : result.unsupported) notes += QStringLiteral("//   - %1\n").arg(item);
    if (notes.isEmpty()) notes = QStringLiteral("//   （未发现）\n");

    result.generatedSource = QStringLiteral(
        "// Generated by Compositor's UXP importer from \"%1\".\n"
        "// Unsupported capabilities detected in the original plugin:\n"
        "%2"
        "#include \"compositor_plugin.h\"\n\n"
        "static const compositor_host_api* g_host;\n\n"
        "static compositor_result %3_get_info(void* user, compositor_plugin_info* out) {\n"
        "    (void)user;\n"
        "    out->abi_version = COMPOSITOR_PLUGIN_ABI_VERSION;\n"
        "    out->id = \"%4\";\n"
        "    out->name = \"%5\";\n"
        "    out->version = \"%6\";\n"
        "    out->vendor = \"Imported from UXP\";\n"
        "    out->description = \"%5\";\n"
        "    return COMPOSITOR_OK;\n"
        "}\n\n"
        "static compositor_result %3_create(void* user) { (void)user; return COMPOSITOR_OK; }\n"
        "static void %3_destroy(void* user) { (void)user; }\n\n"
        "static compositor_result %3_run(void* user) {\n"
        "    (void)user;\n"
        "    if (!g_host) return COMPOSITOR_ERROR_UNKNOWN;\n"
        "    compositor_document_info document_info;\n"
        "    if (g_host->get_document_info(g_host->host_data, &document_info) != COMPOSITOR_OK) {\n"
        "        g_host->log(g_host->host_data, \"no document open\");\n"
        "        return COMPOSITOR_ERROR_NO_DOCUMENT;\n"
        "    }\n"
        "    // TODO: port the original plugin logic here.\n"
        "    g_host->log(g_host->host_data, \"%5 ran\");\n"
        "    return COMPOSITOR_OK;\n"
        "}\n\n"
        "static const compositor_plugin_api g_api = {\n"
        "    0, %3_get_info, %3_create, %3_destroy, %3_run,\n"
        "};\n\n"
        "__declspec(dllexport) const compositor_plugin_api*\n"
        "CompositorPluginEntry(const compositor_host_api* host) {\n"
        "    g_host = host;\n"
        "    return &g_api;\n"
        "}\n")
        .arg(entry.isEmpty() ? QStringLiteral("(no entry)") : entry, notes,
             cIdentifier(result.id), result.id,
             QString(safeName).replace(QLatin1Char('"'), QLatin1Char('\'')), result.version);
    result.analysed = true;
    return true;
}

bool UxpTranslator::writeFiles(const UxpImportResult& result, const QString& outputDirectory, QString& error) {
    QDir dir(outputDirectory);
    if (!dir.exists() && !QDir().mkpath(outputDirectory)) {
        error = QStringLiteral("无法创建输出目录：%1").arg(outputDirectory);
        return false;
    }
    QFile source(dir.filePath(QStringLiteral("plugin.c")));
    if (!source.open(QIODevice::WriteOnly | QIODevice::Text)) {
        error = QStringLiteral("无法写入 plugin.c");
        return false;
    }
    source.write(result.generatedSource.toUtf8());
    source.close();

    QString report = QStringLiteral("UXP 转译报告：%1\n").arg(result.id);
    report += QStringLiteral("========================================\n\n【可自动映射】\n");
    if (result.mapped.isEmpty()) report += QStringLiteral("  （未发现）\n");
    for (const auto& mapping : result.mapped) {
        report += QStringLiteral("  %1\n      -> %2\n").arg(mapping.pattern, mapping.sdkCall);
    }
    report += QStringLiteral("\n【无法自动翻译，需人工移植】\n");
    if (result.unsupported.isEmpty()) report += QStringLiteral("  （未发现）\n");
    for (const auto& item : result.unsupported) report += QStringLiteral("  - %1\n").arg(item);
    if (!result.manifestNotes.isEmpty()) {
        report += QStringLiteral("\n【清单说明】\n");
        for (const auto& note : result.manifestNotes) report += QStringLiteral("  - %1\n").arg(note);
    }
    report += QStringLiteral("\n注意：本工具只做静态翻译，不保证移植后的插件与 Photoshop 行为一致。\n");

    QFile reportFile(dir.filePath(QStringLiteral("report.txt")));
    if (reportFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        reportFile.write(report.toUtf8());
    }
    return true;
}

QString UxpTranslator::findCompiler() {
    // vswhere reports the Build Tools installation; cl.exe lives under it.
    const QString vswhere = QStringLiteral(
        "C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe");
    if (!QFileInfo::exists(vswhere)) return {};
    QProcess process;
    process.start(vswhere, {QStringLiteral("-latest"), QStringLiteral("-products"),
                            QStringLiteral("*"), QStringLiteral("-property"),
                            QStringLiteral("installationPath")});
    if (!process.waitForFinished(10000)) return {};
    const QString root = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
    if (root.isEmpty()) return {};
    QDir tools(QDir(root).filePath(QStringLiteral("VC/Tools/MSVC")));
    for (const auto& version : tools.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed)) {
        const QString candidate = tools.filePath(version + QStringLiteral("/bin/Hostx64/x64/cl.exe"));
        if (QFileInfo::exists(candidate)) return candidate;
    }
    return {};
}

bool UxpTranslator::compile(const UxpImportResult& result, const QString& outputDirectory,
                            const QString& sdkIncludeDirectory, const QString& binaryPath, QString& error) {
    Q_UNUSED(result);
    const QString compiler = findCompiler();
    if (compiler.isEmpty()) {
        error = QStringLiteral("未检测到 Visual Studio C++ 编译器（cl.exe），无法编译。已保留 plugin.c 源码。");
        return false;
    }
    // cl.exe lives at <root>/VC/Tools/MSVC/<ver>/bin/Hostx64/x64/cl.exe; walk up to the
    // installation root so Community/Professional editions work, not just Build Tools.
    // Ascend until the directory that actually contains the environment script.
    QString vsRoot = QFileInfo(compiler).absolutePath();
    QString vcvars;
    for (int level = 0; level < 8 && vcvars.isEmpty(); ++level) {
        const QString candidate = QDir(vsRoot).filePath(QStringLiteral("VC/Auxiliary/Build/vcvars64.bat"));
        if (QFileInfo::exists(candidate)) { vcvars = candidate; break; }
        const QString parent = QFileInfo(vsRoot).absolutePath();
        if (parent == vsRoot) break;
        vsRoot = parent;
    }
    if (vcvars.isEmpty()) {
        const QString devCmd = QDir(vsRoot).filePath(QStringLiteral("Common7/Tools/VsDevCmd.bat"));
        if (QFileInfo::exists(devCmd)) vcvars = devCmd;
    }
    if (!QFileInfo::exists(vcvars)) {
        error = QStringLiteral("找到 cl.exe 但缺少 vcvars64.bat：%1").arg(QDir::toNativeSeparators(vsRoot));
        return false;
    }

    // Compile through a temporary batch file so paths with spaces stay intact.
    QTemporaryFile script(QDir(outputDirectory).filePath(QStringLiteral("build-XXXXXX.bat")));
    if (!script.open()) {
        error = QStringLiteral("无法创建临时编译脚本（目录不可写）：%1")
                    .arg(QDir::toNativeSeparators(outputDirectory));
        return false;
    }
    const QString include = QDir::toNativeSeparators(sdkIncludeDirectory);
    const QString source = QDir::toNativeSeparators(QDir(outputDirectory).filePath(QStringLiteral("plugin.c")));
    const QString outputDll = QDir::toNativeSeparators(binaryPath);
    QString command = QStringLiteral("@echo off\r\n");
    command += QStringLiteral("call \"%1\" >nul 2>&1\r\n").arg(QDir::toNativeSeparators(vcvars));
    command += QStringLiteral("cl /nologo /LD /utf-8 /I\"%1\" \"%2\" /link /OUT:\"%3\" 2>&1\r\n")
                   .arg(include, source, outputDll);
    command += QStringLiteral("exit /b %ERRORLEVEL%\r\n");
    script.write(command.toUtf8());
    script.flush();

    QProcess process;
    process.setWorkingDirectory(outputDirectory);
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(QStringLiteral("cmd.exe"), {QStringLiteral("/c"), QDir::toNativeSeparators(script.fileName())});
    if (!process.waitForFinished(180000)) {
        process.kill();
        error = QStringLiteral("编译超时（180 秒）。已保留 plugin.c 源码。");
        return false;
    }
    const QString buildLog = QString::fromLocal8Bit(process.readAll());
    if (!QFileInfo::exists(binaryPath)) {
        error = buildLog.trimmed().isEmpty()
                    ? QStringLiteral("编译未产出 DLL（退出码 %1）。已保留 plugin.c 源码。").arg(process.exitCode())
                    : QStringLiteral("编译失败（退出码 %1）：\n%2").arg(process.exitCode()).arg(buildLog.trimmed());
        return false;
    }
    Q_UNUSED(buildLog);
    // The link leaves intermediates beside the DLL; they are not part of the plugin.
    for (const char* leftover : {"plugin.obj", "plugin.lib", "plugin.exp"}) {
        QFile::remove(QDir(outputDirectory).filePath(QString::fromLatin1(leftover)));
    }
    return true;
}

}  // namespace compositor::plugin
