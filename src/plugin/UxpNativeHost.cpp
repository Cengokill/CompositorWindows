#include "UxpNativeHost.h"
#include "PluginRuntime.h"
#include "ScriptRuntime.h"
#include "core/Document.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace compositor::plugin {
namespace {

QString readText(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(file.readAll());
}

QString toJson(const QJsonObject& object) {
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

}  // namespace

QString uxpSnapshotGlobal() { return QStringLiteral("__compositorSnapshot"); }
QString uxpMutationsGlobal() { return QStringLiteral("__compositorMutations"); }

UxpNativeHost::UxpNativeHost(PluginRuntime* runtime) : runtime_(runtime) {}
UxpNativeHost::~UxpNativeHost() = default;

bool UxpNativeHost::discover(const QString& directory, UxpPlugin& plugin, QString& error) {
    plugin = UxpPlugin{};
    QString root = directory;
    if (!QFileInfo::exists(QDir(directory).filePath(QStringLiteral("manifest.json")))) {
        QDirIterator it(directory, QStringList{QStringLiteral("manifest.json")}, QDir::Files,
                        QDirIterator::Subdirectories);
        QString best;
        int bestDepth = 99;
        while (it.hasNext()) {
            const QString candidate = it.next();
            const int depth = QDir(directory).relativeFilePath(QFileInfo(candidate).absolutePath())
                                  .count(QLatin1Char('/')) + 1;
            if (depth < bestDepth) { best = candidate; bestDepth = depth; }
        }
        if (best.isEmpty()) {
            error = QStringLiteral("该文件夹里没有 manifest.json，不是 UXP 插件。");
            return false;
        }
        root = QFileInfo(best).absolutePath();
    }
    QDir dir(root);
    plugin.root = root;

    const auto manifest = QJsonDocument::fromJson(readText(dir.filePath(QStringLiteral("manifest.json"))).toUtf8()).object();
    plugin.name = manifest.value(QStringLiteral("name")).toString(dir.dirName());
    plugin.version = manifest.value(QStringLiteral("version")).toString(QStringLiteral("1.0.0"));
    QString id = manifest.value(QStringLiteral("id")).toString(dir.dirName());
    id.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")), QStringLiteral("-"));
    plugin.id = id;
    plugin.description = manifest.value(QStringLiteral("description")).toString();

    QString entry = manifest.value(QStringLiteral("main")).toString();
    if (!entry.isEmpty() && !QFileInfo::exists(dir.filePath(entry))) entry.clear();
    QStringList scripts;
    if (!entry.isEmpty() && entry.endsWith(QStringLiteral(".html"), Qt::CaseInsensitive)) {
        // A panel loads its code with several <script src> tags; the browser runs them
        // in document order in one global scope, so collect all of them.
        const QRegularExpression ref(QStringLiteral(R"(<script[^>]+src\s*=\s*["']([^"']+)["'])"),
                                     QRegularExpression::CaseInsensitiveOption);
        const QString html = readText(dir.filePath(entry));
        auto matches = ref.globalMatch(html);
        while (matches.hasNext()) {
            const QString candidate = QFileInfo(dir.filePath(matches.next().captured(1))).absoluteFilePath();
            if (QFileInfo::exists(candidate)) scripts.append(dir.relativeFilePath(candidate));
        }
        entry = scripts.isEmpty() ? QString() : scripts.first();
    }
    if (entry.isEmpty()) {
        for (const char* name : {"index.js", "main.js", "plugin.js"}) {
            if (QFileInfo::exists(dir.filePath(QString::fromLatin1(name)))) { entry = QString::fromLatin1(name); break; }
        }
    }
    if (entry.isEmpty()) {
        QDirIterator it(root, QStringList{QStringLiteral("*.js")}, QDir::Files, QDirIterator::Subdirectories);
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
        error = QStringLiteral("插件里没有找到可执行的 JavaScript。");
        return false;
    }
    plugin.entryPath = entry;
    plugin.scriptPaths = scripts.isEmpty() ? QStringList{entry} : scripts;
    // Concatenate in document order: each file is a separate <script> in the panel,
    // and they share one global scope.
    QString combined;
    for (const auto& script : plugin.scriptPaths) {
        combined += QStringLiteral("\n// ---- %1 ----\n").arg(script);
        combined += readText(dir.filePath(script));
        combined += QStringLiteral("\n;\n");
    }
    plugin.source = combined;
    if (plugin.source.isEmpty()) {
        error = QStringLiteral("入口脚本为空：%1").arg(entry);
        return false;
    }
    error.clear();
    return true;
}

