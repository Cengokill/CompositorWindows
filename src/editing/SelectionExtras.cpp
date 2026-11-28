#include "SelectionExtras.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <queue>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>
namespace compositor::editing {
namespace {
std::shared_ptr<GrayRaster> dense(int width,int height){auto raster=std::make_shared<GrayRaster>();raster->width=width;raster->height=height;raster->pixels.assign(size_t(width)*height,0);return raster;}
// Cone-weighted blur of the coverage, written into out, which starts empty. Samples past the canvas repeat its edge.
void blurCoverage(const GrayRaster& source,GrayRaster& out,int radius){
    const auto bounds=source.nonzeroBounds();if(bounds.empty())return;
    // Only pixels within one kernel of the coverage can become nonzero.
    const int left=std::max(0,bounds.x-radius),top=std::max(0,bounds.y-radius),width=std::min(source.width,bounds.x+bounds.width+radius)-left,height=std::min(source.height,bounds.y+bounds.height+radius)-top;
    const int diameter=2*radius+1,stride=width+2*radius,rows=height+2*radius;
    std::vector<uint8_t> input(size_t(stride)*rows);
    for(int y=0;y<rows;++y)for(int x=0;x<stride;++x)input[size_t(y)*stride+x]=source.pixel(std::clamp(left-radius+x,0,source.width-1),std::clamp(top-radius+y,0,source.height-1));
    // A window holding one value keeps it, which leaves only the pixels around edges to sum.
    // flat is that value for a whole row of the window, or -1; run counts the equal flat rows stacked in a column.
    std::vector<int16_t> flat(size_t(width)*rows,-1);
    for(int y=0;y<rows;++y){const auto* row=input.data()+size_t(y)*stride;int run=0;for(int x=0;x<stride;++x){run=x&&row[x]==row[x-1]?run+1:1;if(x>=diameter-1&&run>=diameter)flat[size_t(y)*width+x-diameter+1]=row[x];}}
    std::vector<uint8_t> uniform(size_t(width)*height);
    for(int x=0;x<width;++x){int run=0,last=-1;for(int y=0;y<rows;++y){const int value=flat[size_t(y)*width+x];run=value<0?0:value==last?run+1:1;last=value;if(y>=diameter-1&&run>=diameter)uniform[size_t(y-diameter+1)*width+x]=1;}}
    struct Tap{ptrdiff_t offset;double weight;};std::vector<Tap> taps;double total=0;
    for(int dy=-radius;dy<=radius;++dy)for(int dx=-radius;dx<=radius;++dx){const double w=std::max(0.,radius-std::hypot(dx,dy)+1);if(w<=0)continue;taps.push_back({ptrdiff_t(dy)*stride+dx,w});total+=w;}
    std::atomic<int> next{0};
    const auto work=[&](std::vector<double>& sums){
        for(int y=next++;y<height;y=next++){
            const auto* center=input.data()+size_t(y+radius)*stride+radius;const auto* skip=uniform.data()+size_t(y)*width;auto* target=out.pixels.data()+size_t(top+y)*out.width+left;
            for(int begin=0;begin<width;){
                if(skip[begin]){target[begin]=center[begin];++begin;continue;}
                int end=begin+1;while(end<width&&!skip[end])++end;
                const int count=end-begin;std::fill_n(sums.begin(),count,0.);
                // One tap over the whole span at a time: each pixel still adds its samples in kernel order.
                for(const auto& tap:taps){const auto* samples=center+begin+tap.offset;for(int i=0;i<count;++i)sums[i]+=samples[i]*tap.weight;}
                for(int i=0;i<count;++i)target[begin+i]=uint8_t(std::clamp(std::lround(sums[i]/std::max(total,1.)),0L,255L));
                begin=end;
            }
        }
    };
    const int threads=std::clamp(int(std::thread::hardware_concurrency()),1,height);
    // Allocated here so that a worker cannot throw.
    auto sums=std::vector<std::vector<double>>(size_t(threads),std::vector<double>(size_t(width)));
    {std::vector<std::jthread> workers;for(int i=1;i<threads;++i)workers.emplace_back(work,std::ref(sums[size_t(i)]));work(sums[0]);}
}
int channel(Pixel pixel,uint8_t value){return pixel.a?int(std::lround(value*255./pixel.a)):0;}
int distance(Pixel a,Pixel b){return std::max({std::abs(channel(a,a.r)-channel(b,b.r)),std::abs(channel(a,a.g)-channel(b,b.g)),std::abs(channel(a,a.b)-channel(b,b.b))});}
}
Selection featherSelection(const Selection& selection,double radius){
    if(!selection.coverage||!std::isfinite(radius)||radius<0||radius>250)throw std::runtime_error("Invalid feather radius");
    auto soft=dense(selection.coverage->width,selection.coverage->height);if(radius<=0){for(int y=0;y<soft->height;++y)for(int x=0;x<soft->width;++x)soft->pixels[size_t(y)*soft->width+x]=selection.coverage->pixel(x,y);Selection result;result.coverage=soft;return result;}
    int kernel=std::clamp(int(std::ceil(radius)),1,32);
    blurCoverage(*selection.coverage,*soft,kernel);
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
