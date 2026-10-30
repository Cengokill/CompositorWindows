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
using ExportProgress = std::function<void(std::uint32_t completed, std::uint32_t total)>;
// Pinned ImageExporter.swift20-23: 30,000 per side AND 100,000,000 total pixels.
inline constexpr std::uint32_t maxExportSide = 30000;
inline constexpr std::uint64_t maxExportPixels = 100000000;
void validateExportExtent(std::uint32_t width, std::uint32_t height);
struct ExportSize { std::uint32_t width{}, height{}; };
// Pixel size of a PDF export at `scale` (a positive multiplier of the canvas, floored at 1%, a Windows choice; the
// dialog offers 25% and above). A scale that would pass the extent limits above is reduced to the largest that fits.
// The aspect ratio is kept to within one pixel. Throws std::invalid_argument for a canvas outside the limits or a
// non-finite/non-positive scale.
ExportSize scaledExportSize(std::uint32_t width, std::uint32_t height, double scale);
// Input callback writes premultiplied sRGB RGBA8, top-to-bottom, into the supplied storage.
// PNG/TIFF preview is averaged from the losslessly encoded samples (CMYK is converted back to RGB). JPEG preview is
// decoded from the finished JPEG through WIC's bounded native downscaling interface.
StreamingExportResult encodeRowsAtomic(const std::filesystem::path&, std::uint32_t width, std::uint32_t height,
    const RgbaRows&, const ExportOptions& = {}, const StreamExportLimits& = {}, const ExportProgress& = {});
// One-page PDF of the flattened canvas (ExportOptions::format must be Pdf). `rows` supplies the canvas at width x height,
// as for encodeRowsAtomic. The page holds the canvas at options.scale, laid out at options.dpi so that its printed size
// is pixels / dpi inches, over an opaque options.matte* background that shows through transparent pixels. The pixels are
// embedded losslessly (Flate), and unscaled they stream in stripes within limits.maxBufferBytes as for encodeRowsAtomic.
// A scale other than 1 first holds the whole premultiplied canvas (4 bytes a pixel) for resampling, counted in
// peakOwnedPixelBytes but not limited by maxBufferBytes. The preview is the opaque page averaged down to the preview
// limits. Progress counts completed rows out of the total it passes: source rows rendered, then output rows encoded
// when scaling (total = source height + output height), or output rows alone otherwise. Cancellation and failure leave
// an existing destination untouched, as for encodeRowsAtomic.
StreamingExportResult encodePdfAtomic(const std::filesystem::path&, std::uint32_t width, std::uint32_t height,
    const RgbaRows&, const ExportOptions&, const StreamExportLimits& = {}, const ExportProgress& = {});
void copyEncodedAtomic(const std::filesystem::path& source, const std::filesystem::path& destination,
    const std::function<bool()>& cancelled = {});
}
