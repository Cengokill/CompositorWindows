#pragma once
#include "image_types.h"
#include <filesystem>
#include <span>

namespace compositor::imaging {
class ExportCancelled final : public std::runtime_error {
public: ExportCancelled() : std::runtime_error("Image export cancelled") {}
};
struct StreamExportLimits {
    // Owned pixel buffers only; existing document storage and WIC internals are separate.
    std::size_t maxBufferBytes{64 * 1024 * 1024};
    std::uint32_t stripeRows{64};
    std::uint32_t previewWidth{520}, previewHeight{320};
};
struct StreamingExportResult {
    std::uint64_t encodedBytes{};
    std::size_t peakOwnedPixelBytes{};
    std::size_t maxRenderPixelBytes{};
    std::uint32_t stripeRows{};
    RgbaImage preview;
};
using RgbaRows = std::function<void(std::uint32_t y, std::uint32_t rows, std::span<std::uint8_t> output, std::size_t stride)>;
using ExportProgress = std::function<void(std::uint32_t completedRows, std::uint32_t height)>;
// Pinned ImageExporter.swift20-23: 30,000 per side AND 100,000,000 total pixels.
void validateExportExtent(std::uint32_t width, std::uint32_t height);
// Input callback writes premultiplied sRGB RGBA8, top-to-bottom, into the supplied storage.
// PNG preview is averaged from the exact losslessly encoded pixels. JPEG preview is
// decoded from the finished JPEG through WIC's bounded native downscaling interface.
StreamingExportResult encodeRowsAtomic(const std::filesystem::path&, std::uint32_t width, std::uint32_t height,
    const RgbaRows&, const ExportOptions& = {}, const StreamExportLimits& = {}, const ExportProgress& = {});
void copyEncodedAtomic(const std::filesystem::path& source, const std::filesystem::path& destination,
    const std::function<bool()>& cancelled = {});
}
