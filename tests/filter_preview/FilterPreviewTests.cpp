// Source-derived preparation contract: Filters.swift:270-281 and
// BrushStroke.swift:40-57 at a19db901. Kernels use native runPixels as a
// controlled downstream operation; these are not CoreGraphics/CI measurements.
#include "filters/PixelFilters.h"
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace compositor;
namespace {
std::filesystem::path output;
struct Observation {std::string name;int width{},height{},maximum{};std::uint64_t differences{};};
std::vector<Observation> observations;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void save(const std::string& name,const std::vector<std::uint8_t>& bytes){
    if(output.empty())return;std::ofstream file(output/(name+".rgba"),std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));
    require(bool(file),"Evidence write failed");
}
std::shared_ptr<const Raster> fixture(int width,int height){
    std::vector<std::uint8_t> pixels(std::size_t(width)*height*4);
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){
        const unsigned alpha=unsigned((x*73+y*151)%256),iColor=unsigned((x+y)%2?231:17);
        const auto i=(std::size_t(y)*width+x)*4;
        pixels[i]=std::uint8_t(iColor*alpha/255);pixels[i+1]=std::uint8_t(unsigned((x*19+y*41)%256)*alpha/255);
        pixels[i+2]=std::uint8_t(unsigned((x*37+y*11)%256)*alpha/255);pixels[i+3]=std::uint8_t(alpha);
    }
    return Raster::fromRgba(width,height,pixels.data(),std::size_t(width)*4);
}
// Independent nearest oracle with exact integer pixel-center arithmetic.
std::shared_ptr<const Raster> prepare(const Raster& source,filters::PixelRect bounds,int width,int height){
    std::vector<std::uint8_t> pixels(std::size_t(width)*height*4);
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){
        const auto sx=bounds.x+int((std::int64_t(2*x+1)*bounds.width)/(2*width));
        const auto sy=bounds.y+int((std::int64_t(2*y+1)*bounds.height)/(2*height));
        const auto p=source.pixel(sx,sy);const auto i=(std::size_t(y)*width+x)*4;
        pixels[i]=p.r;pixels[i+1]=p.g;pixels[i+2]=p.b;pixels[i+3]=p.a;
    }
    return Raster::fromRgba(width,height,pixels.data(),std::size_t(width)*4);
}
void equal(const Raster& actual,const Raster& expected,const std::string& name){
    require(actual.width==expected.width&&actual.height==expected.height,"Output dimensions differ");
    auto a=actual.rgba(),b=expected.rgba();std::vector<std::uint8_t> diff(a.size());
    Observation observation{name,actual.width,actual.height};
    for(std::size_t i=0;i<a.size();++i){const int delta=std::abs(int(a[i])-int(b[i]));diff[i]=std::uint8_t(delta);observation.maximum=std::max(observation.maximum,delta);observation.differences+=delta!=0;}
    save(name+"-actual",a);save(name+"-expected",b);save(name+"-diff",diff);observations.push_back(observation);
    std::cout<<"OBS "<<name<<" "<<actual.width<<"x"<<actual.height<<" max_channel_difference="<<observation.maximum<<" differing_channels="<<observation.differences<<"\n";
    require(observation.differences==0,"Nearest preparation pixels differ (tolerance 0)");
}
filters::Request request(int width,int height){
    filters::Request job;job.source=fixture(width,height);job.transform={-13.25,9.5,width*1.25,height*.75,23};
    job.kind=filters::Kind::LensCorrection;job.settings.distortion=17;job.preview=true;job.seed=0x15437291;return job;
}
void nearest(int width,int height,const std::string& name){
    auto job=request(width,height);const auto original=job.source->rgba();
    const auto actual=filters::apply(job);const double factor=2048./std::max(width,height);
    const int w=std::max(1,int(width*factor)),h=std::max(1,int(height*factor));
    require(actual.raster->width==w&&actual.raster->height==h,"Source truncation grid differs");
    require(actual.sourceBounds==filters::PixelRect{0,0,width,height},"Preview source bounds changed");
    require(actual.transform==job.transform,"Preview document placement changed");
    require(actual.previewScale==double(w)/width,"Width-derived preview scale differs");
    auto prepared=prepare(*job.source,{0,0,width,height},w,h);
    auto expected=filters::runPixels(job.kind,*prepared,job.settings,double(w)/width,job.seed);
    require(job.source->rgba()==original,"Preparation mutated original pixels");equal(*actual.raster,*expected,name);
}
void cappedBlur(){
    bool matched=true;
    for(const auto kind:{filters::Kind::GaussianBlur,filters::Kind::MotionBlur}){
        auto job=request(4097,17);job.kind=kind;job.settings.radius=.75;job.settings.distance=3;job.settings.angle=37;job.retainedBlurMargin=9;
        const filters::PixelRect bounds{-9,-9,4115,35};const double factor=2048./bounds.width;
        const int w=2048,h=int(bounds.height*factor);auto actual=filters::apply(job);
        require(actual.sourceBounds==bounds,"Retained blur margin changed");
        require(actual.transform==filters::placedGrid(job.transform,4097,17,bounds),"Padded placement changed");
        require(actual.previewScale==double(w)/bounds.width,"Blur preview scale differs");
        auto prepared=prepare(*job.source,bounds,w,h);
        auto expected=filters::runPixels(kind,*prepared,job.settings,double(w)/bounds.width,job.seed);
        try{equal(*actual.raster,*expected,kind==filters::Kind::GaussianBlur?"capped_gaussian":"capped_motion");}
        catch(const std::runtime_error&){matched=false;}
    }
    require(matched,"Blur preparation pixels differ (tolerance 0)");
}
void fullApply(){
    for(const auto kind:{filters::Kind::LensCorrection,filters::Kind::AddNoise}){
        auto job=request(4097,9);job.kind=kind;job.preview=false;job.settings.amount=21;job.settings.gaussian=true;
        const auto actual=filters::apply(job);const auto expected=filters::runPixels(kind,*job.source,job.settings,1,job.seed);
        require(actual.previewScale==1&&actual.transform==job.transform,"Full Apply grid changed");
        equal(*actual.raster,*expected,kind==filters::Kind::LensCorrection?"full_lens":"full_noise");
    }
}
void cancellation(){
    auto job=request(4097,129);const auto original=job.source->rgba();int checks=0;
    job.limits.cancelled=[&]{return ++checks>=5;};bool cancelled=false;
    try{(void)filters::apply(job);}catch(const std::runtime_error& e){cancelled=std::string(e.what())=="Filter cancelled";}
    require(cancelled&&checks==5,"Preview preparation did not cancel between rows");require(job.source->rgba()==original,"Cancellation mutated input");
}
}
int main(int argc,char** argv){
    const std::string selected=argc>1?argv[1]:"all";if(argc>2){output=argv[2];std::filesystem::create_directories(output);}
    struct Case{const char* name;void(*run)();};const std::array<Case,5> cases{{
        {"nearest_rgba_odd",[]{nearest(4097,9,"nearest_rgba_odd");}},
        {"nearest_rgba_portrait",[]{nearest(9,4097,"nearest_rgba_portrait");}},
        {"capped_blur_nearest",cappedBlur},{"full_apply_unchanged",fullApply},{"cancellation",cancellation}}};
    int passed=0,failed=0;std::vector<std::pair<std::string,bool>> results;
    for(const auto& test:cases)if(selected=="all"||selected==test.name){
        try{test.run();++passed;results.emplace_back(test.name,true);std::cout<<"PASS "<<test.name<<"\n";}
        catch(const std::exception& error){++failed;results.emplace_back(test.name,false);std::cout<<"FAIL "<<test.name<<": "<<error.what()<<"\n";}
    }
    if(results.empty()){std::cerr<<"Unknown case\n";return 2;}
    if(!output.empty()){
        std::ofstream report(output/"results.json");report<<"{\"schema\":\"FILTER_PREVIEW_NEAREST_V1\",\"tolerance\":0,\"passed\":"<<passed<<",\"failed\":"<<failed<<",\"cases\":[";
        for(std::size_t i=0;i<results.size();++i){if(i)report<<",";report<<"{\"name\":\""<<results[i].first<<"\",\"pass\":"<<(results[i].second?"true":"false")<<"}";}
        report<<"],\"comparisons\":[";
        for(std::size_t i=0;i<observations.size();++i){const auto& o=observations[i];if(i)report<<",";report<<"{\"name\":\""<<o.name<<"\",\"width\":"<<o.width<<",\"height\":"<<o.height<<",\"max_channel_difference\":"<<o.maximum<<",\"differing_channels\":"<<o.differences<<"}";}
        report<<"]}\n";
    }
    return failed?1:0;
}
