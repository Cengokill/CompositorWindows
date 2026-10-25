#include "FocusStack.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace compositor::editing {
namespace {
struct Gray { int width{},height{}; std::vector<float> values; float at(int x,int y)const{return values[size_t(y)*width+x];} };
void cancelCheck(const FocusStackLimits& limits){if(limits.cancelled&&limits.cancelled())throw std::runtime_error("Focus stack cancelled");}
// When the second-best sharpness reaches this fraction of the best, the choice is noise-driven: the pixel is
// averaged over every covering frame instead, which avoids a patchwork of frame tints in low-texture areas.
constexpr float kContestedRatio=0.6f;
Gray luma(const imaging::RgbaImage& image){
    Gray gray{int(image.width),int(image.height),std::vector<float>(size_t(image.width)*image.height)};
    for(int y=0;y<gray.height;++y){const auto* row=&image.pixels[size_t(y)*image.stride];
        for(int x=0;x<gray.width;++x){const auto* p=row+size_t(x)*4;gray.values[size_t(y)*gray.width+x]=0.2126f*p[0]+0.7152f*p[1]+0.0722f*p[2];}}
    return gray;
}
Gray half(const Gray& in){
    Gray out{std::max(1,in.width/2),std::max(1,in.height/2),{}};out.values.resize(size_t(out.width)*out.height);
    for(int y=0;y<out.height;++y)for(int x=0;x<out.width;++x){
        const int x0=std::min(in.width-1,2*x),x1=std::min(in.width-1,2*x+1),y0=std::min(in.height-1,2*y),y1=std::min(in.height-1,2*y+1);
        out.values[size_t(y)*out.width+x]=(in.at(x0,y0)+in.at(x1,y0)+in.at(x0,y1)+in.at(x1,y1))*0.25f;
    }
    return out;
}
// Mean absolute difference over the overlap; rejects shifts that keep less than half the frame.
double cost(const Gray& reference,const Gray& moving,int dx,int dy){
    const int x0=std::max(0,-dx),x1=std::min(reference.width,moving.width-dx),y0=std::max(0,-dy),y1=std::min(reference.height,moving.height-dy);
    if(x1-x0<reference.width/2||y1-y0<reference.height/2||x1<=x0||y1<=y0)return std::numeric_limits<double>::infinity();
    const int step=std::max(1,std::max(x1-x0,y1-y0)/512);
    double sum=0;size_t n=0;
    for(int y=y0;y<y1;y+=step)for(int x=x0;x<x1;x+=step){sum+=std::abs(reference.at(x,y)-moving.at(x+dx,y+dy));++n;}
    return n?sum/double(n):std::numeric_limits<double>::infinity();
}
FocusShift search(const Gray& reference,const Gray& moving,FocusShift center,int range,int limit){
    FocusShift best=center;double bestCost=std::numeric_limits<double>::infinity();
    for(int dy=center.dy-range;dy<=center.dy+range;++dy)for(int dx=center.dx-range;dx<=center.dx+range;++dx){
        if(std::abs(dx)>limit||std::abs(dy)>limit)continue;
        const double c=cost(reference,moving,dx,dy);
        // Prefer the smaller shift on ties so flat images stay unshifted.
        if(c<bestCost||(c==bestCost&&std::abs(dx)+std::abs(dy)<std::abs(best.dx)+std::abs(best.dy))){bestCost=c;best={dx,dy};}
    }
    return best;
}
FocusShift estimate(const Gray& reference,const Gray& moving,int maxShift){
    // Level 0 is borrowed; only the downscaled levels (at most a third of a frame) are allocated.
    std::vector<Gray> refs,movs;
    auto level=[&](size_t i,bool ref)->const Gray&{return i==0?(ref?reference:moving):(ref?refs[i-1]:movs[i-1]);};
    size_t top=0;
    while(std::max(level(top,true).width,level(top,true).height)>256&&level(top,true).width>8&&level(top,true).height>8){
        refs.push_back(half(level(top,true)));movs.push_back(half(level(top,false)));++top;}
    const int coarseLimit=(maxShift+(1<<top)-1)>>top;
    FocusShift shift=search(level(top,true),level(top,false),{0,0},coarseLimit,coarseLimit);
    for(size_t i=top;i-->0;){
        const int limit=(maxShift+(1<<i)-1)>>i;
        shift=search(level(i,true),level(i,false),{shift.dx*2,shift.dy*2},2,limit);
    }
    shift.dx=std::clamp(shift.dx,-maxShift,maxShift);shift.dy=std::clamp(shift.dy,-maxShift,maxShift);
    return shift;
}
bool covers(int width,int height,FocusShift s,int x,int y){const int sx=x+s.dx,sy=y+s.dy;return sx>=0&&sy>=0&&sx<width&&sy<height;}
std::vector<float> sharpness(const Gray& gray,FocusShift shift,int radius,const FocusStackLimits& limits){
    const int w=gray.width,h=gray.height;
    auto sample=[&](int x,int y){return gray.at(std::clamp(x,0,w-1),std::clamp(y,0,h-1));};
    std::vector<float> energy(size_t(w)*h),row(size_t(w)*h);
    for(int y=0;y<h;++y){if((y&63)==0)cancelCheck(limits);
        for(int x=0;x<w;++x){
            const int sx=x+shift.dx,sy=y+shift.dy;
            float e=0;
            if(sx>=0&&sy>=0&&sx<w&&sy<h){const float l=4*sample(sx,sy)-sample(sx-1,sy)-sample(sx+1,sy)-sample(sx,sy-1)-sample(sx,sy+1);e=l*l;}
            energy[size_t(y)*w+x]=e;
        }}
    // Separable box average with clamped edges.
    for(int y=0;y<h;++y){const float* in=&energy[size_t(y)*w];float* out=&row[size_t(y)*w];double sum=0;
        for(int x=-radius;x<=radius;++x)sum+=in[std::clamp(x,0,w-1)];
        for(int x=0;x<w;++x){out[x]=float(sum);sum+=in[std::clamp(x+radius+1,0,w-1)]-in[std::clamp(x-radius,0,w-1)];}}
    const float norm=1.0f/float((2*radius+1)*(2*radius+1));
    for(int x=0;x<w;++x){if((x&255)==0)cancelCheck(limits);double sum=0;
        for(int y=-radius;y<=radius;++y)sum+=row[size_t(std::clamp(y,0,h-1))*w+x];
        for(int y=0;y<h;++y){energy[size_t(y)*w+x]=float(sum)*norm;sum+=row[size_t(std::clamp(y+radius+1,0,h-1))*w+x]-row[size_t(std::clamp(y-radius,0,h-1))*w+x];}}
    return energy;
}
int radiusFor(std::uint32_t width,std::uint32_t height){return std::clamp(int(std::min(width,height)/200),2,16);}
int maxShiftFor(std::uint32_t width,std::uint32_t height){return std::max(1,int(std::max(width,height)/20));}
}
FocusShift estimateFocusShift(const imaging::RgbaImage& reference,const imaging::RgbaImage& moving,int maxShift){
    imaging::validate(reference);imaging::validate(moving);
    if(reference.width!=moving.width||reference.height!=moving.height)throw std::invalid_argument("Focus stack frames must have identical dimensions");
    if(maxShift<0)throw std::invalid_argument("Invalid focus stack shift range");
    return estimate(luma(reference),luma(moving),maxShift);
}
std::vector<float> focusSharpness(const imaging::RgbaImage& image,FocusShift shift,int radius){
    imaging::validate(image);if(radius<0)throw std::invalid_argument("Invalid sharpness radius");
    return sharpness(luma(image),shift,radius,{});
}
std::uint64_t focusStackWorkingBytes(std::uint32_t width,std::uint32_t height,std::size_t count){
    // Frames and output (4 B each); reference and current luma, energy, box rows and best score (4 B each);
    // both alignment pyramids (under 3 B); source index (1 B).
    const std::uint64_t pixels=std::uint64_t(width)*height;
    return pixels*(4*std::uint64_t(count)+4+4*5+3+1);
}
FocusStackResult focusStack(const std::vector<imaging::RgbaImage>& frames,const FocusStackLimits& limits){
    if(frames.size()<2)throw std::invalid_argument("Focus stacking needs at least two images");
    if(frames.size()>maxFocusStackFrames)throw std::invalid_argument("Focus stacking accepts at most 16 images");
    for(const auto& frame:frames){imaging::validate(frame);
        if(frame.width!=frames.front().width||frame.height!=frames.front().height)throw std::invalid_argument("Focus stack images must all have the same dimensions");}
    const auto width=frames.front().width,height=frames.front().height;
    if(focusStackWorkingBytes(width,height,frames.size())>limits.maxWorkingBytes)throw std::runtime_error("Focus stack exceeds the working memory budget");
    const int w=int(width),h=int(height),radius=radiusFor(width,height),maxShift=maxShiftFor(width,height);
    FocusStackResult result;result.shifts.resize(frames.size());
    result.source.assign(size_t(w)*h,0);
    std::vector<float> best,second;
    const Gray reference=luma(frames.front());
    for(size_t index=0;index<frames.size();++index){
        cancelCheck(limits);
        Gray current=index?luma(frames[index]):reference;
        if(index)result.shifts[index]=estimate(reference,current,maxShift);
        cancelCheck(limits);
        auto score=sharpness(current,result.shifts[index],radius,limits);
        if(!index){best=std::move(score);continue;}
        // Allocated after the sharpness pass so the peak memory stays at the budgeted five float planes.
        if(second.empty())second.assign(best.size(),0.0f);
        const auto shift=result.shifts[index];
        for(int y=0;y<h;++y)for(int x=0;x<w;++x){const size_t i=size_t(y)*w+x;
            if(!covers(w,h,shift,x,y))continue;
            if(score[i]>best[i]){second[i]=best[i];best[i]=score[i];result.source[i]=uint8_t(index);}
            else if(score[i]>second[i])second[i]=score[i];
        }
    }
    cancelCheck(limits);
    auto& out=result.image;out.width=width;out.height=height;out.stride=size_t(width)*4;out.pixels.resize(out.stride*height);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){
        const size_t i=size_t(y)*w+x;auto* dst=&out.pixels[size_t(y)*out.stride+size_t(x)*4];
        if(second[i]<kContestedRatio*best[i]){
            const auto source=result.source[i];const auto shift=result.shifts[source];const auto& frame=frames[source];
            const auto* p=&frame.pixels[size_t(y+shift.dy)*frame.stride+size_t(x+shift.dx)*4];
            std::copy(p,p+4,dst);
            continue;
        }
        unsigned sum[4]{},count=0;
        for(size_t f=0;f<frames.size();++f){
            const auto shift=result.shifts[f];
            if(!covers(w,h,shift,x,y))continue;
            const auto& frame=frames[f];
            const auto* p=&frame.pixels[size_t(y+shift.dy)*frame.stride+size_t(x+shift.dx)*4];
            for(int c=0;c<4;++c)sum[c]+=p[c];
            ++count;
        }
        for(int c=0;c<4;++c)dst[c]=uint8_t((sum[c]+count/2)/count);
    }
    return result;
}
}
