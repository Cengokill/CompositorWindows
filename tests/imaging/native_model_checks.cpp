#include "onnx_subject_provider.h"
#include "wic_codec.h"
#include <chrono>
#include <iostream>
#include <atomic>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
using namespace compositor::imaging;
int main(int argc,char**argv){try{if(argc!=4)throw std::runtime_error("Usage: native_model_checks model.onnx source.png output.png");auto source=WicCodec::decode(argv[2]);OnnxSubjectProvider provider(argv[1]);auto start=std::chrono::steady_clock::now();auto mask=provider.infer(source.image);auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();MatteSettings settings;settings.advanced=true;auto refined=refineSubjectMask(mask,source.image,settings);auto cutout=applySubjectMask(source.image,refined);WicCodec::encode(argv[3],cutout);std::cout<<"Native CPU segmentation plus full-size advanced matte "<<source.image.width<<"x"<<source.image.height<<"; inference "<<elapsed<<" seconds\n";
    ImportOptions cancel;std::atomic<bool> requested=false;const auto begin=std::chrono::steady_clock::now();cancel.cancelled=[&]{const bool due=std::chrono::steady_clock::now()-begin>std::chrono::milliseconds(200);if(due)requested=true;return due;};bool stopped=false;try{provider.infer(source.image,cancel);}catch(const std::exception& error){stopped=true;std::cout<<"Cancellation: "<<error.what()<<'\n';}if(!stopped||!requested)throw std::runtime_error("In-flight model cancellation was not observed");std::cout<<"Cancellation seconds "<<std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count()<<'\n';PROCESS_MEMORY_COUNTERS_EX memory{};memory.cb=sizeof(memory);if(!GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory)))throw std::runtime_error("Cannot measure native process memory");std::cout<<"Peak working set bytes "<<memory.PeakWorkingSetSize<<"; private committed bytes "<<memory.PrivateUsage<<'\n';return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
