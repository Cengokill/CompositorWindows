#pragma once
#include "BrushSession.h"
#include <map>
#include <memory>
#include <functional>
#include <span>
#include <vector>
namespace compositor::graphics {
struct BrushSourceRect {int x{},y{},width{},height{};bool empty()const{return width<=0||height<=0;}bool operator==(const BrushSourceRect&)const=default;};
struct BrushTileKey {int x{},y{};auto operator<=>(const BrushTileKey&)const=default;};
struct GrowingBrushMetrics {
    BrushSessionMetrics coverage;
    size_t sparsePixelTiles{},sparsePixelBytes{};
    uint64_t snapshots{},materializedTiles{},reusedTiles{};
};
// Synchronous composition only. Published snapshots own pixels, never these spans.
struct GrowingTileInput {
    BrushSourceRect rect; // ORIGINAL source-grid coordinates, including negatives
    BrushSessionGeometry mapping;
    std::span<const Pixel> original; // stride 256 pixels
    std::span<const uint8_t> rawCoverage; // stride rect.width, selection not applied
    std::shared_ptr<const GrayRaster> selection;
};
using GrowingTileCompositor=std::function<void(const GrowingTileInput&,std::span<Pixel>)>; // output stride 256
struct GrowingCoverageTile {BrushSourceRect rect;std::shared_ptr<const BrushTile> coverage;};
class GrowingBrushSnapshot {
public:
    struct Impl;
    BrushSourceRect sourceBounds()const;
    Transform transform()const;
    bool changed()const;
    bool isMask()const;
    // Integer coordinates are in the ORIGINAL source pixel grid, including negatives.
    Pixel pixel(int sourceX,int sourceY)const;
    // Fully valid owned region; never renders or flattens the whole virtual canvas.
    std::shared_ptr<const Raster> sample(int sourceX,int sourceY,int width,int height)const;
    // Canonical source-grid sampling without a temporary image or resampling.
    // The returned renderer-only object owns this immutable snapshot's state.
    std::shared_ptr<const LayerRenderPreview> renderPreview()const;
    // Ephemeral layer for a viewport render only. Includes two source pixels of
    // interpolation halo and bounds pixel storage with power-of-two downsampling.
    // Pass the renderer's complete tile-expanded footprint. Never save/export it.
    Layer previewLayer(double documentX,double documentY,double documentWidth,double documentHeight,
        uint64_t maxPixels=4194304)const;
    // Exact source bounds. Unaligned origin must retile the bounded result in
    // today's origin-zero Raster model; aligned unaffected source tiles are shared.
    Layer materializeLayer()const;
private:
    std::shared_ptr<const Impl> impl_;
    explicit GrowingBrushSnapshot(std::shared_ptr<const Impl>);
    friend class GrowingBrushSession;
};
class GrowingBrushSession {
public:
    GrowingBrushSession(Layer original,BrushSessionSettings,int canvasWidth,int canvasHeight,
        std::shared_ptr<D3D11BrushCoverage> accelerator={},std::shared_ptr<const GrayRaster> documentSelection={},
        bool targetMask=false,uint64_t remainingPixelBudget=100000000,GrowingTileCompositor compositor={});
    ~GrowingBrushSession();
    GrowingBrushSession(const GrowingBrushSession&)=delete;
    GrowingBrushSession& operator=(const GrowingBrushSession&)=delete;
    bool begin(Point);
    bool append(Point);
    void flushCoverage();
    BrushSourceRect virtualBounds()const;
    std::vector<GrowingCoverageTile> coverageSnapshot()const;
    std::shared_ptr<const Raster> readOriginal(BrushSourceRect)const;
    // Healing publishes coverage patches even when their final bytes equal the
    // source. Selection clips pixel composition, not that source patch identity.
    void recomposeTouched(GrowingTileCompositor,bool retainCoveredTiles=false);
    std::shared_ptr<const GrowingBrushSnapshot> preview()const;
    std::shared_ptr<const GrowingBrushSnapshot> commit();
    std::shared_ptr<const GrowingBrushSnapshot> cancel();
    GrowingBrushMetrics metrics()const;
    const std::string& acceleratorError()const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
