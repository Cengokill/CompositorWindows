#include "DocumentExport.h"
#include "core/DeepImage.h"
#include "imaging/wic_codec.h"
#include "imaging/hdr_codec.h"
#include "effects/Adjustments.h"
#include "imaging/export_temporary_file.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>

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
        if(layer.deep&&layer.deep->isHdr())return false;
        if (layer.opacity > 0 && layer.deep && layer.deep->valid()) anyDeep = true;
    }
    return anyDeep;
}
std::shared_ptr<DeepImage> flattenDeep(const Document& document, const imaging::ExportOptions& options) {
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
// Renders canvas rows into premultiplied RGBA8 through SoftwareRenderer. The region may start at (originX, originY)
// in document coordinates, so a trimmed export keeps the canvas pixels, grain and masks of a full render.
struct StackRowRenderer {
    static constexpr int chunkWidth = 1024;
    const Document& document;
    const imaging::ExportOptions& options;
    SoftwareRenderer renderer;
    std::size_t renderBound = 0;
    const bool hdrStack;
    // A stack with no image or adjustment has no color contribution, including
    // empty layers/folders with masks. Fill its encoder stripes directly instead
    // of allocating and discarding transparent Raster tiles for every chunk.
    // Adjustments still go through the renderer so invalid effects remain errors.
    const bool emptyStack;
    StackRowRenderer(const Document& source, const imaging::ExportOptions& exportOptions)
        : document(source), options(exportOptions),
          hdrStack(std::any_of(source.layers.begin(), source.layers.end(), [](const Layer& layer){ return layer.deep && layer.deep->isHdr(); })),
          emptyStack(std::none_of(source.layers.begin(), source.layers.end(), [](const Layer& layer){ return layer.raster || !layer.adjustmentJson.empty(); })) {}
    void fill(int originX, int originY, int regionWidth, std::uint32_t top, std::uint32_t rows, std::span<std::uint8_t> output, std::size_t stride) {
        if(emptyStack){for(std::uint32_t y=0;y<rows;++y)std::fill_n(output.data()+std::size_t(y)*stride,std::size_t(regionWidth)*4,uint8_t(0));return;}
        for (int left = 0; left < regionWidth; left += chunkWidth) {
            if (options.cancelled && options.cancelled()) throw imaging::ExportCancelled();
            const int width = std::min(chunkWidth, regionWidth - left);
            const int margin=effects::adjustmentStackPadding(document);
            if(width>30000-2*margin||rows>std::uint32_t(30000-2*margin)||std::uint64_t(width+2*margin)*(rows+2*margin)>100000000)throw std::runtime_error("Adjustment export padding exceeds render budget");
            const int x = originX + left, y0 = originY + int(top);
            auto raster = hdrStack?rasterFromDeep(*renderer.renderDeep(document,x,y0,width,int(rows),options.cancelled)):renderer.render(document, x-margin, y0-margin, width+2*margin, int(rows)+2*margin);
            const int offset=hdrStack?0:margin;
            // StackRenderer holds at most result/group/alpha scanlines plus two
            // adjustment tile sets. Add one 33^3 RGB float lookup cube allowance.
            renderBound = std::max(renderBound, std::size_t(width+2*offset) * (rows+2*offset) * 9 + raster->retainedBytes() * 3 + 1024 * 1024);
            if(hdrStack)renderBound=std::max(renderBound,size_t(width+2*margin)*(rows+2*margin)*80+raster->retainedBytes());
            for (std::uint32_t y = 0; y < rows; ++y) {
                auto* row = output.data() + std::size_t(y) * stride + std::size_t(left) * 4;
                for (int px = 0; px < width; ++px) {
                    auto p = raster->pixel(px+offset, int(y)+offset);
                    row[px * 4] = p.r; row[px * 4 + 1] = p.g; row[px * 4 + 2] = p.b; row[px * 4 + 3] = p.a;
                }
            }
        }
    }
};
}
imaging::StreamingExportResult exportDocumentAtomic(const Document& document, const std::filesystem::path& path,
    imaging::ExportOptions options, const imaging::StreamExportLimits& limits, const imaging::ExportProgress& progress) {
    imaging::validateExportExtent(std::uint32_t(document.width), std::uint32_t(document.height));
    validateDocument(document);
    if (options.cmykProfile && options.format == imaging::ImageFormat::Png) throw std::invalid_argument("PNG does not support CMYK output");
    options.dpi = document.resolution;
    if(options.format==imaging::ImageFormat::Exr){
        imaging::detail::TemporaryFile temporary(path);SoftwareRenderer renderer;
        if(!limits.stripeRows||limits.maxBufferBytes<512*1024)throw std::runtime_error("EXR export buffer budget is too small");
        imaging::StreamingExportResult result;result.stripeRows=limits.stripeRows;
        const auto width=uint32_t(document.width),height=uint32_t(document.height);
        const auto previewSize=imaging::detail::fitPreview(width,height,limits.previewWidth,limits.previewHeight);
        result.preview={previewSize.first,previewSize.second,size_t(previewSize.first)*4,std::vector<uint8_t>(size_t(previewSize.first)*previewSize.second*4)};
        // One row of HDR values is handed to the encoder. Render tiles stay
        // bounded independently of canvas size; adjustment halos are supplied
        // by the same export region renderer as the SDR stack.
        int cachedTop=-1;std::shared_ptr<const DeepImage> stripe;
        const int margin=effects::adjustmentStackPadding(document);
        const auto renderBytes=[&](int rows){return uint64_t(std::min(1024,document.width)+2*margin)*(rows+2*margin)*80+uint64_t(width)*rows*8+uint64_t(width)*sizeof(WidePixel)+result.preview.pixels.size();};
        int stripeRows=int(std::min<uint64_t>(limits.stripeRows,height));
        while(stripeRows>1&&renderBytes(stripeRows)>limits.maxBufferBytes)stripeRows=std::max(1,stripeRows/2);
        if(renderBytes(stripeRows)>limits.maxBufferBytes)throw std::runtime_error("EXR adjustment halo exceeds export buffer budget");
        result.stripeRows=uint32_t(stripeRows);
        try{imaging::encodeExr(temporary.path,document.width,document.height,[&](int y,std::span<WidePixel> row){
            if(options.cancelled&&options.cancelled())throw imaging::ExportCancelled();
            if(cachedTop<0||y>=cachedTop+stripe->height){cachedTop=y;stripe.reset();auto next=makeDeep(document.width,std::min(stripeRows,document.height-y));
                for(int left=0;left<document.width;left+=1024){const int tileWidth=std::min(1024,document.width-left);auto tile=renderer.renderDeep(document,left,y,tileWidth,next->height,options.cancelled);for(int row=0;row<next->height;++row)for(int col=0;col<tileWidth;++col)next->setPixel(left+col,row,tile->pixel(col,row));result.maxRenderPixelBytes=std::max(result.maxRenderPixelBytes,size_t(tileWidth+2*margin)*(next->height+2*margin)*80+next->samples.size()*2);}
                stripe=std::move(next);
            }
            for(int x=0;x<document.width;++x)row[size_t(x)]=stripe->pixel(x,y-cachedTop);
            if(result.preview.width&&result.preview.height){
                for(uint32_t py=0;py<result.preview.height;++py)if(int((uint64_t(2*py+1)*height)/(2*result.preview.height))==y){
                    for(uint32_t px=0;px<result.preview.width;++px){auto p=row[size_t((uint64_t(2*px+1)*width)/(2*result.preview.width))];const auto i=size_t(py)*result.preview.stride+px*4;auto alpha=uint8_t(std::clamp(std::lround(p[3]*255),0L,255L));result.preview.pixels[i+3]=alpha;for(int c=0;c<3;++c)result.preview.pixels[i+c]=uint8_t(std::clamp(std::lround(p[c]*255),0L,long(alpha)));}
                }
            }
            if(progress)progress(uint32_t(y+1),height);
        },options.cancelled);}catch(...){if(options.cancelled&&options.cancelled())throw imaging::ExportCancelled();throw;}
        if(options.cancelled&&options.cancelled())throw imaging::ExportCancelled();
        result.encodedBytes=std::filesystem::file_size(temporary.path);result.peakOwnedPixelBytes=result.maxRenderPixelBytes+size_t(width)*sizeof(WidePixel)+result.preview.pixels.size();temporary.replace(path,options.cancelled);return result;
    }
    const bool deepPng = options.format == imaging::ImageFormat::Png && flatDeepExport(document);
    if (deepPng) {
        if(options.pngInterlaced&&std::uint64_t(document.width)*document.height*8>512ULL*1024*1024)throw std::invalid_argument("16-bit interlaced PNG exceeds the 512 MiB encoder image budget for this canvas.");
        auto flat = flattenDeep(document, options);
        if(!options.pngTransparency){
            const std::uint16_t matte[]{promote8(options.matteR),promote8(options.matteG),promote8(options.matteB)};
            for(int y=0;y<flat->height;++y){if(options.cancelled&&options.cancelled())throw imaging::ExportCancelled();for(int x=0;x<flat->width;++x){auto* pixel=flat->at(x,y);const auto inverse=65535u-pixel[3];for(int c=0;c<3;++c)pixel[c]=std::uint16_t(std::min(65535u,unsigned(pixel[c])+(unsigned(matte[c])*inverse+32767u)/65535u));pixel[3]=65535;}}
        }
        imaging::WicCodec::encodeProjectDeepPng(path, *flat,&options);
        if (progress) progress(std::uint32_t(document.height), std::uint32_t(document.height));
        imaging::StreamingExportResult result;
        result.encodedBytes = std::filesystem::file_size(path);
        result.preview = deepPreview(*flat, limits.previewWidth, limits.previewHeight);
        result.stripeRows = limits.stripeRows;
        result.maxRenderPixelBytes = flat->samples.size() * sizeof(std::uint16_t);
        return result;
    }
    StackRowRenderer stack(document, options);
    const imaging::RgbaRows fillRows =
        [&](std::uint32_t top, std::uint32_t rows, std::span<std::uint8_t> output, std::size_t stride) {
            stack.fill(0, 0, document.width, top, rows, output, stride);
        };
    // A PDF page is the same 8-bit composite as the canvas (never the 16-bit PNG path), encoded at the document's
    // resolution; encodePdfAtomic scales it and fills transparent pixels with the chosen background.
    auto result = options.format == imaging::ImageFormat::Pdf
        ? imaging::encodePdfAtomic(path, std::uint32_t(document.width), std::uint32_t(document.height), fillRows, options, limits, progress)
        : imaging::encodeRowsAtomic(path, std::uint32_t(document.width), std::uint32_t(document.height), fillRows, options, limits, progress);
    result.maxRenderPixelBytes = stack.renderBound;
    return result;
}

