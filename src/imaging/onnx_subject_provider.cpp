#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "onnx_subject_provider.h"
#include <onnxruntime_cxx_api.h>
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <cmath>
#include <fstream>
#include <thread>
#include <chrono>
#include <mutex>
namespace compositor::imaging {
namespace {
std::string hash(const std::filesystem::path& file){
    BCRYPT_ALG_HANDLE alg{};BCRYPT_HASH_HANDLE value{};
    if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("SHA256 initialization failed");
    struct Clean{BCRYPT_ALG_HANDLE& a;BCRYPT_HASH_HANDLE& h;~Clean(){if(h)BCryptDestroyHash(h);if(a)BCryptCloseAlgorithmProvider(a,0);}}clean{alg,value};
    if(BCryptCreateHash(alg,&value,nullptr,0,nullptr,0,0)<0)throw std::runtime_error("SHA256 creation failed");
    std::ifstream stream(file,std::ios::binary);if(!stream)throw std::runtime_error("Local foreground model is missing");std::array<char,65536> buffer{};
    while(stream){stream.read(buffer.data(),buffer.size());if(BCryptHashData(value,reinterpret_cast<PUCHAR>(buffer.data()),ULONG(stream.gcount()),0)<0)throw std::runtime_error("Model hash failed");}
    if(!stream.eof())throw std::runtime_error("Model read failed");std::array<UCHAR,32> digest{};if(BCryptFinishHash(value,digest.data(),ULONG(digest.size()),0)<0)throw std::runtime_error("Model hash finalization failed");
    std::string out;const char* hex="0123456789abcdef";for(auto c:digest){out+=hex[c>>4];out+=hex[c&15];}return out;
}
// Explicit bilinear resampling in straight RGB; transparent source contributes black.
float source(const RgbaImage& image,int x,int y,int c){const auto* p=&image.pixels[std::size_t(y)*image.stride+std::size_t(x)*4];return p[3]?std::min(255.F,float(p[c])*255/p[3]):0;}
std::vector<float> preprocess(const RgbaImage& image,const ImportOptions& options){
    constexpr int size=1024;constexpr float mean[]={.485F,.456F,.406F},sd[]={.229F,.224F,.225F};std::vector<float> input(3*size*size);
    for(int y=0;y<size;++y){checkCancelled(options);double sy=std::clamp((y+.5)*image.height/size-.5,0.,double(image.height-1));int y0=int(sy),y1=std::min(y0+1,int(image.height)-1);float fy=float(sy-y0);
        for(int x=0;x<size;++x){double sx=std::clamp((x+.5)*image.width/size-.5,0.,double(image.width-1));int x0=int(sx),x1=std::min(x0+1,int(image.width)-1);float fx=float(sx-x0);for(int c=0;c<3;++c){float value=(source(image,x0,y0,c)*(1-fx)+source(image,x1,y0,c)*fx)*(1-fy)+(source(image,x0,y1,c)*(1-fx)+source(image,x1,y1,c)*fx)*fy;input[std::size_t(c)*size*size+y*size+x]=(std::round(value)/255-mean[c])/sd[c];}}
    }return input;
}
}
struct OnnxSubjectProvider::Impl {
    Ort::Env environment{ORT_LOGGING_LEVEL_WARNING,"CompositorForeground"};Ort::SessionOptions options;Ort::Session session{nullptr};std::mutex mutex;
    explicit Impl(const std::filesystem::path& path){
        if(hash(path)!=OnnxSubjectProvider::modelSha256)throw std::runtime_error("Foreground model hash does not match pinned conversion");
        environment.DisableTelemetryEvents();options.SetIntraOpNumThreads(8);options.SetInterOpNumThreads(1);options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        // Do not retain the model's multi-gigabyte temporary arena after an edit.
        // This reduces retained memory; a hard inference-process budget remains a separate gate.
        options.DisableCpuMemArena();options.DisableMemPattern();
        // No accelerator/provider registration: ONNX Runtime's mandatory CPU provider is used.
        session=Ort::Session(environment,path.c_str(),options);
        if(session.GetInputCount()!=1||session.GetOutputCount()!=1)throw std::runtime_error("Unexpected model graph interface");
        auto inType=session.GetInputTypeInfo(0);auto outType=session.GetOutputTypeInfo(0);auto in=inType.GetTensorTypeAndShapeInfo(),out=outType.GetTensorTypeAndShapeInfo();
        if(in.GetElementType()!=ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT||out.GetElementType()!=ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT||in.GetShape()!=std::vector<int64_t>{1,3,1024,1024}||out.GetShape()!=std::vector<int64_t>{1,1,1024,1024})throw std::runtime_error("Unexpected model tensor format");
    }
};
OnnxSubjectProvider::OnnxSubjectProvider(const std::filesystem::path& path):impl_(std::make_unique<Impl>(path)){}
OnnxSubjectProvider::~OnnxSubjectProvider()=default;
GrayMask OnnxSubjectProvider::infer(const RgbaImage& image,const ImportOptions& options){
    return inferImpl(image,options,true);
}
void OnnxSubjectProvider::healthCheck(const ImportOptions& options){
    RgbaImage image{2,2,8,std::vector<std::uint8_t>(16,255)};
    validate(inferImpl(image,options,false));
}
GrayMask OnnxSubjectProvider::inferImpl(const RgbaImage& image,const ImportOptions& options,bool requireSubject){
    validate(image);checkCancelled(options);checkedBytes(image.width,image.height,4,options);auto data=preprocess(image,options);std::lock_guard lock(impl_->mutex);
    auto memory=Ort::MemoryInfo::CreateCpu(OrtArenaAllocator,OrtMemTypeDefault);const int64_t dims[]={1,3,1024,1024};auto tensor=Ort::Value::CreateTensor<float>(memory,data.data(),data.size(),dims,4);const char* names[]={"image"};const char* outputs[]={"mask"};Ort::RunOptions run;
    std::jthread cancellation([&](std::stop_token stop){while(!stop.stop_requested()){if(options.cancelled&&options.cancelled()){run.SetTerminate();return;}std::this_thread::sleep_for(std::chrono::milliseconds(20));}});
    auto result=impl_->session.Run(run,names,&tensor,1,outputs,1);cancellation.request_stop();checkCancelled(options);
    const auto outputInfo=result[0].GetTensorTypeAndShapeInfo();
    if(outputInfo.GetElementType()!=ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT||outputInfo.GetShape()!=std::vector<int64_t>{1,1,1024,1024})throw std::runtime_error("Foreground inference returned an unexpected output shape");
    const auto* mask=result[0].GetTensorData<float>();float max=0;for(std::size_t i=0;i<1024*1024;++i){if(!std::isfinite(mask[i]))throw std::runtime_error("Foreground inference returned non-finite mask");max=std::max(max,mask[i]);}if(requireSubject&&max<.5F)throw std::runtime_error("No foreground subject was detected");
    GrayMask out{image.width,image.height,image.width,std::vector<std::uint8_t>(std::size_t(image.width)*image.height)};
    for(std::uint32_t y=0;y<image.height;++y){checkCancelled(options);double sy=std::clamp((y+.5)*1024/image.height-.5,0.,1023.);int y0=int(sy),y1=std::min(y0+1,1023);float fy=float(sy-y0);for(std::uint32_t x=0;x<image.width;++x){double sx=std::clamp((x+.5)*1024/image.width-.5,0.,1023.);int x0=int(sx),x1=std::min(x0+1,1023);float fx=float(sx-x0);float value=(mask[y0*1024+x0]*(1-fx)+mask[y0*1024+x1]*fx)*(1-fy)+(mask[y1*1024+x0]*(1-fx)+mask[y1*1024+x1]*fx)*fy;out.pixels[std::size_t(y)*image.width+x]=std::uint8_t(std::clamp(value*255+.5F,0.F,255.F));}}return out;
}
}
