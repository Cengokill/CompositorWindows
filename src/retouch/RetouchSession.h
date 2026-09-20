#pragma once
// Translated from Compositor retouch tools, MIT license in LICENSE.
#include "graphics/BrushSession.h"
#include "graphics/GrowingBrushSession.h"

namespace compositor::retouch {
// Existing document-sized sample/working-buffer limit; call before rendering
// an all-layer sample. Sparse destination growth does not remove this limit.
void validateCanvasExtent(int width,int height);
enum class Mode { Clone,HealContentAware,HealCreateTexture,HealProximity,Blur,Smudge,Liquify };
struct Settings {
    Mode mode{Mode::Clone};
    double radius{20},hardness{},opacity{1};
    bool sampleAllLayers{};
    uint32_t healingSeed{1}; // Host chooses a fresh random seed per real stroke.
};
class CloneAlignment {
public:
    bool aligned{true};
    void setSource(Point);
    std::optional<Point> strokeOffset(Point start) const;
    std::optional<Point> beginStroke(Point start);
    std::optional<Point> samplePoint(Point pointer,bool strokeActive=false) const;
    std::optional<Point> source()const{return source_;}
private:
    std::optional<Point> source_,offset_;
};
struct Sources {
    // Immutable document-sized samples taken when the stroke starts. Current
    // defaults to rendering the original layer alone, without opacity/masks.
    std::shared_ptr<const Raster> currentLayer,allLayers;
    std::optional<Point> cloneOffset;
};
struct Metrics {
    uint64_t publishedTileCopies{},documentRasterizations{},workingBufferPixels{},gaussianScratchPixels{},healingRegionPixels{},warpDabs{};
};
class RetouchSession {
public:
    RetouchSession(Layer original,int canvasWidth,int canvasHeight,Settings,Sources={},
        std::shared_ptr<const GrayRaster> selection={},std::shared_ptr<graphics::D3D11BrushCoverage> accelerator={},
        bool targetMask=false,uint64_t remainingPixelBudget=100000000);
    RetouchSession(std::shared_ptr<const Raster> original,Transform,int canvasWidth,int canvasHeight,
        Settings,Sources={},std::shared_ptr<const GrayRaster> selection={},std::shared_ptr<graphics::D3D11BrushCoverage> accelerator={});
    // Blur is the sole retouch mode supported on mask targets, matching source.
    RetouchSession(std::shared_ptr<const GrayRaster> originalMask,Transform,int canvasWidth,int canvasHeight,
        Settings,std::shared_ptr<const GrayRaster> selection={},std::shared_ptr<graphics::D3D11BrushCoverage> accelerator={});
    ~RetouchSession();
    RetouchSession(const RetouchSession&)=delete;
    RetouchSession& operator=(const RetouchSession&)=delete;
    bool begin(Point documentPoint);
    bool append(Point documentPoint);
    // Layer-based sessions retain source-grid growth, placement, masks and phase.
    std::shared_ptr<const graphics::GrowingBrushSnapshot> previewSnapshot()const;
    std::shared_ptr<const graphics::GrowingBrushSnapshot> commitSnapshot();
    std::shared_ptr<const graphics::GrowingBrushSnapshot> cancelSnapshot();
    // Warp retains the source document-sized preview before selection clipping.
    std::shared_ptr<const LayerRenderPreview> livePreview()const;
    std::shared_ptr<const Raster> preview()const;
    // Source live Smudge/Liquify preview before final selection clipping. Place
    // across the document and retain layer opacity/blend plus its placed mask.
    std::shared_ptr<const Raster> warpDocumentPreview()const;
    std::shared_ptr<const Raster> commit();
    std::shared_ptr<const Raster> cancel();
    std::shared_ptr<const GrayRaster> previewMask()const;
    std::shared_ptr<const GrayRaster> commitMask();
    std::shared_ptr<const GrayRaster> cancelMask();
    const Metrics& metrics()const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
