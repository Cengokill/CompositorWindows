#include "SamplingSource.h"
#include "DownsampleKernel.h"
#include "RasterSampling.h"
#include <algorithm>
#include <compare>
#include <map>
#include <mutex>
#include <stdexcept>

namespace compositor::graphics {
namespace {
int64_t floorDivide(int64_t value,int64_t divisor){return value>=0?value/divisor:-1-((-1-value)/divisor);}
uint8_t byte(double value){return uint8_t(std::clamp(std::lround(value),0L,255L));}
struct SourceKey {
    uintptr_t identity{};int width{},height{},x{},y{};bool gray{};
    auto operator<=>(const SourceKey&)const=default;
};
SourceKey key(const SamplingSource& s){return {reinterpret_cast<uintptr_t>(s.identity.get()),s.width,s.height,s.alignmentX,s.alignmentY,bool(s.gray)};}
struct TileKey {SourceKey source;int level{},x{},y{};auto operator<=>(const TileKey&)const=default;};
struct LevelKey {SourceKey source;int level{};auto operator<=>(const LevelKey&)const=default;};
}
SamplingGrid samplingGrid(const SamplingSource& source,int level){
    if(level<0||level>16||source.width<1||source.height<1||source.width>30000||source.height>30000||
       std::abs(int64_t(source.alignmentX))>10000000||std::abs(int64_t(source.alignmentY))>10000000)
        throw std::invalid_argument("Invalid reduction grid");
    const int64_t step=int64_t(1)<<level;
    const auto first=[&](int alignment){return int(int64_t(alignment)+floorDivide(-int64_t(alignment),step)*step);};
    const auto last=[&](int size,int alignment){return int(int64_t(alignment)-floorDivide(int64_t(alignment)-size,step)*step);};
    const int x=first(source.alignmentX),y=first(source.alignmentY);
    return {x,y,int((last(source.width,source.alignmentX)-x)/step),int((last(source.height,source.alignmentY)-y)/step),int(step)};
}
struct ReducedSource::Storage {
    struct TileEntry{std::shared_ptr<const SamplingSource> source;std::shared_ptr<const Raster::Tile> tile;uint64_t use{};};
    struct LevelEntry{std::weak_ptr<const ReducedSource> handle;uint64_t use{};};
    mutable std::mutex mutex;
    std::map<TileKey,TileEntry> tiles;
    std::map<LevelKey,LevelEntry> levels;
    size_t tileBudget,sourceBudget;uint64_t tick{},generated{};
    Storage(size_t tileLimit,size_t sourceLimit):tileBudget(tileLimit),sourceBudget(sourceLimit){}
    void trim(){while(tiles.size()>tileBudget){auto oldest=std::min_element(tiles.begin(),tiles.end(),[](const auto& a,const auto& b){return a.second.use<b.second.use;});tiles.erase(oldest);}}
    Pixel pixel(const std::shared_ptr<const SamplingSource>& source,int level,int x,int y){
        const auto grid=samplingGrid(*source,level);
        if(source->gray){x=std::clamp(x,0,grid.width-1);y=std::clamp(y,0,grid.height-1);}
        else if(x<0||y<0||x>=grid.width||y>=grid.height)return {};
        if(level==0){if(source->gray){const auto v=source->gray(x,y);return {v,v,v,255};}return source->rgba(x,y);}
        const TileKey tileKey{key(*source),level,x/256,y/256};
        auto found=tiles.find(tileKey);
        if(found==tiles.end()){
            auto tile=generate(source,level,tileKey.x,tileKey.y);
            found=tiles.emplace(tileKey,TileEntry{source,std::move(tile),++tick}).first;
            ++generated;trim();
        }else found->second.use=++tick;
        return found->second.tile->pixels[size_t(y%256)*256+x%256];
    }
    std::shared_ptr<const Raster::Tile> generate(const std::shared_ptr<const SamplingSource>& source,int level,int tx,int ty){
        const auto grid=samplingGrid(*source,level),previous=samplingGrid(*source,level-1);
        const int x0=tx*256,y0=ty*256,width=std::min(256,grid.width-x0),height=std::min(256,grid.height-y0);
        const int ox=(grid.x-previous.x)/previous.step,oy=(grid.y-previous.y)/previous.step;
        const int channels=source->gray?1:4;const auto& weights=halvingWeights();
        auto output=std::make_shared<Raster::Tile>();
        using Row=std::vector<std::array<double,4>>;std::map<int,Row> rows;
        // Horizontal rows plus a 20-row ring; recursive lower levels are cached
        // in bounded tiles. A tile never needs a full source or level raster.
        for(int y=0;y<height;++y){const int first=2*(y0+y)+oy-9;
            for(auto it=rows.begin();it!=rows.end();)if(it->first<first)it=rows.erase(it);else ++it;
            for(int sy=first;sy<first+20;++sy)if(!rows.contains(sy)){
                Row row(size_t(width),std::array<double,4>{});
                // Cache this scanline locally so overlapping twenty-tap windows
                // do not repeatedly traverse the recursive tile map.
                std::vector<Pixel> line(size_t(2*width+18));const int left=2*x0+ox-9;
                for(size_t i=0;i<line.size();++i)line[i]=pixel(source,level-1,left+int(i),sy);
                for(int x=0;x<width;++x)for(int tap=0;tap<20;++tap){const auto p=line[size_t(2*x+tap)];const std::array<double,4> values{double(p.r),double(p.g),double(p.b),double(p.a)};for(int c=0;c<channels;++c)row[size_t(x)][size_t(c)]+=values[size_t(c)]*weights[size_t(tap)];}
                rows.emplace(sy,std::move(row));
            }
            for(int x=0;x<width;++x){std::array<double,4> value{};for(int tap=0;tap<20;++tap){const auto& row=rows.at(first+tap);for(int c=0;c<channels;++c)value[size_t(c)]+=row[size_t(x)][size_t(c)]*weights[size_t(tap)];}
                auto& out=output->pixels[size_t(y)*256+x];if(source->gray){const auto v=byte(value[0]);out={v,v,v,255};}else{const auto a=byte(value[3]);out={std::min(byte(value[0]),a),std::min(byte(value[1]),a),std::min(byte(value[2]),a),a};}
            }
        }
        return output;
    }
};
std::shared_ptr<const SamplingSource> samplingSource(std::shared_ptr<const Raster> image){
    if(!image)return {};auto source=std::make_shared<SamplingSource>();source->width=image->width;source->height=image->height;
    source->alignmentX=image->samplingOriginX;source->alignmentY=image->samplingOriginY;source->identity=image;
    source->rgba=[image](int x,int y){return image->pixel(x,y);};return source;
}
std::shared_ptr<const SamplingSource> samplingSource(std::shared_ptr<const GrayRaster> image){
    if(!image)return {};auto source=std::make_shared<SamplingSource>();source->width=image->width;source->height=image->height;
    source->alignmentX=image->samplingOriginX;source->alignmentY=image->samplingOriginY;source->identity=image;
    source->gray=[image](int x,int y){return image->pixel(x,y);};return source;
}
ReducedSourceCache::ReducedSourceCache(size_t tileBudget,size_t sourceBudget):storage_(std::make_shared<ReducedSource::Storage>(tileBudget,sourceBudget)){
    if(!tileBudget||tileBudget>4096||!sourceBudget||sourceBudget>1024)throw std::invalid_argument("Invalid reduction cache budget");
}
ReducedSourceCache::~ReducedSourceCache()=default;
std::shared_ptr<const ReducedSource> ReducedSourceCache::resolve(std::shared_ptr<const SamplingSource> source,int level){
    if(!source||!source->identity||bool(source->rgba)==bool(source->gray))throw std::invalid_argument("Invalid immutable sampling source");
    const auto grid=samplingGrid(*source,level);const LevelKey id{key(*source),level};std::lock_guard lock(storage_->mutex);
    auto& entry=storage_->levels[id];entry.use=++storage_->tick;if(auto cached=entry.handle.lock())return cached;
    auto result=std::make_shared<ReducedSource>();result->storage_=storage_;result->source_=std::move(source);result->grid_=grid;result->level_=level;entry.handle=result;
    while(storage_->levels.size()>storage_->sourceBudget){auto oldest=std::min_element(storage_->levels.begin(),storage_->levels.end(),[](const auto& a,const auto& b){return a.second.use<b.second.use;});const auto retired=oldest->first;storage_->levels.erase(oldest);for(auto it=storage_->tiles.begin();it!=storage_->tiles.end();)if(it->first.source==retired.source)it=storage_->tiles.erase(it);else ++it;}
    return result;
}
Pixel ReducedSource::pixel(int x,int y)const{std::lock_guard lock(storage_->mutex);return storage_->pixel(source_,level_,x,y);}
uint8_t ReducedSource::gray(int x,int y)const{return pixel(x,y).r;}
Pixel ReducedSource::sample(Point unit,Transform::Sampling sampling)const{
    const Point mapped{(unit.x*source_->width-grid_.x)/(double(grid_.width)*grid_.step),(unit.y*source_->height-grid_.y)/(double(grid_.height)*grid_.step)};
    std::lock_guard lock(storage_->mutex);return sampleRasterPixels(grid_.width,grid_.height,[&](int x,int y){return storage_->pixel(source_,level_,x,y);},mapped,sampling);
}
double ReducedSource::sampleGray(Point unit,Transform::Sampling sampling,uint8_t exterior)const{
    const Point mapped{(unit.x*source_->width-grid_.x)/(double(grid_.width)*grid_.step),(unit.y*source_->height-grid_.y)/(double(grid_.height)*grid_.step)};
    std::lock_guard lock(storage_->mutex);return sampleMaskPixels(grid_.width,grid_.height,[&](int x,int y){return storage_->pixel(source_,level_,x,y).r;},mapped,sampling,exterior);
}
size_t ReducedSourceCache::retainedTiles()const{std::lock_guard lock(storage_->mutex);return storage_->tiles.size();}
size_t ReducedSourceCache::retainedSources()const{std::lock_guard lock(storage_->mutex);return storage_->levels.size();}
uint64_t ReducedSourceCache::generatedTiles()const{std::lock_guard lock(storage_->mutex);return storage_->generated;}
void ReducedSourceCache::clear(){std::lock_guard lock(storage_->mutex);storage_->tiles.clear();storage_->levels.clear();storage_->tick=0;storage_->generated=0;}
ReducedSourceCache& ReducedSourceCache::shared(){static ReducedSourceCache cache;return cache;}
}
