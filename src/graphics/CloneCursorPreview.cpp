#include "CloneCursorPreview.h"
#include "BrushSession.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace compositor::graphics {
void CloneCursorPreview::reset(){document_.reset();active_.clear();preview_.reset();tip_.reset();result_.reset();}
std::shared_ptr<const Raster> CloneCursorPreview::render(const Document& input,const std::string& activeLayer,
    Point center,double diameter,double hardness,double zoom,bool all,std::shared_ptr<const LayerRenderPreview> preview){
    if(!std::isfinite(center.x)||!std::isfinite(center.y)||!std::isfinite(diameter)||diameter<=0||diameter>2000||
       !std::isfinite(hardness)||hardness<0||hardness>1||!std::isfinite(zoom)||zoom<=0||!std::isfinite(diameter*zoom))
        throw std::invalid_argument("Invalid clone cursor preview");
    if(document_&&*document_==input&&active_==activeLayer&&center_==center&&diameter_==diameter&&hardness_==hardness&&zoom_==zoom&&all_==all&&preview_==preview)return result_;
    Document source=input;
    if(!all){
        const auto found=std::find_if(input.layers.begin(),input.layers.end(),[&](const Layer& layer){return layer.id==activeLayer;});
        if(found==input.layers.end()||!found->raster){reset();return {};}
        // Current Layer samples the asset, not that layer's appearance in stack.
        Layer raw=*found;raw.parentId.clear();raw.maskSourceId.clear();raw.mask.reset();raw.visible=true;raw.group=false;raw.opacity=1;raw.blend=Blend::Normal;raw.adjustmentJson.clear();raw.shapeJson.clear();
        source.layers={std::move(raw)};source.selection.reset();
    }
    const int side=int(std::clamp(std::ceil(diameter*zoom),1.,1024.));
    auto image=SoftwareRenderer(all?preview:nullptr).renderCursorRegion(source,center.x-diameter/2,center.y-diameter/2,side,side,diameter/side);
    if(!tip_||tipDiameter_!=diameter||tipHardness_!=hardness){
        const int tipSide=std::max(1,int(std::ceil(diameter)));
        BrushSessionSettings settings;settings.radius=diameter/2;settings.hardness=hardness;settings.color={255,255,255};
        BrushSession stroke(Raster::filled(tipSide,tipSide),settings);stroke.begin({tipSide/2.,tipSide/2.});
        tip_=stroke.commit();tipDiameter_=diameter;tipHardness_=hardness;
    }
    Document tipDocument;tipDocument.id="clone-cursor-tip";tipDocument.width=tip_->width;tipDocument.height=tip_->height;
    Layer tipLayer;tipLayer.id="tip";tipLayer.raster=tip_;tipLayer.transform={0,0,diameter,diameter};tipDocument.layers={std::move(tipLayer)};
    auto coverage=SoftwareRenderer().renderCursorRegion(tipDocument,0,0,side,side,diameter/side);
    std::vector<Pixel> pixels(size_t(side)*side);
    for(int y=0;y<side;++y)for(int x=0;x<side;++x){const auto p=image->pixel(x,y);const unsigned a=coverage->pixel(x,y).a;auto scale=[&](uint8_t value){return uint8_t((unsigned(value)*a+127)/255);};pixels[size_t(y)*side+x]={scale(p.r),scale(p.g),scale(p.b),scale(p.a)};}
    auto result=Raster::fromRgba(side,side,reinterpret_cast<const uint8_t*>(pixels.data()),size_t(side)*4);
    document_=input;active_=activeLayer;center_=center;diameter_=diameter;hardness_=hardness;zoom_=zoom;all_=all;preview_=std::move(preview);result_=std::move(result);return result_;
}
}
