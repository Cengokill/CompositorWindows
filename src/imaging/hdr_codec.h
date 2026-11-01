#pragma once
#include "core/DeepImage.h"
#include "image_types.h"
#include <filesystem>
#include <span>

namespace compositor::imaging {
struct HdrExtent { int width{},height{}; };
// Header-only preflight; EXR must be a single RGB(A) image, never deep/multipart.
HdrExtent inspectExr(const std::filesystem::path&,const ImportOptions& = {});
std::shared_ptr<const DeepImage> decodeExr(const std::filesystem::path&,const ImportOptions& = {});
std::shared_ptr<const DeepImage> decodeRadiance(const std::filesystem::path&,const ImportOptions& = {});
// Raw encoder for a project staging directory or an export temporary file.
// ZIP-compressed scene-linear FLOAT RGBA; callers own replacement/rollback.
using HdrRows=std::function<void(int y,std::span<WidePixel>)>;
void encodeExr(const std::filesystem::path&,int width,int height,const HdrRows&,const std::function<bool()>& cancelled={});
void encodeExr(const std::filesystem::path&,const DeepImage&,const std::function<bool()>& cancelled={});
}
