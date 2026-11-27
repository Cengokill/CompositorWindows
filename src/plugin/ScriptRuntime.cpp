#include "ScriptRuntime.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

namespace compositor::plugin {
namespace {

// A UXP panel expects the browser globals a panel is normally given. This shim is
// deliberately small: it lets the panel build its tree and register handlers, which
// the host fires itself, so no real DOM is required.
QString domShimSource() {
    return QStringLiteral(R"JS(
var __compositorReadyHandlers = [];
var __compositorLoadHandlers = [];
globalThis.__compositorReadyHandlers = __compositorReadyHandlers;
globalThis.__compositorLoadHandlers = __compositorLoadHandlers;
if (typeof globalThis.window === 'undefined') { globalThis.window = globalThis; }
if (typeof globalThis.self === 'undefined') { globalThis.self = globalThis; }
var window = globalThis;
var self = globalThis;

function __Element(tag) {
  this.tagName = String(tag || 'div').toUpperCase();
  this.nodeName = this.tagName;
  this.children = [];
  this.childNodes = this.children;
  this.style = {};
  this.dataset = {};
  this.attributes = {};
  this.classList = {
    add: function () {}, remove: function () {}, toggle: function () {},
    contains: function () { return false; }
  };
  this.innerHTML = '';
  this.textContent = '';
  this.value = '';
  this.checked = false;
  this.parentNode = null;
  this.offsetWidth = 0;
  this.offsetHeight = 0;
}
__Element.prototype.appendChild = function (child) { this.children.push(child); if (child) child.parentNode = this; return child; };
__Element.prototype.removeChild = function (child) { var i = this.children.indexOf(child); if (i >= 0) this.children.splice(i, 1); return child; };
__Element.prototype.insertBefore = function (child) { this.children.unshift(child); return child; };
__Element.prototype.replaceChild = function (next, old) { return this.appendChild(next); };
__Element.prototype.setAttribute = function (name, value) { this.attributes[name] = value; if (name === 'id') this.id = value; };
__Element.prototype.getAttribute = function (name) { return this.attributes[name] === undefined ? null : this.attributes[name]; };
__Element.prototype.removeAttribute = function (name) { delete this.attributes[name]; };
__Element.prototype.hasAttribute = function (name) { return this.attributes[name] !== undefined; };
__Element.prototype.addEventListener = function () {};
__Element.prototype.removeEventListener = function () {};
__Element.prototype.dispatchEvent = function () { return true; };
__Element.prototype.querySelector = function () { return null; };
__Element.prototype.querySelectorAll = function () { return []; };
__Element.prototype.getElementsByTagName = function () { return []; };
__Element.prototype.getBoundingClientRect = function () {
  return { left: 0, top: 0, right: 0, bottom: 0, width: 0, height: 0, x: 0, y: 0 };
};
__Element.prototype.focus = function () {};
__Element.prototype.blur = function () {};
__Element.prototype.click = function () {};
__Element.prototype.cloneNode = function () { return new __Element(this.tagName); };

if (typeof globalThis.document === 'undefined') {
  var __body = new __Element('body');
  var __head = new __Element('head');
  globalThis.document = {
    body: __body,
    head: __head,
    documentElement: new __Element('html'),
    createElement: function (tag) { return new __Element(tag); },
    createTextNode: function (text) { var node = new __Element('#text'); node.textContent = text; return node; },
    createDocumentFragment: function () { return new __Element('#fragment'); },
    getElementById: function () { return null; },
    querySelector: function () { return null; },
    querySelectorAll: function () { return []; },
    getElementsByClassName: function () { return []; },
    getElementsByTagName: function () { return []; },
    addEventListener: function (type, handler) {
      if (type === 'DOMContentLoaded') __compositorReadyHandlers.push(handler);
      else if (type === 'load') __compositorLoadHandlers.push(handler);
    },
    removeEventListener: function () {},
    readyState: 'complete'
  };
}
if (typeof globalThis.window.addEventListener !== 'function') {
  globalThis.window.addEventListener = function (type, handler) {
    if (type === 'DOMContentLoaded') __compositorReadyHandlers.push(handler);
    else if (type === 'load') __compositorLoadHandlers.push(handler);
  };
}
if (typeof globalThis.HTMLElement === 'undefined') { globalThis.HTMLElement = __Element; }
if (typeof globalThis.Node === 'undefined') { globalThis.Node = __Element; }
if (typeof globalThis.customElements === 'undefined') {
  globalThis.customElements = { define: function () {}, get: function () { return undefined; } };
}
if (typeof globalThis.navigator === 'undefined') {
  globalThis.navigator = { userAgent: 'Compositor UXP', platform: 'Win32', language: 'zh-CN' };
}
if (typeof globalThis.location === 'undefined') {
  globalThis.location = { href: 'plugin://', protocol: 'plugin:', host: '' };
}
if (typeof globalThis.localStorage === 'undefined') {
  var __store = {};
  globalThis.localStorage = {
    getItem: function (key) { return Object.prototype.hasOwnProperty.call(__store, key) ? __store[key] : null; },
    setItem: function (key, value) { __store[key] = String(value); },
    removeItem: function (key) { delete __store[key]; },
    clear: function () { __store = {}; }
  };
  globalThis.sessionStorage = globalThis.localStorage;
}
if (typeof globalThis.requestAnimationFrame === 'undefined') {
  globalThis.requestAnimationFrame = function (handler) { return setTimeout(function () { handler(Date.now()); }, 0); };
  globalThis.cancelAnimationFrame = function (handle) { clearTimeout(handle); };
}
if (typeof globalThis.matchMedia === 'undefined') {
  globalThis.matchMedia = function () { return { matches: false, addListener: function () {}, removeListener: function () {} }; };
}
if (typeof globalThis.getComputedStyle === 'undefined') {
  globalThis.getComputedStyle = function () { return {}; };
}
if (typeof globalThis.alert === 'undefined') {
  globalThis.alert = function () {}; globalThis.confirm = function () { return true; }; globalThis.prompt = function () { return null; };
}
)JS");
}
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
    program += QStringLiteral("var require = function(name){ return globalThis.require ? globalThis.require(name) : {}; };\n");
    // A UXP panel is written against a browser-ish environment: its bundle ends with
    // `})(window)`, and it registers DOMContentLoaded handlers to initialise itself.
    program += domShimSource();
    program += shim;
    // The plugin is evaluated at module scope so its top-level declarations, such as
    // `function run()`, stay visible to the entry call below.
    program += QStringLiteral("\n");
    program += plugin;
    program += QStringLiteral("\n");
    // Entry points: a global run(), an exported run, onLoad(), or the panel's own
    // DOMContentLoaded/load listeners, which no real DOM will ever fire for.
    program += QStringLiteral(
        "try{\n"
        "  var __invoked=false;\n"
        "  if(typeof run==='function'){ run(); __invoked=true; }\n"
        "  else if(typeof module!=='undefined'&&module.exports&&typeof module.exports.run==='function'){ module.exports.run(); __invoked=true; }\n"
        "  else if(typeof onLoad==='function'){ onLoad(); __invoked=true; }\n"
        "  var __fire=function(list){ for(var i=0;i<list.length;i++){ try{ list[i]({type:'init'}); }catch(e){ process.stderr.write(String(e&&e.stack||e)+'\\n'); } } };\n"
        "  __fire(globalThis.__compositorReadyHandlers||[]);\n"
        "  __fire(globalThis.__compositorLoadHandlers||[]);\n"
        "}catch(e){ process.stderr.write(String(e && e.stack || e) + '\\n'); process.exit(3); }\n");
    // Panels install timers and pollers, so the event loop would never drain: print the
    // result and stop the process explicitly.
    program += QStringLiteral(
        "process.stdout.write('__MUTATIONS__'+JSON.stringify(__compositorMutations)+'\\n');\n"
        "process.exit(0);\n");
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
