#pragma once
#include "image_types.h"
#include <filesystem>
namespace compositor::imaging {
class HeifCodec { public: static DecodedImage decode(const std::filesystem::path&,const ImportOptions& = {}); };
}
