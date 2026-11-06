#pragma once

#include "core/Document.h"
#include "image_types.h"
#include <filesystem>

namespace compositor::imaging {
// Called on a worker thread. Pages are returned in PDF order.
std::vector<std::shared_ptr<const Raster>> rasterizePdf(
    const std::filesystem::path&, double dpi, const ImportOptions&);
}
