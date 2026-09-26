// The before branch invokes the exact frozen inferImpl preprocessing prefix.
// Including its TU exposes the internal function without adding a production test hook.
#ifdef SUBJECT_INPUT_PRODUCTION_ONLY
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <cmath>
#else
#include "onnx_subject_provider.cpp"
#endif
#include "subject_input.h"
#include <cstdio>
#include <iomanip>
#include <sstream>
using namespace compositor::imaging;
namespace {
void require(bool condition, const char* message) { if(!condition) throw std::runtime_error(message); }
template<class T> std::vector<T> read(const std::filesystem::path& path, std::size_t count) {
    require(std::filesystem::file_size(path)==count*sizeof(T), "Required fixture has incorrect byte length");
    std::ifstream stream(path,std::ios::binary);
    std::vector<T> out(count);
    stream.read(reinterpret_cast<char*>(out.data()),std::streamsize(count*sizeof(T)));
    require(bool(stream),"Required fixture could not be read");
    return out;
}
std::vector<float> call(bool before, const RgbaImage& image, const ImportOptions& options,
                        SubjectInputStats& stats) {
    if(!before) return subjectInputTensor(image,options,&stats);
#ifdef SUBJECT_INPUT_PRODUCTION_ONLY
    throw std::runtime_error("Before mode requires the frozen standalone provider source");
#else
    validate(image);checkCancelled(options);checkedBytes(image.width,image.height,4,options);
    return preprocess(image,options);
#endif
}
RgbaImage fixture(const std::filesystem::path& directory, std::uint32_t width,
                  std::uint32_t height, std::size_t stride) {
    return {width,height,stride,read<std::uint8_t>(directory/L"source.rgba",stride*height)};
}
std::string escaped(const std::string& input) {
    std::string out;
    for(char c:input) { if(c=='"'||c=='\\')out+='\\';if(c=='\n')out+="\\n";else if(c!='\r')out+=c; }
    return out;
}
}
int wmain(int argc,wchar_t** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    if(argc!=9) {std::fprintf(stderr,"mode fixture-directory width height stride case output-json kind\n");return 2;}
    const bool before=std::wstring(argv[1])==L"before";
    const std::string key=std::filesystem::path(argv[6]).string();
    const std::filesystem::path output(argv[7]);
    const bool resource=std::wstring(argv[8])==L"resource";
    SubjectInputStats stats;
    double maximum=0, mean=0;
    std::uint64_t differing=0;
    int maximumByte=0;
    std::string error;
    bool passed=false;
    try {
        require(!std::filesystem::exists(output),"Fresh result file required");
        auto input=fixture(argv[2],std::uint32_t(std::stoul(argv[3])),
                           std::uint32_t(std::stoul(argv[4])),std::stoull(argv[5]));
        ImportOptions options;
        if(!resource) {
            const auto expected=read<float>(std::filesystem::path(argv[2])/L"expected.f32",3145728);
            const auto rgb=read<std::uint8_t>(std::filesystem::path(argv[2])/L"expected.rgb",3145728);
            const auto actual=call(before,input,options,stats);
            require(actual.size()==expected.size(),"Tensor shape differs");
            constexpr float means[]{.485F,.456F,.406F}, deviations[]{.229F,.224F,.225F};
            for(std::size_t i=0;i<actual.size();++i) {
                require(std::isfinite(actual[i]),"Non-finite input tensor");
                const double delta=std::abs(double(actual[i])-expected[i]);
                maximum=std::max(maximum,delta);mean+=delta;
                const auto channel=i/(1024*1024), pixel=i%(1024*1024);
                const int byte=int(std::lround((double(actual[i])*deviations[channel]+means[channel])*255));
                const int difference=std::abs(byte-int(rgb[pixel*3+channel]));
                if(difference)++differing;
                maximumByte=std::max(maximumByte,difference);
            }
            mean/=double(actual.size());
            require(maximumByte==0,"Resized RGB differs from exact Pillow bytes");
            require(maximum<=.000001,"Tensor exceeds frozen float32 tolerance");
        } else if(key=="source_unchanged") {
            const auto original=input.pixels;
            (void)call(before,input,options,stats);
            require(input.pixels==original,"Source bytes or padding were mutated");
        } else if(key=="maximum_axis_memory") {
            (void)call(before,input,options,stats);
            require(stats.outputBytes==12582912,"Output payload accounting differs");
            require(stats.temporaryBytes<=1048576,"Temporary payload exceeds 1MiB");
            require(stats.maximumCachedRows==61,"30000-pixel side did not exercise maximum row support");
            require(stats.preparedSourceRows==30000,"Maximum-axis fixture failed to exercise every source row");
        } else {
            std::string expected;
            if(key=="cancel_before") {
                input.pixels[0]=255;input.pixels[3]=0;
                options.cancelled=[] {return true;};expected="cancelled";
            } else if(key=="cancel_validation") {
                options.cancelled=[&] {return stats.validatedPixels>=4096;};expected="cancelled";
            } else if(key=="cancel_resample") {
                options.cancelled=[&] {return stats.preparedSourceRows>=1;};expected="cancelled";
            } else if(key=="budget_reject") {
                options.maxWorkingBytes=std::uint64_t(input.stride)*input.height+12582912-1;
                expected="working buffer budget";
            } else if(key=="invalid_premultiplication") {
                input.pixels[0]=255;input.pixels[3]=0;expected="not premultiplied";
            } else if(key=="invalid_stride") {
                input.stride=std::size_t(input.width)*4-1;expected="stride";
            } else if(key=="dimension_preflight") {
                input.width=30001;expected="dimensions";
            } else throw std::runtime_error("Unknown resource case");
            std::string thrown;
            try {(void)call(before,input,options,stats);}catch(const std::exception& caught){thrown=caught.what();}
            require(thrown.find(expected)!=std::string::npos,"Expected rejection or cancellation was not observed");
            if(!before && key=="cancel_validation")
                require(stats.validatedPixels==4096 && stats.preparedSourceRows==0,"Validation cancellation exceeded frozen pixel interval");
            if(!before && key=="cancel_resample")
                require(stats.preparedSourceRows==1,"Sampling cancellation exceeded one completed source row");
            if(!before && key=="budget_reject")
                require(stats.validatedPixels==0 && stats.preparedSourceRows==0,"Budget was checked after work started");
        }
        passed=true;
    } catch(const std::exception& caught) {error=caught.what();}
    std::ofstream report(output);
    report<<std::setprecision(17)<<"{\"schema\":\"SUBJECT_INPUT_RESULT_V1\",\"case\":\""<<key
        <<"\",\"mode\":\""<<(before?"before":"candidate")<<"\",\"status\":\""<<(passed?"passed":"failed")
        <<"\",\"error\":\""<<escaped(error)<<"\",\"tensor_max_abs\":"<<maximum<<",\"tensor_mean_abs\":"<<mean
        <<",\"rgb_max_byte_difference\":"<<maximumByte<<",\"rgb_differing_channels\":"<<differing
        <<",\"source_bytes\":"<<stats.sourceBytes<<",\"output_bytes\":"<<stats.outputBytes
        <<",\"temporary_bytes\":"<<stats.temporaryBytes<<",\"validated_pixels\":"<<stats.validatedPixels
        <<",\"prepared_source_rows\":"<<stats.preparedSourceRows<<",\"maximum_cached_rows\":"<<stats.maximumCachedRows
        <<",\"cancellation_checks\":"<<stats.cancellationChecks<<"}\n";
    if(!report){std::fprintf(stderr,"Could not save result\n");return 2;}
    std::printf("%s %s %s max=%g byte=%d %s\n",passed?"PASS":"FAIL",before?"before":"candidate",key.c_str(),maximum,maximumByte,error.c_str());
    return passed?0:1;
}
