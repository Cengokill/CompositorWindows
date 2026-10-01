#include "SelectionExtras.h"
#include <algorithm>
#include <cmath>
#include <queue>
#include <stdexcept>
#include <utility>
namespace compositor::editing {
namespace {
std::shared_ptr<GrayRaster> dense(int width,int height){auto raster=std::make_shared<GrayRaster>();raster->width=width;raster->height=height;raster->pixels.assign(size_t(width)*height,0);return raster;}
uint8_t blurred(const GrayRaster& source,int x,int y,int radius){double sum=0,weight=0;for(int dy=-radius;dy<=radius;++dy)for(int dx=-radius;dx<=radius;++dx){double w=std::max(0.,radius-std::hypot(dx,dy)+1);if(w<=0)continue;sum+=source.pixel(std::clamp(x+dx,0,source.width-1),std::clamp(y+dy,0,source.height-1))*w;weight+=w;}return uint8_t(std::clamp(std::lround(sum/std::max(weight,1.)),0L,255L));}
int channel(Pixel pixel,uint8_t value){return pixel.a?int(std::lround(value*255./pixel.a)):0;}
int distance(Pixel a,Pixel b){return std::max({std::abs(channel(a,a.r)-channel(b,b.r)),std::abs(channel(a,a.g)-channel(b,b.g)),std::abs(channel(a,a.b)-channel(b,b.b))});}
}
Selection featherSelection(const Selection& selection,double radius){
    if(!selection.coverage||!std::isfinite(radius)||radius<0||radius>250)throw std::runtime_error("Invalid feather radius");
    auto soft=dense(selection.coverage->width,selection.coverage->height);if(radius<=0){for(int y=0;y<soft->height;++y)for(int x=0;x<soft->width;++x)soft->pixels[size_t(y)*soft->width+x]=selection.coverage->pixel(x,y);Selection result;result.coverage=soft;return result;}
    int kernel=std::clamp(int(std::ceil(radius)),1,32);
    for(int y=0;y<soft->height;++y)for(int x=0;x<soft->width;++x)soft->pixels[size_t(y)*soft->width+x]=blurred(*selection.coverage,x,y,kernel);
    Selection result;result.coverage=soft;return result;
}
std::shared_ptr<const GrayRaster> colorRangeMask(const Raster& source,int x,int y,int tolerance){
    if(x<0||y<0||x>=source.width||y>=source.height||tolerance<0||tolerance>255)throw std::runtime_error("Invalid color range");
    auto seed=source.pixel(x,y);auto mask=dense(source.width,source.height);
    for(int py=0;py<source.height;++py)for(int px=0;px<source.width;++px)if(distance(seed,source.pixel(px,py))<=tolerance)mask->pixels[size_t(py)*source.width+px]=255;return mask;
}
std::shared_ptr<const GrayRaster> connectedComponent(const GrayRaster& mask,int x,int y,int threshold){
    if(mask.width<=0||mask.height<=0)throw std::runtime_error("Invalid mask");
    auto out=dense(mask.width,mask.height);if(x<0||y<0||x>=mask.width||y>=mask.height||mask.pixel(x,y)<threshold)return out;
    std::queue<std::pair<int,int>> pending;pending.push({x,y});out->pixels[size_t(y)*mask.width+x]=255;
    while(!pending.empty()){auto [cx,cy]=pending.front();pending.pop();for(auto [dx,dy]:{std::pair{1,0},{-1,0},{0,1},{0,-1}}){int nx=cx+dx,ny=cy+dy;if(nx<0||ny<0||nx>=mask.width||ny>=mask.height)continue;auto index=size_t(ny)*mask.width+nx;if(out->pixels[index]||mask.pixel(nx,ny)<threshold)continue;out->pixels[index]=255;pending.push({nx,ny});}}
    return out;
}
}
