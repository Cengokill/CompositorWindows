#pragma once
// Internal to the streaming exporters (stream_export.cpp, pdf_export.cpp).
// Output is written to a uniquely named file beside the destination and only moved over it
// once complete, so a cancelled or failed export leaves an existing destination untouched.
#include "stream_export.h"
#include "win32_file_path.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <functional>
#include <sstream>

namespace compositor::imaging::detail {
inline void ok(HRESULT hr, const char* context) {
    if (FAILED(hr)) { std::ostringstream text; text << context << " (HRESULT 0x" << std::hex << static_cast<unsigned long>(hr) << ")"; throw std::runtime_error(text.str()); }
}
inline void cancelled(const std::function<bool()>& check) { if (check && check()) throw ExportCancelled(); }
struct Apartment {
    HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    Apartment() { if (FAILED(result) && result != RPC_E_CHANGED_MODE) ok(result, "Initialize export COM apartment"); }
    ~Apartment() { if (SUCCEEDED(result)) CoUninitialize(); }
};
inline Microsoft::WRL::ComPtr<IWICImagingFactory> factory() {
    Microsoft::WRL::ComPtr<IWICImagingFactory> value;
    ok(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&value)), "Create export imaging factory");
    return value;
}
struct Handle {
    HANDLE value{INVALID_HANDLE_VALUE};
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
inline void regularFile(HANDLE handle) {
    BY_HANDLE_FILE_INFORMATION info{};
    if (GetFileType(handle) != FILE_TYPE_DISK || !GetFileInformationByHandle(handle, &info) || (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        throw std::runtime_error("Image export requires a regular file");
}
struct TemporaryFile {
    std::filesystem::path path;
    explicit TemporaryFile(const std::filesystem::path& destination) {
        GUID id{}; ok(CoCreateGuid(&id), "Create temporary export identifier");
        wchar_t text[40]{}; if (!StringFromGUID2(id, text, 40)) throw std::runtime_error("Cannot name temporary export");
        path = win32FilePath(destination); path += std::wstring(L".") + text + L".tmp";
        Handle file{CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr)};
        if (file.value == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot create temporary export beside destination");
        try { regularFile(file.value); }
        catch (...) { CloseHandle(file.value); file.value = INVALID_HANDLE_VALUE; DeleteFileW(path.c_str()); throw; }
    }
    ~TemporaryFile() { std::error_code error; std::filesystem::remove(path, error); }
    void replace(const std::filesystem::path& destination, const std::function<bool()>& check) {
        cancelled(check);
        const auto target=win32FilePath(destination);
        if (!MoveFileExW(path.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Atomic export replacement failed");
    }
};
}
