#include "ScriptRuntime.h"
#include <QLibrary>
#include <QDir>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <QByteArray>
#include <vector>

// Chakra is the JavaScript engine shipped with Windows. Every entry point is
// resolved by name so the application still starts when the engine is absent.
namespace compositor::plugin {
namespace {

using JsValueRef = void*;
using JsPropertyIdRef = void*;
using JsContextRef = void*;
using JsRuntimeHandle = void*;

enum JsValueType {
    JsUndefined = 0, JsNull = 1, JsNumber = 2, JsString = 3, JsBoolean = 4,
    JsObject = 5, JsFunction = 6, JsError = 7,
};

struct EngineApi {
    HMODULE module{};
    QString error;

    int (*createRuntime)(int attributes, unsigned flags, JsRuntimeHandle* runtime){};
    int (*createContext)(JsRuntimeHandle runtime, JsContextRef* context){};
    int (*setCurrentContext)(JsContextRef context){};
    int (*getGlobalObject)(JsValueRef* value){};
    int (*runScript)(JsContextRef context, void* sourceContext, const wchar_t* script,
                     const wchar_t* name, JsValueRef* result){};
    int (*createString)(const char* content, size_t length, JsValueRef* value){};
    int (*convertToString)(JsValueRef value, JsValueRef* stringValue){};
    int (*stringToPointer)(JsValueRef value, const wchar_t** buffer){};
    int (*getStringLength)(JsValueRef value, size_t* length){};
    int (*getProperty)(JsValueRef object, JsPropertyIdRef id, JsValueRef* value){};
    int (*setProperty)(JsValueRef object, JsPropertyIdRef id, JsValueRef value, bool strict){};
    int (*hasProperty)(JsValueRef object, JsPropertyIdRef id, bool* has){};
    int (*propertyIdFromName)(const wchar_t* name, JsPropertyIdRef* id){};
    int (*callFunction)(JsValueRef function, JsValueRef* result, unsigned short argc, JsValueRef* argv){};
    int (*getValueType)(JsValueRef value, JsValueType* type){};
    int (*getAndClearException)(JsValueRef* exception){};
    int (*disposeRuntime)(JsRuntimeHandle runtime){};

    template <typename T>
    T resolve(const char* name) {
        return reinterpret_cast<T>(::GetProcAddress(module, name));
    }

    bool open() {
        if (module) return true;
        // The engine lives in the system directory; a bare name is not always found.
        wchar_t systemDirectory[MAX_PATH]{};
        QString path = QStringLiteral("Chakra.dll");
        if (::GetSystemDirectoryW(systemDirectory, MAX_PATH) > 0) {
            path = QDir(QString::fromWCharArray(systemDirectory)).filePath(QStringLiteral("Chakra.dll"));
        }
        module = ::LoadLibraryW(reinterpret_cast<LPCWSTR>(path.utf16()));
        if (!module) {
            const DWORD code = ::GetLastError();
            module = ::LoadLibraryW(L"Chakra.dll");
            if (!module) {
                error = QStringLiteral("LoadLibraryW 失败，错误码 %1").arg(code);
                return false;
            }
        }
        createRuntime = resolve<decltype(createRuntime)>("JsCreateRuntime");
        createContext = resolve<decltype(createContext)>("JsCreateContext");
        setCurrentContext = resolve<decltype(setCurrentContext)>("JsSetCurrentContext");
        getGlobalObject = resolve<decltype(getGlobalObject)>("JsGetGlobalObject");
        runScript = resolve<decltype(runScript)>("JsRunScript");
        createString = resolve<decltype(createString)>("JsCreateString");
        convertToString = resolve<decltype(convertToString)>("JsConvertValueToString");
        stringToPointer = resolve<decltype(stringToPointer)>("JsStringToPointer");
        getStringLength = resolve<decltype(getStringLength)>("JsGetStringLength");
        getProperty = resolve<decltype(getProperty)>("JsGetProperty");
        setProperty = resolve<decltype(setProperty)>("JsSetProperty");
        hasProperty = resolve<decltype(hasProperty)>("JsHasProperty");
        propertyIdFromName = resolve<decltype(propertyIdFromName)>("JsGetPropertyIdFromName");
        callFunction = resolve<decltype(callFunction)>("JsCallFunction");
        getValueType = resolve<decltype(getValueType)>("JsGetValueType");
        getAndClearException = resolve<decltype(getAndClearException)>("JsGetAndClearException");
        disposeRuntime = resolve<decltype(disposeRuntime)>("JsDisposeRuntime");
        if (!createRuntime || !createContext || !runScript || !createString || !propertyIdFromName) {
            error = QStringLiteral("Chakra.dll 缺少所需导出。");
            return false;
        }
        return true;
    }
};
}  // namespace

struct ScriptRuntime::Impl {
    EngineApi api;
    JsRuntimeHandle runtime{};
    JsContextRef context{};
    bool ready{};

    QString stringOf(JsValueRef value) {
        if (!value || !api.convertToString) return {};
        JsValueRef text = nullptr;
        if (api.convertToString(value, &text) != 0) return {};
        const wchar_t* buffer = nullptr;
        if (!api.stringToPointer || api.stringToPointer(text, &buffer) != 0) return {};
        size_t length = 0;
        if (api.getStringLength && api.getStringLength(text, &length) != 0) return {};
        if (!buffer) return {};
        return QString::fromWCharArray(buffer, int(length));
    }

