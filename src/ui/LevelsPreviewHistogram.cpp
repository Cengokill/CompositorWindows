#include "LevelsPreviewHistogram.h"
#include "editing/Selection.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
extern "C" {
#include "graphics/upstream/LevelsPixels.h"
}
namespace compositor {
LevelsPreviewHistogramResult levelsPreviewHistogram(const Raster& source,const Transform& mapping,
    const Document& document,const std::function<bool()>& cancelled){
    auto check=[&]{if(cancelled&&cancelled())throw std::runtime_error("Histogram cancelled");};check();
    if(source.width<1||source.height<1||source.width>30000||source.height>30000||
       uint64_t(source.width)*source.height>100000000||!mapping.valid()||
       source.tiles.size()!=size_t((source.width+255)/256)*size_t((source.height+255)/256)||
       std::any_of(source.tiles.begin(),source.tiles.end(),[](const auto& tile){return !tile;}))
        throw std::invalid_argument("Invalid Levels preview source");
    const double factor=std::min(1.,8000./std::max(source.width,source.height));
    LevelsPreviewHistogramResult result;
    result.width=std::max(1,int(source.width*factor));result.height=std::max(1,int(source.height*factor));
    // The source's previewMapping places this grid in the same full layer
    // transform. Keep the existing coverage sampler, including canvas clipping.
    const auto coverage=editing::mappedCoverage(document,mapping,result.width,result.height);
    std::vector<Pixel> row(size_t(result.width));
    for(int y=0;y<result.height;++y){
        check();const int sourceY=std::min(source.height-1,int(std::floor((y+.5)*source.height/result.height)));
        for(int x=0;x<result.width;++x){const int sourceX=std::min(source.width-1,int(std::floor((x+.5)*source.width/result.width)));row[size_t(x)]=source.pixel(sourceX,sourceY);}
        levels_histogram(reinterpret_cast<const uint8_t*>(row.data()),coverage?coverage->pixels.data()+size_t(y)*result.width:nullptr,size_t(result.width),result.bins[0].data());
    }
    return result;
}
}
