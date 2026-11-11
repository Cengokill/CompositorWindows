#include "core/Document.h"
#include "graphics/StackRenderer.h"
#include "effects/Adjustments.h"
#include <cmath>
#include <stdexcept>
#include <vector>

namespace compositor {
namespace {
graphics::StackRenderer stack(const Document& document,const std::shared_ptr<const LayerRenderPreview>& preview,int lensPreviewMaxEdge,const std::function<bool()>& cancelled={}){
    return graphics::StackRenderer([&document,lensPreviewMaxEdge,cancelled](const Layer& layer,std::shared_ptr<const Raster> current,graphics::RenderRegion region){
        return effects::applyAdjustment(std::move(current),layer.adjustmentJson,nullptr,{region.x,region.y,region.unitsPerPixel,double(document.width),double(document.height),lensPreviewMaxEdge},cancelled);
    },preview,[&document,cancelled,lensPreviewMaxEdge](const Layer& layer,std::shared_ptr<const DeepImage> current,graphics::RenderRegion region){return effects::applyAdjustment(std::move(current),layer.adjustmentJson,nullptr,{region.x,region.y,region.unitsPerPixel,double(document.width),double(document.height),lensPreviewMaxEdge},cancelled);});
}
std::shared_ptr<const Raster> cropRaster(const Raster& source,int ox,int oy,int width,int height){
    if(ox%256==0&&oy%256==0&&width==256&&height==256){
        auto tile=std::make_shared<Raster>();
        tile->width=width;tile->height=height;
        tile->tiles.push_back(source.tiles[size_t(oy/256)*((source.width+255)/256)+ox/256]);
        return tile;
    }
    std::vector<Pixel> pixels(std::size_t(width)*height);
    for(int y=0;y<height;++y)for(int x=0;x<width;++x)pixels[std::size_t(y)*width+x]=source.pixel(ox+x,oy+y);
    return Raster::fromRgba(width,height,reinterpret_cast<const uint8_t*>(pixels.data()),std::size_t(width)*4);
}
}

std::shared_ptr<const Raster> SoftwareRenderer::renderScaledPatch(const Document& document,double tileX,double tileY,int offsetX,int offsetY,int width,int height,double unitsPerPixel)const {
    return graphics::StackRenderer({},preview_,[&document](const Layer& layer,std::shared_ptr<const DeepImage> current,graphics::RenderRegion region){return effects::applyAdjustment(std::move(current),layer.adjustmentJson,nullptr,{region.x,region.y,region.unitsPerPixel,double(document.width),double(document.height)});}).renderScaledPatch(document,tileX,tileY,offsetX,offsetY,width,height,unitsPerPixel);
}
std::shared_ptr<const DeepImage> SoftwareRenderer::renderDeep(const Document& document,int x,int y,int width,int height,const std::function<bool()>& cancelled)const {
    const int margin=effects::adjustmentStackPadding(document);
    if(width<1||height<1||width>30000-2*margin||height>30000-2*margin||uint64_t(width+2*margin)*(height+2*margin)*80>1200000000)throw std::runtime_error("HDR render exceeds working memory budget");
    auto result=graphics::StackRenderer({},preview_,[&](const Layer& layer,std::shared_ptr<const DeepImage> current,graphics::RenderRegion region){return effects::applyAdjustment(std::move(current),layer.adjustmentJson,nullptr,{region.x,region.y,region.unitsPerPixel,double(document.width),double(document.height)},cancelled);}).renderDeep(document,{double(x-margin),double(y-margin),width+2*margin,height+2*margin,1},cancelled);
    if(!margin)return result;
    auto cropped=makeDeep(width,height,result->format);for(int row=0;row<height;++row){if(cancelled&&cancelled())throw std::runtime_error("HDR render cancelled");for(int col=0;col<width;++col)cropped->setPixel(col,row,result->pixel(col+margin,row+margin));}return cropped;
}
std::shared_ptr<const Raster> SoftwareRenderer::render(const Document& document,int x,int y,int width,int height)const {
    if(width<1||height<1||width>30000||height>30000||std::abs(int64_t(x))>10000000||std::abs(int64_t(y))>10000000)throw std::invalid_argument("Invalid render region");
    if(effects::hasLensAdjustment(document)){
        const int margin=effects::adjustmentStackPadding(document);
        const int left=std::min(-margin,x),top=std::min(-margin,y),right=std::max(document.width+margin,x+width),bottom=std::max(document.height+margin,y+height);
        if(uint64_t(right-left)*(bottom-top)>5000000)throw std::runtime_error("Lens Effects full-image render exceeds working memory budget");
        if(!lensFrame_||!lensDocument_||*lensDocument_!=document||lensX_!=left||lensY_!=top||lensUnits_!=1||lensFrame_->width!=right-left||lensFrame_->height!=bottom-top){
            lensFrame_=stack(document,preview_,lensPreviewMaxEdge_).render(document,left,top,right-left,bottom-top);
            lensDocument_=document;lensX_=left;lensY_=top;lensUnits_=1;
        }
        return cropRaster(*lensFrame_,x-left,y-top,width,height);
    }
    const int margin=effects::adjustmentStackPadding(document);
    auto rendered=stack(document,preview_,lensPreviewMaxEdge_).render(document,x-margin,y-margin,width+2*margin,height+2*margin);
    return margin?cropRaster(*rendered,margin,margin,width,height):rendered;
}
std::shared_ptr<const Raster> SoftwareRenderer::renderScaled(const Document& document,double x,double y,int width,int height,double unitsPerPixel,const std::function<bool()>& cancelled)const {
    if(width<1||height<1||width>30000||height>30000||!std::isfinite(x)||!std::isfinite(y)||std::abs(x)>10000000||std::abs(y)>10000000||!std::isfinite(unitsPerPixel)||unitsPerPixel<1./32||unitsPerPixel>32768)throw std::invalid_argument("Invalid scaled render region");
    if(effects::hasLensAdjustment(document)){
        double left=std::fmod(x,unitsPerPixel),top=std::fmod(y,unitsPerPixel);
        if(left>0)left-=unitsPerPixel;if(top>0)top-=unitsPerPixel;
        // All viewport tiles share a grid, but fmod on each tile's document
        // coordinates can differ by rounding and even wrap across zero.
        const double tolerance=unitsPerPixel*1e-8;
        if(std::abs(left)<tolerance||std::abs(left+unitsPerPixel)<tolerance)left=0;
        if(std::abs(top)<tolerance||std::abs(top+unitsPerPixel)<tolerance)top=0;
        const int margin=int(std::ceil(effects::adjustmentStackPadding(document)/unitsPerPixel));
        left-=margin*unitsPerPixel;top-=margin*unitsPerPixel;
        const int ox=int(std::lround((x-left)/unitsPerPixel)),oy=int(std::lround((y-top)/unitsPerPixel));
        const int fullWidth=std::max(ox+width,int(std::ceil((document.width-left)/unitsPerPixel))+margin);
        const int fullHeight=std::max(oy+height,int(std::ceil((document.height-top)/unitsPerPixel))+margin);
        if(ox<0||oy<0||fullWidth>30000||fullHeight>30000||uint64_t(fullWidth)*fullHeight>5000000)throw std::runtime_error("Lens Effects full-image preview exceeds working memory budget");
        if(!lensFrame_||!lensDocument_||*lensDocument_!=document||std::abs(lensX_-left)>tolerance||std::abs(lensY_-top)>tolerance||lensUnits_!=unitsPerPixel||lensFrame_->width!=fullWidth||lensFrame_->height!=fullHeight){
            lensFrame_=stack(document,preview_,lensPreviewMaxEdge_,cancelled).renderScaled(document,left,top,fullWidth,fullHeight,unitsPerPixel);
            lensDocument_=document;lensX_=left;lensY_=top;lensUnits_=unitsPerPixel;
        }
        return cropRaster(*lensFrame_,ox,oy,width,height);
    }
    const int margin=int(std::ceil(effects::adjustmentStackPadding(document)/unitsPerPixel));
    auto rendered=stack(document,preview_,lensPreviewMaxEdge_).renderScaled(document,x-margin*unitsPerPixel,y-margin*unitsPerPixel,width+2*margin,height+2*margin,unitsPerPixel);
    return margin?cropRaster(*rendered,margin,margin,width,height):rendered;
}
std::shared_ptr<const Raster> SoftwareRenderer::renderCursorRegion(const Document& document,double x,double y,int width,int height,double unitsPerPixel)const {
    return graphics::StackRenderer([&document](const Layer& layer,std::shared_ptr<const Raster> current,graphics::RenderRegion region){
        return effects::applyAdjustment(std::move(current),layer.adjustmentJson,nullptr,{region.x,region.y,region.unitsPerPixel,double(document.width),double(document.height)});
    },preview_,[&document](const Layer& layer,std::shared_ptr<const DeepImage> current,graphics::RenderRegion region){return effects::applyAdjustment(std::move(current),layer.adjustmentJson,nullptr,{region.x,region.y,region.unitsPerPixel,double(document.width),double(document.height)});}).renderCursorRegion(document,x,y,width,height,unitsPerPixel);
}
}
