#include "ProjectStore.h"
#include "imaging/wic_codec.h"
#include <algorithm>

namespace compositor {
ProjectAssetCodec makeWicProjectCodec(){ProjectAssetCodec c;
    c.readColor=[](const std::filesystem::path&p){auto image=imaging::WicCodec::decodeProjectRgbaPng(p);return Raster::fromRgba(int(image.width),int(image.height),image.pixels.data(),image.stride);};
    c.readGray=[](const std::filesystem::path&p){auto image=imaging::WicCodec::decodeProjectGrayPng(p);auto result=std::make_shared<GrayRaster>();result->width=int(image.width);result->height=int(image.height);result->pixels.resize(size_t(image.width)*image.height);for(uint32_t y=0;y<image.height;++y)std::copy_n(image.pixels.data()+size_t(y)*image.stride,image.width,result->pixels.data()+size_t(y)*image.width);return std::shared_ptr<const GrayRaster>(result);};
    c.writeColor=[](const std::filesystem::path&p,const Raster&r){imaging::RgbaImage image{uint32_t(r.width),uint32_t(r.height),size_t(r.width)*4,r.rgba()};imaging::ExportOptions options;options.format=imaging::ImageFormat::Png;imaging::WicCodec::encode(p,image,options);};
    c.writeGray=[](const std::filesystem::path&p,const GrayRaster&r){imaging::GrayMask mask{uint32_t(r.width),uint32_t(r.height),size_t(r.width),r.pixels};imaging::WicCodec::encodeProjectGrayPng(p,mask);};
    return c;
}
}
