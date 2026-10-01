#pragma once
#include "core/Document.h"
#include <filesystem>
#include <string>

namespace compositor::imaging {
struct PsdImport { Document document; std::string report; };
PsdImport readPsd(const std::filesystem::path&);
PsdImport readPsd(const uint8_t* bytes, size_t size);
}
