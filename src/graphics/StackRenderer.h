#pragma once
#include "core/Document.h"
#include <functional>

namespace compositor::graphics {
struct RenderRegion {double x{},y{};int width{},height{};double unitsPerPixel{1};};
// Receives the unmodified composited region, before this adjustment's blend,
// opacity or masks. Returns adjusted premultiplied RGBA of the same dimensions.
using AdjustmentCallback=std::function<std::shared_ptr<const Raster>(
    const Layer&,std::shared_ptr<const Raster>,RenderRegion)>;
class StackRenderer final:public IRasterBackend {
public:
    explicit StackRenderer(AdjustmentCallback adjustment={},std::shared_ptr<const LayerRenderPreview> preview={}):adjustment_(std::move(adjustment)),preview_(std::move(preview)){}
    std::shared_ptr<const Raster> render(const Document&,int x,int y,int width,int height)const override;
    // Display sampling on the original document geometry. Export stays at step 1.
    std::shared_ptr<const Raster> renderScaled(const Document&,double x,double y,int width,int height,double unitsPerPixel)const;
private:
    AdjustmentCallback adjustment_;
    std::shared_ptr<const LayerRenderPreview> preview_;
};
} // namespace compositor::graphics
