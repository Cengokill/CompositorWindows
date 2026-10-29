#include "DeepImage.h"
#include "Document.h"
#include <algorithm>
#include <stdexcept>
#include <bit>
#include <cmath>
#include <limits>

namespace compositor {
float halfToFloat(std::uint16_t h) {
    const float magnitude = (h & 0x7c00) == 0x7c00 ? ((h & 1023) ? std::numeric_limits<float>::quiet_NaN() : std::numeric_limits<float>::infinity())
        : std::ldexp(float((h & 0x7c00) ? 1024 + (h & 1023) : h & 1023), (h & 0x7c00) ? int((h >> 10) & 31) - 25 : -24);
    return h & 0x8000 ? -magnitude : magnitude;
}
std::uint16_t floatToHalf(float value) {
    if(!std::isfinite(value)||std::abs(value)>65504)throw std::runtime_error("HDR sample is not finite or exceeds half-float range");
    const auto bits=std::bit_cast<std::uint32_t>(value);const auto sign=std::uint16_t((bits>>16)&0x8000);
    const int exponent=int((bits>>23)&255)-127;auto mantissa=bits&0x7fffff;
    if(exponent < -25)return sign;
    if(exponent < -14){mantissa|=0x800000;const unsigned shift=unsigned(-exponent-1);const auto rounded=(mantissa+((1u<<(shift-1))-1)+((mantissa>>shift)&1))>>shift;return std::uint16_t(sign|rounded);}
    const auto rounded=mantissa+4095+((mantissa>>13)&1);
    return std::uint16_t(sign|((std::uint32_t(exponent+15)<<10)+(rounded>>13)));
}
double srgbToLinear(double v){return v<=.04045?v/12.92:std::pow((v+.055)/1.055,2.4);}
double linearToSrgb(double v){return v<=.0031308?v*12.92:1.055*std::pow(v,1/2.4)-.055;}
std::array<float,4> DeepImage::pixel(int x,int y)const{
    if(x<0||y<0||x>=width||y>=height)return {};
    const auto* p=at(x,y);std::array<float,4> result{};
    for(int c=0;c<4;++c)result[c]=isHdr()?halfToFloat(p[c]):p[c]/65535.f;
    return result;
}
void DeepImage::setPixel(int x,int y,const std::array<float,4>& value){
    for(float v:value)if(!std::isfinite(v))throw std::runtime_error("Non-finite deep image sample");
    if(value[3]<0||value[3]>1)throw std::runtime_error("Invalid deep image alpha");
    auto* p=at(x,y);
    if(isHdr()){p[3]=floatToHalf(value[3]);const float alpha=halfToFloat(p[3]);for(int c=0;c<3;++c)p[c]=floatToHalf(value[3]>0?value[c]*(alpha/value[3]):0);}
    else for(int c=0;c<4;++c)p[c]=std::uint16_t(std::lround(std::clamp(value[c],0.f,1.f)*65535));
}
std::shared_ptr<DeepImage> makeDeep(int width,int height,DeepImage::Format format){
    if(width<1||height<1||width>30000||height>30000||std::uint64_t(width)*height>100000000)throw std::runtime_error("Deep image exceeds raster budget");
    auto out=std::make_shared<DeepImage>();out->width=width;out->height=height;out->format=format;out->samples.resize(std::size_t(width)*height*4);return out;
}
std::shared_ptr<const DeepImage> hdrTo16Bit(const DeepImage& source,const std::function<bool()>& cancelled){
    if(cancelled&&cancelled())throw std::runtime_error("HDR conversion cancelled");
    if(!source.valid()||uint64_t(source.width)*source.height*16>1200000000)throw std::runtime_error("HDR conversion exceeds working memory budget");auto out=makeDeep(source.width,source.height,DeepImage::Format::Unorm16);
    for(int y=0;y<source.height;++y){if(cancelled&&cancelled())throw std::runtime_error("HDR conversion cancelled");for(int x=0;x<source.width;++x){auto p=source.pixel(x,y);for(int c=0;c<3;++c)p[c]=std::clamp(p[c],0.f,p[3]);out->setPixel(x,y,p);}}return out;
}
NormalizedDeep normalizeDeep(const DeepImage& source,const std::function<bool()>& cancelled){
    if(!source.valid())throw std::runtime_error("Invalid HDR source");
    if(uint64_t(source.width)*source.height*32>1200000000)throw std::runtime_error("HDR color operation exceeds working memory budget");
    NormalizedDeep out;out.scales.resize(std::size_t(source.width)*source.height);auto normal=makeDeep(source.width,source.height,DeepImage::Format::Unorm16);
    for(int y=0;y<source.height;++y){if(cancelled&&cancelled())throw std::runtime_error("HDR operation cancelled");for(int x=0;x<source.width;++x){auto p=source.pixel(x,y);float scale=p[3]>0?std::max({1.f,p[0]/p[3],p[1]/p[3],p[2]/p[3]}):1;
        out.scales[std::size_t(y)*source.width+x]=scale;for(int c=0;c<3;++c)p[c]/=scale;normal->setPixel(x,y,p);}}
    out.raster=rasterFromDeep(*normal);return out;
}
std::shared_ptr<const DeepImage> restoreDeep(const Raster& source,const std::vector<float>& scales,const std::function<bool()>& cancelled){
    if(scales.size()!=std::size_t(source.width)*source.height)throw std::runtime_error("HDR scale grid changed");
    auto out=makeDeep(source.width,source.height);for(int y=0;y<source.height;++y){if(cancelled&&cancelled())throw std::runtime_error("HDR operation cancelled");for(int x=0;x<source.width;++x){auto p=source.pixel(x,y);float scale=scales[std::size_t(y)*source.width+x]/255;
        out->setPixel(x,y,{p.r*scale,p.g*scale,p.b*scale,p.a/255.f});}}return out;
}
std::shared_ptr<const DeepImage> deepFromPlanes(int width, int height, const std::uint16_t* red, const std::uint16_t* green, const std::uint16_t* blue, const std::uint16_t* alpha) {
    if (width < 1 || height < 1 || !red || !green || !blue) throw std::runtime_error("Invalid 16-bit image");
    auto image = std::make_shared<DeepImage>();
    image->width = width;
    image->height = height;
    image->samples.resize(std::size_t(width) * std::size_t(height) * 4);
    for (std::size_t i = 0; i < std::size_t(width) * std::size_t(height); ++i) {
        const std::uint16_t a = alpha ? alpha[i] : std::uint16_t{65535};
        auto* pixel = image->samples.data() + i * 4;
        pixel[0] = std::uint16_t(std::uint32_t(red[i]) * a / 65535);
        pixel[1] = std::uint16_t(std::uint32_t(green[i]) * a / 65535);
        pixel[2] = std::uint16_t(std::uint32_t(blue[i]) * a / 65535);
        pixel[3] = a;
    }
    return image;
}
std::shared_ptr<const Raster> rasterFromDeep(const DeepImage& image) {
    if (!image.valid()) throw std::runtime_error("Invalid 16-bit image");
    std::vector<std::uint8_t> rgba(std::size_t(image.width) * std::size_t(image.height) * 4);
    for (int y = 0; y < image.height; ++y) for (int x = 0; x < image.width; ++x) {
        const auto value = image.pixel(x, y);
        std::uint16_t sample[4];for(int c=0;c<4;++c)sample[c]=std::uint16_t(std::lround(std::clamp(value[c],0.f,c==3?1.f:value[3])*65535));
        auto* pixel = rgba.data() + (std::size_t(y) * std::size_t(image.width) + std::size_t(x)) * 4;
        pixel[3] = ditherChannel(sample[3], x, y);
        for (int channel = 0; channel < 3; ++channel) pixel[channel] = std::min(pixel[3], ditherChannel(sample[channel], x, y));
    }
    return Raster::fromRgba(image.width, image.height, rgba.data(), std::size_t(image.width) * 4);
}
}
