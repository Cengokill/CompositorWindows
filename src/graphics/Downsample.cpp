#include "Downsample.h"
#include "MaskSampling.h"
#include "DownsampleKernel.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <stdexcept>
namespace compositor::graphics {
namespace {
void dimensions(int width,int height){if(width<1||height<1||width>30000||height>30000||uint64_t(width)*height>100000000)throw std::invalid_argument("Invalid downsample source extent");}
uint8_t byte(double value){return uint8_t(std::clamp(std::lround(value),0L,255L));}
// Keep only the twenty horizontally filtered rows needed by one destination row.
// This avoids a source-sized intermediate floating-point raster.
template<int Channels,class Reader,class Writer>void halvePixels(int sourceWidth,int sourceHeight,int width,int height,const Reader& read,const Writer& write){
    const auto& kernel=halvingWeights();using Row=std::vector<std::array<double,Channels>>;std::map<int,Row> rows;
    for(int y=0;y<height;++y){const int first=2*y-9,last=first+19;for(auto it=rows.begin();it!=rows.end();)if(it->first<first)it=rows.erase(it);else ++it;
        for(int sourceY=first;sourceY<=last;++sourceY)if(!rows.contains(sourceY)){Row row(size_t(width),std::array<double,Channels>{});for(int x=0;x<width;++x)for(int tap=0;tap<20;++tap){auto pixel=read(2*x-9+tap,sourceY,sourceWidth,sourceHeight);for(int c=0;c<Channels;++c)row[size_t(x)][size_t(c)]+=pixel[size_t(c)]*kernel[size_t(tap)];}rows.emplace(sourceY,std::move(row));}
        for(int x=0;x<width;++x){std::array<double,Channels> value{};for(int tap=0;tap<20;++tap){const auto& row=rows.at(first+tap);for(int c=0;c<Channels;++c)value[size_t(c)]+=row[size_t(x)][size_t(c)]*kernel[size_t(tap)];}write(x,y,value);}
    }
}
template<class Image>struct Entry{std::shared_ptr<const Image> source;std::vector<std::shared_ptr<const Image>> levels;uint64_t use{};uint64_t pixels()const{uint64_t total=0;for(const auto& level:levels)total+=uint64_t(level->width)*level->height;return total;}};
Transform coverage(const Transform& original,int oldWidth,int oldHeight,int width,int height,int level){auto result=original;double xs=double(uint64_t(width)<<level)/oldWidth,ys=double(uint64_t(height)<<level)/oldHeight;auto center=original.fromUnit({xs/2,ys/2});result.width*=xs;result.height*=ys;result.x=center.x-result.width/2;result.y=center.y-result.height/2;return result;}
}
struct DownsampleCache::Impl{
    mutable std::mutex mutex;uint64_t budget,tick{};std::map<const Raster*,Entry<Raster>> rgba;std::map<const GrayRaster*,Entry<GrayRaster>> gray;
    explicit Impl(uint64_t value):budget(value){}
    uint64_t pixels()const{uint64_t count=0;for(const auto& [key,entry]:rgba){(void)key;count+=entry.pixels();}for(const auto& [key,entry]:gray){(void)key;count+=entry.pixels();}return count;}
    void evict(const void* keep){while(pixels()>budget){uint64_t oldest=UINT64_MAX;const Raster* color=nullptr;const GrayRaster* mask=nullptr;for(const auto& [key,entry]:rgba)if(key!=keep&&entry.use<oldest){oldest=entry.use;color=key;mask=nullptr;}for(const auto& [key,entry]:gray)if(key!=keep&&entry.use<oldest){oldest=entry.use;mask=key;color=nullptr;}if(color)rgba.erase(color);else if(mask)gray.erase(mask);else break;}}
};
DownsampleCache::DownsampleCache(uint64_t budget):impl_(std::make_unique<Impl>(budget)){if(!budget)throw std::invalid_argument("Downsample budget must be positive");}
DownsampleCache::~DownsampleCache()=default;
int DownsampleCache::levelFor(double factor){if(!std::isfinite(factor)||factor<=0||factor>=.5)return 0;if(factor<=std::ldexp(1.,-maxLevel))return maxLevel;return std::min(maxLevel,int(std::floor(std::log2(1/factor))));}
std::shared_ptr<const Raster> DownsampleCache::halve(const Raster& source){dimensions(source.width,source.height);if(source.tiles.size()!=size_t((source.width+255)/256)*((source.height+255)/256)||std::any_of(source.tiles.begin(),source.tiles.end(),[](const auto& tile){return !tile;}))throw std::invalid_argument("Invalid downsample tile storage");int width=(source.width+1)/2,height=(source.height+1)/2;auto result=std::make_shared<Raster>(*Raster::filled(width,height));std::vector<std::shared_ptr<Raster::Tile>> tiles;for(size_t i=0;i<result->tiles.size();++i){auto tile=std::make_shared<Raster::Tile>();result->tiles[i]=tile;tiles.push_back(std::move(tile));}int columns=(width+255)/256;
    // Transparent exterior is equivalent to the source's eight-pixel padding
    // and four-pixel output crop; premultiplied channels clamp after both passes.
    halvePixels<4>(source.width,source.height,width,height,[&](int x,int y,int,int){auto p=source.pixel(x,y);return std::array<double,4>{double(p.r),double(p.g),double(p.b),double(p.a)};},[&](int x,int y,const std::array<double,4>& p){auto alpha=byte(p[3]);tiles[size_t(y/256)*columns+x/256]->pixels[size_t(y%256)*256+x%256]={std::min(byte(p[0]),alpha),std::min(byte(p[1]),alpha),std::min(byte(p[2]),alpha),alpha};});return result;
}
std::shared_ptr<const GrayRaster> DownsampleCache::halve(const GrayRaster& source){dimensions(source.width,source.height);if(source.pixels.size()!=size_t(source.width)*source.height)throw std::invalid_argument("Invalid gray downsample storage");auto result=std::make_shared<GrayRaster>(GrayRaster{(source.width+1)/2,(source.height+1)/2,{}});result->pixels.resize(size_t(result->width)*result->height);
    halvePixels<1>(source.width,source.height,result->width,result->height,[&](int x,int y,int width,int height){return std::array<double,1>{double(source.pixel(std::clamp(x,0,width-1),std::clamp(y,0,height-1)))};},[&](int x,int y,const std::array<double,1>& p){result->pixels[size_t(y)*result->width+x]=byte(p[0]);});return result;
}
DownsampledRaster DownsampleCache::level(std::shared_ptr<const Raster> source,int wanted){if(!source)throw std::invalid_argument("Missing downsample image");dimensions(source->width,source->height);if(wanted<1||source->width==1&&source->height==1)return {std::move(source),0};std::lock_guard lock(impl_->mutex);auto& entry=impl_->rgba[source.get()];entry.source=source;entry.use=++impl_->tick;while(int(entry.levels.size())<wanted){auto previous=entry.levels.empty()?source:entry.levels.back();if(previous->width==1&&previous->height==1)break;entry.levels.push_back(halve(*previous));}int applied=std::min(wanted,int(entry.levels.size()));auto output=entry.levels[size_t(applied-1)];impl_->evict(source.get());return {std::move(output),applied};}
DownsampledGray DownsampleCache::level(std::shared_ptr<const GrayRaster> source,int wanted){if(!source)throw std::invalid_argument("Missing downsample mask");dimensions(source->width,source->height);if(wanted<1||source->width==1&&source->height==1)return {std::move(source),0};std::lock_guard lock(impl_->mutex);auto& entry=impl_->gray[source.get()];entry.source=source;entry.use=++impl_->tick;while(int(entry.levels.size())<wanted){auto previous=entry.levels.empty()?source:entry.levels.back();if(previous->width==1&&previous->height==1)break;entry.levels.push_back(halve(*previous));}int applied=std::min(wanted,int(entry.levels.size()));auto output=entry.levels[size_t(applied-1)];impl_->evict(source.get());return {std::move(output),applied};}
uint64_t DownsampleCache::retainedPixels()const{std::lock_guard lock(impl_->mutex);return impl_->pixels();}
size_t DownsampleCache::entryCount()const{std::lock_guard lock(impl_->mutex);return impl_->rgba.size()+impl_->gray.size();}
void DownsampleCache::clear(){std::lock_guard lock(impl_->mutex);impl_->rgba.clear();impl_->gray.clear();impl_->tick=0;}
Layer downsampleLayer(const Layer& original,double device,DownsampleCache& cache){if(!std::isfinite(device)||device<=0)throw std::invalid_argument("Invalid downsample device scale");auto layer=original;if(!layer.raster||layer.transform.sampling==Transform::Sampling::Nearest)return layer;const auto image=layer.raster;auto result=cache.level(image,DownsampleCache::levelFor(layer.transform.width*device/image->width));if(result.level){layer.raster=result.image;layer.transform=coverage(original.transform,image->width,image->height,result.image->width,result.image->height,result.level);}if(layer.mask){auto placement=original.mask->placement.value_or(original.transform);auto mask=original.mask->raster;auto reduced=cache.level(mask,DownsampleCache::levelFor(placement.width*device/mask->width));layer.mask->previewExterior=original.mask->previewExterior.value_or(original.mask->placement?cachedMaskBackground(mask):0);layer.mask->raster=reduced.image;layer.mask->placement=coverage(placement,mask->width,mask->height,reduced.image->width,reduced.image->height,reduced.level);}return layer;}
}
