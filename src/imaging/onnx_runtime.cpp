#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "onnx_runtime.h"
#define ORT_API_MANUAL_INIT
#include <onnxruntime_cxx_api.h>
#undef ORT_API_MANUAL_INIT
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <fstream>
#include <mutex>
#include <stdexcept>

namespace compositor::imaging {
void initializeOnnxRuntime(){
    // Resolve once on the first inference request, before any Ort wrapper member
    // can dereference its API table. A system DLL may be older than our headers.
    static std::once_flag initialized;
    std::call_once(initialized,[]{
        const auto* base=OrtGetApiBase();const auto* api=base?base->GetApi(ORT_API_VERSION):nullptr;
        if(!api){
            std::array<wchar_t,32768> path{};const auto module=GetModuleHandleW(L"onnxruntime.dll");
            const DWORD length=module?GetModuleFileNameW(module,path.data(),DWORD(path.size())):0;
            const auto utf8=length?std::filesystem::path(std::wstring(path.data(),length)).u8string():std::u8string(u8"unknown module");
            const std::string loaded(utf8.begin(),utf8.end());
            throw std::runtime_error("ONNX Runtime API "+std::to_string(ORT_API_VERSION)+" is unavailable; loaded "+(base?base->GetVersionString():"unknown version")+" from "+loaded);
        }
        Ort::InitApi(api);
    });
}
std::string modelFileSha256(const std::filesystem::path& file,const char* missing){
    BCRYPT_ALG_HANDLE alg{};BCRYPT_HASH_HANDLE value{};
    if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("SHA256 initialization failed");
    struct Clean{BCRYPT_ALG_HANDLE& a;BCRYPT_HASH_HANDLE& h;~Clean(){if(h)BCryptDestroyHash(h);if(a)BCryptCloseAlgorithmProvider(a,0);}}clean{alg,value};
    if(BCryptCreateHash(alg,&value,nullptr,0,nullptr,0,0)<0)throw std::runtime_error("SHA256 creation failed");
    std::ifstream stream(file,std::ios::binary);if(!stream)throw std::runtime_error(missing);std::array<char,65536> buffer{};
    while(stream){stream.read(buffer.data(),buffer.size());if(BCryptHashData(value,reinterpret_cast<PUCHAR>(buffer.data()),ULONG(stream.gcount()),0)<0)throw std::runtime_error("Model hash failed");}
    if(!stream.eof())throw std::runtime_error("Model read failed");std::array<UCHAR,32> digest{};if(BCryptFinishHash(value,digest.data(),ULONG(digest.size()),0)<0)throw std::runtime_error("Model hash finalization failed");
    std::string out;const char* hex="0123456789abcdef";for(auto c:digest){out+=hex[c>>4];out+=hex[c&15];}return out;
}
}
