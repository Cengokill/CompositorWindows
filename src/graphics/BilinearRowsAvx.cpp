// Compiled with /arch:AVX. Keep this file free of standard library templates and
// inline functions: an AVX copy of one could be linked into callers on any processor.
#include "BilinearRows.h"
#include <immintrin.h>

namespace compositor::graphics {
namespace {
// bilinearPixel's rounding of nonnegative values, in double: truncate, then add
// one when the exact fraction is at least a half.
__m256d rounded(__m256d value){
    const __m256d whole=_mm256_round_pd(value,_MM_FROUND_TO_ZERO|_MM_FROUND_NO_EXC);
    const __m256d fraction=_mm256_sub_pd(value,whole);
    return _mm256_add_pd(whole,_mm256_and_pd(_mm256_cmp_pd(fraction,_mm256_set1_pd(.5),_CMP_GE_OQ),_mm256_set1_pd(1.)));
}
// Saturating to 0..255 as bilinearPixel's packs, and as clamp after lround.
uint32_t packed(__m256d channels){
    const __m128i whole=_mm256_cvttpd_epi32(channels);
    return uint32_t(_mm_cvtsi128_si32(_mm_packus_epi16(_mm_packs_epi32(whole,_mm_setzero_si128()),_mm_setzero_si128())));
}
// A source pixel's channels as floats (exact for bytes): from the widened row when
// there is one, otherwise converted from the packed row as it is read.
template<bool Wide> __m128 texel(const BilinearRow& row,bool upperRow,size_t index){
    if constexpr(Wide)return _mm_loadu_ps((upperRow?row.upperWide:row.lowerWide)+index*4);
    else return _mm_cvtepi32_ps(_mm_cvtepu8_epi32(_mm_cvtsi32_si128(int((upperRow?row.upper:row.lower)[index]))));
}
// One column exactly as bilinearPixel, in double: the products and sums per channel.
template<bool Wide> void exact(const BilinearRow& row,size_t column,double opacity,uint32_t* raw,uint32_t* scaled){
    const size_t at=size_t(row.x[column]-row.left);
    const auto wide=[&](bool upperRow,size_t index){return _mm256_cvtps_pd(texel<Wide>(row,upperRow,index));};
    const __m256d left=_mm256_broadcast_sd(row.fxComplement+column),right=_mm256_broadcast_sd(row.fx+column);
    const __m256d upper=_mm256_add_pd(_mm256_mul_pd(left,wide(true,at)),_mm256_mul_pd(right,wide(true,at+1)));
    const __m256d lower=_mm256_add_pd(_mm256_mul_pd(left,wide(false,at)),_mm256_mul_pd(right,wide(false,at+1)));
    const __m256d channels=rounded(_mm256_add_pd(_mm256_mul_pd(_mm256_set1_pd(1-row.fy),upper),_mm256_mul_pd(_mm256_set1_pd(row.fy),lower)));
    const uint32_t value=packed(opacity!=1?rounded(_mm256_mul_pd(channels,_mm256_set1_pd(opacity))):channels);
    scaled[column]=value;
    if(raw)raw[column]=opacity!=1?packed(channels):value;
}
// Round half up in float, and flag values within margin of a half integer. Away
// from one, the float result rounds as the double computation does.
__m256 roundedNear(__m256 value,__m256& unsure){
    const __m256 whole=_mm256_floor_ps(value),fraction=_mm256_sub_ps(value,whole);
    const __m256 distance=_mm256_andnot_ps(_mm256_set1_ps(-0.f),_mm256_sub_ps(fraction,_mm256_set1_ps(.5f)));
    // The float sample differs from the double one by under 2e-4: about ten float
    // roundings of values up to 255 (each under 1.6e-5), and fx, fy rounded to float.
    unsure=_mm256_or_ps(unsure,_mm256_cmp_ps(distance,_mm256_set1_ps(1.f/1024),_CMP_LT_OQ));
    return _mm256_add_ps(whole,_mm256_and_ps(_mm256_cmp_ps(fraction,_mm256_set1_ps(.5f),_CMP_GE_OQ),_mm256_set1_ps(1.f)));
}
// Two pixels' channels, 0..255 in float, as two packed pixels.
void store2(__m256 channels,uint32_t* output){
    const __m256i whole=_mm256_cvttps_epi32(channels);
    const __m128i words=_mm_packs_epi32(_mm256_castsi256_si128(whole),_mm256_extractf128_si256(whole,1));
    _mm_storel_epi64(reinterpret_cast<__m128i*>(output),_mm_packus_epi16(words,_mm_setzero_si128()));
}
template<bool Wide> __m256 pair(const BilinearRow& row,bool upperRow,size_t first,size_t second){return _mm256_insertf128_ps(_mm256_castps128_ps256(texel<Wide>(row,upperRow,first)),texel<Wide>(row,upperRow,second),1);}
template<bool Wide> size_t sampleRow(const BilinearRow& row,int* stop,int stopAt,size_t width,double opacity,uint32_t* raw,uint32_t* scaled);
}
void widenPixels(const uint32_t* pixels,size_t count,float* wide){
    for(size_t i=0;i<count;++i)_mm_storeu_ps(wide+i*4,_mm_cvtepi32_ps(_mm_cvtepu8_epi32(_mm_cvtsi32_si128(int(pixels[i])))));
}
void widenWeights(const double* weights,size_t count,float* wide){
    for(size_t i=0;i<count;++i)_mm_storeu_ps(wide+i*4,_mm_set1_ps(float(weights[i])));
}
size_t sampleBilinearRowAvx(const BilinearRow& row,int* stop,int stopAt,size_t width,double opacity,uint32_t* raw,uint32_t* scaled){
    return row.upperWide?sampleRow<true>(row,stop,stopAt,width,opacity,raw,scaled):sampleRow<false>(row,stop,stopAt,width,opacity,raw,scaled);
}
namespace {
template<bool Wide> size_t sampleRow(const BilinearRow& row,int* stop,int stopAt,size_t width,double opacity,uint32_t* raw,uint32_t* scaled){
    const __m256 fy=_mm256_set1_ps(float(row.fy)),oneMinusFy=_mm256_set1_ps(float(1-row.fy));
    const __m256d factor=_mm256_set1_pd(opacity);
    const bool scale=opacity!=1;
    size_t stopped=0;
    const auto settle=[&](size_t column){if(stopAt>=0&&scaled[column]>=0xFF000000u){stop[column]=stopAt;++stopped;}};
    for(size_t column=0;column<width;){
        if(column+1<width&&stop[column]<0&&stop[column+1]<0&&row.inside[column]&&row.inside[column+1]){
            // Two columns at once in float, the same products and sums per channel.
            const size_t first=size_t(row.x[column]-row.left),second=size_t(row.x[column+1]-row.left);
            const __m256 left=_mm256_loadu_ps(row.fxComplementWide+column*4),right=_mm256_loadu_ps(row.fxWide+column*4);
            const __m256 upper=_mm256_add_ps(_mm256_mul_ps(left,pair<Wide>(row,true,first,second)),_mm256_mul_ps(right,pair<Wide>(row,true,first+1,second+1)));
            const __m256 lower=_mm256_add_ps(_mm256_mul_ps(left,pair<Wide>(row,false,first,second)),_mm256_mul_ps(right,pair<Wide>(row,false,first+1,second+1)));
            __m256 unsure=_mm256_setzero_ps();
            const __m256 channels=roundedNear(_mm256_add_ps(_mm256_mul_ps(oneMinusFy,upper),_mm256_mul_ps(fy,lower)),unsure);
            if(_mm256_movemask_ps(unsure)){exact<Wide>(row,column,opacity,raw,scaled);exact<Wide>(row,column+1,opacity,raw,scaled);}
            else if(!scale){store2(channels,scaled+column);if(raw)store2(channels,raw+column);}
            else if(row.opacityFixed){
                // Whole channels times opacity through integers checked for all 256 values.
                const __m256i whole=_mm256_cvttps_epi32(channels);
                const __m128i multiplier=_mm_set1_epi32(int(row.opacityMultiplier)),offset=_mm_set1_epi32(int(row.opacityOffset));
                const __m128i low=_mm_srli_epi32(_mm_add_epi32(_mm_mullo_epi32(_mm256_castsi256_si128(whole),multiplier),offset),16);
                const __m128i high=_mm_srli_epi32(_mm_add_epi32(_mm_mullo_epi32(_mm256_extractf128_si256(whole,1),multiplier),offset),16);
                _mm_storel_epi64(reinterpret_cast<__m128i*>(scaled+column),_mm_packus_epi16(_mm_packs_epi32(low,high),_mm_setzero_si128()));
                if(raw)store2(channels,raw+column);
            }
            else{
                // The opacity product of whole channels often lands exactly on a half
                // (5*0.9), so it stays in double: exact, like the LUT it replaces.
                scaled[column]=packed(rounded(_mm256_mul_pd(_mm256_cvtps_pd(_mm256_castps256_ps128(channels)),factor)));
                scaled[column+1]=packed(rounded(_mm256_mul_pd(_mm256_cvtps_pd(_mm256_extractf128_ps(channels,1)),factor)));
                if(raw)store2(channels,raw+column);
            }
            settle(column);settle(column+1);
            column+=2;continue;
        }
        if(stop[column]<0){
            if(!row.inside[column]){if(raw)raw[column]=0;scaled[column]=0;}
            else exact<Wide>(row,column,opacity,raw,scaled);
            settle(column);
        }
        ++column;
    }
    return stopped;
}
}
}
