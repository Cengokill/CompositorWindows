#pragma once
#include "core/Document.h"
#include "editing/DocumentGeometry.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <stdexcept>

namespace compositor::ui {
struct CropViewportSamples {
    double x{},y{},units{};
    int width{},height{};
    bool operator==(const CropViewportSamples&) const = default;
};

inline bool cropViewportFits(double width,double height){
    constexpr double side=Raster::tileSide;
    return width<=30000&&height<=30000&&std::ceil(width/side)*std::ceil(height/side)<=64;
}

// EditorCanvas.renderBounds expands the presentation surface for Crop without
// changing the document coordinate system. Calculate the visible sample grid
// before rendering so overlay updates can retain a matching raster.
inline std::optional<CropViewportSamples> cropViewportSamples(int documentWidth,int documentHeight,editing::Rect crop,
    double x,double y,double width,double height,double requestedUnits) {
    const double left=std::max(x,std::min(0.,crop.x)),top=std::max(y,std::min(0.,crop.y));
    const double right=std::min(x+width,std::max(double(documentWidth),crop.x+crop.width));
    const double bottom=std::min(y+height,std::max(double(documentHeight),crop.y+crop.height));
    if(right<=left||bottom<=top)return {};
    double units=requestedUnits,originX{},originY{},pixelsWide{},pixelsHigh{};
    for(;;){
        // Cropping the visible request must preserve its physical sample phase.
        originX=x+std::floor((left-x)/units)*units;originY=y+std::floor((top-y)/units)*units;
        pixelsWide=std::ceil((right-originX)/units);pixelsHigh=std::ceil((bottom-originY)/units);
        if(cropViewportFits(pixelsWide,pixelsHigh))break;
        units*=2;
        if(units>32768)throw std::invalid_argument("Crop viewport exceeds bounded sampling range");
    }
    return CropViewportSamples{originX,originY,units,int(pixelsWide),int(pixelsHigh)};
}

class CropViewportCache {
    std::optional<Document> document_;
    CropViewportSamples grid_;
    std::shared_ptr<const Raster> raster_;

    static bool canTile(const Document& document,const CropViewportSamples& grid){
        if(std::any_of(document.layers.begin(),document.layers.end(),[](const Layer& layer){return !layer.adjustmentJson.empty();}))return false;
        // At the viewport bounds, coordinates on this binary lattice sum
        // exactly in double precision regardless of where the tile begins.
        const auto exact=[](double value){
            const double scaled=std::ldexp(value,20);
            return scaled==std::trunc(scaled);
        };
        if(exact(grid.x)&&exact(grid.y)&&exact(grid.units/2))return true;

        // Fractional regrouping can flip nearest sampling or hard image
        // edges. A single smooth, axis-aligned layer away from its edges
        // can only differ by an interpolation rounding step.
        if(document.layers.size()!=1)return false;
        const auto& layer=document.layers.front();
        const auto& transform=layer.transform;
        if(!layer.raster||layer.deep||layer.mask||layer.effects.active()||layer.blend!=Blend::Normal||
           transform.sampling==Transform::Sampling::Nearest||transform.rotation!=0)return false;
        const auto clearOfCenter=[&](double edge,double origin){
            const double index=(edge-origin)/grid.units-.5;
            return std::abs(index-std::round(index))*grid.units>1e-6;
        };
        return clearOfCenter(transform.x,grid.x)&&clearOfCenter(transform.x+transform.width,grid.x)&&
            clearOfCenter(transform.y,grid.y)&&clearOfCenter(transform.y+transform.height,grid.y);
    }
public:
    std::optional<CompositeViewport> render(const Document& document,const CropViewportSamples& samples,
        double x,double y,double width,double height){
        const double columns=std::ceil(width/samples.units),rows=std::ceil(height/samples.units);
        const double firstX=std::round((samples.x-x)/samples.units),firstY=std::round((samples.y-y)/samples.units);
        if(!cropViewportFits(columns,rows)||firstX<0||firstY<0||
           firstX+samples.width>columns||firstY+samples.height>rows)return {};
        const CropViewportSamples grid{x,y,samples.units,int(columns),int(rows)};
        if(!canTile(document,grid))return {};

        const bool matches=raster_&&grid==grid_&&*document_==document;
        constexpr int side=Raster::tileSide;
        const int tileColumns=(grid.width+side-1)/side,tileRows=(grid.height+side-1)/side;
        static const auto emptyTile=std::make_shared<const Raster::Tile>();
        std::shared_ptr<Raster> updated;
        if(!matches){
            updated=std::make_shared<Raster>();
            updated->width=grid.width;
            updated->height=grid.height;
            updated->tiles.assign(size_t(tileColumns)*tileRows,emptyTile);
        }
        const int left=int(firstX),top=int(firstY);
        const SoftwareRenderer renderer;
        // The view origin fixes tile positions as crop edges move. The canvas
        // clips the retained raster to the live crop and document bounds.
        for(int ty=top/side;ty<=(top+samples.height-1)/side;++ty){
            for(int tx=left/side;tx<=(left+samples.width-1)/side;++tx){
                const size_t index=size_t(ty)*tileColumns+tx;
                const auto& tiles=updated?updated->tiles:raster_->tiles;
                if(tiles[index]!=emptyTile)continue;
                if(!updated)updated=std::make_shared<Raster>(*raster_);
                const int ox=tx*side,oy=ty*side;
                auto tile=renderer.renderScaled(document,x+ox*grid.units,y+oy*grid.units,
                    std::min(side,grid.width-ox),std::min(side,grid.height-oy),grid.units);
                updated->tiles[index]=tile->tiles.front();
            }
        }
        if(!matches){
            document_=Document(document);
            grid_=grid;
        }
        if(updated)raster_=std::move(updated);
        return CompositeViewport{raster_,x,y,grid.units,document.width,document.height};
    }
};

inline CompositeViewport renderCropViewport(const Document& document,editing::Rect crop,
    double x,double y,double width,double height,double requestedUnits,
    std::shared_ptr<const LayerRenderPreview> preview={},CropViewportCache* cache=nullptr,
    const std::function<bool()>& cancelled={}) {
    validateDocument(document);
    if(!std::isfinite(x)||!std::isfinite(y)||std::abs(x)>10000000||std::abs(y)>10000000||
       !std::isfinite(width)||!std::isfinite(height)||width<0||height<0||
       !std::isfinite(x+width)||!std::isfinite(y+height)||
       !std::isfinite(requestedUnits)||requestedUnits<1./32||requestedUnits>30000||!editing::validCrop(crop))
        throw std::invalid_argument("Invalid crop viewport");
    CompositeViewport result;result.documentWidth=document.width;result.documentHeight=document.height;
    if(const auto samples=cropViewportSamples(document.width,document.height,crop,x,y,width,height,requestedUnits)){
        if(cache&&!preview)
            if(auto cached=cache->render(document,*samples,x,y,width,height))return *cached;
        result.documentX=samples->x;result.documentY=samples->y;result.unitsPerPixel=samples->units;
        result.raster=SoftwareRenderer(std::move(preview)).renderScaled(document,samples->x,samples->y,samples->width,samples->height,samples->units,cancelled);
    }
    return result;
}
} // namespace compositor::ui
