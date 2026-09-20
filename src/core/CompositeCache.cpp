#include "Document.h"
#include "graphics/Downsample.h"
#include "graphics/SamplingSource.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <unordered_map>

namespace compositor {
namespace {
using Tile=std::shared_ptr<const Raster::Tile>;
Tile zeroTile(){static const Tile zero=std::make_shared<Raster::Tile>();return zero;}
struct Bounds {double left{},top{},right{},bottom{};};
Bounds bounds(const Transform& transform,double x,double y,double width,double height){
    Bounds out{INFINITY,INFINITY,-INFINITY,-INFINITY};
    for(auto unit:std::array<Point,4>{{{x,y},{x+width,y},{x,y+height},{x+width,y+height}}}){auto p=transform.fromUnit(unit);out.left=std::min(out.left,p.x);out.top=std::min(out.top,p.y);out.right=std::max(out.right,p.x);out.bottom=std::max(out.bottom,p.y);}return out;
}
std::vector<Bounds> paintedBounds(const Document& doc,double units=1,const LayerRenderPreview* preview=nullptr){
    std::unordered_map<std::string,const Layer*> byId;for(const auto& layer:doc.layers)byId.emplace(layer.id,&layer);
    std::vector<Bounds> result;
    for(const auto& layer:doc.layers){if(!layer.raster||!layer.visible||layer.opacity==0)continue;bool visible=true;auto parent=layer.parentId;while(!parent.empty()){auto ancestor=byId.at(parent);if(!ancestor->visible){visible=false;break;}parent=ancestor->parentId;}if(visible){auto source=preview&&preview->layer.id==layer.id&&preview->imageSource?preview->imageSource:graphics::samplingSource(layer.raster);const int level=layer.transform.sampling==Transform::Sampling::Nearest?0:graphics::DownsampleCache::levelFor(layer.transform.width/(units*source->width));auto grid=graphics::samplingGrid(*source,level);result.push_back(bounds(layer.transform,double(grid.x)/source->width,double(grid.y)/source->height,double(grid.width*grid.step)/source->width,double(grid.height*grid.step)/source->height));}}
    return result;
}
bool touches(const std::vector<Bounds>& painted,double x,double y,double width,double height){return std::any_of(painted.begin(),painted.end(),[&](Bounds b){return b.right>x&&b.bottom>y&&b.left<x+width&&b.top<y+height;});}
std::shared_ptr<const Raster> directRaster(const Document& doc){
    if(doc.layers.size()!=1)return {};const auto& layer=doc.layers.front();
    if(layer.visible&&!layer.group&&layer.raster&&layer.raster->width==doc.width&&layer.raster->height==doc.height&&
       layer.opacity==1&&layer.blend==Blend::Normal&&!layer.mask&&layer.parentId.empty()&&layer.maskSourceId.empty()&&layer.adjustmentJson.empty()&&
       layer.transform.x==0&&layer.transform.y==0&&layer.transform.width==doc.width&&layer.transform.height==doc.height&&layer.transform.rotation==0&&!layer.transform.flipX&&!layer.transform.flipY)return layer.raster;
    return {};
}
std::vector<uint8_t> invalidTiles(const std::optional<Document>& previous,const Document& doc,double units,int columns,int rows){
    std::vector<uint8_t> dirty(size_t(columns)*rows,0);auto invalidateAll=[&]{std::fill(dirty.begin(),dirty.end(),uint8_t(1));};
    if(!previous||previous->width!=doc.width||previous->height!=doc.height||previous->layers.size()!=doc.layers.size()){invalidateAll();return dirty;}
    bool dependent=std::any_of(doc.layers.begin(),doc.layers.end(),[](const Layer& layer){return !layer.maskSourceId.empty()||!layer.adjustmentJson.empty();});
    for(size_t i=0;i<doc.layers.size();++i){const auto& before=previous->layers[i];const auto& after=doc.layers[i];if(before==after)continue;auto metadata=before;metadata.raster=after.raster;
        if(metadata!=after||!before.raster||!after.raster||before.raster->width!=after.raster->width||before.raster->height!=after.raster->height||before.raster->samplingOriginX!=after.raster->samplingOriginX||before.raster->samplingOriginY!=after.raster->samplingOriginY||dependent||
           (after.transform.sampling!=Transform::Sampling::Nearest&&graphics::DownsampleCache::levelFor(after.transform.width/(units*after.raster->width))>0)){invalidateAll();return dirty;}
        const int sourceColumns=(after.raster->width+255)/256;
        for(size_t tile=0;tile<after.raster->tiles.size();++tile)if(before.raster->tiles[tile]!=after.raster->tiles[tile]){
            int x=int(tile%sourceColumns)*256,y=int(tile/sourceColumns)*256;
            // One source pixel on each side covers bilinear neighbors. Rotation,
            // flips and the display LOD use the same conservative document AABB.
            auto box=bounds(after.transform,double(x-1)/after.raster->width,double(y-1)/after.raster->height,258./after.raster->width,258./after.raster->height);
            const double side=256*units;int minX=std::clamp(int(std::floor(box.left/side)),0,columns),maxX=std::clamp(int(std::ceil(box.right/side)),0,columns);int minY=std::clamp(int(std::floor(box.top/side)),0,rows),maxY=std::clamp(int(std::ceil(box.bottom/side)),0,rows);
            for(int ty=minY;ty<maxY;++ty)for(int tx=minX;tx<maxX;++tx)dirty[size_t(ty)*columns+tx]=1;
        }
    }
    return dirty;
}
void validateCulledAdjustments(const Document& doc,std::shared_ptr<const LayerRenderPreview> preview={}){
    // Even a transparent stack must report malformed or unsupported live effects.
    // One canonical pixel validates visible adjustment callbacks before blank culling.
    if(std::any_of(doc.layers.begin(),doc.layers.end(),[](const Layer& layer){return !layer.adjustmentJson.empty();}))SoftwareRenderer(std::move(preview)).render(doc,0,0,1,1);
}
std::shared_ptr<const Raster> tiled(int width,int height,Tile value){auto raster=std::make_shared<Raster>();raster->width=width;raster->height=height;raster->tiles.assign(size_t((width+255)/256)*((height+255)/256),std::move(value));return raster;}
}
std::shared_ptr<const Raster> CompositeCache::render(const Document& doc){
    validateDocument(doc);if(auto direct=directRaster(doc)){previous_=doc;output_=direct;return output_;}
    int columns=(doc.width+255)/256,rows=(doc.height+255)/256;auto dirty=invalidTiles(previous_,doc,1,columns,rows);
    if(output_&&std::none_of(dirty.begin(),dirty.end(),[](uint8_t value){return value!=0;})){previous_=doc;return output_;}
    validateCulledAdjustments(doc);auto painted=paintedBounds(doc);if(painted.empty()){previous_=doc;output_=tiled(doc.width,doc.height,zeroTile());return output_;}
    auto result=std::make_shared<Raster>();result->width=doc.width;result->height=doc.height;
    if(output_&&output_->width==doc.width&&output_->height==doc.height)result->tiles=output_->tiles;else result->tiles.resize(size_t(columns)*rows);
    SoftwareRenderer renderer;
    for(size_t index=0;index<dirty.size();++index)if(dirty[index]){int x=int(index%columns)*256,y=int(index/columns)*256,w=std::min(256,doc.width-x),h=std::min(256,doc.height-y);result->tiles[index]=touches(painted,x,y,w,h)?renderer.render(doc,x,y,w,h)->tiles.front():zeroTile();}
    previous_=doc;output_=result;return output_;
}
CompositeViewport CompositeCache::renderViewport(const Document& input,double x,double y,double width,double height,double requestedUnits,size_t maxVisibleTiles,size_t maxRetainedTiles,std::shared_ptr<const LayerRenderPreview> preview){
    Document doc=input;
    if(preview){auto found=std::find_if(doc.layers.begin(),doc.layers.end(),[&](const Layer& layer){return layer.id==preview->layer.id;});if(found==doc.layers.end()||!preview->identity)throw std::invalid_argument("Invalid viewport render preview");*found=preview->layer;}
    validateDocument(doc);
    if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(width)||!std::isfinite(height)||!std::isfinite(x+width)||!std::isfinite(y+height)||width<0||height<0||
       !std::isfinite(requestedUnits)||requestedUnits<=0||requestedUnits>30000||maxVisibleTiles<1||maxVisibleTiles>16384||maxRetainedTiles<maxVisibleTiles||maxRetainedTiles>16384)
        throw std::invalid_argument("Invalid viewport or tile budget");
    CompositeViewport result;result.documentWidth=doc.width;result.documentHeight=doc.height;
    double left=std::clamp(x,0.,double(doc.width)),top=std::clamp(y,0.,double(doc.height));double right=std::clamp(x+width,0.,double(doc.width)),bottom=std::clamp(y+height,0.,double(doc.height));
    if(right<=left||bottom<=top)return result;
    double units=std::pow(2.,std::ceil(std::log2(std::max(1.,requestedUnits))));int pixelWidth{},pixelHeight{},columns{},rows{},tx0{},ty0{},tx1{},ty1{};
    for(;;){pixelWidth=int(std::ceil(doc.width/units));pixelHeight=int(std::ceil(doc.height/units));columns=(pixelWidth+255)/256;rows=(pixelHeight+255)/256;double side=256*units;tx0=int(std::floor(left/side));ty0=int(std::floor(top/side));tx1=std::min(columns,int(std::ceil(right/side)));ty1=std::min(rows,int(std::ceil(bottom/side)));if(size_t(tx1-tx0)*size_t(ty1-ty0)<=maxVisibleTiles)break;units*=2;}
    result.unitsPerPixel=units;result.documentX=tx0*256*units;result.documentY=ty0*256*units;
    if(viewportUnits_!=units||viewportTiles_.size()!=size_t(columns)*rows){viewportPrevious_.reset();viewportTiles_.assign(size_t(columns)*rows,{});viewportUse_.assign(viewportTiles_.size(),0);viewportUnits_=units;viewportTick_=0;}
    // Compare canonical documents separately from ephemeral crop metadata. The
    // snapshot damage map uses original source coordinates across crop growth.
    auto invalid=invalidTiles(viewportPrevious_,input,units,columns,rows);
    const auto identity=preview?preview->identity:std::shared_ptr<const void>{};
    if(identity!=(viewportPreview_?viewportPreview_->identity:std::shared_ptr<const void>{})){
        std::optional<std::vector<LayerRenderPreview::Damage>> damage;
        if(preview&&preview->damageComparedWith)damage=preview->damageComparedWith(viewportPreview_.get());
        else if(!preview&&viewportPreview_&&viewportPreview_->damageComparedWith)damage=viewportPreview_->damageComparedWith(nullptr);
        // Effects and live clipping can propagate changed source pixels outside
        // the painted layer; retain conservative complete invalidation there.
        const bool dependent=std::any_of(doc.layers.begin(),doc.layers.end(),[](const Layer& layer){return !layer.maskSourceId.empty()||!layer.adjustmentJson.empty();});
        const auto reduced=[&](const LayerRenderPreview* source){if(!source)return false;const auto& layer=source->layer;const auto image=source->imageSource?source->imageSource:graphics::samplingSource(layer.raster);if(image&&layer.transform.sampling!=Transform::Sampling::Nearest&&graphics::DownsampleCache::levelFor(layer.transform.width/(units*image->width)))return true;if(layer.mask&&layer.mask->enabled){const auto mask=source->maskSource?source->maskSource:graphics::samplingSource(layer.mask->raster);const auto placement=layer.mask->placement.value_or(layer.transform);if(mask&&placement.sampling!=Transform::Sampling::Nearest&&graphics::DownsampleCache::levelFor(placement.width/(units*mask->width)))return true;}return false;};
        // A halving's filter halo reaches farther than the level-zero damage
        // rectangles. Until that support is mapped, invalidate all output tiles.
        if(!damage||dependent||reduced(preview.get())||reduced(viewportPreview_.get()))std::fill(invalid.begin(),invalid.end(),uint8_t(1));
        else for(const auto& rect:*damage){const double side=256*units;const int x0=int(std::clamp(std::floor(rect.left/side),0.,double(columns))),y0=int(std::clamp(std::floor(rect.top/side),0.,double(rows))),x1=int(std::clamp(std::ceil(rect.right/side),0.,double(columns))),y1=int(std::clamp(std::ceil(rect.bottom/side),0.,double(rows)));for(int ty=y0;ty<y1;++ty)for(int tx=x0;tx<x1;++tx)invalid[size_t(ty)*columns+tx]=1;}
    }
    for(size_t i=0;i<invalid.size();++i)if(invalid[i]){viewportTiles_[i].reset();viewportUse_[i]=0;}
    if(std::any_of(invalid.begin(),invalid.end(),[](uint8_t value){return value!=0;}))validateCulledAdjustments(doc,preview);
    ++viewportTick_;auto painted=paintedBounds(doc,units,preview.get());auto direct=units==1&&!preview?directRaster(doc):std::shared_ptr<const Raster>{};SoftwareRenderer renderer(preview);
    auto patch=std::make_shared<Raster>();patch->width=std::min(pixelWidth,tx1*256)-tx0*256;patch->height=std::min(pixelHeight,ty1*256)-ty0*256;patch->tiles.reserve(size_t(tx1-tx0)*size_t(ty1-ty0));
    for(int ty=ty0;ty<ty1;++ty)for(int tx=tx0;tx<tx1;++tx){size_t index=size_t(ty)*columns+tx;auto& tile=viewportTiles_[index];if(!tile){int px=tx*256,py=ty*256,w=std::min(256,pixelWidth-px),h=std::min(256,pixelHeight-py);if(direct)tile=direct->tiles[index];else if(!touches(painted,px*units,py*units,w*units,h*units))tile=zeroTile();else tile=renderer.renderScaled(doc,px*units,py*units,w,h,units)->tiles.front();}viewportUse_[index]=viewportTick_;patch->tiles.push_back(tile);}
    // Retained source/display handles are bounded independently of document area.
    // The returned immutable patch owns its visible handles until its caller drops it.
    std::vector<size_t> retained;for(size_t i=0;i<viewportTiles_.size();++i)if(viewportTiles_[i])retained.push_back(i);
    if(retained.size()>maxRetainedTiles){std::sort(retained.begin(),retained.end(),[&](size_t a,size_t b){return viewportUse_[a]<viewportUse_[b];});size_t remove=retained.size()-maxRetainedTiles;for(size_t i=0;i<remove;++i){auto at=retained[i];viewportTiles_[at].reset();viewportUse_[at]=0;}}
    viewportPrevious_=input;viewportPreview_=std::move(preview);result.raster=std::move(patch);return result;
}
}