    QString takeException() {
        if (!api.getAndClearException) return {};
        JsValueRef exception = nullptr;
        if (api.getAndClearException(&exception) != 0) return {};
        return stringOf(exception);
    }
};

ScriptRuntime::ScriptRuntime() : impl_(std::make_unique<Impl>()) {}

ScriptRuntime::~ScriptRuntime() {
    if (impl_->ready && impl_->api.module) {
        if (impl_->api.setCurrentContext) impl_->api.setCurrentContext(nullptr);
        if (impl_->runtime && impl_->api.disposeRuntime) impl_->api.disposeRuntime(impl_->runtime);
    }
}

bool ScriptRuntime::available(QString& error) {
    if (!impl_->api.open()) {
            error = QStringLiteral("无法加载系统 JavaScript 引擎：%1").arg(impl_->api.error);
        return false;
    }
    if (!impl_->ready) {
        if (impl_->api.createRuntime(0, 0x2 /* disable background work */, &impl_->runtime) != 0) {
            error = QStringLiteral("无法创建 JS 运行时。");
            return false;
        }
        if (impl_->api.createContext(impl_->runtime, &impl_->context) != 0) {
            error = QStringLiteral("无法创建 JS 上下文。");
            return false;
        }
        impl_->api.setCurrentContext(impl_->context);
        impl_->ready = true;
    }
    error.clear();
    return true;
}

bool ScriptRuntime::setGlobalJson(const QString& name, const QString& json, QString& error) {
    if (!available(error)) return false;
    JsValueRef global = nullptr;
    if (impl_->api.getGlobalObject(&global) != 0) {
        error = QStringLiteral("无法获取全局对象。");
        return false;
    }
    // Parse the JSON inside the engine so the value is a real object, not a string.
    QByteArray utf8 = json.toUtf8();
    JsValueRef text = nullptr;
    if (impl_->api.createString(utf8.constData(), size_t(utf8.size()), &text) != 0 || !text) {
        error = QStringLiteral("无法创建 JSON 字符串。");
        return false;
    }
    const auto wideName = name.toStdWString();
    JsPropertyIdRef nameId = nullptr;
    if (impl_->api.propertyIdFromName(wideName.c_str(), &nameId) != 0) {
        error = QStringLiteral("无法解析全局名 %1。").arg(name);
        return false;
    }
    if (impl_->api.setProperty(global, nameId, text, false) != 0) {
        error = QStringLiteral("无法设置全局 %1。").arg(name);
        return false;
    }
    // Replace the string with JSON.parse(string) so plugins mutate real objects.
    const QString assign = QStringLiteral("this.%1 = JSON.parse(this.%1);").arg(name);
    return evaluate(assign, QStringLiteral("compositor-json.js"), error);
}

bool ScriptRuntime::evaluate(const QString& source, const QString& sourceName, QString& error) {
    if (!available(error)) return false;
    JsValueRef result = nullptr;
    const auto script = source.toStdWString();
    const auto name = sourceName.toStdWString();
    const int code = impl_->api.runScript(impl_->context, nullptr, script.c_str(), name.c_str(), &result);
    if (code != 0) {
        const QString message = impl_->takeException();
        error = message.isEmpty()
                    ? QStringLiteral("脚本执行失败（引擎错误码 %1）：%2").arg(code).arg(sourceName)
                    : QStringLiteral("%1：%2").arg(sourceName, message);
        return false;
    }
    error.clear();
    return true;
}

bool ScriptRuntime::evaluateToString(const QString& expression, QString& out, QString& error) {
    if (!available(error)) return false;
    JsValueRef result = nullptr;
    const auto script = expression.toStdWString();
    const auto name = QStringLiteral("compositor-expr.js").toStdWString();
    const int code = impl_->api.runScript(impl_->context, nullptr, script.c_str(), name.c_str(), &result);
    if (code != 0) {
        const QString message = impl_->takeException();
        error = message.isEmpty() ? QStringLiteral("表达式求值失败（错误码 %1）").arg(code) : message;
        return false;
    }
    out = impl_->stringOf(result);
    error.clear();
    return true;
}

bool ScriptRuntime::hasGlobalFunction(const QString& name) {
    if (!impl_->ready) return false;
    JsValueRef global = nullptr;
    if (impl_->api.getGlobalObject(&global) != 0) return false;
    const auto wide = name.toStdWString();
    JsPropertyIdRef id = nullptr;
    if (impl_->api.propertyIdFromName(wide.c_str(), &id) != 0) return false;
    bool has = false;
    if (impl_->api.hasProperty(global, id, &has) != 0 || !has) return false;
    JsValueRef value = nullptr;
    if (impl_->api.getProperty(global, id, &value) != 0) return false;
    JsValueType type = JsUndefined;
    if (impl_->api.getValueType(value, &type) != 0) return false;
    return type == JsFunction;
}

bool ScriptRuntime::callGlobal(const QString& functionName, QString& error) {
    if (!available(error)) return false;
    JsValueRef global = nullptr;
    if (impl_->api.getGlobalObject(&global) != 0) {
        error = QStringLiteral("无法获取全局对象。");
        return false;
    }
    const auto wide = functionName.toStdWString();
    JsPropertyIdRef id = nullptr;
    if (impl_->api.propertyIdFromName(wide.c_str(), &id) != 0) {
        error = QStringLiteral("无法解析函数名 %1。").arg(functionName);
        return false;
    }
    JsValueRef function = nullptr;
    if (impl_->api.getProperty(global, id, &function) != 0) {
        error = QStringLiteral("未找到入口函数 %1。").arg(functionName);
        return false;
    }
    JsValueRef result = nullptr;
    if (impl_->api.callFunction(function, &result, 0, nullptr) != 0) {
        const QString message = impl_->takeException();
        error = message.isEmpty() ? QStringLiteral("调用 %1 失败。").arg(functionName) : message;
        return false;
    }
    error.clear();
    return true;
}

}  // namespace compositor::plugin
