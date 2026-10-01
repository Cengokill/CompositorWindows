#pragma once
#include "image_types.h"
#include <filesystem>

namespace compositor::imaging {
struct RawDevelop { double exposure{}; double temperature{}; };
DecodedImage decodeRaw(const std::filesystem::path&, RawDevelop = {}, const ImportOptions& = {});
}
