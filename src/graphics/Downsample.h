#pragma once
// DownsampleCache.swift, a19db901, copyright Wonder Assembly LLC2026 (MIT).
#include "core/Document.h"
#include <memory>
namespace compositor::graphics {
struct DownsampledRaster {std::shared_ptr<const Raster> image;int level{};};
struct DownsampledGray {std::shared_ptr<const GrayRaster> image;int level{};};
class DownsampleCache {
public:
    static constexpr uint64_t pixelBudget=100000000;
    static constexpr int maxLevel=6;
    explicit DownsampleCache(uint64_t budget=pixelBudget);
    ~DownsampleCache();
    DownsampleCache(const DownsampleCache&)=delete;
    DownsampleCache& operator=(const DownsampleCache&)=delete;
    static int levelFor(double factor);
    static std::shared_ptr<const Raster> halve(const Raster&);
    static std::shared_ptr<const GrayRaster> halve(const GrayRaster&);
    DownsampledRaster level(std::shared_ptr<const Raster>,int wanted);
    DownsampledGray level(std::shared_ptr<const GrayRaster>,int wanted);
    std::shared_ptr<const Raster> image(std::shared_ptr<const Raster> source,double factor){return level(std::move(source),levelFor(factor)).image;}
    std::shared_ptr<const GrayRaster> image(std::shared_ptr<const GrayRaster> source,double factor){return level(std::move(source),levelFor(factor)).image;}
    uint64_t retainedPixels()const;
    size_t entryCount()const;
    void clear();
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
// Isolated LayerRenderer.reduced/coverage adapter. No global sampler is changed.
Layer downsampleLayer(const Layer&,double devicePixelsPerDocumentPixel,DownsampleCache&);
}