int UxpNativeHost::scan(const QString& directory, QStringList* errors) {
    plugins_.clear();
    QDir root(directory);
    if (!root.exists()) return 0;
    for (const auto& entry : root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        UxpPlugin plugin;
        QString error;
        if (discover(entry.absoluteFilePath(), plugin, error)) {
            plugins_.push_back(std::move(plugin));
        } else if (errors) {
            errors->append(QStringLiteral("%1：%2").arg(entry.fileName(), error));
        }
    }
    return int(plugins_.size());
}

QString UxpNativeHost::snapshotJson() const {
    QJsonObject snapshot;
    auto* doc = runtime_ ? runtime_->document() : nullptr;
    if (!doc) {
        snapshot[QStringLiteral("hasDocument")] = false;
        return toJson(snapshot);
    }
    snapshot[QStringLiteral("hasDocument")] = true;
    QJsonObject info;
    info[QStringLiteral("width")] = doc->width;
    info[QStringLiteral("height")] = doc->height;
    info[QStringLiteral("resolution")] = doc->resolution;
    info[QStringLiteral("title")] = QString::fromStdString(runtime_->documentTitle());
    info[QStringLiteral("activeIndex")] = runtime_->activeLayerIndex();
    info[QStringLiteral("layerCount")] = int(doc->layers.size());
    snapshot[QStringLiteral("doc")] = info;

    QJsonArray layers;
    for (const auto& layer : doc->layers) {
        QJsonObject entry;
        entry[QStringLiteral("id")] = QString::fromStdString(layer.id);
        entry[QStringLiteral("name")] = QString::fromStdString(layer.name);
        entry[QStringLiteral("visible")] = layer.visible;
        entry[QStringLiteral("group")] = layer.group;
        entry[QStringLiteral("hasMask")] = layer.mask.has_value();
        entry[QStringLiteral("blend")] = int(layer.blend);
        entry[QStringLiteral("opacity")] = layer.opacity;
        layers.append(entry);
    }
    snapshot[QStringLiteral("layers")] = layers;
    return toJson(snapshot);
}

int UxpNativeHost::applyMutations(const QString& mutationsJson, QString& error) {
    const auto document = QJsonDocument::fromJson(mutationsJson.toUtf8());
    if (!document.isArray()) {
        error = QStringLiteral("变更记录格式错误。");
        return 0;
    }
    auto* doc = runtime_ ? runtime_->document() : nullptr;
    if (!doc) {
        error = QStringLiteral("没有打开的文档。");
        return 0;
    }
    int applied = 0;
    for (const auto& value : document.array()) {
        const auto mutation = value.toObject();
        const QString op = mutation.value(QStringLiteral("op")).toString();
        const int index = mutation.value(QStringLiteral("index")).toInt(-1);
        if (index < 0 || size_t(index) >= doc->layers.size()) continue;
        if (op == QStringLiteral("opacity")) {
            const double opacity = mutation.value(QStringLiteral("value")).toDouble(1.0);
            if (runtime_->editLayer("UXP: Layer Opacity", index, [opacity](compositor::Layer& layer) {
                    layer.opacity = qBound(0.0, opacity, 1.0);
                }) == COMPOSITOR_OK) ++applied;
        } else if (op == QStringLiteral("visible")) {
            const bool visible = mutation.value(QStringLiteral("value")).toBool(true);
            if (runtime_->editLayer("UXP: Layer Visibility", index, [visible](compositor::Layer& layer) {
                    layer.visible = visible;
                }) == COMPOSITOR_OK) ++applied;
        } else if (op == QStringLiteral("blend")) {
            const int mode = mutation.value(QStringLiteral("value")).toInt(0);
            if (mode >= 0 && mode < int(blendNames.size())) {
                if (runtime_->editLayer("UXP: Blend Mode", index, [mode](compositor::Layer& layer) {
                        layer.blend = compositor::Blend(mode);
                    }) == COMPOSITOR_OK) ++applied;
            }
        }
    }
    error.clear();
    return applied;
}

