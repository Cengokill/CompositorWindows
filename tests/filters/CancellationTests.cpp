#include "filters/PixelFilters.h"
#include "graphics/PixelAlgorithms.h"
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

using namespace compositor;
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct CallbackFailure {};
constexpr int width = 64, height = 32;
graphics::Rgba8View view(std::vector<uint8_t>& bytes) { return {bytes,width,height,width*4}; }
filters::Kind kind(const std::string& name) {
    if (name.starts_with("noise_")) return filters::Kind::AddNoise;
    if (name.starts_with("lens_")) return filters::Kind::LensCorrection;
    if (name.starts_with("fill_")) return filters::Kind::ContentAwareFill;
    throw std::runtime_error("Unknown kernel");
}
}
int main(int argc, char** argv) { try {
    require(argc == 2,"Provide case");
    const std::string name=argv[1];const auto selectedKind=kind(name);
    const auto source=Raster::filled(width,height,{70,100,150,255});
    const auto original=source->rgba();auto output=original;
    GrayRaster mask{width,height,std::vector<uint8_t>(width*height)};
    for(int y=8;y<24;++y)for(int x=16;x<48;++x)mask.pixels[size_t(y)*width+x]=255;
    int checks=0;bool observed=false;
    const bool filter=name.ends_with("_filter"),exception=name.ends_with("_throw");
    require(filter||exception||name.ends_with("_stop"),"Unknown operation");
    const int target=filter?8:3;
    graphics::Cancellation cancelled=[&]{if(++checks<target)return false;if(exception)throw CallbackFailure{};return true;};
    try {
        if(filter){filters::Settings settings;settings.gaussian=true;settings.distortion=85;filters::Limits limits;limits.cancelled=cancelled;
            filters::runPixels(selectedKind,*source,settings,1,77,selectedKind==filters::Kind::ContentAwareFill?&mask:nullptr,limits);
        } else if(selectedKind==filters::Kind::AddNoise) graphics::addNoise(view(output),80,true,false,77,cancelled);
        else if(selectedKind==filters::Kind::LensCorrection) graphics::lensDistort({original,width,height,width*4},view(output),.7,cancelled);
        else graphics::contentFill(view(output),{mask.pixels,width,height,width},cancelled);
    } catch(const CallbackFailure&) { require(exception,"Unexpected callback exception");observed=true; }
      catch(const std::runtime_error& error) { require(!exception&&std::string(error.what()).find("cancelled")!=std::string::npos,"Cancellation was misclassified");observed=true; }
    require(observed&&checks==target,"Kernel did not stop at the requested bounded checkpoint");
    require(source->rgba()==original,"Cancelled operation mutated immutable source");
    std::printf("PASS %s: %d checks; no result published; immutable source\n",name.c_str(),checks);return 0;
}catch(const std::exception& error){std::fprintf(stderr,"FAIL %s\n",error.what());return 1;}}