Document isolateLayerForExport(const Document& document, const std::string& id) {
    validateDocument(document);
    std::unordered_map<std::string, std::size_t> index;
    for (std::size_t i = 0; i < document.layers.size(); ++i) index.emplace(document.layers[i].id, i);
    const auto found = index.find(id);
    if (found == index.end()) throw std::invalid_argument("Layer to export is missing");
    const std::size_t target = found->second;
    std::vector<char> keep(document.layers.size(), 0), shown(document.layers.size(), 0);
    // The exported layer and its folders are drawn even when hidden, so their opacity and masks apply.
    for (std::size_t i = target;;) {
        keep[i] = shown[i] = 1;
        if (document.layers[i].parentId.empty()) break;
        i = index.at(document.layers[i].parentId);
    }
    // A folder keeps the visibility its contents have in the document.
    if (document.layers[target].group) for (std::size_t i = 0; i < document.layers.size(); ++i) {
        if (i == target) continue;
        bool inside = false;
        for (std::size_t node = i; document.layers[node].visible && !document.layers[node].parentId.empty();) {
            node = index.at(document.layers[node].parentId);
            if (node == target) { inside = true; break; }
        }
        if (inside) keep[i] = shown[i] = 1;
    }
    // Clipping sources keep clipping while hidden: the renderer reads their coverage whatever their visibility,
    // so a clipped layer is cut by its base without drawing the base. Their folders are kept for the hierarchy.
    for (bool changed = true; changed;) {
        changed = false;
        for (std::size_t i = 0; i < document.layers.size(); ++i) {
            if (!keep[i]) continue;
            for (const auto* related : {&document.layers[i].maskSourceId, &document.layers[i].parentId}) {
                if (related->empty()) continue;
                const auto at = index.at(*related);
                if (!keep[at]) { keep[at] = 1; changed = true; }
            }
        }
    }
    Document isolated = document;
    isolated.layers.clear();
    for (std::size_t i = 0; i < document.layers.size(); ++i) {
        if (!keep[i]) continue;
        isolated.layers.push_back(document.layers[i]);
        isolated.layers.back().visible = shown[i] != 0;
    }
    validateDocument(isolated);
    return isolated;
}

