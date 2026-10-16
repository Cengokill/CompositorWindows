#pragma once
#include "core/DeepImage.h"
#include "image_types.h"
#include <filesystem>
#include <span>
namespace compositor::imaging {
// Each call owns a COM apartment if needed; safe on initialized STA/MTA worker threads.
class WicCodec {
public:
    static DecodedImage decode(const std::filesystem::path&,const ImportOptions& = {});
    static void encode(const std::filesystem::path&,const RgbaImage&,const ExportOptions& = {});
    static RgbaImage decodeProjectRgbaPng(const std::filesystem::path&,const ImportOptions& = {});
    static std::shared_ptr<const DeepImage> decodeProjectDeepPng(const std::filesystem::path&,const ImportOptions& = {});
    static void encodeProjectDeepPng(const std::filesystem::path&,const DeepImage&);
    static GrayMask decodeProjectGrayPng(const std::filesystem::path&,const ImportOptions& = {});
    static void encodeProjectGrayPng(const std::filesystem::path&,const GrayMask&);
    // HEIC adapter uses this to normalize embedded ICC before premultiplication.
    static void normalizeStraightRgba(std::span<std::uint8_t>,std::uint32_t,std::uint32_t,std::span<const std::uint8_t> icc);
};
}
