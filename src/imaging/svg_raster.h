#pragma once
#include "image_types.h"
#include <filesystem>

namespace compositor::imaging {
DecodedImage rasterizeSvg(const std::filesystem::path&, const ImportOptions& = {});
}