std::optional<imaging::StreamingExportResult> exportLayerPngAtomic(const Document& document, const std::string& id,
    const std::filesystem::path& path, imaging::ExportOptions options, const imaging::StreamExportLimits& limits,
    const imaging::ExportProgress& progress) {
    imaging::validateExportExtent(std::uint32_t(document.width), std::uint32_t(document.height));
    const Document isolated = isolateLayerForExport(document, id);
    options.format = imaging::ImageFormat::Png;
    options.cmykProfile = nullptr;
    options.pngTransparency = true;
    options.dpi = document.resolution;
    int minX = document.width, minY = document.height, maxX = -1, maxY = -1;
    if (flatDeepExport(isolated)) {
        const auto flat = flattenDeep(isolated, options);
        for (int y = 0; y < flat->height; ++y) for (int x = 0; x < flat->width; ++x) if (flat->at(x, y)[3]) {
            minX = std::min(minX, x); maxX = std::max(maxX, x); minY = std::min(minY, y); maxY = std::max(maxY, y);
        }
        if (maxX < 0) return std::nullopt;
        auto cropped = std::make_shared<DeepImage>();
        cropped->width = maxX - minX + 1; cropped->height = maxY - minY + 1;
        if (options.pngInterlaced && std::uint64_t(cropped->width) * cropped->height * 8 > 512ULL * 1024 * 1024)
            throw std::invalid_argument("16-bit interlaced PNG exceeds the 512 MiB encoder image budget for this canvas.");
        cropped->samples.resize(std::size_t(cropped->width) * std::size_t(cropped->height) * 4);
        for (int y = 0; y < cropped->height; ++y) std::copy_n(flat->at(minX, minY + y), std::size_t(cropped->width) * 4, cropped->at(0, y));
        imaging::WicCodec::encodeProjectDeepPng(path, *cropped, &options);
        if (progress) progress(std::uint32_t(cropped->height), std::uint32_t(cropped->height));
        imaging::StreamingExportResult result;
        result.encodedBytes = std::filesystem::file_size(path);
        result.preview = deepPreview(*cropped, limits.previewWidth, limits.previewHeight);
        result.stripeRows = limits.stripeRows;
        result.maxRenderPixelBytes = flat->samples.size() * sizeof(std::uint16_t);
        return result;
    }
    StackRowRenderer stack(isolated, options);
    if (stack.emptyStack) return std::nullopt;
    // Pass 1 finds the pixels that are not fully transparent, one bounded stripe at a time.
    std::uint32_t stripeRows = std::max(1u, std::min(limits.stripeRows, std::uint32_t(document.height)));
    while (stripeRows > 1 && std::uint64_t(document.width) * stripeRows * 4 > limits.maxBufferBytes) stripeRows /= 2;
    const std::size_t stride = std::size_t(document.width) * 4;
    std::vector<std::uint8_t> stripe(stride * stripeRows);
    for (std::uint32_t top = 0; top < std::uint32_t(document.height); top += stripeRows) {
        const std::uint32_t rows = std::min(stripeRows, std::uint32_t(document.height) - top);
        stack.fill(0, 0, document.width, top, rows, stripe, stride);
        for (std::uint32_t y = 0; y < rows; ++y) for (int x = 0; x < document.width; ++x) if (stripe[std::size_t(y) * stride + std::size_t(x) * 4 + 3]) {
            minX = std::min(minX, x); maxX = std::max(maxX, x); minY = std::min(minY, int(top + y)); maxY = std::max(maxY, int(top + y));
        }
    }
    if (maxX < 0) return std::nullopt;
    const int width = maxX - minX + 1, height = maxY - minY + 1;
    const imaging::RgbaRows fillRows = [&](std::uint32_t top, std::uint32_t rows, std::span<std::uint8_t> output, std::size_t outputStride) {
        stack.fill(minX, minY, width, top, rows, output, outputStride);
    };
    auto result = imaging::encodeRowsAtomic(path, std::uint32_t(width), std::uint32_t(height), fillRows, options, limits, progress);
    result.maxRenderPixelBytes = stack.renderBound;
    return result;
}

