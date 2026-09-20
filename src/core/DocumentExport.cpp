#include "DocumentExport.h"
#include <algorithm>

namespace compositor {
imaging::StreamingExportResult exportDocumentAtomic(const Document& document, const std::filesystem::path& path,
    imaging::ExportOptions options, const imaging::StreamExportLimits& limits, const imaging::ExportProgress& progress) {
    imaging::validateExportExtent(std::uint32_t(document.width), std::uint32_t(document.height));
    validateDocument(document);
    options.dpi = document.resolution;
    constexpr int chunkWidth = 1024;
    std::size_t renderBound = 0;
    SoftwareRenderer renderer;
    auto result = imaging::encodeRowsAtomic(path, std::uint32_t(document.width), std::uint32_t(document.height),
        [&](std::uint32_t top, std::uint32_t rows, std::span<std::uint8_t> output, std::size_t stride) {
            for (int left = 0; left < document.width; left += chunkWidth) {
                if (options.cancelled && options.cancelled()) throw imaging::ExportCancelled();
                const int width = std::min(chunkWidth, document.width - left);
                auto raster = renderer.render(document, left, int(top), width, int(rows));
                // StackRenderer holds at most result/group/alpha scanlines plus two
                // adjustment tile sets. Add one 33^3 RGB float lookup cube allowance.
                renderBound = std::max(renderBound, std::size_t(width) * rows * 9 + raster->retainedBytes() * 3 + 1024 * 1024);
                for (std::uint32_t y = 0; y < rows; ++y) {
                    auto* row = output.data() + std::size_t(y) * stride + std::size_t(left) * 4;
                    for (int x = 0; x < width; ++x) {
                        auto p = raster->pixel(x, int(y));
                        row[x * 4] = p.r; row[x * 4 + 1] = p.g; row[x * 4 + 2] = p.b; row[x * 4 + 3] = p.a;
                    }
                }
            }
        }, options, limits, progress);
    result.maxRenderPixelBytes = renderBound;
    return result;
}
}
