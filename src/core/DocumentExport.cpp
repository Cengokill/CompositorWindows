#include "DocumentExport.h"
#include "core/DeepImage.h"
#include "imaging/wic_codec.h"
#include <algorithm>
#include <cmath>

namespace compositor {
namespace {
void over16(std::uint16_t* destination, const std::uint16_t* source, double amount) {
    const auto scale = [&](std::uint16_t value) { return std::min(65535u, static_cast<std::uint32_t>(std::lround(value * amount))); };
    const auto sourceAlpha = scale(source[3]);
    const auto inverse = 65535u - sourceAlpha;
    for (int channel = 0; channel < 3; ++channel) destination[channel] = std::uint16_t(std::min(65535u, scale(source[channel]) + std::uint32_t(destination[channel]) * inverse / 65535u));
    destination[3] = std::uint16_t(std::min(65535u, sourceAlpha + std::uint32_t(destination[3]) * inverse / 65535u));
}
bool sampleLayer(const Layer& layer, int x, int y, std::uint16_t sample[4], double& coverage) {
    const auto unit = layer.transform.toUnit({x + 0.5, y + 0.5});
    if (unit.x < 0 || unit.y < 0 || unit.x >= 1 || unit.y >= 1) return false;
    if (layer.deep && layer.deep->valid()) {
        const int sx = std::min(layer.deep->width - 1, int(unit.x * layer.deep->width));
        const int sy = std::min(layer.deep->height - 1, int(unit.y * layer.deep->height));
        std::copy_n(layer.deep->at(sx, sy), 4, sample);
    } else if (layer.raster) {
        const int sx = std::min(layer.raster->width - 1, int(unit.x * layer.raster->width));
        const int sy = std::min(layer.raster->height - 1, int(unit.y * layer.raster->height));
        const auto pixel = layer.raster->pixel(sx, sy);
        sample[0] = promote8(pixel.r); sample[1] = promote8(pixel.g); sample[2] = promote8(pixel.b); sample[3] = promote8(pixel.a);
    } else return false;
    coverage = layer.opacity;
    if (layer.mask && layer.mask->enabled && layer.mask->raster) {
        const auto placement = layer.mask->placement.value_or(layer.transform);
        const auto maskUnit = placement.toUnit({x + 0.5, y + 0.5});
        if (maskUnit.x < 0 || maskUnit.y < 0 || maskUnit.x >= 1 || maskUnit.y >= 1) coverage = 0;
        else {
            const int mx = std::min(layer.mask->raster->width - 1, int(maskUnit.x * layer.mask->raster->width));
            const int my = std::min(layer.mask->raster->height - 1, int(maskUnit.y * layer.mask->raster->height));
            coverage *= layer.mask->raster->pixel(mx, my) / 255.;
        }
    }
    return coverage > 0;
}
// Root layers only. Folders, clipping, adjustments, and effects change coverage in
// SoftwareRenderer; this walk does not, so those documents export through that renderer.
bool flatDeepExport(const Document& document) {
    bool anyDeep = false;
    for (const auto& layer : document.layers) {
        if (layer.group || !layer.parentId.empty() || !layer.maskSourceId.empty()) return false;
        if (!layer.visible) continue;
        if (!layer.adjustmentJson.empty() || layer.effects.active()) return false;
        if (layer.opacity > 0 && layer.deep && layer.deep->valid()) anyDeep = true;
    }
    return anyDeep;
}
std::shared_ptr<const DeepImage> flattenDeep(const Document& document, const imaging::ExportOptions& options) {
    auto image = std::make_shared<DeepImage>();
    image->width = document.width; image->height = document.height;
    image->samples.assign(std::size_t(document.width) * std::size_t(document.height) * 4, 0);
    for (const auto& layer : document.layers) {
        if (!layer.visible || layer.group || layer.opacity <= 0 || (!layer.deep && !layer.raster)) continue;
        for (int y = 0; y < document.height; ++y) {
            if (options.cancelled && options.cancelled()) throw imaging::ExportCancelled();
            for (int x = 0; x < document.width; ++x) {
                std::uint16_t sample[4]{};
                double coverage = 0;
                if (!sampleLayer(layer, x, y, sample, coverage)) continue;
                auto* destination = image->at(x, y);
                if (layer.blend == Blend::Normal) over16(destination, sample, coverage);
                else {
                    Pixel under{ditherChannel(destination[0], x, y), ditherChannel(destination[1], x, y), ditherChannel(destination[2], x, y), ditherChannel(destination[3], x, y)};
                    Pixel over{ditherChannel(sample[0], x, y), ditherChannel(sample[1], x, y), ditherChannel(sample[2], x, y), ditherChannel(sample[3], x, y)};
                    over.a = std::uint8_t(std::clamp(std::lround(over.a * coverage), 0L, 255L));
                    for (int channel = 0; channel < 3; ++channel) (&over.r)[channel] = std::uint8_t(std::min<int>(over.a, std::lround((&over.r)[channel] * coverage)));
                    const auto blended = blendPixel(under, over, layer.blend);
                    destination[0] = promote8(blended.r); destination[1] = promote8(blended.g); destination[2] = promote8(blended.b); destination[3] = promote8(blended.a);
                }
            }
        }
    }
    return image;
}
imaging::RgbaImage deepPreview(const DeepImage& image, std::uint32_t maxWidth, std::uint32_t maxHeight) {
    const int width = std::max(1, std::min(int(maxWidth), image.width));
    const int height = std::max(1, std::min(int(maxHeight), image.height));
    imaging::RgbaImage preview{std::uint32_t(width), std::uint32_t(height), std::size_t(width) * 4, std::vector<std::uint8_t>(std::size_t(width) * std::size_t(height) * 4)};
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        const int sx = std::min(image.width - 1, x * image.width / width), sy = std::min(image.height - 1, y * image.height / height);
        const auto* sample = image.at(sx, sy);
        auto* pixel = preview.pixels.data() + (std::size_t(y) * std::size_t(width) + std::size_t(x)) * 4;
        pixel[3] = ditherChannel(sample[3], sx, sy);
        for (int channel = 0; channel < 3; ++channel) pixel[channel] = std::min(pixel[3], ditherChannel(sample[channel], sx, sy));
    }
    return preview;
}
}
imaging::StreamingExportResult exportDocumentAtomic(const Document& document, const std::filesystem::path& path,
    imaging::ExportOptions options, const imaging::StreamExportLimits& limits, const imaging::ExportProgress& progress) {
    imaging::validateExportExtent(std::uint32_t(document.width), std::uint32_t(document.height));
    validateDocument(document);
    if (options.cmykProfile && options.format == imaging::ImageFormat::Png) throw std::invalid_argument("PNG does not support CMYK output");
    options.dpi = document.resolution;
    const bool deepPng = options.format == imaging::ImageFormat::Png && flatDeepExport(document);
    if (deepPng) {
        auto flat = flattenDeep(document, options);
        imaging::WicCodec::encodeProjectDeepPng(path, *flat);
        if (progress) progress(std::uint32_t(document.height), std::uint32_t(document.height));
        imaging::StreamingExportResult result;
        result.encodedBytes = std::filesystem::file_size(path);
        result.preview = deepPreview(*flat, limits.previewWidth, limits.previewHeight);
        result.stripeRows = limits.stripeRows;
        result.maxRenderPixelBytes = flat->samples.size() * sizeof(std::uint16_t);
        return result;
    }
    constexpr int chunkWidth = 1024;
    std::size_t renderBound = 0;
    SoftwareRenderer renderer;
    // A stack with no image or adjustment has no color contribution, including
    // empty layers/folders with masks. Fill its encoder stripes directly instead
    // of allocating and discarding transparent Raster tiles for every chunk.
    // Adjustments still go through the renderer so invalid effects remain errors.
    const bool emptyStack=std::none_of(document.layers.begin(),document.layers.end(),[](const Layer& layer){return layer.raster||!layer.adjustmentJson.empty();});
    const imaging::RgbaRows fillRows =
        [&](std::uint32_t top, std::uint32_t rows, std::span<std::uint8_t> output, std::size_t stride) {
            if(emptyStack){for(std::uint32_t y=0;y<rows;++y)std::fill_n(output.data()+std::size_t(y)*stride,std::size_t(document.width)*4,uint8_t(0));return;}
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
        };
    // A PDF page is the same 8-bit composite as the canvas (never the 16-bit PNG path), encoded at the document's
    // resolution; encodePdfAtomic scales it and fills transparent pixels with the chosen background.
    auto result = options.format == imaging::ImageFormat::Pdf
        ? imaging::encodePdfAtomic(path, std::uint32_t(document.width), std::uint32_t(document.height), fillRows, options, limits, progress)
        : imaging::encodeRowsAtomic(path, std::uint32_t(document.width), std::uint32_t(document.height), fillRows, options, limits, progress);
    result.maxRenderPixelBytes = renderBound;
    return result;
}
}