imaging::StreamingExportResult exportChannelPackAtomic(const Document& document, const std::filesystem::path& path,
    const std::array<std::string, 4>& sources, imaging::ExportOptions options,
    const imaging::StreamExportLimits& limits, const imaging::ExportProgress& progress) {
    imaging::validateExportExtent(std::uint32_t(document.width), std::uint32_t(document.height));
    validateDocument(document);
    std::array<Document, 4> isolated;
    for (size_t c = 0; c < sources.size(); ++c) {
        if (sources[c].empty() || sources[c] == "white") continue;
        if (sources[c][0] != 'L') throw std::invalid_argument("Invalid channel source");
        const auto id = sources[c].substr(1);
        const auto find = [&](const std::string& key) { return std::find_if(document.layers.begin(), document.layers.end(), [&](const Layer& l) { return l.id == key; }); };
        if (find(id) == document.layers.end()) throw std::invalid_argument("Channel source layer is missing");
        isolated[c] = document;
        for (auto& layer : isolated[c].layers) {
            bool ancestor = false, descendant = false;
            auto current = find(id);
            while (current != document.layers.end()) { if (current->id == layer.id) { ancestor = true; break; } if (current->parentId.empty()) break; current = find(current->parentId); }
            current = find(layer.id);
            while (current != document.layers.end() && !current->parentId.empty()) { if (current->parentId == id) { descendant = true; break; } current = find(current->parentId); }
            if (ancestor) layer.visible = true;
            else if (!descendant) layer.visible = false;
        }
    }
    options.format = imaging::ImageFormat::Png; options.cmykProfile = nullptr; options.dpi = document.resolution;
    SoftwareRenderer renderer; std::size_t renderBound = 0;
    auto rows = [&](std::uint32_t top, std::uint32_t count, std::span<std::uint8_t> output, std::size_t stride) {
        for (size_t c = 0; c < sources.size(); ++c) {
            if (options.cancelled && options.cancelled()) throw imaging::ExportCancelled();
            if (sources[c].empty() || sources[c] == "white") {
                for (std::uint32_t y = 0; y < count; ++y) for (int x = 0; x < document.width; ++x) output[size_t(y)*stride+size_t(x)*4+c] = sources[c].empty() ? 0 : 255;
                continue;
            }
            const auto& source = isolated[c]; const int margin = effects::adjustmentStackPadding(source);
            for (int left = 0; left < document.width; left += 1024) {
                if (options.cancelled && options.cancelled()) throw imaging::ExportCancelled();
                const int width = std::min(1024, document.width-left);
                if (width > 30000-2*margin || count > std::uint32_t(30000-2*margin) || std::uint64_t(width+2*margin)*(count+2*margin)>100000000) throw std::runtime_error("Adjustment export padding exceeds render budget");
                const bool hdr = std::any_of(source.layers.begin(), source.layers.end(), [](const Layer& l){return l.deep && l.deep->isHdr();});
                auto raster = hdr ? rasterFromDeep(*renderer.renderDeep(source,left,int(top),width,int(count),options.cancelled)) : renderer.render(source,left-margin,int(top)-margin,width+2*margin,int(count)+2*margin);
                const int offset = hdr ? 0 : margin;
                renderBound = std::max(renderBound, size_t(width+2*margin)*(count+2*margin)*(hdr?80:9)+raster->retainedBytes()*3+1024*1024);
                for (std::uint32_t y=0;y<count;++y) for (int x=0;x<width;++x) {
                    const auto p = raster->pixel(x+offset,int(y)+offset);
                    output[size_t(y)*stride+size_t(left+x)*4+c] = std::uint8_t(std::clamp(std::lround(.2126*p.r+.7152*p.g+.0722*p.b),0L,255L));
                }
            }
        }
    };
    auto result = imaging::encodeRowsAtomic(path, std::uint32_t(document.width), std::uint32_t(document.height), rows, options, limits, progress, true);
    result.maxRenderPixelBytes = renderBound; return result;
}

}
