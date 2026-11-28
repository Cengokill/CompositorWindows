#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "onnx_depth_provider.h"
#include "onnx_runtime.h"
#include "subject_matte.h"
#define ORT_API_MANUAL_INIT
#include <onnxruntime_cxx_api.h>
#undef ORT_API_MANUAL_INIT
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <mutex>
#include <thread>

namespace compositor::imaging {
namespace {
// Box-averaged premultiplied RGB (composited on black) at width x height, 0-1.
std::vector<float> averaged(const RgbaImage& image,std::uint32_t width,std::uint32_t height,const ImportOptions& options){
    std::vector<float> out(std::size_t(width)*height*3);
    for(std::uint32_t y=0;y<height;++y){
        checkCancelled(options);
        const auto y0=std::uint32_t(std::uint64_t(y)*image.height/height),y1=std::max(y0+1,std::uint32_t(std::uint64_t(y+1)*image.height/height));
        for(std::uint32_t x=0;x<width;++x){
            const auto x0=std::uint32_t(std::uint64_t(x)*image.width/width),x1=std::max(x0+1,std::uint32_t(std::uint64_t(x+1)*image.width/width));
            std::array<double,3> sum{};
            for(auto sy=y0;sy<y1;++sy)for(auto sx=x0;sx<x1;++sx){const auto* p=image.pixels.data()+sy*image.stride+std::size_t(sx)*4;for(int c=0;c<3;++c)sum[c]+=p[c];}
            const double count=double(x1-x0)*(y1-y0);
            for(int c=0;c<3;++c)out[(std::size_t(y)*width+x)*3+c]=float(sum[c]/count/255);
        }
    }
    return out;
}
}
struct OnnxDepthProvider::Impl {
    Ort::Env environment{ORT_LOGGING_LEVEL_WARNING,"CompositorDepth"};Ort::SessionOptions options;Ort::Session session{nullptr};std::mutex mutex;
    explicit Impl(const std::filesystem::path& path){
        if(modelFileSha256(path,"Local depth model is missing")!=OnnxDepthProvider::modelSha256)throw std::runtime_error("Depth model hash does not match pinned conversion");
        environment.DisableTelemetryEvents();options.SetIntraOpNumThreads(8);options.SetInterOpNumThreads(1);options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        options.DisableCpuMemArena();options.DisableMemPattern();
        session=Ort::Session(environment,path.c_str(),options);
        if(session.GetInputCount()!=1||session.GetOutputCount()!=1)throw std::runtime_error("Unexpected depth model graph interface");
        auto inType=session.GetInputTypeInfo(0);auto outType=session.GetOutputTypeInfo(0);auto in=inType.GetTensorTypeAndShapeInfo(),out=outType.GetTensorTypeAndShapeInfo();
        const auto inShape=in.GetShape(),outShape=out.GetShape();
        if(in.GetElementType()!=ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT||out.GetElementType()!=ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT||inShape.size()!=4||outShape.size()!=4||inShape[0]!=1||inShape[1]!=3||outShape[1]!=1)
            throw std::runtime_error("Unexpected depth model tensor format");
    }
};
OnnxDepthProvider::OnnxDepthProvider(const std::filesystem::path& path){initializeOnnxRuntime();impl_=std::make_unique<Impl>(path);}
OnnxDepthProvider::~OnnxDepthProvider()=default;
std::pair<std::uint32_t,std::uint32_t> OnnxDepthProvider::inputSize(std::uint32_t width,std::uint32_t height){
    const double longer=std::max(width,height),shorter=std::min(width,height);
    const auto side=std::uint32_t(std::clamp(std::lround(inputSide*shorter/std::max(1.,longer)/14.),2L,long(inputSide/14))*14);
    return width>=height?std::pair{inputSide,side}:std::pair{side,inputSide};
}
std::optional<DepthEstimate> OnnxDepthProvider::estimate(const RgbaImage& image,const ImportOptions& options){
    if(!image.width||!image.height||image.width>30000||image.height>30000||image.stride<std::size_t(image.width)*4||image.pixels.size()<image.stride*(image.height-1)+std::size_t(image.width)*4)
        throw std::runtime_error("Invalid depth input image");
    const auto [columns,rows]=inputSize(image.width,image.height);
    // ImageNet normalization, NCHW.
    const auto rgb=averaged(image,columns,rows,options);const std::size_t plane=std::size_t(columns)*rows;
    std::vector<float> input(plane*3);constexpr std::array<float,3> mean{.485F,.456F,.406F},deviation{.229F,.224F,.225F};
    for(std::size_t i=0;i<plane;++i)for(int c=0;c<3;++c)input[std::size_t(c)*plane+i]=(rgb[i*3+c]-mean[c])/deviation[c];
    std::vector<float> predicted;
    {
        std::lock_guard lock(impl_->mutex);
        auto memory=Ort::MemoryInfo::CreateCpu(OrtArenaAllocator,OrtMemTypeDefault);const int64_t dims[]={1,3,rows,columns};
        auto tensor=Ort::Value::CreateTensor<float>(memory,input.data(),input.size(),dims,4);const char* names[]={"image"};const char* outputs[]={"depth"};Ort::RunOptions run;
        std::jthread cancellation([&](std::stop_token stop){while(!stop.stop_requested()){if(options.cancelled&&options.cancelled()){run.SetTerminate();return;}std::this_thread::sleep_for(std::chrono::milliseconds(20));}});
        auto result=impl_->session.Run(run,names,&tensor,1,outputs,1);cancellation.request_stop();checkCancelled(options);
        const auto info=result[0].GetTensorTypeAndShapeInfo();
        if(info.GetElementType()!=ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT||info.GetShape()!=std::vector<int64_t>{1,1,rows,columns})throw std::runtime_error("Depth inference returned an unexpected output shape");
        const auto* data=result[0].GetTensorData<float>();predicted.assign(data,data+plane);
    }
    float low=std::numeric_limits<float>::max(),high=std::numeric_limits<float>::lowest();
    for(float v:predicted){if(!std::isfinite(v))throw std::runtime_error("Depth inference returned non-finite values");low=std::min(low,v);high=std::max(high,v);}
    if(!(high>low))return {};
    // Enlarged to the photo's shape, then pulled onto its edges, its brightness the guide.
    const double scale=double(refinedSide)/std::max(image.width,image.height);
    const auto width=std::max(8U,std::uint32_t(std::lround(image.width*scale))),height=std::max(8U,std::uint32_t(std::lround(image.height*scale)));
    std::vector<float> enlarged(std::size_t(width)*height);
    for(std::uint32_t y=0;y<height;++y){
        checkCancelled(options);
        const float fy=std::clamp((y+.5F)*rows/height-.5F,0.F,float(rows-1));const auto y0=std::uint32_t(fy),y1=std::min(rows-1,y0+1);const float ty=fy-y0;
        for(std::uint32_t x=0;x<width;++x){
            const float fx=std::clamp((x+.5F)*columns/width-.5F,0.F,float(columns-1));const auto x0=std::uint32_t(fx),x1=std::min(columns-1,x0+1);const float tx=fx-x0;
            const float top=predicted[std::size_t(y0)*columns+x0]*(1-tx)+predicted[std::size_t(y0)*columns+x1]*tx,bottom=predicted[std::size_t(y1)*columns+x0]*(1-tx)+predicted[std::size_t(y1)*columns+x1]*tx;
            enlarged[std::size_t(y)*width+x]=(top*(1-ty)+bottom*ty-low)/(high-low);
        }
    }
    const auto guideRgb=averaged(image,width,height,options);std::vector<float> guide(std::size_t(width)*height);
    for(std::size_t i=0;i<guide.size();++i)guide[i]=.2126F*guideRgb[i*3]+.7152F*guideRgb[i*3+1]+.0722F*guideRgb[i*3+2];
    auto refined=guidedFilter(enlarged,guide,width,height,int(std::max(2U,refinedSide/48)),1e-3F);checkCancelled(options);
    for(auto& v:refined)v=std::clamp(v,0.F,1.F);
    return DepthEstimate{width,height,std::move(refined)};
}
}
