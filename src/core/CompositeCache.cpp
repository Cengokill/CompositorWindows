#include "Document.h"
#include "graphics/Downsample.h"
#include "graphics/SamplingSource.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numeric>
#include <numbers>
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
bool samePreviewGrid(const LayerRenderPreview* a,const LayerRenderPreview* b){
    if(!a||!b||a->layer.transform!=b->layer.transform||bool(a->layer.mask)!=bool(b->layer.mask))return false;
    const auto same=[](const std::shared_ptr<const graphics::SamplingSource>& x,const std::shared_ptr<const graphics::SamplingSource>& y){return !x&&!y||x&&y&&x->width==y->width&&x->height==y->height&&x->alignmentX==y->alignmentX&&x->alignmentY==y->alignmentY&&bool(x->gray)==bool(y->gray);};
    if(!same(a->imageSource?a->imageSource:graphics::samplingSource(a->layer.raster),b->imageSource?b->imageSource:graphics::samplingSource(b->layer.raster)))return false;
    if(a->layer.mask){if(a->layer.mask->enabled!=b->layer.mask->enabled||a->layer.mask->placement!=b->layer.mask->placement)return false;if(!same(a->maskSource?a->maskSource:graphics::samplingSource(a->layer.mask->raster),b->maskSource?b->maskSource:graphics::samplingSource(b->layer.mask->raster)))return false;}
    return true;
}
Point filterHalo(const LayerRenderPreview& preview,double units){
    Point result{};const auto add=[&](const std::shared_ptr<const graphics::SamplingSource>& source,const Transform& transform){
        if(!source||transform.sampling==Transform::Sampling::Nearest)return;
        const int level=graphics::DownsampleCache::levelFor(transform.width/(units*source->width));if(!level)return;
        const double step=double(uint64_t(1)<<level),radius=10*(step-1)+step;
        const double angle=std::remainder(transform.rotation,360.)*std::numbers::pi/180,c=std::abs(std::cos(angle)),s=std::abs(std::sin(angle));
        const double x=radius*transform.width/source->width,y=radius*transform.height/source->height;
        result.x=std::max(result.x,c*x+s*y);result.y=std::max(result.y,s*x+c*y);
    };
    add(preview.imageSource?preview.imageSource:graphics::samplingSource(preview.layer.raster),preview.layer.transform);
    if(preview.layer.mask&&preview.layer.mask->enabled)add(preview.maskSource?preview.maskSource:graphics::samplingSource(preview.layer.mask->raster),preview.layer.mask->placement.value_or(preview.layer.transform));
    return result;
}
std::shared_ptr<const Raster> directRaster(const Document& doc){
    if(doc.layers.size()!=1)return {};const auto& layer=doc.layers.front();
    if(layer.visible&&!layer.group&&layer.raster&&layer.raster->width==doc.width&&layer.raster->height==doc.height&&
       layer.opacity==1&&layer.blend==Blend::Normal&&!layer.mask&&layer.parentId.empty()&&layer.maskSourceId.empty()&&layer.adjustmentJson.empty()&&
       layer.transform.x==0&&layer.transform.y==0&&layer.transform.width==doc.width&&layer.transform.height==doc.height&&layer.transform.rotation==0&&!layer.transform.flipX&&!layer.transform.flipY)return layer.raster;
    return {};
}
double effectPad(const LayerEffects& fx){
    if(!fx.active())return 0;
    double pad=0;auto add=[&](double value){pad=std::max(pad,value);};
    if(fx.dropShadow.enabled)add(std::abs(fx.dropShadow.distance)+std::min(std::max(fx.dropShadow.size,0.),12.)+2);
    if(fx.innerShadow.enabled)add(std::abs(fx.innerShadow.distance)+std::min(std::max(fx.innerShadow.size,0.),12.)+2);
    if(fx.outerGlow.enabled)add(std::min(std::max(fx.outerGlow.size,0.),12.)+2);
    if(fx.innerGlow.enabled)add(std::min(std::max(fx.innerGlow.size,0.),12.)+2);
    if(fx.stroke.enabled)add(std::min(std::max(fx.stroke.size,0.),24.)+2);
    return pad;
}
std::optional<Bounds> layerPaintBounds(const Layer& layer,double units){
    if(!layer.raster||!layer.transform.valid())return {};
    auto source=graphics::samplingSource(layer.raster);
    const int level=layer.transform.sampling==Transform::Sampling::Nearest?0:graphics::DownsampleCache::levelFor(layer.transform.width/(units*source->width));
    auto grid=graphics::samplingGrid(*source,level);
    auto box=bounds(layer.transform,double(grid.x)/source->width,double(grid.y)/source->height,double(grid.width*grid.step)/source->width,double(grid.height*grid.step)/source->height);
    const double pad=effectPad(layer.effects)+1;
    box.left-=pad;box.top-=pad;box.right+=pad;box.bottom+=pad;return box;
}
void addPainted(const Document& doc,size_t index,double units,std::vector<Bounds>& damage,bool withDescendants){
    if(auto box=layerPaintBounds(doc.layers[index],units))damage.push_back(*box);
    if(!withDescendants)return;
    std::unordered_map<std::string,const Layer*> byId;for(const auto& layer:doc.layers)byId.emplace(layer.id,&layer);
    const std::string& id=doc.layers[index].id;
    for(const auto& layer:doc.layers){if(&layer==&doc.layers[index])continue;std::string parent=layer.parentId;
        for(int guard=0;!parent.empty()&&guard<64;++guard){if(parent==id){if(auto box=layerPaintBounds(layer,units))damage.push_back(*box);break;}auto found=byId.find(parent);if(found==byId.end())break;parent=found->second->parentId;}}
}
bool lodActive(const Layer& layer,double units){return layer.raster&&layer.transform.sampling!=Transform::Sampling::Nearest&&graphics::DownsampleCache::levelFor(layer.transform.width/(units*layer.raster->width))>0;}
bool boundsOnlyMetadata(const Layer& before,const Layer& after){
    Layer probe=before;probe.name=after.name;probe.text=after.text;probe.shapeJson=after.shapeJson;probe.visible=after.visible;probe.opacity=after.opacity;probe.blend=after.blend;probe.transform=after.transform;probe.effects=after.effects;return probe==after;
}
bool visualMetadata(const Layer& before,const Layer& after){return before.visible!=after.visible||before.opacity!=after.opacity||before.blend!=after.blend||before.transform!=after.transform||before.effects!=after.effects;}
struct LayerDamage {bool all{};std::vector<Bounds> regions;};
LayerDamage compareLayers(const Document* previous,const Document& doc,double units){
    LayerDamage result;
    if(!previous||previous->width!=doc.width||previous->height!=doc.height||previous->layers.size()!=doc.layers.size()){result.all=true;return result;}
    const bool dependent=std::any_of(doc.layers.begin(),doc.layers.end(),[](const Layer& layer){return !layer.maskSourceId.empty()||!layer.adjustmentJson.empty();});
    for(size_t i=0;i<doc.layers.size();++i){const auto& before=previous->layers[i];const auto& after=doc.layers[i];if(before==after)continue;
        auto metadata=before;metadata.raster=after.raster;
        const bool rasterGeometry=bool(before.raster)!=bool(after.raster)||(before.raster&&(before.raster->width!=after.raster->width||before.raster->height!=after.raster->height||before.raster->samplingOriginX!=after.raster->samplingOriginX||before.raster->samplingOriginY!=after.raster->samplingOriginY));
        if(rasterGeometry||dependent||lodActive(before,units)||lodActive(after,units)){result.all=true;return result;}
        if(metadata!=after){if(!boundsOnlyMetadata(metadata,after)){result.all=true;return result;}
            if(visualMetadata(before,after)){const bool children=before.visible!=after.visible||before.opacity!=after.opacity;addPainted(*previous,i,units,result.regions,children);addPainted(doc,i,units,result.regions,children);}
            continue;}
        const int sourceColumns=(after.raster->width+255)/256;
        for(size_t tile=0;tile<after.raster->tiles.size();++tile)if(before.raster->tiles[tile]!=after.raster->tiles[tile]){
            int x=int(tile%sourceColumns)*256,y=int(tile/sourceColumns)*256;
            result.regions.push_back(bounds(after.transform,double(x-1)/after.raster->width,double(y-1)/after.raster->height,258./after.raster->width,258./after.raster->height));
        }
    }
    return result;
}
void markTiles(std::vector<uint8_t>& dirty,const std::vector<Bounds>& regions,double units,int columns,int rows){
    const double side=256*units;
    for(const auto& box:regions){int minX=std::clamp(int(std::floor(box.left/side)),0,columns),maxX=std::clamp(int(std::ceil(box.right/side)),0,columns);int minY=std::clamp(int(std::floor(box.top/side)),0,rows),maxY=std::clamp(int(std::ceil(box.bottom/side)),0,rows);
        for(int ty=minY;ty<maxY;++ty)for(int tx=minX;tx<maxX;++tx)dirty[size_t(ty)*columns+tx]=1;}
}
const Layer* flippedDirect(const Document& doc){
    if(doc.layers.size()!=1)return nullptr;const auto& layer=doc.layers.front();
    if(layer.visible&&!layer.group&&layer.raster&&layer.raster->width==doc.width&&layer.raster->height==doc.height&&
       layer.opacity==1&&layer.blend==Blend::Normal&&!layer.mask&&layer.parentId.empty()&&layer.maskSourceId.empty()&&layer.adjustmentJson.empty()&&!layer.effects.active()&&
       layer.transform.x==0&&layer.transform.y==0&&layer.transform.width==doc.width&&layer.transform.height==doc.height&&layer.transform.rotation==0&&(layer.transform.flipX||layer.transform.flipY))return &layer;
    return nullptr;
}
Tile mirrorTile(const Raster& source,bool flipX,bool flipY,int originX,int originY,int width,int height){
    auto tile=std::make_shared<Raster::Tile>();const int tw=std::min(256,width-originX),th=std::min(256,height-originY);
    for(int y=0;y<th;++y){const int sy=flipY?height-1-(originY+y):originY+y;for(int x=0;x<tw;++x){const int sx=flipX?width-1-(originX+x):originX+x;tile->pixels[size_t(y)*Raster::tileSide+x]=source.pixel(sx,sy);}}
    return tile;
}
std::vector<uint8_t> invalidTiles(const std::optional<Document>& previous,const Document& doc,double units,int columns,int rows){
    std::vector<uint8_t> dirty(size_t(columns)*rows,0);
    auto damage=compareLayers(previous?&*previous:nullptr,doc,units);
    if(damage.all){std::fill(dirty.begin(),dirty.end(),uint8_t(1));return dirty;}
    markTiles(dirty,damage.regions,units,columns,rows);return dirty;
}
void validateCulledAdjustments(const Document& doc,std::shared_ptr<const LayerRenderPreview> preview={}){
    // Even a transparent stack must report malformed or unsupported live effects.
    // One canonical pixel validates visible adjustment callbacks before blank culling.
    if(std::any_of(doc.layers.begin(),doc.layers.end(),[](const Layer& layer){return !layer.adjustmentJson.empty();}))SoftwareRenderer(std::move(preview)).render(doc,0,0,1,1);
}
std::shared_ptr<const Raster> tiled(int width,int height,Tile value){auto raster=std::make_shared<Raster>();raster->width=width;raster->height=height;raster->tiles.assign(size_t((width+255)/256)*((height+255)/256),std::move(value));return raster;}
double jsonField(const std::string& json,std::size_t from,const char* key,double fallback){
    const auto token=std::string("\"")+key+"\"";auto at=json.find(token,from);if(at==std::string::npos)return fallback;at=json.find(':',at+token.size());if(at==std::string::npos)return fallback;
    char* end=nullptr;const double value=std::strtod(json.c_str()+at+1,&end);return end==json.c_str()+at+1?fallback:value;
}
int spatialMargin(const Document& doc){
    int margin=0;
    for(const auto& layer:doc.layers){const auto& json=layer.adjustmentJson;if(!layer.visible||json.empty())continue;
        const bool gaussian=json.find("\"Gaussian Blur\"")!=std::string::npos,motion=json.find("\"Motion Blur\"")!=std::string::npos;if(!gaussian&&!motion)continue;
        const auto settings=json.find("\"filterSettings\"");
        if(gaussian){const double radius=settings==std::string::npos?8:jsonField(json,settings,"radius",8);if(radius>0)margin+=int(std::ceil(radius*3+2));}
        if(motion){const double distance=settings==std::string::npos?16:jsonField(json,settings,"distance",16);if(distance>0)margin+=int(std::ceil(distance/2+2));}}
    return std::clamp(margin,0,2048);
}
Tile cropTile(const Raster& raster,int ox,int oy,int w,int h){auto tile=std::make_shared<Raster::Tile>();for(int y=0;y<h;++y)for(int x=0;x<w;++x)tile->pixels[std::size_t(y)*Raster::tileSide+x]=raster.pixel(ox+x,oy+y);return tile;}
}
std::shared_ptr<const Raster> CompositeCache::render(const Document& doc){
    validateDocument(doc);if(auto direct=directRaster(doc)){previous_=doc;output_=direct;return output_;}
    const Layer* flipped=flippedDirect(doc);
    int columns=(doc.width+255)/256,rows=(doc.height+255)/256;auto dirty=invalidTiles(previous_,doc,1,columns,rows);
    if(output_&&std::none_of(dirty.begin(),dirty.end(),[](uint8_t value){return value!=0;})){previous_=doc;return output_;}
    validateCulledAdjustments(doc);auto painted=paintedBounds(doc);if(painted.empty()){previous_=doc;output_=tiled(doc.width,doc.height,zeroTile());return output_;}
    auto result=std::make_shared<Raster>();result->width=doc.width;result->height=doc.height;
    if(output_&&output_->width==doc.width&&output_->height==doc.height)result->tiles=output_->tiles;else result->tiles.resize(size_t(columns)*rows);
    SoftwareRenderer renderer;const int margin=spatialMargin(doc);
    for(size_t index=0;index<dirty.size();++index)if(dirty[index]){int x=int(index%columns)*256,y=int(index/columns)*256,w=std::min(256,doc.width-x),h=std::min(256,doc.height-y);
        if(!touches(painted,x-margin,y-margin,w+margin*2.,h+margin*2.)){result->tiles[index]=zeroTile();continue;}
        if(flipped)result->tiles[index]=mirrorTile(*flipped->raster,flipped->transform.flipX,flipped->transform.flipY,x,y,doc.width,doc.height);
        else if(margin>0)result->tiles[index]=cropTile(*renderer.render(doc,x-margin,y-margin,w+margin*2,h+margin*2),margin,margin,w,h);
        else result->tiles[index]=renderer.render(doc,x,y,w,h)->tiles.front();}
    previous_=doc;output_=result;return output_;
}
CompositeViewport CompositeCache::renderViewport(const Document& input,double x,double y,double width,double height,double requestedUnits,size_t maxVisibleTiles,size_t maxRetainedTiles,std::shared_ptr<const LayerRenderPreview> preview){
    Document doc=input;
    if(preview){auto found=std::find_if(doc.layers.begin(),doc.layers.end(),[&](const Layer& layer){return layer.id==preview->layer.id;});if(found==doc.layers.end()||!preview->identity)throw std::invalid_argument("Invalid viewport render preview");*found=preview->layer;}
    validateDocument(doc);
    if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(width)||!std::isfinite(height)||!std::isfinite(x+width)||!std::isfinite(y+height)||width<0||height<0||
       !std::isfinite(requestedUnits)||requestedUnits<1./32||requestedUnits>30000||maxVisibleTiles<1||maxVisibleTiles>16384||maxRetainedTiles<maxVisibleTiles||maxRetainedTiles>16384)
        throw std::invalid_argument("Invalid viewport or tile budget");
    CompositeViewport result;result.documentWidth=doc.width;result.documentHeight=doc.height;
    const double left=std::clamp(x,0.,double(doc.width)),top=std::clamp(y,0.,double(doc.height)),right=std::clamp(x+width,0.,double(doc.width)),bottom=std::clamp(y+height,0.,double(doc.height));
    if(right<=left||bottom<=top)return result;
    // Put pixel zero at or immediately before the document origin. The phase
    // remains congruent to the physical viewport origin, including fractional
    // pan. Sparse tile keys then stay nonnegative without a document-sized array.
    const auto phase=[](double origin,double units){double value=std::fmod(origin,units);if(value>0)value-=units;return value==0?0:value;};
    double units=requestedUnits,px{},py{};int pixelWidth{},pixelHeight{},tx0{},ty0{},tx1{},ty1{},patchWidth{},patchHeight{};
    for(;;){px=phase(x,units);py=phase(y,units);pixelWidth=int(std::ceil((doc.width-px)/units));pixelHeight=int(std::ceil((doc.height-py)/units));const double side=256*units;
        tx0=std::max(0,int(std::floor((left-px)/side)));ty0=std::max(0,int(std::floor((top-py)/side)));tx1=int(std::ceil((right-px)/side));ty1=int(std::ceil((bottom-py)/side));
        patchWidth=std::min(pixelWidth,tx1*256)-tx0*256;patchHeight=std::min(pixelHeight,ty1*256)-ty0*256;
        if(size_t(tx1-tx0)*size_t(ty1-ty0)<=maxVisibleTiles&&patchWidth<=30000&&patchHeight<=30000&&uint64_t(patchWidth)*patchHeight<=100000000)break;
        units*=2;
    }
    result.unitsPerPixel=units;result.documentX=px+tx0*256*units;result.documentY=py+ty0*256*units;
    bool all=viewportUnits_!=units||viewportPhaseX_!=px||viewportPhaseY_!=py;
    std::vector<Bounds> damage;
    const bool dependent=std::any_of(doc.layers.begin(),doc.layers.end(),[](const Layer& layer){return !layer.maskSourceId.empty()||!layer.adjustmentJson.empty();});
    // Compare canonical documents independently from transient preview metadata.
    // Only retained output entries will be checked against these damage bounds.
    auto compared=compareLayers(viewportPrevious_?&*viewportPrevious_:nullptr,input,units);
    if(compared.all)all=true;else damage.insert(damage.end(),compared.regions.begin(),compared.regions.end());
    const auto identity=preview?preview->identity:std::shared_ptr<const void>{};
    if(!all&&identity!=(viewportPreview_?viewportPreview_->identity:std::shared_ptr<const void>{})){
        std::optional<std::vector<LayerRenderPreview::Damage>> changed;
        if(preview&&preview->damageComparedWith)changed=preview->damageComparedWith(viewportPreview_.get());
        else if(!preview&&viewportPreview_&&viewportPreview_->damageComparedWith)changed=viewportPreview_->damageComparedWith(nullptr);
        if(!changed||dependent||!samePreviewGrid(preview.get(),viewportPreview_.get()))all=true;
        else{const auto current=filterHalo(*preview,units),previous=filterHalo(*viewportPreview_,units);const double hx=std::max(current.x,previous.x),hy=std::max(current.y,previous.y);for(const auto& rect:*changed)damage.push_back({rect.left-hx,rect.top-hy,rect.right+hx,rect.bottom+hy});}
    }
    std::map<std::pair<int,int>,Bounds> partial;
    const Layer* flipped=flippedDirect(doc);
    const bool mirror=flipped&&units==1&&px==0&&py==0;
    if(mirror&&(all||!damage.empty())){all=true;damage.clear();}
    if(all)viewportTiles_.clear();
    else if(!damage.empty())for(auto it=viewportTiles_.begin();it!=viewportTiles_.end();){const auto [tx,ty]=it->first;const double dx=px+tx*256*units,dy=py+ty*256*units;if(!touches(damage,dx,dy,256*units,256*units)){++it;continue;}
        // Offscreen dirty entries cannot retain stale pixels until a later pan.
        if(tx<tx0||tx>=tx1||ty<ty0||ty>=ty1||dependent){it=viewportTiles_.erase(it);continue;}
        Bounds region{INFINITY,INFINITY,-INFINITY,-INFINITY};for(const auto& box:damage)if(box.right>dx&&box.bottom>dy&&box.left<dx+256*units&&box.top<dy+256*units){region.left=std::min(region.left,box.left);region.top=std::min(region.top,box.top);region.right=std::max(region.right,box.right);region.bottom=std::max(region.bottom,box.bottom);}partial.emplace(it->first,region);++it;
    }
    if(all||!damage.empty())validateCulledAdjustments(doc,preview);
    viewportUnits_=units;viewportPhaseX_=px;viewportPhaseY_=py;++viewportTick_;
    auto painted=paintedBounds(doc,units,preview.get());auto direct=units==1&&px==0&&py==0&&!preview?directRaster(doc):std::shared_ptr<const Raster>{};SoftwareRenderer renderer(preview);const int margin=spatialMargin(doc);
    auto patch=std::make_shared<Raster>();patch->width=patchWidth;patch->height=patchHeight;patch->tiles.reserve(size_t(tx1-tx0)*size_t(ty1-ty0));
    for(int ty=ty0;ty<ty1;++ty)for(int tx=tx0;tx<tx1;++tx){const std::pair<int,int> key{tx,ty};auto found=viewportTiles_.find(key);if(found==viewportTiles_.end()){
            const int ix=tx*256,iy=ty*256,w=std::min(256,pixelWidth-ix),h=std::min(256,pixelHeight-iy);const double dx=px+ix*units,dy=py+iy*units;Tile tile;
            if(w<=0||h<=0)tile=zeroTile();
            else if(direct){const size_t index=size_t(ty)*((direct->width+255)/256)+tx;tile=index<direct->tiles.size()&&direct->tiles[index]?direct->tiles[index]:zeroTile();}
            else if(mirror)tile=mirrorTile(*flipped->raster,flipped->transform.flipX,flipped->transform.flipY,tx*256,ty*256,doc.width,doc.height);
            else if(!touches(painted,dx-margin*units,dy-margin*units,(w+margin*2.)*units,(h+margin*2.)*units))tile=zeroTile();
            else if(margin>0)tile=cropTile(*renderer.renderScaled(doc,dx-margin*units,dy-margin*units,w+margin*2,h+margin*2,units),margin,margin,w,h);
            else {auto scaled=renderer.renderScaled(doc,dx,dy,w,h,units);tile=scaled&&!scaled->tiles.empty()&&scaled->tiles.front()?scaled->tiles.front():zeroTile();}
            while(viewportTiles_.size()>=maxRetainedTiles){auto oldest=std::min_element(viewportTiles_.begin(),viewportTiles_.end(),[](const auto& a,const auto& b){return a.second.use<b.second.use;});viewportTiles_.erase(oldest);}
            found=viewportTiles_.emplace(key,ViewportTile{std::move(tile),viewportTick_}).first;
        }else{
            if(auto dirty=partial.find(key);dirty!=partial.end()){
                const int ix=tx*256,iy=ty*256,w=std::min(256,pixelWidth-ix),h=std::min(256,pixelHeight-iy);const double dx=px+ix*units,dy=py+iy*units;const auto& box=dirty->second;
                const int sx=std::clamp(int(std::floor((box.left-dx)/units))-1,0,w),sy=std::clamp(int(std::floor((box.top-dy)/units))-1,0,h),ex=std::clamp(int(std::ceil((box.right-dx)/units))+1,0,w),ey=std::clamp(int(std::ceil((box.bottom-dy)/units))+1,0,h);
                if(ex>sx&&ey>sy){auto updated=std::make_shared<Raster::Tile>(*found->second.pixels);auto region=renderer.renderScaledPatch(doc,dx,dy,sx,sy,ex-sx,ey-sy,units);for(int row=0;row<ey-sy;++row)std::copy_n(region->tiles.front()->pixels.data()+size_t(row)*256,ex-sx,updated->pixels.data()+size_t(sy+row)*256+sx);found->second.pixels=std::move(updated);}
            }
            found->second.use=viewportTick_;
        }patch->tiles.push_back(found->second.pixels);
    }
    // The returned patch owns its visible immutable tiles; cache entries and
    // their metadata are bounded independently of document area and zoom.
    while(viewportTiles_.size()>maxRetainedTiles){auto oldest=std::min_element(viewportTiles_.begin(),viewportTiles_.end(),[](const auto& a,const auto& b){return a.second.use<b.second.use;});viewportTiles_.erase(oldest);}
    viewportPrevious_=input;viewportPreview_=std::move(preview);result.raster=std::move(patch);return result;
}
}
