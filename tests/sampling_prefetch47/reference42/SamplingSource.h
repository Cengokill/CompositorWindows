#pragma once
#include "core/Document.h"
#include <memory>

namespace compositor::graphics {
// Readers own an immutable image or snapshot. Coordinates are integer source
// pixels; they are called only within the stated extent. This object never
// enters a saved Document. Alignment is the inherited halving grid origin.
struct SamplingSource {
    int width{},height{},alignmentX{},alignmentY{};
    std::shared_ptr<const void> identity;
    std::function<Pixel(int,int)> rgba;
    std::function<uint8_t(int,int)> gray;
    // Optional equivalent contiguous in-bounds reads. For gray sources only
    // Pixel::r is used. The callback must initialize every requested pixel.
    std::function<void(int,int,int,Pixel*)> readRow;
    // Optional cross-snapshot reuse. Within one domain, these ordered immutable
    // tokens determine every pixel in the requested in-bounds source rectangle.
    // Geometry and sampling type are checked separately by the reduction cache.
    std::shared_ptr<const void> reuseDomain;
    std::function<std::vector<std::shared_ptr<const void>>(int,int,int,int)> dependencies;
};
std::shared_ptr<const SamplingSource> samplingSource(std::shared_ptr<const Raster>);
std::shared_ptr<const SamplingSource> samplingSource(std::shared_ptr<const GrayRaster>);
struct SamplingGrid {int x{},y{},width{},height{},step{1};};
SamplingGrid samplingGrid(const SamplingSource&,int level);

class ReducedSource {
public:
    const SamplingGrid& grid()const{return grid_;}
    int level()const{return level_;}
    const std::shared_ptr<const SamplingSource>& source()const{return source_;}
    Pixel pixel(int x,int y)const;
    uint8_t gray(int x,int y)const;
    // Unit coordinates remain in the original source, including any extended
    // ceil/aligned reduction coverage, so live and committed geometry agrees.
    Pixel sample(Point unit,Transform::Sampling)const;
    double sampleGray(Point unit,Transform::Sampling,uint8_t exterior=0)const;
private:
    friend class ReducedSourceCache;
    struct Storage;
    std::shared_ptr<Storage> storage_;
    std::shared_ptr<const SamplingSource> source_;
    SamplingGrid grid_;
    int level_{};
};
class ReducedSourceCache {
public:
    // Reduced pixels only: at most 256 256x256 RGBA tiles (64MiB). Gray tiles
    // use this same upper bound. Level zero reads original storage directly.
    explicit ReducedSourceCache(size_t tileBudget=256,size_t sourceBudget=32);
    ~ReducedSourceCache();
    ReducedSourceCache(const ReducedSourceCache&)=delete;
    ReducedSourceCache& operator=(const ReducedSourceCache&)=delete;
    std::shared_ptr<const ReducedSource> resolve(std::shared_ptr<const SamplingSource>,int level);
    size_t retainedTiles()const;
    size_t retainedSources()const;
    uint64_t generatedTiles()const;
    uint64_t reusedTiles()const;
    void clear();
    static ReducedSourceCache& shared();
private:
    std::shared_ptr<ReducedSource::Storage> storage_;
};
}
