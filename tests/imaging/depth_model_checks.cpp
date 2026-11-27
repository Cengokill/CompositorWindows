// Depth and optional-subject providers on the real pinned models.
#include "onnx_depth_provider.h"
#include "onnx_subject_provider.h"
#include "wic_codec.h"
#include <iostream>
#include <stdexcept>
#include <string>
using namespace compositor::imaging;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F> std::string failure(F&& action){try{action();}catch(const std::exception& error){return error.what();}return {};}
}
int wmain(int argc,wchar_t** argv){try{
    if(argc!=4)throw std::runtime_error("Usage: depth_model_checks depth.onnx birefnet-lite.onnx astronaut.png");
    require(OnnxDepthProvider::inputSize(4000,3000)==std::pair{644U,490U}&&OnnxDepthProvider::inputSize(1080,1920)==std::pair{364U,644U}&&OnnxDepthProvider::inputSize(500,500)==std::pair{644U,644U}&&OnnxDepthProvider::inputSize(30000,10)==std::pair{644U,28U},"depth input keeps proportions in multiples of 14");
    require(failure([&]{OnnxDepthProvider missing(std::filesystem::path(argv[1]).concat(L".missing"));})=="Local depth model is missing","missing depth model is reported");
    require(failure([&]{OnnxDepthProvider foreign(argv[2]);})=="Depth model hash does not match pinned conversion","another file's fingerprint is refused");
    OnnxDepthProvider depth(argv[1]);OnnxSubjectProvider subject(argv[2]);
    auto source=WicCodec::decode(argv[3]).image;
    // A sky without a subject is not an error for the optional entry.
    RgbaImage sky{256,128,256*4,std::vector<std::uint8_t>(256*128*4)};
    for(std::uint32_t y=0;y<sky.height;++y)for(std::uint32_t x=0;x<sky.width;++x){auto* p=sky.pixels.data()+y*sky.stride+x*4;p[0]=std::uint8_t(90+y/2);p[1]=std::uint8_t(140+y/2);p[2]=230;p[3]=255;}
    require(!subject.inferOptional(sky),"empty sky has no subject and no error");
    const auto mask=subject.inferOptional(source);require(mask.has_value(),"astronaut has a subject");
    bool cancelled=false;ImportOptions stop;stop.cancelled=[&]{return cancelled;};cancelled=true;
    require(!failure([&]{depth.estimate(source,stop);}).empty(),"cancelled depth estimate throws");
    const auto estimate=depth.estimate(source);require(estimate.has_value(),"astronaut has a depth");
    require(std::max(estimate->width,estimate->height)==OnnxDepthProvider::refinedSide&&estimate->values.size()==std::size_t(estimate->width)*estimate->height,"depth refined to 640");
    double inside=0,outside=0;int ins=0,outs=0;
    for(std::uint32_t y=0;y<estimate->height;++y)for(std::uint32_t x=0;x<estimate->width;++x){
        const auto sx=x*mask->width/estimate->width,sy=y*mask->height/estimate->height;const float v=estimate->values[std::size_t(y)*estimate->width+x];
        require(v>=0&&v<=1,"depth within 0-1");
        if(mask->pixels[std::size_t(sy)*mask->width+sx]>200){inside+=v;++ins;}else if(mask->pixels[std::size_t(sy)*mask->width+sx]<20){outside+=v;++outs;}
    }
    require(ins>0&&outs>0&&inside/ins>outside/outs+.1,"subject is nearer than the backdrop");
    std::cout<<"PASS depth model: proportions, missing and foreign files refused, cancellation, subject mean "<<inside/ins<<" > backdrop "<<outside/outs<<"; empty sky has no subject\n";
    return 0;
}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
