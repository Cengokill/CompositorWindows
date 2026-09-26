#include "PixelAlgorithms.h"
#include "UpstreamAlgorithms.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <stdexcept>

namespace compositor::graphics {
namespace {
struct KernelCancellation {
    const Cancellation& callback;
    std::exception_ptr exception;
    static int poll(void* context) noexcept {
        auto& state = *static_cast<KernelCancellation*>(context);
        try { return state.callback() ? 1 : 0; }
        catch (...) { state.exception = std::current_exception(); return 1; }
    }
    auto function() const -> int (*)(void*) { return callback ? poll : nullptr; }
    void finish(int result) const {
        if (exception) std::rethrow_exception(exception);
        if (result == -2) throw std::runtime_error("Filter cancelled");
    }
};
void check(size_t size, uint32_t w, uint32_t h, size_t stride, size_t channels) {
    // The editor's 30,000px source dimension limit also keeps signed int C indices
    // and the LLP64 wand result count within range (900,000,000 pixels).
    if (!w || !h || w > 30000 || h > 30000) throw std::invalid_argument("Raster dimensions must be 1..30000");
    const size_t row = size_t(w) * channels;
    if (stride < row || size_t(h - 1) > (std::numeric_limits<size_t>::max() - row) / stride)
        throw std::invalid_argument("Invalid raster stride or extent overflow");
    if (size < size_t(h - 1) * stride + row) throw std::invalid_argument("Raster buffer is too short");
}
template<class A, class B> void sameSize(A a, B b) {
    validate(a); validate(b);
    if (a.width != b.width || a.height != b.height) throw std::invalid_argument("Raster dimensions differ");
}
void range(double v, double lo, double hi, const char* name) {
    if (!std::isfinite(v) || v < lo || v > hi) throw std::invalid_argument(name);
}
template<class A, class B> bool overlap(const A& a, const B& b) {
    const auto first=reinterpret_cast<uintptr_t>(a.data()), second=reinterpret_cast<uintptr_t>(b.data());
    return (first<=second && second-first<a.size_bytes()) || (second<first && first-second<b.size_bytes());
}
std::vector<uint8_t> packed(ConstGray8View v) {
    validate(v);
    std::vector<uint8_t> result(size_t(v.width) * v.height);
    for (uint32_t y = 0; y < v.height; ++y)
        std::memcpy(result.data() + size_t(y) * v.width, v.bytes.data() + size_t(y) * v.stride, v.width);
    return result;
}
}
void validate(Rgba8View v) { check(v.bytes.size(),v.width,v.height,v.stride,4); }
void validate(ConstRgba8View v) { check(v.bytes.size(),v.width,v.height,v.stride,4); }
void validate(Gray8View v) { check(v.bytes.size(),v.width,v.height,v.stride,1); }
void validate(ConstGray8View v) { check(v.bytes.size(),v.width,v.height,v.stride,1); }
ConstRgba8View readOnly(Rgba8View v) { return {v.bytes,v.width,v.height,v.stride}; }
ConstGray8View readOnly(Gray8View v) { return {v.bytes,v.width,v.height,v.stride}; }
std::array<size_t,4> alphaBounds(ConstRgba8View v) {
    validate(v); std::array<size_t,4> result{};
    brush_alpha_bounds(v.bytes.data(),v.width,v.height,v.stride,result.data()); return result;
}
std::array<int64_t,4> coverageBounds(ConstGray8View v) {
    validate(v); std::array<int64_t,4> result{};
    heal_coverage_bounds(v.bytes.data(),v.width,v.height,v.stride,result.data()); return result;
}
void extractAlpha(ConstRgba8View a, Gray8View b) {
    sameSize(a,b); if(overlap(a.bytes,b.bytes))throw std::invalid_argument("Alpha source and destination overlap");
    layer_extract_alpha(a.bytes.data(),a.stride,b.bytes.data(),b.stride,a.width,a.height);
}
void unpremultiplyOpaque(Rgba8View v) { validate(v); layer_unpremultiply_opaque(v.bytes.data(),v.stride,v.width,v.height); }
void restoreAlpha(Rgba8View a, ConstGray8View b) {
    sameSize(a,b); if(overlap(a.bytes,b.bytes))throw std::invalid_argument("Alpha source and destination overlap");
    layer_restore_alpha(a.bytes.data(),a.stride,b.bytes.data(),b.stride,a.width,a.height);
}
void clampPremultiplied(Rgba8View v) {
    validate(v); for (uint32_t y=0;y<v.height;++y) rgba_clamp_premultiplied(v.bytes.data()+y*v.stride,v.width);
}
void applyLevels(Rgba8View v, std::span<const float,768> tables) {
    validate(v); if(overlap(v.bytes,tables))throw std::invalid_argument("Levels table overlaps pixels");
    for(float x:tables) range(x,0,1,"Levels table outside [0,1]");
    for(uint32_t y=0;y<v.height;++y) levels_apply(v.bytes.data()+y*v.stride,v.width,tables.data());
}
std::array<double,1024> histogram(ConstRgba8View v, const ConstGray8View* mask) {
    validate(v); if(mask) sameSize(v,*mask); std::array<double,1024> bins{};
    for(uint32_t y=0;y<v.height;++y) levels_histogram(v.bytes.data()+y*v.stride,
        mask ? mask->bytes.data()+y*mask->stride : nullptr,v.width,bins.data()); return bins;
}
void gradientMap(Rgba8View v,std::span<const uint8_t,768> table) {
    validate(v); if(overlap(v.bytes,table))throw std::invalid_argument("Gradient table overlaps pixels");
    adjust_gradient_map(v.bytes.data(),v.width,v.height,v.stride,table.data());
}
void addNoise(Rgba8View v,float amount,bool gaussian,bool monochromatic,uint32_t seed,const Cancellation& cancelled) {
    validate(v); range(amount,0,400,"Noise amount outside [0,400]");
    KernelCancellation state{cancelled,{}};
    state.finish(noise_add_cancellable(v.bytes.data(),v.width,v.height,v.stride,amount,gaussian,monochromatic,seed,state.function(),&state));
}
void grain(Rgba8View v,double amount,double size,double roughness,uint32_t seed,double ox,double oy,double scale) {
    validate(v); range(amount,0,100,"Grain amount"); range(size,0.001,10000000,"Grain size");
    range(roughness,0,100,"Grain roughness"); range(ox,-1e7,1e7,"Grain origin"); range(oy,-1e7,1e7,"Grain origin");
    range(scale,1e-7,1e7,"Grain scale");
    // Bounds keep floor(position/size) far inside int64_t, including neighbors.
    adjust_grain(v.bytes.data(),v.width,v.height,v.stride,amount,size,roughness,seed,ox,oy,scale);
}
void lensDistort(ConstRgba8View a,Rgba8View b,double k,const Cancellation& cancelled) {
    sameSize(a,b); range(k,-1,1,"Lens coefficient outside [-1,1]");
    if(a.stride!=b.stride) throw std::invalid_argument("Lens source/destination strides differ");
    if(overlap(a.bytes,b.bytes))throw std::invalid_argument("Lens source and destination overlap");
    KernelCancellation state{cancelled,{}};
    state.finish(lens_distort_cancellable(a.bytes.data(),b.bytes.data(),a.width,a.height,a.stride,k,state.function(),&state));
}
long wandMask(ConstRgba8View a,size_t x,size_t y,size_t radius,int tolerance,bool contiguous,Gray8View b) {
    sameSize(a,b); if(radius>30000 || tolerance<0 || tolerance>255) throw std::invalid_argument("Wand parameters");
    std::vector<uint8_t> mask(size_t(a.width)*a.height);
    long n=wand_mask(a.bytes.data(),a.width,a.height,a.stride,x,y,radius,tolerance,contiguous,mask.data());
    if(n<0) throw std::bad_alloc();
    for(uint32_t row=0;row<a.height;++row) std::memcpy(b.bytes.data()+row*b.stride,mask.data()+size_t(row)*a.width,a.width);
    return n;
}
WandOutline wandOutline(ConstGray8View v) {
    auto mask=packed(v); int32_t *p=nullptr,*l=nullptr; size_t np=0,nl=0;
    int status=wand_trace(mask.data(),v.width,v.height,&p,&np,&l,&nl);
    std::unique_ptr<int32_t,decltype(&std::free)> points(p,&std::free),loops(l,&std::free);
    if(status==-1) throw std::bad_alloc(); if(status==-2) throw std::length_error("Wand outline exceeds 8000000 edges");
    WandOutline result; if(np) result.points.assign(p,p+np*2); if(nl) result.loopLengths.assign(l,l+nl); return result;
}
bool contentFill(Rgba8View v,ConstGray8View mask,const Cancellation& cancelled) {
    sameSize(v,mask); KernelCancellation state{cancelled,{}};
    int result=content_fill_cancellable(v.bytes.data(),v.stride,mask.bytes.data(),mask.stride,int(v.width),int(v.height),state.function(),&state);
    state.finish(result);
    if(result<0) throw std::bad_alloc(); return result!=0;
}
void spotHeal(Rgba8View v,ConstGray8View mask,float opacity,int mode,uint32_t seed) {
    sameSize(v,mask); range(opacity,0,1,"Healing opacity"); if(mode<0 || mode>2) throw std::invalid_argument("Healing mode");
    auto coverage=packed(mask);
    if(spot_heal(v.bytes.data(),coverage.data(),v.width,v.height,v.stride,opacity,mode,seed)<0) throw std::bad_alloc();
}
} // namespace compositor::graphics
