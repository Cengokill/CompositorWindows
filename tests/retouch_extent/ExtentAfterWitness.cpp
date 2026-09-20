// After adapter: original frozen corpus and loss predicates; Layer snapshot result.
// A native padded-grid control isolates extent handling; this is not Mac differential evidence.
#include "retouch/RetouchSession.h"
#include "graphics/RasterSampling.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace compositor;
using namespace compositor::retouch;
namespace {
constexpr int width=64,height=32;
constexpr Pixel color{200,40,20,255};
constexpr Transform smallPlacement{8,8,16,16,0,false,false,Transform::Sampling::Nearest};
constexpr Transform fullPlacement{0,0,width,height,0,false,false,Transform::Sampling::Nearest};
bool outside(int x,int y){return x<8||y<8||x>=24||y>=24;}
std::shared_ptr<const Raster> padded(){
    std::array<Pixel,16*16> pixels;pixels.fill(color);
    return Raster::filled(width,height)->replacing(8,8,16,16,pixels.data(),16);
}
Pixel documentPixel(const Raster& raster,Transform placement,int x,int y){
    return graphics::sampleRaster(raster,placement.toUnit({x+.5,y+.5}),Transform::Sampling::Nearest);
}
void raw(const std::filesystem::path& path,const std::array<Pixel,width*height>& pixels){
    std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(pixels.data()),sizeof(pixels));
    if(!out)throw std::runtime_error("Cannot write raw RGBA witness");
}
struct Case{const char* id;Mode mode;Point start,end,probe;};
constexpr std::array cases{
    Case{"clone",Mode::Clone,{40,16},{40,16},{40,16}},
    Case{"heal_content",Mode::HealContentAware,{24,16},{24,16},{24,16}},
    Case{"heal_texture",Mode::HealCreateTexture,{24,16},{24,16},{24,16}},
    Case{"heal_proximity",Mode::HealProximity,{24,16},{24,16},{24,16}},
    Case{"blur",Mode::Blur,{24,16},{24,16},{24,16}},
    Case{"smudge",Mode::Smudge,{22,16},{26,16},{26,16}},
    Case{"liquify",Mode::Liquify,{22,16},{26,16},{26,16}}
};
}
int main(int argc,char** argv){try{
    if(argc!=2)throw std::runtime_error("usage: ExtentBeforeWitness.exe fresh-output-directory");
    const std::filesystem::path directory(argv[1]);
    if(std::filesystem::exists(directory))throw std::runtime_error("Output directory must be new");
    std::filesystem::create_directories(directory);
    auto small=Raster::filled(16,16,color),full=padded();
    std::ofstream json(directory/"results.json");
    json<<"{\"schema\":\"RETOUCH_EXTENT_AFTER_V1\",\"mac_differential\":false,\"cases\":[";
    int failed=0,missing=0;bool first=true;
    for(const auto& c:cases){
        Settings settings;settings.mode=c.mode;settings.radius=(c.mode==Mode::Smudge||c.mode==Mode::Liquify)?4:3;
        settings.hardness=1;settings.opacity=1;settings.healingSeed=1;
        Sources sources;if(c.mode==Mode::Clone)sources.cloneOffset=Point{-24,0};
        Layer originalLayer;originalLayer.id="extent-target";originalLayer.raster=small;originalLayer.transform=smallPlacement; RetouchSession actual(originalLayer,width,height,settings,sources);
        RetouchSession control(full,fullPlacement,width,height,settings,sources);
        const bool actualBegan=actual.begin(c.start),controlBegan=control.begin(c.start);
        if(c.end!=c.start){actual.append(c.end);control.append(c.end);}
        int liveOutsideAlpha=-1;
        if(c.mode==Mode::Smudge||c.mode==Mode::Liquify)liveOutsideAlpha=actual.warpDocumentPreview()->pixel(int(c.probe.x),int(c.probe.y)).a;
        const auto resultLayer=actual.commitSnapshot()->materializeLayer();auto result=resultLayer.raster,reference=control.commit();
        std::array<Pixel,width*height> actualPixels{},controlPixels{},difference{};
        int outsideExpected=0,lost=0,maxError=0;
        for(int y=0;y<height;++y)for(int x=0;x<width;++x){
            const auto i=size_t(y)*width+x;auto a=documentPixel(*result,resultLayer.transform,x,y),b=reference->pixel(x,y);
            actualPixels[i]=a;controlPixels[i]=b;
            const auto error=uint8_t(std::abs(int(a.a)-int(b.a)));difference[i]={error,error,error,255};maxError=std::max(maxError,int(error));
            if(outside(x,y)&&b.a){++outsideExpected;if(a.a<b.a)++lost;}
        }
        const bool precondition=controlBegan&&outsideExpected>0&&(c.mode!=Mode::Clone||reference->pixel(40,16)==color);
        const char* status=!precondition?"PRECONDITION_MISSING":lost?"FAIL_LOST_OUTSIDE_PIXELS":"PASS";
        if(!precondition)++missing;else if(lost)++failed;
        raw(directory/(std::string(c.id)+"-actual.rgba"),actualPixels);raw(directory/(std::string(c.id)+"-padded-control.rgba"),controlPixels);raw(directory/(std::string(c.id)+"-alpha-diff.rgba"),difference);
        if(!first)json<<',';first=false;
        json<<"{\"id\":\""<<c.id<<"\",\"status\":\""<<status<<"\",\"actual_began\":"<<(actualBegan?"true":"false")<<",\"control_began\":"<<(controlBegan?"true":"false")<<",\"expected_outside_nonzero_pixels\":"<<outsideExpected<<",\"lost_outside_pixels\":"<<lost<<",\"max_alpha_error\":"<<maxError<<",\"warp_live_probe_alpha\":"<<liveOutsideAlpha<<",\"commit_probe_alpha\":"<<int(documentPixel(*result,resultLayer.transform,int(c.probe.x),int(c.probe.y)).a)<<'}';
        std::cout<<status<<' '<<c.id<<" outside="<<outsideExpected<<" lost="<<lost<<" live="<<liveOutsideAlpha<<'\n';
    }
    json<<"],\"failed\":"<<failed<<",\"missing_preconditions\":"<<missing<<"}\n";
    return missing?2:failed?1:0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 3;}}

