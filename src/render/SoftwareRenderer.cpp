#include "core/Document.h"
#include "graphics/StackRenderer.h"
#include "effects/Adjustments.h"
#include <stdexcept>

namespace compositor {
std::shared_ptr<const Raster> SoftwareRenderer::renderScaledPatch(const Document& document,double tileX,double tileY,int offsetX,int offsetY,int width,int height,double unitsPerPixel)const {
    return graphics::StackRenderer({},preview_,[&document](const Layer& layer,std::shared_ptr<const DeepImage> current,graphics::RenderRegion region){return effects::applyAdjustment(std::move(current),layer.adjustmentJson,nullptr,{region.x,region.y,region.unitsPerPixel,double(document.width),double(document.height)});}).renderScaledPatch(document,tileX,tileY,offsetX,offsetY,width,height,unitsPerPixel);
}
std::shared_ptr<const DeepImage> SoftwareRenderer::renderDeep(const Document& document,int x,int y,int width,int height,const std::function<bool()>& cancelled)const {
    const int margin=effects::adjustmentStackPadding(document);
    if(width<1||height<1||width>30000-2*margin||height>30000-2*margin||uint64_t(width+2*margin)*(height+2*margin)*80>1200000000)throw std::runtime_error("HDR render exceeds working memory budget");
    auto result=graphics::StackRenderer({},preview_,[&](const Layer& layer,std::shared_ptr<const DeepImage> current,graphics::RenderRegion region){return effects::applyAdjustment(std::move(current),layer.adjustmentJson,nullptr,{region.x,region.y,region.unitsPerPixel,double(document.width),double(document.height)},cancelled);}).renderDeep(document,{double(x-margin),double(y-margin),width+2*margin,height+2*margin,1},cancelled);
    if(!margin)return result;
    auto cropped=makeDeep(width,height);for(int row=0;row<height;++row){if(cancelled&&cancelled())throw std::runtime_error("HDR render cancelled");for(int col=0;col<width;++col)cropped->setPixel(col,row,result->pixel(col+margin,row+margin));}return cropped;
}
std::shared_ptr<const Raster> SoftwareRenderer::render(const Document& document,int x,int y,int width,int height)const {
    return graphics::StackRenderer([&document](const Layer& layer,std::shared_ptr<const Raster> current,graphics::RenderRegion region){
        return effects::applyAdjustment(std::move(current),layer.adjustmentJson,nullptr,{region.x,region.y,region.unitsPerPixel,double(document.width),double(document.height)});
    },preview_,[&document](const Layer& layer,std::shared_ptr<const DeepImage> current,graphics::RenderRegion region){return effects::applyAdjustment(std::move(current),layer.adjustmentJson,nullptr,{region.x,region.y,region.unitsPerPixel,double(document.width),double(document.height)});}).render(document,x,y,width,height);
}
std::shared_ptr<const Raster> SoftwareRenderer::renderScaled(const Document& document,double x,double y,int width,int height,double unitsPerPixel)const {
    return graphics::StackRenderer([&document](const Layer& layer,std::shared_ptr<const Raster> current,graphics::RenderRegion region){
        return effects::applyAdjustment(std::move(current),layer.adjustmentJson,nullptr,{region.x,region.y,region.unitsPerPixel,double(document.width),double(document.height)});
    },preview_,[&document](const Layer& layer,std::shared_ptr<const DeepImage> current,graphics::RenderRegion region){return effects::applyAdjustment(std::move(current),layer.adjustmentJson,nullptr,{region.x,region.y,region.unitsPerPixel,double(document.width),double(document.height)});}).renderScaled(document,x,y,width,height,unitsPerPixel);
}
std::shared_ptr<const Raster> SoftwareRenderer::renderCursorRegion(const Document& document,double x,double y,int width,int height,double unitsPerPixel)const {
    return graphics::StackRenderer([&document](const Layer& layer,std::shared_ptr<const Raster> current,graphics::RenderRegion region){
        return effects::applyAdjustment(std::move(current),layer.adjustmentJson,nullptr,{region.x,region.y,region.unitsPerPixel,double(document.width),double(document.height)});
    },preview_,[&document](const Layer& layer,std::shared_ptr<const DeepImage> current,graphics::RenderRegion region){return effects::applyAdjustment(std::move(current),layer.adjustmentJson,nullptr,{region.x,region.y,region.unitsPerPixel,double(document.width),double(document.height)});}).renderCursorRegion(document,x,y,width,height,unitsPerPixel);
}
}
