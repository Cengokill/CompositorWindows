#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <vector>
extern "C" {
#include "NoisePixels.h"
#include "LensPixels.h"
#include "ContentFill.h"
void noise_add_reference(uint8_t*,size_t,size_t,size_t,float,int,int,uint32_t);
void lens_distort_reference(const uint8_t*,uint8_t*,size_t,size_t,size_t,double);
int content_fill_reference(uint8_t*,size_t,const uint8_t*,size_t,int,int);
}
void require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
int countChecks(void* context){auto& count=*static_cast<int*>(context);++count;return 0;}
int stopThird(void* context){return ++*static_cast<int*>(context)>=3;}
std::vector<uint8_t> pixels(int w,int h,size_t stride,bool opaque=false){std::vector<uint8_t> data(stride*h,0xcd);uint32_t state=19;for(int y=0;y<h;++y)for(int x=0;x<w;++x){state=state*1664525u+1013904223u;const auto i=size_t(y)*stride+x*4;const auto alpha=opaque?255:uint8_t(state>>24);data[i]=uint8_t(unsigned(uint8_t(state))*alpha/255);data[i+1]=uint8_t(unsigned(uint8_t(state>>8))*alpha/255);data[i+2]=uint8_t(unsigned(uint8_t(state>>16))*alpha/255);data[i+3]=uint8_t(alpha);}return data;}
int main(){try{int cases=0;for(int w:{1,3,19,64,257})for(int h:{1,3,17}){const size_t stride=size_t(w)*4+12;const auto input=pixels(w,h,stride);for(int gaussian:{0,1})for(int mono:{0,1})for(uint32_t seed:{0U,77U,0xffffffffU}){auto actual=input,expected=input;int checks=0;noise_add_reference(expected.data(),w,h,stride,87,gaussian,mono,seed);require(noise_add_cancellable(actual.data(),w,h,stride,87,gaussian,mono,seed,countChecks,&checks)==0,"noise status");require(actual==expected&&checks>0,"noise bytes or padding differ from pinned kernel");++cases;}for(double k:{-.85,0.,.85}){auto actual=input,expected=input;int checks=0;lens_distort_reference(input.data(),expected.data(),w,h,stride,k);require(lens_distort_cancellable(input.data(),actual.data(),w,h,stride,k,countChecks,&checks)==0,"lens status");require(actual==expected&&checks>0,"lens bytes or padding differ from pinned kernel");++cases;}}
    for(int mode=0;mode<4;++mode){constexpr int w=33,h=29;constexpr size_t stride=w*4+12,ms=w+3;auto input=pixels(w,h,stride,true),actual=input,expected=input;std::vector<uint8_t> mask(ms*h);for(int y=0;y<h;++y)for(int x=0;x<w;++x)if(mode==0||(mode==1&&x>10&&x<21&&y>8&&y<20)||(mode==2&&x==16&&y==14))mask[size_t(y)*ms+x]=255;const auto reference=content_fill_reference(expected.data(),stride,mask.data(),ms,w,h);int checks=0;require(content_fill_cancellable(actual.data(),stride,mask.data(),ms,w,h,countChecks,&checks)==reference,"fill status");require(actual==expected&&checks>0,"fill bytes or padding differ from pinned kernel");++cases;}
    constexpr int w=64,h=32;constexpr size_t stride=w*4;auto source=pixels(w,h,stride,true),out=source;int checks=0;require(noise_add_cancellable(out.data(),w,h,stride,87,1,0,77,stopThird,&checks)==-2&&checks==3,"noise cancellation checkpoints");checks=0;require(lens_distort_cancellable(source.data(),out.data(),w,h,stride,.7,stopThird,&checks)==-2&&checks==3,"lens cancellation checkpoints");checks=0;std::vector<uint8_t> mask(w*h);mask[16*w+16]=255;require(content_fill_cancellable(out.data(),stride,mask.data(),w,w,h,stopThird,&checks)==-2&&checks==3,"fill cancellation cleanup");
    std::printf("PASS %d exact pinned-kernel comparisons and3 deterministic cancellation checks\n",cases);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
