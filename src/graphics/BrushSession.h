#pragma once
#include "BrushCoverage.h"
#include "core/Document.h"
#include <map>
#include <set>

namespace compositor::graphics {
struct BrushSessionSettings {
    double radius{20},hardness{1},opacity{1};
    std::array<uint8_t,3> color{0,0,0}; // straight sRGB, opaque paint color
    bool erase{};
};
struct BrushSessionGeometry {
    double a{1},b{},c{},d{1},tx{},ty{}; // source pixel edge to document pixel edge
    int canvasWidth{},canvasHeight{}; // zero selects the original raster dimensions
    static BrushSessionGeometry forLayer(const Transform&,int sourceWidth,int sourceHeight,int canvasWidth,int canvasHeight);
};
struct BrushSessionMetrics {
    uint64_t acceptedSamples{},settledSegments{},coverageTileAllocations{},outputTileAllocations{},publishedSnapshots{},acceleratorFallbacks{};
    size_t touchedTiles{},coverageStorageBytes{};
    // Session never calls Raster::rgba/fromRgba or creates a flattened image.
    uint64_t fullRasterMaterializations{};
    double tilePreparationMilliseconds{},coverageRenderMilliseconds{},tileCompositionMilliseconds{};
};
class BrushSession {
public:
    using CoverageMap=std::map<size_t,std::shared_ptr<const BrushTile>>;
    enum class Output { Paint,CoverageOnly };
    BrushSession(std::shared_ptr<const Raster> original,BrushSessionSettings,
        std::shared_ptr<D3D11BrushCoverage> accelerator={},
        std::shared_ptr<const GrayRaster> selection={},BrushSessionGeometry geometry={},Output output=Output::Paint);
    bool begin(Point);
    bool append(Point);
    std::shared_ptr<const Raster> preview() const {return published_;}
    std::shared_ptr<const Raster> commit();
    std::shared_ptr<const Raster> cancel();
    const BrushSessionMetrics& metrics()const{return metrics_;}
    const std::string& acceleratorError()const{return acceleratorError_;}
    const CoverageMap& coverageTiles()const{return coverage_;}
private:
    std::shared_ptr<const Raster> original_,published_;
    BrushSessionSettings settings_;
    BrushSessionGeometry geometry_;
    std::shared_ptr<D3D11BrushCoverage> accelerator_;
    std::shared_ptr<const GrayRaster> selection_;
    CoverageMap coverage_;
    std::set<size_t> tailKeys_;
    std::vector<Point> samples_;
    BrushSessionMetrics metrics_;
    std::string acceleratorError_;
    bool begun_{},finished_{},emptySelection_{};
    Output output_{Output::Paint};
    std::set<size_t> affectedKeys(std::span<const BrushSegment>)const;
    void render(std::span<const BrushSegment> settled,std::span<const BrushSegment> tail);
};
// Direct translation of upstream centripetal Catmull-Rom adaptive chords,
// maximum centerline deviation 0.2 document pixels, recursive depth <=10.
std::vector<BrushSegment> brushContinuousCurve(Point start,Point end,Point before,Point after);
} // namespace compositor::graphics
