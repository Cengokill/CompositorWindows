#pragma once
#include "core/Document.h"
#include <functional>

namespace compositor::graphics {
struct RenderRegion {int x{},y{},width{},height{};};
// Receives the unmodified composited region, before this adjustment's blend,
// opacity or masks. Returns adjusted premultiplied RGBA of the same dimensions.
using AdjustmentCallback=std::function<std::shared_ptr<const Raster>(
    const Layer&,std::shared_ptr<const Raster>,RenderRegion)>;
class StackRenderer final:public IRasterBackend {
public:
    explicit StackRenderer(AdjustmentCallback adjustment={}):adjustment_(std::move(adjustment)){}
    std::shared_ptr<const Raster> render(const Document&,int x,int y,int width,int height)const override;
private:
    AdjustmentCallback adjustment_;
};
} // namespace compositor::graphics
