// Source: BrushStroke.liftSelection/moveLifted, FloatingSelection/FloatingMerge,
// SelectionClipboard.renderSelectedPixels, SelectionEdits. MIT: LICENSE.
#include "PixelTransform.h"
#include "graphics/RasterSampling.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace compositor::editing_transform {
namespace {
void cancelled(const CancelCheck& check){if(check&&check())throw TransformCancelled();}
uint8_t byte(double value){return uint8_t(std::clamp(std::round(value),0.,255.));}
Pixel scale(Pixel p,double v){return {byte(p.r*v),byte(p.g*v),byte(p.b*v),byte(p.a*v)};}
Pixel over(Pixel destination,Pixel source){const double inv=1-source.a/255.;return {byte(source.r+destination.r*inv),byte(source.g+destination.g*inv),byte(source.b+destination.b*inv),byte(source.a+destination.a*inv)};}
Rect integral(Rect r){double x=std::floor(r.x),y=std::floor(r.y);return {x,y,std::ceil(r.x+r.width)-x,std::ceil(r.y+r.height)-y};}
Rect united(Rect a,Rect b){if(a.width<=0||a.height<=0)return b;if(b.width<=0||b.height<=0)return a;const double x=std::min(a.x,b.x),y=std::min(a.y,b.y);return {x,y,std::max(a.x+a.width,b.x+b.width)-x,std::max(a.y+a.height,b.y+b.height)-y};}
Rect intersected(Rect a,Rect b){const double x=std::max(a.x,b.x),y=std::max(a.y,b.y);return {x,y,std::max(0.,std::min(a.x+a.width,b.x+b.width)-x),std::max(0.,std::min(a.y+a.height,b.y+b.height)-y)};}
template<class Map>Rect mapped(Rect r,Map map){const std::array<Point,4> points{{map({r.x,r.y}),map({r.x+r.width,r.y}),map({r.x+r.width,r.y+r.height}),map({r.x,r.y+r.height})}};double x0=points[0].x,x1=x0,y0=points[0].y,y1=y0;for(auto p:points){x0=std::min(x0,p.x);x1=std::max(x1,p.x);y0=std::min(y0,p.y);y1=std::max(y1,p.y);}return {x0,y0,x1-x0,y1-y0};}
void budget(Rect r){if(!std::isfinite(r.width)||!std::isfinite(r.height)||r.width<1||r.height<1||r.width>30000||r.height>30000||r.width*r.height>100000000)throw std::invalid_argument("Selected pixel extent exceeds budget");}
std::shared_ptr<const Raster> makeRaster(int width,int height,const std::function<Pixel(int,int)>& pixel,CancelCheck check={}){
    auto out=std::make_shared<Raster>();out->width=width;out->height=height;
    for(int ty=0;ty<(height+255)/256;++ty)for(int tx=0;tx<(width+255)/256;++tx){auto tile=std::make_shared<Raster::Tile>();for(int y=0;y<std::min(256,height-ty*256);++y){cancelled(check);for(int x=0;x<std::min(256,width-tx*256);++x)tile->pixels[size_t(y)*256+x]=pixel(tx*256+x,ty*256+y);}out->tiles.push_back(std::move(tile));}return out;
}
Rect selectionBounds(const GrayRaster& gray){int x0=gray.width,y0=gray.height,x1=0,y1=0;for(int y=0;y<gray.height;++y)for(int x=0;x<gray.width;++x)if(gray.pixel(x,y)){x0=std::min(x0,x);y0=std::min(y0,y);x1=std::max(x1,x+1);y1=std::max(y1,y+1);}return x0<x1&&y0<y1?Rect{double(x0),double(y0),double(x1-x0),double(y1-y0)}:Rect{};}
}
struct PixelTransformSession::Impl {
    Layer original;
    std::shared_ptr<const GrayRaster> selection;
    PixelTransformKind kind;
    bool duplicate{},started{},finished{},dirty{true};
    Rect selectionBox,liftBox;
    std::shared_ptr<const Raster> lifted;
    std::shared_ptr<const Raster> cleared;
    std::string temporaryId;
    Transform floatingOriginal,floatingDraft;
    Point offset;
    std::optional<Corners> distortion;
    double mapA{},mapB{},mapC{},mapD{},mapX{},mapY{},determinant{};
    PixelTransformResult result;
    Impl(Layer source,std::shared_ptr<const GrayRaster> clip,PixelTransformKind mode,bool copy):original(std::move(source)),selection(std::move(clip)),kind(mode),duplicate(copy){
        if(!original.raster||original.group||!original.transform.valid()||!selection)throw std::invalid_argument("Selected pixel transform requires image pixels and selection");
        budget({0,0,double(original.raster->width),double(original.raster->height)});budget({0,0,double(selection->width),double(selection->height)});
        if(selection->pixels.size()!=size_t(selection->width)*selection->height||original.raster->tiles.size()!=size_t((original.raster->width+255)/256)*size_t((original.raster->height+255)/256))throw std::invalid_argument("Invalid selected pixel storage");for(auto& tile:original.raster->tiles)if(!tile)throw std::invalid_argument("Missing selected pixel tile");
        if(original.mask&&(!original.mask->raster||original.mask->raster->width<1||original.mask->raster->height<1||original.mask->raster->pixels.size()!=size_t(original.mask->raster->width)*original.mask->raster->height))throw std::invalid_argument("Invalid selected layer mask");
        const auto& t=original.transform;const double angle=std::fmod(t.rotation,360)*std::numbers::pi/180,sx=t.width/original.raster->width*(t.flipX?-1:1),sy=t.height/original.raster->height*(t.flipY?-1:1);
        mapA=std::cos(angle)*sx;mapB=std::sin(angle)*sx;mapC=-std::sin(angle)*sy;mapD=std::cos(angle)*sy;const auto middle=center(t);mapX=middle.x-mapA*original.raster->width/2-mapC*original.raster->height/2;mapY=middle.y-mapB*original.raster->width/2-mapD*original.raster->height/2;determinant=mapA*mapD-mapB*mapC;
        selectionBox=selectionBounds(*selection);result={original,selection,selectionBox};
    }
    Point toPixels(Point doc)const{doc.x-=mapX;doc.y-=mapY;return {(mapD*doc.x-mapC*doc.y)/determinant,(-mapB*doc.x+mapA*doc.y)/determinant};}
    Point toDocument(Point pixel)const{return {mapX+mapA*pixel.x+mapC*pixel.y,mapY+mapB*pixel.x+mapD*pixel.y};}
    double selected(Point documentPoint)const{return graphics::sampleGray(*selection,{documentPoint.x/selection->width,documentPoint.y/selection->height},Transform::Sampling::Nearest);}
    Transform sourceExtentTransform(Rect r)const{auto out=original.transform;const auto c=toDocument({r.x+r.width/2,r.y+r.height/2});out.width=r.width*original.transform.width/original.raster->width;out.height=r.height*original.transform.height/original.raster->height;out.x=c.x-out.width/2;out.y=c.y-out.height/2;return out;}
    bool noChange()const{return kind==PixelTransformKind::Move?offset==Point{}:floatingDraft==floatingOriginal&&!distortion;}
    Point mapSelection(Point p)const{
        if(kind==PixelTransformKind::Move)return {p.x+offset.x,p.y+offset.y};
        auto unit=floatingOriginal.toUnit(p);
        if(distortion){if(floatingDraft.flipX)unit.x=1-unit.x;if(floatingDraft.flipY)unit.y=1-unit.y;return Homography::fromCorners(*distortion).map(unit);}
        return floatingDraft.fromUnit(unit);
    }
    std::shared_ptr<const GrayRaster> movedSelection(CancelCheck check,bool affineOnly=false)const{
        auto out=std::make_shared<GrayRaster>();out->width=selection->width;out->height=selection->height;out->pixels.resize(selection->pixels.size());std::optional<Homography> inverse;if(distortion&&!affineOnly)inverse=Homography::fromCorners(*distortion).inverse();
        for(int y=0;y<out->height;++y){cancelled(check);for(int x=0;x<out->width;++x){Point p{x+.5,y+.5},source;
            if(kind==PixelTransformKind::Move)source={p.x-offset.x,p.y-offset.y};
            else if(inverse){auto unit=inverse->map(p);if(floatingDraft.flipX)unit.x=1-unit.x;if(floatingDraft.flipY)unit.y=1-unit.y;source=floatingOriginal.fromUnit(unit);}
            else source=floatingOriginal.fromUnit(floatingDraft.toUnit(p));
            out->pixels[size_t(y)*out->width+x]=byte(graphics::sampleGray(*selection,{source.x/selection->width,source.y/selection->height},kind==PixelTransformKind::Move?Transform::Sampling::Nearest:Transform::Sampling::Smooth)*255);}}
        return out;
    }
    PixelTransformResult render(CancelCheck check){
        cancelled(check);if(!dirty)return result;if(noChange()){dirty=false;return result={original,selection,selectionBox};}
        const auto& source=*original.raster;const Rect oldExtent{0,0,double(source.width),double(source.height)};Rect extent,target;Point pixelOffset;
        std::shared_ptr<const Raster> placedPixels=lifted;Transform placedTransform=floatingDraft;
        if(kind==PixelTransformKind::Move){const auto zero=toPixels({0,0}),translated=toPixels(offset);pixelOffset={translated.x-zero.x,translated.y-zero.y};target={liftBox.x+pixelOffset.x,liftBox.y+pixelOffset.y,liftBox.width,liftBox.height};
            const Rect canvasPixels=integral(mapped({0,0,double(selection->width),double(selection->height)},[&](Point p){return toPixels(p);}));const auto workspace=united(oldExtent,canvasPixels);extent=integral(united(oldExtent,intersected(target,workspace)));
        }else{
            if(distortion){auto warped=warpRaster(lifted,floatingDraft,*distortion,{0,true,check});placedPixels=warped.raster;placedTransform=warped.transform;}
            const auto documentBox=bounds(placedTransform);target=mapped(documentBox,[&](Point p){return toPixels(p);});extent=integral(united(oldExtent,target));
        }
        budget(extent);const int w=int(extent.width),h=int(extent.height);auto image=std::make_shared<Raster>();image->width=w;image->height=h;const bool sameGrid=extent.x==0&&extent.y==0&&w==source.width&&h==source.height;
        const bool whole=target.x==std::round(target.x)&&target.y==std::round(target.y);size_t key=0;
        for(int ty=0;ty<(h+255)/256;++ty)for(int tx=0;tx<(w+255)/256;++tx,++key){auto tile=std::make_shared<Raster::Tile>();bool tileUnchanged=sameGrid;
            for(int y=0;y<std::min(256,h-ty*256);++y){cancelled(check);for(int x=0;x<std::min(256,w-tx*256);++x){const int xx=int(extent.x)+tx*256+x,yy=int(extent.y)+ty*256+y;const auto base=source.pixel(xx,yy);const auto doc=toDocument({xx+.5,yy+.5});auto under=duplicate?base:scale(base,1-selected(doc));Pixel copied;
                if(kind==PixelTransformKind::Move){const double coverageX=std::max(0.,std::min(double(xx+1),target.x+target.width)-std::max(double(xx),target.x)),coverageY=std::max(0.,std::min(double(yy+1),target.y+target.height)-std::max(double(yy),target.y));
                    if(coverageX*coverageY>0){const Point unit{std::clamp((xx+.5-target.x)/target.width,0.,1.-1e-12),std::clamp((yy+.5-target.y)/target.height,0.,1.-1e-12)};copied=scale(graphics::sampleRaster(*lifted,unit,whole?Transform::Sampling::Nearest:Transform::Sampling::Smooth),coverageX*coverageY);}}
                else copied=graphics::sampleRaster(*placedPixels,placedTransform.toUnit(doc),placedTransform.sampling);
                const auto value=over(under,copied);tile->pixels[size_t(y)*256+x]=value;if(value!=base)tileUnchanged=false;}}
            image->tiles.push_back(tileUnchanged?source.tiles[key]:std::move(tile));
        }
        auto layer=original;layer.raster=image;layer.transform=sourceExtentTransform(extent);
        if(layer.mask&&!layer.mask->placement&&!sameGrid){const auto& current=*layer.mask->raster;auto grown=std::make_shared<GrayRaster>();grown->width=w;grown->height=h;grown->pixels.resize(size_t(w)*h,255);
            for(int y=0;y<h;++y){cancelled(check);for(int x=0;x<w;++x){const double px=extent.x+x+.5,py=extent.y+y+.5;if(px>=0&&py>=0&&px<source.width&&py<source.height)grown->pixels[size_t(y)*w+x]=byte(graphics::sampleGray(current,{px/source.width,py/source.height},Transform::Sampling::Nearest)*255);}}
            layer.mask->raster=std::move(grown);
        }
        auto moved=movedSelection(check);const auto movedBox=mapped(selectionBox,[&](Point p){return mapSelection(p);});PixelTransformResult next{std::move(layer),std::move(moved),movedBox};cancelled(check);result=std::move(next);dirty=false;return result;
    }
};
PixelTransformSession::PixelTransformSession(Layer layer,std::shared_ptr<const GrayRaster> selection,PixelTransformKind kind,bool duplicate):impl_(std::make_unique<Impl>(std::move(layer),std::move(selection),kind,duplicate)){}
PixelTransformSession::~PixelTransformSession()=default;
bool PixelTransformSession::begin(CancelCheck check){auto& s=*impl_;if(s.started||s.finished)throw std::logic_error("Pixel transform already started or finished");cancelled(check);if(s.selectionBox.width<=0||s.selectionBox.height<=0)return false;
    const auto& source=*s.original.raster;
    if(s.kind==PixelTransformKind::Move){s.liftBox=intersected(integral(mapped(s.selectionBox,[&](Point p){return s.toPixels(p);})),{0,0,double(source.width),double(source.height)});if(s.liftBox.width<1||s.liftBox.height<1)return false;
        s.lifted=makeRaster(int(s.liftBox.width),int(s.liftBox.height),[&](int x,int y){const int xx=int(s.liftBox.x)+x,yy=int(s.liftBox.y)+y;return scale(source.pixel(xx,yy),s.selected(s.toDocument({xx+.5,yy+.5})));},check);s.floatingOriginal=s.sourceExtentTransform(s.liftBox);
    }else{s.liftBox=s.selectionBox;s.lifted=makeRaster(int(s.liftBox.width),int(s.liftBox.height),[&](int x,int y){const Point doc{s.liftBox.x+x+.5,s.liftBox.y+y+.5};return scale(graphics::sampleRaster(source,s.original.transform.toUnit(doc),s.original.transform.sampling),s.selected(doc));},check);s.floatingOriginal={s.liftBox.x,s.liftBox.y,s.liftBox.width,s.liftBox.height};}
    s.floatingDraft=s.floatingOriginal;s.started=true;return true;
}
bool PixelTransformSession::move(Point offset){auto& s=*impl_;if(!s.started||s.finished)throw std::logic_error("Pixel transform not active");if(!std::isfinite(offset.x)||!std::isfinite(offset.y)||std::abs(offset.x)>1000000||std::abs(offset.y)>1000000)return false;offset={std::round(offset.x),std::round(offset.y)};s.offset=offset;s.floatingDraft=s.floatingOriginal;s.floatingDraft.x+=offset.x;s.floatingDraft.y+=offset.y;s.distortion.reset();s.dirty=true;return true;}
bool PixelTransformSession::update(Transform draft,std::optional<Corners> shape){auto& s=*impl_;if(!s.started||s.finished||s.kind!=PixelTransformKind::Affine)throw std::logic_error("Affine pixel transform not active");if(!draft.valid()||(shape&&!usableCorners(*shape)))return false;s.floatingDraft=draft;s.distortion=shape;s.dirty=true;return true;}
PixelTransformResult PixelTransformSession::preview(CancelCheck check){auto& s=*impl_;if(s.finished||!s.started)return s.result;return s.render(std::move(check));}
FloatingPixelPreview PixelTransformSession::floatingPreview(CancelCheck check){auto& s=*impl_;if(!s.started||s.finished||s.kind!=PixelTransformKind::Affine)throw std::logic_error("Floating preview requires an active affine selection");
    if(!s.cleared){const auto& source=*s.original.raster;auto image=std::make_shared<Raster>(source);const int columns=(source.width+255)/256;
        for(size_t key=0;key<source.tiles.size();++key){std::shared_ptr<Raster::Tile> copy;const int x0=int(key%size_t(columns))*256,y0=int(key/size_t(columns))*256;
            for(int y=0;y<std::min(256,source.height-y0);++y){cancelled(check);for(int x=0;x<std::min(256,source.width-x0);++x){const size_t local=size_t(y)*256+x;const auto base=source.tiles[key]->pixels[local],value=s.duplicate?base:scale(base,1-s.selected(s.toDocument({x0+x+.5,y0+y+.5})));if(value!=base){if(!copy)copy=std::make_shared<Raster::Tile>(*source.tiles[key]);copy->pixels[local]=value;}}}if(copy)image->tiles[key]=std::move(copy);}
        s.cleared=std::move(image);s.temporaryId=newId();}
    Layer base=s.original;base.raster=s.cleared;Layer floating;floating.id=s.temporaryId;floating.name="Floating Selection";floating.parentId=s.original.parentId;floating.opacity=s.original.opacity;floating.blend=s.original.blend;floating.raster=s.lifted;floating.transform=s.floatingDraft;
    if(s.distortion)floating=distortLayer(floating,s.floatingDraft,*s.distortion,{2048,false,check});cancelled(check);return {std::move(base),std::move(floating)};
}
PixelTransformResult PixelTransformSession::apply(CancelCheck check){auto& s=*impl_;if(s.finished)return s.result;if(s.started)s.render(std::move(check));s.finished=true;return s.result;}
std::shared_ptr<const GrayRaster> PixelTransformSession::selectionPreview(CancelCheck check,bool affineOnly)const{const auto& s=*impl_;if(s.finished)return s.result.selection;if(!s.started||s.noChange()||(affineOnly&&s.kind==PixelTransformKind::Affine&&s.floatingDraft==s.floatingOriginal))return s.selection;return s.movedSelection(std::move(check),affineOnly);}
PixelTransformResult PixelTransformSession::cancel(){auto& s=*impl_;s.result={s.original,s.selection,s.selectionBox};s.finished=true;return s.result;}
const Transform& PixelTransformSession::originalFloatingTransform()const{return impl_->floatingOriginal;}
const Transform& PixelTransformSession::draft()const{return impl_->floatingDraft;}
Point PixelTransformSession::mapSelectionPoint(Point point)const{return impl_->mapSelection(point);}
}
