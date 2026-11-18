#include "BilinearRows.h"
#include "CompositeRows.h"
#include "RasterSampling.h"
#include <atomic>
#include <cmath>
#include <cstring>
#include <intrin.h>
#include <map>
#include <mutex>

namespace compositor::graphics {
namespace {
Pixel unpack(uint32_t bits){Pixel p;std::memcpy(&p,&bits,4);return p;}
uint32_t pack(Pixel p){uint32_t bits;std::memcpy(&bits,&p,4);return bits;}
std::atomic<bool> wideVectorsAllowed{true};
// StackRenderer's clamp(lround(v),0,255).
uint8_t byte(double v){if(v>=255.5)return 255;if(!(v>=.5))return 0;const int whole=int(v);return uint8_t(whole+(v-whole>=.5));}
}
bool opacityFixedPoint(double opacity,uint32_t& multiplier,uint32_t& offset){
    if(!(opacity>=0&&opacity<=1))return false;
    struct Found {bool ok;uint32_t multiplier,offset;};
    static std::mutex mutex;static std::map<double,Found> found;
    std::lock_guard lock(mutex);
    if(auto it=found.find(opacity);it!=found.end()){multiplier=it->second.multiplier;offset=it->second.offset;return it->second.ok;}
    Found result{false,0,0};
    const int64_t centre=int64_t(std::floor(opacity*65536));
    for(int64_t m=std::max<int64_t>(0,centre-64);m<=centre+64&&!result.ok;++m){
        // Each n bounds the offset: target*65536 <= n*m+offset < (target+1)*65536.
        int64_t low=0,high=65535;
        for(int n=0;n<256&&low<=high;++n){
            const int64_t target=byte(n*opacity);
            low=std::max(low,target*65536-n*m);high=std::min(high,(target+1)*65536-1-n*m);
        }
        if(low<=high)result={true,uint32_t(m),uint32_t(low)};
    }
    if(result.ok)for(int n=0;n<256;++n)if((uint64_t(n)*result.multiplier+result.offset)>>16!=byte(n*opacity))result.ok=false;
    found.emplace(opacity,result);
    multiplier=result.multiplier;offset=result.offset;return result.ok;
}
bool bilinearRowAvxAvailable(){
    static const bool available=[]{
        int info[4]{};__cpuid(info,1);
        const bool osxsave=(info[2]>>27)&1,avx=(info[2]>>28)&1;
        // Windows must also save the YMM registers across context switches.
        return osxsave&&avx&&(_xgetbv(0)&6)==6;
    }();
    return available&&wideVectorsAllowed;
}
// Checked here, outside the AVX2 file, whose code may use AVX2 instructions anywhere.
bool compositeRowsAvx2Available(){
    static const bool available=[]{
        int info[4]{};__cpuid(info,0);
        if(info[0]<7)return false;
        __cpuidex(info,7,0);
        return ((info[1]>>5)&1)!=0;
    }();
    return available&&bilinearRowAvxAvailable();
}
void setWideVectorsAllowed(bool allowed){wideVectorsAllowed=allowed;}
size_t sampleBilinearRowPortable(const BilinearRow& row,int* stop,int stopAt,size_t width,double opacity,uint32_t* raw,uint32_t* scaled){
    size_t stopped=0;
    for(size_t column=0;column<width;++column){
        if(stop[column]>=0)continue;
        if(!row.inside[column]){if(raw)raw[column]=0;scaled[column]=0;continue;}
        const size_t at=size_t(row.x[column]-row.left);
        const Pixel value=bilinearPixel(unpack(row.upper[at]),unpack(row.upper[at+1]),unpack(row.lower[at]),unpack(row.lower[at+1]),row.fx[column],row.fy);
        const Pixel result=opacity==1?value:Pixel{byte(value.r*opacity),byte(value.g*opacity),byte(value.b*opacity),byte(value.a*opacity)};
        if(raw)raw[column]=pack(value);
        scaled[column]=pack(result);
        if(stopAt>=0&&result.a==255){stop[column]=stopAt;++stopped;}
    }
    return stopped;
}
size_t sampleBilinearRow(const BilinearRow& row,int* stop,int stopAt,size_t width,double opacity,uint32_t* raw,uint32_t* scaled){
    return bilinearRowAvxAvailable()?sampleBilinearRowAvx(row,stop,stopAt,width,opacity,raw,scaled):sampleBilinearRowPortable(row,stop,stopAt,width,opacity,raw,scaled);
}
}