compositor_result UxpNativeHost::run(const UxpPlugin& plugin, QString& error) {
    if (!runtime_) {
        error = QStringLiteral("插件运行时未初始化。");
        return COMPOSITOR_ERROR_UNKNOWN;
    }
    // The plugin runs in a separate engine process: the host hands it a document
    // snapshot and reads back the mutations it recorded.
    QString shim = uxpShimSource();
    shim.replace(QStringLiteral("})(this);"), QStringLiteral("})(globalThis);"));
    const auto result = ScriptRuntime::runBundle(shim, snapshotJson(), plugin.source, plugin.entryPath);
    if (!result.ok) {
        error = result.error;
        return COMPOSITOR_ERROR_UNKNOWN;
    }
    const int applied = applyMutations(result.mutationsJson, error);
    Q_UNUSED(applied);
    if (!error.isEmpty()) return COMPOSITOR_ERROR_UNKNOWN;
    return COMPOSITOR_OK;
}
QString uxpShimSource() {
    return QStringLiteral(R"JS(
// Compositor's Photoshop UXP host shim.
//
// The plugin runs unmodified. Reads are served from an injected snapshot; writes
// are appended to a mutation list that the host applies after the script ends.
(function (global) {
  'use strict';

  var SNAPSHOT = global.__compositorSnapshot || { hasDocument: false };
  var MUTATIONS = global.__compositorMutations || (global.__compositorMutations = []);

  function record(op, index, value) {
    MUTATIONS.push({ op: op, index: index, value: value });
    if (SNAPSHOT.layers && SNAPSHOT.layers[index]) {
      if (op === 'opacity') SNAPSHOT.layers[index].opacity = value;
      if (op === 'visible') SNAPSHOT.layers[index].visible = value;
      if (op === 'blend') SNAPSHOT.layers[index].blend = value;
    }
  }

  var BLEND_NAMES = ['normal','multiply','screen','overlay','darken','lighten','difference',
    'colorDodge','colorBurn','hue','saturation','color','luminosity','linearBurn','linearDodge',
    'softLight','hardLight','vividLight','linearLight','pinLight','hardMix','exclusion','subtract','divide'];

  function blendIndex(name) {
    if (typeof name === 'number') return name;
    var lowered = String(name || 'normal').replace(/[^a-z]/gi, '').toLowerCase();
    for (var i = 0; i < BLEND_NAMES.length; i++) {
      if (BLEND_NAMES[i].toLowerCase() === lowered) return i;
    }
    return 0;
  }

  function layerAt(index) {
    var data = (SNAPSHOT.layers && SNAPSHOT.layers[index]) || {};
    return {
      get id() { return data.id; },
      get name() { return data.name; },
      get visible() { return !!data.visible; },
      set visible(value) { data.visible = !!value; record('visible', index, data.visible); },
      get opacity() { return Math.round((data.opacity || 0) * 100); },
      set opacity(value) {
        var normalized = Math.max(0, Math.min(1, Number(value) / 100));
        data.opacity = normalized;
        record('opacity', index, normalized);
      },
      get blendMode() { return data.blend; },
      set blendMode(value) {
        var index2 = blendIndex(value);
        data.blend = index2;
        record('blend', index, index2);
      },
      get isGroup() { return !!data.group; },
      get kind() { return data.group ? 'group' : 'pixel'; },
      delete() {
        // Deleting needs a document change the snapshot cannot express; the plugin
        // logs the attempt rather than silently doing nothing.
        console.warn('layer.delete is not supported by this host yet');
      }
    };
  }

  var photoshop = {
    app: {
      get documents() { var doc = this.activeDocument; return doc ? [doc] : []; },
      get activeDocument() {
        if (!SNAPSHOT.hasDocument) return null;
        var info = SNAPSHOT.doc || {};
        return {
          get title() { return info.title; },
          get width() { return info.width; },
          get height() { return info.height; },
          get resolution() { return info.resolution; },
          get layers() {
            var list = [];
            for (var i = 0; i < (SNAPSHOT.layers || []).length; i++) list.push(layerAt(i));
            return list;
          },
          get activeLayers() {
            var index = info.activeIndex;
            return index >= 0 ? [layerAt(index)] : [];
          },
          createLayer: function () {
            console.warn('createLayer is not supported by this host yet');
            return null;
          }
        };
      }
    },
    core: {
      executeAsModal: function (target, options) {
        var scope = { isCancelled: false, onCancel: null, reportProgress: function () {} };
        return target(scope);
      }
    },
    action: {
      batchPlay: function () {
        return Promise.reject(new Error('batchPlay is not supported by Compositor'));
      }
    },
    constants: { BlendMode: { NORMAL: 'normal', MULTIPLY: 'multiply', SCREEN: 'screen', OVERLAY: 'overlay' } }
  };

  var uxp = {
    host: { name: 'Compositor', version: '1.6.0', uiLocale: 'zh_CN' },
    storage: { localFileSystem: { getFileForOpening: function () { return Promise.resolve(null); },
                                 getFolder: function () { return Promise.resolve(null); } } },
    versions: { uxp: '7.0.0', plugin: '1.0.0' }
  };

  var consoleShim = {
    log: function () {}, info: function () {}, warn: function () {}, error: function () {}
  };

  var modules = { photoshop: photoshop, uxp: uxp };
  global.require = function (name) {
    if (Object.prototype.hasOwnProperty.call(modules, name)) return modules[name];
    return {};
  };
  global.photoshop = photoshop;
  global.uxp = uxp;
  global.console = consoleShim;
  global.module = { exports: {} };
  global.exports = global.module.exports;
})(this);
)JS");
}

}  // namespace compositor::plugin
