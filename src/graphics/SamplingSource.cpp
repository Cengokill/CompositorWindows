#include "SamplingSource.h"
#include "DownsampleKernel.h"
#include "RasterSampling.h"
#include "ParallelBatch.h"
#include <algorithm>
#include <compare>
#include <map>
#include <mutex>
#include <stdexcept>
#include <emmintrin.h>

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
    struct Work{int x{},y{},width{},height{};TileEntry prior;};
    mutable std::mutex mutex;
    std::map<TileKey,TileEntry> tiles;
    std::map<LevelKey,LevelEntry> levels;
    size_t tileBudget,sourceBudget;uint64_t tick{},generated{},reused{};
    Storage(size_t tileLimit,size_t sourceLimit):tileBudget(tileLimit),sourceBudget(sourceLimit){}
    void trim(){while(tiles.size()>tileBudget){auto oldest=std::min_element(tiles.begin(),tiles.end(),[](const auto& a,const auto& b){return a.second.use<b.second.use;});tiles.erase(oldest);}}
    std::shared_ptr<const Raster::Tile> reusable(const std::shared_ptr<const SamplingSource>& source,const TileKey& requested){
        if(!source->reuseDomain||!source->dependencies)return {};
        const auto grid=samplingGrid(*source,requested.level);
        const int x0=requested.x*256,y0=requested.y*256;
        const int width=std::min(256,grid.width-x0),height=std::min(256,grid.height-y0);
        // Repeated halving expands the original-source footprint by
        // [-9*(step-1),10*(step-1)]. Include the last output pixel's full halo.
        const int64_t step=grid.step;
        const int left=int(std::clamp(int64_t(grid.x)+x0*step-9*(step-1),int64_t(0),int64_t(source->width-1)));
        const int top=int(std::clamp(int64_t(grid.y)+y0*step-9*(step-1),int64_t(0),int64_t(source->height-1)));
        const int right=int(std::clamp(int64_t(grid.x)+(x0+width-1)*step+10*(step-1),int64_t(0),int64_t(source->width-1)));
        const int bottom=int(std::clamp(int64_t(grid.y)+(y0+height-1)*step+10*(step-1),int64_t(0),int64_t(source->height-1)));
        const auto tokens=source->dependencies(left,top,right-left+1,bottom-top+1);
        auto geometry=requested.source;geometry.identity=0;
        // Search only already-owned entries: no secondary token index or pixel
        // storage can outlive the cache budget or the source that owns its tokens.
        for(auto& [candidate,entry]:tiles){
            auto prior=candidate.source;prior.identity=0;
            if(candidate.level!=requested.level||candidate.x!=requested.x||candidate.y!=requested.y||prior!=geometry||entry.source->reuseDomain!=source->reuseDomain||!entry.source->dependencies)continue;
            if(entry.source->dependencies(left,top,right-left+1,bottom-top+1)==tokens){entry.use=++tick;++reused;return entry.tile;}
        }
        return {};
    }
    Work changedRegion(const std::shared_ptr<const SamplingSource>& source,const TileKey& requested){
        const auto grid=samplingGrid(*source,requested.level);
        const int x0=requested.x*256,y0=requested.y*256;
        Work work{0,0,std::min(256,grid.width-x0),std::min(256,grid.height-y0),{}};
        if(!source->reuseDomain||!source->dependencies)return work;
        auto geometry=requested.source;geometry.identity=0;
        // Copy both owners before a lower-level read can trim this map. There
        // is no candidate index and at most one extra source/tile owner.
        for(const auto& [candidate,entry]:tiles){
            auto prior=candidate.source;prior.identity=0;
            if(candidate.level==requested.level&&candidate.x==requested.x&&candidate.y==requested.y&&prior==geometry&&entry.source->reuseDomain==source->reuseDomain&&entry.source->dependencies&&entry.use>work.prior.use)work.prior=entry;
        }
        if(!work.prior.tile)return work;
        int left=work.width,top=work.height,right=0,bottom=0;
        const int64_t step=grid.step;
        for(int by=0;by<work.height;by+=32)for(int bx=0;bx<work.width;bx+=32){
            const int width=std::min(32,work.width-bx),height=std::min(32,work.height-by);
            // The complete recursive20-tap support in original-source pixels.
            // RGBA transparent and gray clamped exterior retain the same
            // conservative in-bounds dependencies as whole-tile reuse.
            const int sx=int(std::clamp(int64_t(grid.x)+(x0+bx)*step-9*(step-1),int64_t(0),int64_t(source->width-1)));
            const int sy=int(std::clamp(int64_t(grid.y)+(y0+by)*step-9*(step-1),int64_t(0),int64_t(source->height-1)));
            const int ex=int(std::clamp(int64_t(grid.x)+(x0+bx+width-1)*step+10*(step-1),int64_t(0),int64_t(source->width-1)));
            const int ey=int(std::clamp(int64_t(grid.y)+(y0+by+height-1)*step+10*(step-1),int64_t(0),int64_t(source->height-1)));
            if(source->dependencies(sx,sy,ex-sx+1,ey-sy+1)!=work.prior.source->dependencies(sx,sy,ex-sx+1,ey-sy+1)){
                left=std::min(left,bx);top=std::min(top,by);right=std::max(right,bx+width);bottom=std::max(bottom,by+height);
            }
        }
        if(right==0){work.x=work.y=work.width=work.height=0;return work;}
        work.x=left;work.y=top;work.width=right-left;work.height=bottom-top;
        return work;
    }
    std::shared_ptr<const Raster::Tile> tile(const std::shared_ptr<const SamplingSource>& source,int level,int tx,int ty){
        const TileKey tileKey{key(*source),level,tx,ty};
        auto found=tiles.find(tileKey);
        if(found==tiles.end()){
            auto tile=reusable(source,tileKey);
            if(!tile){
                const auto work=changedRegion(source,tileKey);
                if(!work.width){tile=work.prior.tile;++reused;}
                else{tile=generate(source,level,tileKey.x,tileKey.y,work);++generated;}
            }
            found=tiles.emplace(tileKey,TileEntry{source,std::move(tile),++tick}).first;
            trim();
        }else found->second.use=++tick;
        return found->second.tile;
    }
    Pixel pixel(const std::shared_ptr<const SamplingSource>& source,int level,int x,int y){
        const auto grid=samplingGrid(*source,level);
        if(source->gray){x=std::clamp(x,0,grid.width-1);y=std::clamp(y,0,grid.height-1);}
        else if(x<0||y<0||x>=grid.width||y>=grid.height)return {};
        if(level==0){if(source->gray){const auto v=source->gray(x,y);return {v,v,v,255};}return source->rgba(x,y);}
        return tile(source,level,x/256,y/256)->pixels[size_t(y%256)*256+x%256];
    }
    void readRow(const std::shared_ptr<const SamplingSource>& source,int level,int x,int y,int count,Pixel* output){
        const auto grid=samplingGrid(*source,level);
        if(source->gray)y=std::clamp(y,0,grid.height-1);
        else if(y<0||y>=grid.height){std::fill_n(output,count,Pixel{});return;}
        if(x<0){const int run=std::min(count,-x);const auto edge=source->gray?pixel(source,level,0,y):Pixel{};std::fill_n(output,run,edge);output+=run;x+=run;count-=run;}
        const int inside=std::min(count,std::max(0,grid.width-x));
        if(inside>0){
            if(level==0){if(source->readRow)source->readRow(x,y,inside,output);else for(int i=0;i<inside;++i)output[i]=pixel(source,0,x+i,y);}
            else for(int copied=0;copied<inside;){const int sx=x+copied,run=std::min(inside-copied,256-sx%256);const auto input=tile(source,level,sx/256,y/256);std::copy_n(input->pixels.data()+size_t(y%256)*256+sx%256,run,output+copied);copied+=run;}
            output+=inside;x+=inside;count-=inside;
        }
        if(count){const auto edge=source->gray?pixel(source,level,grid.width-1,y):Pixel{};std::fill_n(output,count,edge);}
    }
    void prefetch(const std::shared_ptr<const SamplingSource>& source,int level,int x,int y,int width,int height,Pixel* output){
        if(level==0){for(int row=0;row<height;++row)readRow(source,level,x,y+row,width,output+size_t(row)*width);return;}
        const auto grid=samplingGrid(*source,level);
        const bool gray=bool(source->gray);
        // Copy each lower tile's complete intersection before acquiring another
        // tile. A one-tile cache can otherwise regenerate siblings on every row.
        // Gray edge replication belongs to the corresponding edge tile, so it
        // does not cause a second acquisition after an interior tile evicts it.
        const int left=gray?std::clamp(x,0,grid.width-1):std::max(x,0);
        const int top=gray?std::clamp(y,0,grid.height-1):std::max(y,0);
        const int right=gray?std::clamp(x+width-1,0,grid.width-1)+1:std::min(x+width,grid.width);
        const int bottom=gray?std::clamp(y+height-1,0,grid.height-1)+1:std::min(y+height,grid.height);
        if(!gray)std::fill_n(output,size_t(width)*height,Pixel{});
        if(left>=right||top>=bottom)return;
        for(int ty=top/256;ty<=(bottom-1)/256;++ty)for(int tx=left/256;tx<=(right-1)/256;++tx){
            const int sx=tx*256,sy=ty*256,ex=std::min(sx+256,grid.width),ey=std::min(sy+256,grid.height);
            const int firstX=std::max(x,gray&&sx==0?x:sx),lastX=std::min(x+width,gray&&ex==grid.width?x+width:ex);
            const int firstY=std::max(y,gray&&sy==0?y:sy),lastY=std::min(y+height,gray&&ey==grid.height?y+height:ey);
            const auto input=tile(source,level,tx,ty);
            for(int dy=firstY;dy<lastY;++dy){
                const auto* row=input->pixels.data()+size_t(std::clamp(dy,sy,ey-1)-sy)*256;
                auto* target=output+size_t(dy-y)*width+firstX-x;
                int dx=firstX;
                if(dx<0){const int count=std::min(lastX,0)-dx;std::fill_n(target,count,row[0]);target+=count;dx+=count;}
                const int end=std::min(lastX,grid.width);
                if(dx<end){std::copy_n(row+dx-sx,end-dx,target);target+=end-dx;dx=end;}
                if(dx<lastX)std::fill_n(target,lastX-dx,row[ex-sx-1]);
            }
        }
    }
    std::shared_ptr<const Raster::Tile> generate(const std::shared_ptr<const SamplingSource>& source,int level,int tx,int ty,const Work& work){
        const auto grid=samplingGrid(*source,level),previous=samplingGrid(*source,level-1);
        const int x0=tx*256+work.x,y0=ty*256+work.y,width=work.width,height=work.height;
        const int ox=(grid.x-previous.x)/previous.step,oy=(grid.y-previous.y)/previous.step;
        const auto& weights=halvingWeights();
        auto output=work.prior.tile?std::make_shared<Raster::Tile>(*work.prior.tile):std::make_shared<Raster::Tile>();
        // Resolve lower levels on the owning thread before dispatch. Workers
        // never access the cache mutex or recursively submit reduction work.
        const int inputWidth=2*width+18,inputHeight=2*height+18;
        std::vector<Pixel> input(size_t(inputWidth)*inputHeight);
        prefetch(source,level-1,2*x0+ox-9,2*y0+oy-9,inputWidth,inputHeight,input.data());
        using Row=std::vector<std::array<double,4>>;
        const auto compute=[&]<bool Gray>(size_t block){
            std::array<Row,20> rows;std::array<int,20> tags;tags.fill(-1);
            const int begin=int(block)*32,end=std::min(height,begin+32);
            for(int y=begin;y<end;++y){std::array<const Row*,20> inputRows{};
                for(int tap=0;tap<20;++tap){const int sy=2*y+tap,slot=sy%20;auto& row=rows[size_t(slot)];
                    if(tags[size_t(slot)]!=sy){row.assign(size_t(width),std::array<double,4>{});const auto* line=input.data()+size_t(sy)*inputWidth;
                        for(int x=0;x<width;++x){
                            if constexpr(Gray){for(int horizontal=0;horizontal<20;++horizontal)row[size_t(x)][0]+=double(line[2*x+horizontal].r)*weights[size_t(horizontal)];}
                            else{auto rg=_mm_setzero_pd(),ba=_mm_setzero_pd();for(int horizontal=0;horizontal<20;++horizontal){const auto p=line[2*x+horizontal];const auto weight=_mm_set1_pd(weights[size_t(horizontal)]);rg=_mm_add_pd(rg,_mm_mul_pd(_mm_set_pd(double(p.g),double(p.r)),weight));ba=_mm_add_pd(ba,_mm_mul_pd(_mm_set_pd(double(p.a),double(p.b)),weight));}_mm_storeu_pd(row[size_t(x)].data(),rg);_mm_storeu_pd(row[size_t(x)].data()+2,ba);}
                        }
                        tags[size_t(slot)]=sy;
                    }
                    inputRows[size_t(tap)]=&row;
                }
                for(int x=0;x<width;++x){std::array<double,4> value{};
                    if constexpr(Gray){for(int tap=0;tap<20;++tap)value[0]+=(*inputRows[size_t(tap)])[size_t(x)][0]*weights[size_t(tap)];}
                    else{auto rg=_mm_setzero_pd(),ba=_mm_setzero_pd();for(int tap=0;tap<20;++tap){const auto& row=(*inputRows[size_t(tap)])[size_t(x)];const auto weight=_mm_set1_pd(weights[size_t(tap)]);rg=_mm_add_pd(rg,_mm_mul_pd(_mm_loadu_pd(row.data()),weight));ba=_mm_add_pd(ba,_mm_mul_pd(_mm_loadu_pd(row.data()+2),weight));}_mm_storeu_pd(value.data(),rg);_mm_storeu_pd(value.data()+2,ba);}
                    auto& out=output->pixels[size_t(work.y+y)*256+work.x+x];if constexpr(Gray){const auto v=byte(value[0]);out={v,v,v,255};}else{const auto a=byte(value[3]);out={std::min(byte(value[0]),a),std::min(byte(value[1]),a),std::min(byte(value[2]),a),a};}
                }
            }
        };
        const auto blocks=size_t((height+31)/32);
        if(source->gray)runParallelBatch(blocks,0,[&](size_t block){compute.operator()<true>(block);});
        else runParallelBatch(blocks,0,[&](size_t block){compute.operator()<false>(block);});
        return output;
    }
};
std::shared_ptr<const SamplingSource> samplingSource(std::shared_ptr<const Raster> image){
    if(!image)return {};auto source=std::make_shared<SamplingSource>();source->width=image->width;source->height=image->height;
    source->alignmentX=image->samplingOriginX;source->alignmentY=image->samplingOriginY;source->identity=image;
    source->rgba=[image](int x,int y){return image->pixel(x,y);};
    source->readRow=[image](int x,int y,int count,Pixel* output){const int columns=(image->width+255)/256;while(count){const int run=std::min(count,256-x%256);const auto& tile=image->tiles[size_t(y/256)*columns+x/256];std::copy_n(tile->pixels.data()+size_t(y%256)*256+x%256,run,output);x+=run;count-=run;output+=run;}};return source;
}
std::shared_ptr<const SamplingSource> samplingSource(std::shared_ptr<const GrayRaster> image){
    if(!image)return {};auto source=std::make_shared<SamplingSource>();source->width=image->width;source->height=image->height;
    source->alignmentX=image->samplingOriginX;source->alignmentY=image->samplingOriginY;source->identity=image;
    source->gray=[image](int x,int y){return image->pixel(x,y);};
    source->readRow=[image](int x,int y,int count,Pixel* output){for(int i=0;i<count;++i){const auto v=image->pixel(x+i,y);output[i]={v,v,v,255};}};return source;
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
uint64_t ReducedSourceCache::reusedTiles()const{std::lock_guard lock(storage_->mutex);return storage_->reused;}
void ReducedSourceCache::clear(){std::lock_guard lock(storage_->mutex);storage_->tiles.clear();storage_->levels.clear();storage_->tick=0;storage_->generated=0;storage_->reused=0;}
ReducedSourceCache& ReducedSourceCache::shared(){static ReducedSourceCache cache;return cache;}
}
