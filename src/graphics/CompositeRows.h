#pragma once
#include <cstddef>
#include <cstdint>

namespace compositor::graphics {
// Composites layers from..count-1 over sixteen packed pixels. sources holds each
// layer's row of samples, stride pixels apart. A Normal layer over pixels that are
// all opaque uses the opaque-destination Normal formula, eight pixels at a time;
// any other layer is handed to fallback, which composites all sixteen in place.
using CompositeFallback=void(*)(void* context,size_t layer,uint32_t* pixels);
void compositeSixteenAvx2(const uint32_t* sources,size_t stride,const uint8_t* normal,size_t from,size_t count,
    uint32_t* pixels,CompositeFallback fallback,void* context);
bool compositeRowsAvx2Available();
}
