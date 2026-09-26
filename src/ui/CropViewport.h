#pragma once
#include "core/Document.h"
#include "editing/DocumentGeometry.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace compositor::ui {
// EditorCanvas.renderBounds expands the presentation surface for Crop without
// changing the document coordinate system. This temporary patch is viewport
// sized; it never creates a canvas-sized raster or alters layer metadata.
inline CompositeViewport renderCropViewport(const Document& document,editing::Rect crop,
    double x,double y,double width,double height,double requestedUnits,
    std::shared_ptr<const LayerRenderPreview> preview={}) {
    validateDocument(document);
    if(!std::isfinite(x)||!std::isfinite(y)||std::abs(x)>10000000||std::abs(y)>10000000||
       !std::isfinite(width)||!std::isfinite(height)||width<0||height<0||
       !std::isfinite(x+width)||!std::isfinite(y+height)||
       !std::isfinite(requestedUnits)||requestedUnits<1./32||requestedUnits>30000||!editing::validCrop(crop))
        throw std::invalid_argument("Invalid crop viewport");
    CompositeViewport result;result.documentWidth=document.width;result.documentHeight=document.height;
    const double left=std::max(x,std::min(0.,crop.x)),top=std::max(y,std::min(0.,crop.y));
    const double right=std::min(x+width,std::max(double(document.width),crop.x+crop.width));
    const double bottom=std::min(y+height,std::max(double(document.height),crop.y+crop.height));
    if(right<=left||bottom<=top)return result;
    double units=requestedUnits,originX{},originY{},pixelsWide{},pixelsHigh{};
    for(;;){
        // Cropping the visible request must preserve its physical sample phase.
        originX=x+std::floor((left-x)/units)*units;originY=y+std::floor((top-y)/units)*units;
        pixelsWide=std::ceil((right-originX)/units);pixelsHigh=std::ceil((bottom-originY)/units);
        if(pixelsWide<=30000&&pixelsHigh<=30000&&std::ceil(pixelsWide/256)*std::ceil(pixelsHigh/256)<=64)break;
        units*=2;
        if(units>32768)throw std::invalid_argument("Crop viewport exceeds bounded sampling range");
    }
    result.documentX=originX;result.documentY=originY;result.unitsPerPixel=units;
    result.raster=SoftwareRenderer(std::move(preview)).renderScaled(document,originX,originY,int(pixelsWide),int(pixelsHigh),units);
    return result;
}
} // namespace compositor::ui
