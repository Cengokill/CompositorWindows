#pragma once
#include "core/Document.h"
#include <filesystem>
#include <string>
#include <vector>

namespace compositor::imaging {
// `warnings` lists layers whose Photoshop vector content stayed pixels or was omitted.
struct PsdArtboard { std::string name; Document document; };
struct PsdImport { Document document; std::string report; std::vector<std::string> warnings; std::vector<PsdArtboard> artboards; };
PsdImport readPsd(const std::filesystem::path&,CmykProfilePtr fallback={});
PsdImport readPsd(const uint8_t* bytes, size_t size,CmykProfilePtr fallback={});
}
