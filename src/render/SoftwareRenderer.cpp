#include "core/Document.h"
#include "graphics/StackRenderer.h"
#include "effects/Adjustments.h"

namespace compositor {
std::shared_ptr<const Raster> SoftwareRenderer::renderScaledPatch(const Document& document,double tileX,double tileY,int offsetX,int offsetY,int width,int height,double unitsPerPixel)const {
    return graphics::StackRenderer({},preview_).renderScaledPatch(document,tileX,tileY,offsetX,offsetY,width,height,unitsPerPixel);
}
std::shared_ptr<const Raster> SoftwareRenderer::render(const Document& document,int x,int y,int width,int height)const {
    return graphics::StackRenderer([](const Layer& layer,std::shared_ptr<const Raster> current,graphics::RenderRegion region){
        return effects::applyAdjustment(std::move(current),layer.adjustmentJson,nullptr,{region.x,region.y,region.unitsPerPixel});
    },preview_).render(document,x,y,width,height);
}
std::shared_ptr<const Raster> SoftwareRenderer::renderScaled(const Document& document,double x,double y,int width,int height,double unitsPerPixel)const {
    return graphics::StackRenderer([](const Layer& layer,std::shared_ptr<const Raster> current,graphics::RenderRegion region){
        return effects::applyAdjustment(std::move(current),layer.adjustmentJson,nullptr,{region.x,region.y,region.unitsPerPixel});
    },preview_).renderScaled(document,x,y,width,height,unitsPerPixel);
}
std::shared_ptr<const Raster> SoftwareRenderer::renderCursorRegion(const Document& document,double x,double y,int width,int height,double unitsPerPixel)const {
    return graphics::StackRenderer([](const Layer& layer,std::shared_ptr<const Raster> current,graphics::RenderRegion region){
        return effects::applyAdjustment(std::move(current),layer.adjustmentJson,nullptr,{region.x,region.y,region.unitsPerPixel});
    },preview_).renderCursorRegion(document,x,y,width,height,unitsPerPixel);
}
}
