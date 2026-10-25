#include "editing/FocusStack.h"
#include "FocusStackFrames.h"
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace compositor;
using focus_stack_fixture::frame;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void fails(F operation,const std::string& expected){
    try{operation();}catch(const std::exception& error){require(std::string(error.what())==expected,("Unexpected failure: "+std::string(error.what())).c_str());return;}
    throw std::runtime_error("Expected failure was not raised: "+expected);
}
const auto none=[](int,int){return false;};
constexpr int W=240,H=160;
// Left half sharp in "near", right half sharp in "far"; the 32 px band around the seam is excluded.
bool leftInterior(int x){return x<W/2-16;}
bool rightInterior(int x){return x>=W/2+16;}
imaging::RgbaImage nearFrame(){return frame(W,H,[](int x,int){return x>=W/2;});}
imaging::RgbaImage farFrame(){return frame(W,H,[](int x,int){return x<W/2;});}
void run(const std::string& key){
    if(key=="alignment_recovers_shift"){
        // moving(x,y)=reference(x-3,y+2), so moving(x+3,y-2) matches reference(x,y).
        const auto reference=frame(W,H,none),moving=frame(W,H,none,3,-2);
        const auto shift=editing::estimateFocusShift(reference,moving,12);
        std::cout<<"shift="<<shift.dx<<","<<shift.dy<<'\n';
        require(shift==editing::FocusShift{3,-2},"Alignment must recover the (+3,-2) shift");
        const auto large=frame(640,480,none),largeMoving=frame(640,480,none,-7,5);
        require(editing::estimateFocusShift(large,largeMoving,32)==editing::FocusShift{-7,5},"Pyramid alignment must recover (-7,+5) on 640x480");
    }else if(key=="alignment_flat_stays_zero"){
        const auto flat=frame(64,48,[](int,int){return true;},0,0);
        imaging::RgbaImage gray{64,48,256,std::vector<uint8_t>(256*48,128)};
        require(editing::estimateFocusShift(gray,gray,6)==editing::FocusShift{},"Flat frames stay unshifted");
        require(editing::estimateFocusShift(flat,flat,6)==editing::FocusShift{},"Identical frames stay unshifted");
    }else if(key=="mask_prefers_sharp_region"){
        const auto nearSharp=editing::focusSharpness(nearFrame(),{},2),farSharp=editing::focusSharpness(farFrame(),{},2);
        size_t left=0,right=0,leftWins=0,rightWins=0;
        for(int y=0;y<H;++y)for(int x=0;x<W;++x){const size_t i=size_t(y)*W+x;
            if(leftInterior(x)){++left;leftWins+=nearSharp[i]>farSharp[i];}
            if(rightInterior(x)){++right;rightWins+=farSharp[i]>nearSharp[i];}}
        require(leftWins==left&&rightWins==right,"Sharpness must be higher in each frame's in-focus half");
        const auto result=editing::focusStack({nearFrame(),farFrame()});
        for(int y=0;y<H;++y)for(int x=0;x<W;++x){const auto source=result.source[size_t(y)*W+x];
            if(leftInterior(x))require(source==0,"Left interior must come from the near frame");
            if(rightInterior(x))require(source==1,"Right interior must come from the far frame");}
    }else if(key=="output_pixels"){
        const auto nearImage=nearFrame(),farImage=farFrame();
        // Independently defined expectation: the unblurred texture in both interiors.
        const auto sharp=frame(W,H,none);
        const auto result=editing::focusStack({nearImage,farImage});
        require(result.image.width==W&&result.image.height==H&&result.shifts[1]==editing::FocusShift{},"Output size and zero shift");
        for(int y=0;y<H;++y)for(int x=0;x<W;++x)if(leftInterior(x)||rightInterior(x))
            for(int c=0;c<4;++c)require(result.image.pixels[size_t(y)*result.image.stride+size_t(x)*4+c]==sharp.pixels[size_t(y)*sharp.stride+size_t(x)*4+c],"Interior pixels must equal the sharp texture");
        // Seam pixels must each be copied from one of the two frames.
        for(int y=0;y<H;++y)for(int x=W/2-16;x<W/2+16;++x){const auto* o=&result.image.pixels[size_t(y)*result.image.stride+size_t(x)*4];
            const auto* a=&nearImage.pixels[size_t(y)*nearImage.stride+size_t(x)*4];const auto* b=&farImage.pixels[size_t(y)*farImage.stride+size_t(x)*4];
            require(std::equal(o,o+4,a)||std::equal(o,o+4,b),"Seam pixels come from one source");}
    }else if(key=="uncovered_pixels_use_reference"){
        // The shifted frame is sharp everywhere and the reference is blurred everywhere.
        const auto reference=frame(W,H,[](int,int){return true;}),moving=frame(W,H,none,3,-2);
        const auto result=editing::focusStack({reference,moving});
        require(result.shifts[1]==editing::FocusShift{3,-2},"Shifted frame alignment");
        size_t uncovered=0;
        for(int y=0;y<H;++y)for(int x=0;x<W;++x){const bool covered=x+3<W&&y-2>=0;
            if(!covered){++uncovered;require(result.source[size_t(y)*W+x]==0,"Uncovered pixels must use the reference frame");}}
        require(uncovered==size_t(3*H+2*W-6),"Expected uncovered border count");
        require(result.source[size_t(80)*W+100]==1,"Covered interior uses the sharp shifted frame");
    }else if(key=="rejects_size_mismatch"){
        fails([]{editing::focusStack({frame(64,48,none),frame(64,49,none)});},"Focus stack images must all have the same dimensions");
    }else if(key=="rejects_single_image"){
        fails([]{editing::focusStack({frame(64,48,none)});},"Focus stacking needs at least two images");
    }else if(key=="rejects_too_many"){
        fails([]{editing::focusStack(std::vector<imaging::RgbaImage>(17,frame(16,16,none)));},"Focus stacking accepts at most 16 images");
    }else if(key=="rejects_budget"){
        editing::FocusStackLimits limits;limits.maxWorkingBytes=editing::focusStackWorkingBytes(64,48,2)-1;
        fails([&]{editing::focusStack({frame(64,48,none),frame(64,48,none)},limits);},"Focus stack exceeds the working memory budget");
        limits.maxWorkingBytes+=1;editing::focusStack({frame(64,48,none),frame(64,48,none)},limits);
    }else if(key=="cancel_stops"){
        int polls=0;editing::FocusStackLimits limits;limits.cancelled=[&]{return ++polls>2;};
        fails([&]{editing::focusStack({nearFrame(),farFrame()},limits);},"Focus stack cancelled");
        require(polls==3,"Cancellation must stop at the first poll that reports it");
    }else throw std::runtime_error("unknown case");
}
}
int main(int argc,char** argv){
    try{if(argc!=2)throw std::runtime_error("one test name required");run(argv[1]);std::cout<<"PASS "<<argv[1]<<'\n';return 0;}
    catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
