// Compiled with /arch:AVX2. Keep this file free of standard library templates and
// inline functions: an AVX2 copy of one could be linked into callers on any processor.
#include "CompositeRows.h"
#include <immintrin.h>

namespace compositor::graphics {
namespace {
bool opaque(__m256i pixels){const __m256i alphas=_mm256_set1_epi32(int(0xFF000000));return _mm256_movemask_epi8(_mm256_cmpeq_epi32(_mm256_and_si256(pixels,alphas),alphas))==-1;}
// StackRenderer's overOpaque4 for eight pixels: the same 16-bit operations per lane.
__m256i overOpaque(__m256i destination,__m256i source){
    const __m256i zero=_mm256_setzero_si256(),alphas=_mm256_set1_epi32(int(0xFF000000));
    const auto half=[&](__m256i dst,__m256i src){
        const __m256i alpha=_mm256_shufflehi_epi16(_mm256_shufflelo_epi16(src,_MM_SHUFFLE(3,3,3,3)),_MM_SHUFFLE(3,3,3,3));
        const __m256i scaled=_mm256_add_epi16(_mm256_mullo_epi16(dst,_mm256_sub_epi16(_mm256_set1_epi16(255),alpha)),_mm256_set1_epi16(127));
        const __m256i quotient=_mm256_srli_epi16(_mm256_mulhi_epu16(scaled,_mm256_set1_epi16(-32639)),7); // 0x8081
        return _mm256_min_epi16(_mm256_add_epi16(src,quotient),_mm256_set1_epi16(255));
    };
    const __m256i blended=_mm256_or_si256(_mm256_packus_epi16(half(_mm256_unpacklo_epi8(destination,zero),_mm256_unpacklo_epi8(source,zero)),
        half(_mm256_unpackhi_epi8(destination,zero),_mm256_unpackhi_epi8(source,zero))),alphas);
    const __m256i transparent=_mm256_cmpeq_epi32(_mm256_and_si256(source,alphas),zero);
    return _mm256_or_si256(_mm256_and_si256(transparent,destination),_mm256_andnot_si256(transparent,blended));
}
}
void compositeSixteenAvx2(const uint32_t* sources,size_t stride,const uint8_t* normal,size_t from,size_t count,
    uint32_t* pixels,CompositeFallback fallback,void* context){
    __m256i first=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(pixels)),second=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(pixels+8));
    for(size_t k=from;k<count;++k){
        const uint32_t* row=sources+k*stride;
        // Two independent chains of eight keep the processor busy.
        if(normal[k]&&opaque(first)&&opaque(second)){
            first=overOpaque(first,_mm256_loadu_si256(reinterpret_cast<const __m256i*>(row)));
            second=overOpaque(second,_mm256_loadu_si256(reinterpret_cast<const __m256i*>(row+8)));
            continue;
        }
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(pixels),first);_mm256_storeu_si256(reinterpret_cast<__m256i*>(pixels+8),second);
        fallback(context,k,pixels);
        first=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(pixels));second=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(pixels+8));
    }
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(pixels),first);_mm256_storeu_si256(reinterpret_cast<__m256i*>(pixels+8),second);
}
}
