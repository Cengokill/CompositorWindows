#include "core/Document.h"
#include "graphics/StackRenderer.h"
#include "effects/Adjustments.h"

namespace compositor {
std::shared_ptr<const Raster> SoftwareRenderer::render(const Document& document,int x,int y,int width,int height)const {
    return graphics::StackRenderer([](const Layer& layer,std::shared_ptr<const Raster> current,graphics::RenderRegion region){
        return effects::applyAdjustment(std::move(current),layer.adjustmentJson,nullptr,{double(region.x),double(region.y),1});
    }).render(document,x,y,width,height);
}
}
