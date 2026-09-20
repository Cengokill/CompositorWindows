#include "imaging/onnx_subject_provider.h"
#include <windows.h>
#include <array>
#include <iostream>
#include <stdexcept>
using namespace compositor::imaging;
int main(int argc,char** argv){try{
 if(argc!=3)throw std::runtime_error("Provide compatible|unavailable and model path");
 std::array<wchar_t,32768> loaded{};auto module=GetModuleHandleW(L"onnxruntime.dll");const auto count=module?GetModuleFileNameW(module,loaded.data(),DWORD(loaded.size())):0;
 if(!count)throw std::runtime_error("Cannot identify loaded runtime DLL");std::wcout<<L"loaded_runtime="<<loaded.data()<<L'\n';
 const bool compatible=std::string(argv[1])=="compatible";
 for(int attempt=0;attempt<2;++attempt){bool constructed=false;try{OnnxSubjectProvider provider(argv[2]);constructed=true;}catch(const std::exception& error){std::cout<<"attempt="<<attempt<<" error="<<error.what()<<'\n';if(compatible||std::string(error.what()).find("ONNX Runtime API 30 is unavailable")==std::string::npos)throw;}
  if(constructed!=compatible)throw std::runtime_error("Runtime compatibility expectation failed");
 }
 std::cout<<"PASS runtime_"<<(compatible?"compatible":"unavailable")<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
