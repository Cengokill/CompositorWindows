// Adapted from Compositor, copyright (c) 2026 Wonder Assembly LLC.
// Distributed under the MIT license in LICENSE in this directory.
#include "TransformGeometry.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace compositor::editing_transform {
namespace {
constexpr double pi=std::numbers::pi;
double radians(const Transform& t){return std::fmod(t.rotation,360)*pi/180;}
bool finite(Point p){return std::isfinite(p.x)&&std::isfinite(p.y);}
Point add(Point a,Point b){return {a.x+b.x,a.y+b.y};}
Point subtract(Point a,Point b){return {a.x-b.x,a.y-b.y};}
void checkHandle(int index){if(index<0||index>=8)throw std::invalid_argument("Transform handle must be in 0..7");}
double safeZoom(double zoom){if(!std::isfinite(zoom)||zoom<=0)throw std::invalid_argument("View scale must be finite and positive");return zoom;}
Transform checked(Transform t,const Transform& previous){return t.valid()?t:previous;}
struct Affine {
    double a,b,c,d,tx,ty;
    Point apply(Point p) const{return {a*p.x+c*p.y+tx,b*p.x+d*p.y+ty};}
};
Affine affine(const Transform& t){
    const auto origin=t.fromUnit({0,0}),x=t.fromUnit({1,0}),y=t.fromUnit({0,1});
    return {x.x-origin.x,x.y-origin.y,y.x-origin.x,y.y-origin.y,origin.x,origin.y};
}
Transform placing(const Transform& original,Affine map){
    const double sign=original.flipX?-1:1;
    const double angle=std::atan2(map.b*sign,map.a*sign);
    const double along=-map.c*std::sin(angle)+map.d*std::cos(angle);
    const auto middle=map.apply({.5,.5});
    auto result=original;
    result.width=std::hypot(map.a,map.b);result.height=std::abs(along);
    const double degrees=angle*180/pi;
    result.rotation=degrees+std::round((original.rotation-degrees)/360)*360;
    result.flipY=along<0;
    result.x=middle.x-result.width/2;result.y=middle.y-result.height/2;
    return result; // Source deliberately drops shear during uneven group scaling.
}
}
Point ViewMapping::toView(Point p) const{const auto z=safeZoom(pointsPerPixel);return {origin.x+p.x*z,origin.y+p.y*z};}
Point ViewMapping::toDocument(Point p) const{const auto z=safeZoom(pointsPerPixel);return {(p.x-origin.x)/z,(p.y-origin.y)/z};}
Point center(const Transform& t){return {t.x+t.width/2,t.y+t.height/2};}
Point outlinePoint(const Transform& t,Point p){
    const auto c=center(t);const double r=radians(t),x=(p.x-.5)*t.width,y=(p.y-.5)*t.height;
    return {c.x+x*std::cos(r)-y*std::sin(r),c.y+x*std::sin(r)+y*std::cos(r)};
}
Corners corners(const Transform& t){return {outlinePoint(t,{0,0}),outlinePoint(t,{1,0}),outlinePoint(t,{1,1}),outlinePoint(t,{0,1})};}
Rect bounds(const Transform& t){
    const auto c=corners(t);double minX=c[0].x,maxX=minX,minY=c[0].y,maxY=minY;
    for(auto p:c){minX=std::min(minX,p.x);maxX=std::max(maxX,p.x);minY=std::min(minY,p.y);maxY=std::max(maxY,p.y);}
    return {minX,minY,maxX-minX,maxY-minY};
}
bool contains(const Transform& t,Point p){
    if(!t.valid()||!finite(p))return false;
    p=subtract(p,center(t));const auto r=radians(t);
    return std::abs(p.x*std::cos(r)+p.y*std::sin(r))<=t.width/2&&std::abs(-p.x*std::sin(r)+p.y*std::cos(r))<=t.height/2;
}
Transform roundedTransform(const Transform& t){auto out=t;out.x=std::round(t.x);out.y=std::round(t.y);out.width=std::max(1.,std::round(t.width));out.height=std::max(1.,std::round(t.height));out.rotation=std::round(t.rotation);return checked(out,t);}
double scalePercent(const Transform& t,Point size){return t.width/std::max(1.,size.x)*100;}
Transform scaledPercent(const Transform& t,double percent,Point size){
    auto out=t;out.width=size.x*percent/100;out.height=size.y*percent/100;
    const auto c=center(t);out.x=c.x-out.width/2;out.y=c.y-out.height/2;return checked(out,t);
}
Transform resizedField(const Transform& t,double value,bool width,bool locked){
    if(!std::isfinite(value)||value<1)return t;
    auto out=t;if(width){if(locked)out.height*=value/out.width;out.width=value;}else{if(locked)out.width*=value/out.height;out.height=value;}return checked(out,t);
}
Transform flippedLocal(const Transform& t,bool horizontal){auto out=t;if(horizontal)out.flipX=!out.flipX;else out.flipY=!out.flipY;return out;}
Transform mirrored(const Transform& t,bool horizontal,double axis){
    auto out=flippedLocal(t,horizontal);if(horizontal)out.x=2*axis-center(t).x-t.width/2;else out.y=2*axis-center(t).y-t.height/2;
    out.rotation=-t.rotation;return checked(out,t);
}
Transform following(const Transform& placement,const Transform& oldParent,const Transform& newParent){
    if(!placement.valid()||!oldParent.valid()||!newParent.valid())return placement;
    if(oldParent==newParent)return placement;
    if(oldParent.width==newParent.width&&oldParent.height==newParent.height&&oldParent.rotation==newParent.rotation&&oldParent.flipX==newParent.flipX&&oldParent.flipY==newParent.flipY){
        auto out=placement;out.x+=newParent.x-oldParent.x;out.y+=newParent.y-oldParent.y;return checked(out,placement);
    }
    const auto map=affine(placement);
    const auto carry=[&](Point p){return newParent.fromUnit(oldParent.toUnit(map.apply(p)));};
    const auto p=carry({0,0}),x=carry({1,0}),y=carry({0,1});
    return checked(placing(placement,{x.x-p.x,x.y-p.y,y.x-p.x,y.y-p.y,p.x,p.y}),placement);
}
bool samePlacement(Transform a,Transform b){a.sampling=b.sampling;return a==b;}
OverlayGeometry OverlayGeometry::fromTransform(const Transform& t,const ViewMapping& view){
    if(!t.valid())throw std::invalid_argument("Invalid transform overlay");
    OverlayGeometry out;for(size_t i=0;i<8;++i)out.handles[i]=view.toView(outlinePoint(t,handleUnits[i]));
    for(size_t i=0;i<4;++i)out.outline[i]=out.handles[i*2];
    out.rotationHandle={out.handles[1].x+std::sin(radians(t))*28,out.handles[1].y-std::cos(radians(t))*28};return out;
}
OverlayGeometry OverlayGeometry::fromCorners(const Corners& c,const ViewMapping& view){
    OverlayGeometry out;out.showsRotation=false;
    for(size_t i=0;i<4;++i){out.outline[i]=view.toView(c[i]);out.handles[2*i]=out.outline[i];out.handles[2*i+1]=view.toView({(c[i].x+c[(i+1)%4].x)/2,(c[i].y+c[(i+1)%4].y)/2});}
    out.rotationHandle=out.handles[1];return out;
}
std::optional<Mode> OverlayGeometry::hit(Point p) const{
    if(!finite(p))return {};
    const auto near=[p](Point q){return std::hypot(p.x-q.x,p.y-q.y)<=10;};
    if(showsRotation&&near(rotationHandle))return Mode{ModeKind::Rotate};
    for(int i=0;i<8;++i)if(near(handles[static_cast<size_t>(i)]))return Mode{ModeKind::Resize,i};
    for(size_t side=0;side<4;++side){const auto a=handles[side*2],b=handles[(side*2+2)%8];const auto d=subtract(b,a);const double square=d.x*d.x+d.y*d.y;
        if(square<=0)continue;const double t=((p.x-a.x)*d.x+(p.y-a.y)*d.y)/square;
        if(t>=0&&t<=1&&std::hypot(p.x-a.x-t*d.x,p.y-a.y-t*d.y)<=10)return Mode{ModeKind::Resize,static_cast<int>(side*2+1)};
    }return {};
}
Cursor OverlayGeometry::resizeCursor(int index) const{
    checkHandle(index);const double angle=std::atan2(handles[2].y-handles[0].y,handles[2].x-handles[0].x);
    constexpr std::array<double,8> offsets{pi/4,pi/2,3*pi/4,0,pi/4,pi/2,3*pi/4,0};
    const int direction=(static_cast<int>(std::round((angle+offsets[static_cast<size_t>(index)])/(pi/4)))%4+4)%4;
    constexpr std::array<Cursor,4> cursors{Cursor::Horizontal,Cursor::DiagonalDown,Cursor::Vertical,Cursor::DiagonalUp};return cursors[static_cast<size_t>(direction)];
}
Cursor OverlayGeometry::cursor(Mode m,Modifiers flags,bool distorted) const{
    switch(m.kind){case ModeKind::Move:return flags.option?Cursor::Duplicate:Cursor::Move;case ModeKind::Rotate:return Cursor::Rotate;case ModeKind::Distort:return Cursor::Distort;case ModeKind::Resize:return flags.command||distorted?Cursor::Distort:resizeCursor(m.handle);}return Cursor::Move;
}
Transform Drag::updated(Point point,bool lockRatio,Modifiers flags) const{
    if(!original.valid()||!finite(point)||!finite(start))return original;
    auto out=original;const auto delta=subtract(point,start);
    switch(mode.kind){
    case ModeKind::Distort:break;
    case ModeKind::Move:{auto d=delta;if(flags.shift){if(std::abs(d.x)>=std::abs(d.y))d.y=0;else d.x=0;}out.x+=d.x;out.y+=d.y;break;}
    case ModeKind::Rotate:{const auto c=center(original);out.rotation+=(std::atan2(point.y-c.y,point.x-c.x)-std::atan2(start.y-c.y,start.x-c.x))*180/pi;if(flags.shift)out.rotation=std::round(out.rotation/15)*15;break;}
    case ModeKind::Resize:{
        checkHandle(mode.handle);const auto h=handleUnits[static_cast<size_t>(mode.handle)];
        const Point anchorUnit=flags.option?Point{.5,.5}:Point{1-h.x,1-h.y};const auto anchor=outlinePoint(original,anchorUnit);
        const auto d=subtract(add(outlinePoint(original,h),delta),anchor);const double r=radians(original),span=flags.option?2:1;
        const double localX=(d.x*std::cos(r)+d.y*std::sin(r))*span,localY=(-d.x*std::sin(r)+d.y*std::cos(r))*span;
        const double sx=h.x*2-1,sy=h.y*2-1;
        double width=sx==0?original.width:std::max(1.,localX*sx),height=sy==0?original.height:std::max(1.,localY*sy);
        if(lockRatio!=flags.shift){double factor;
            if(sx==0)factor=height/original.height;else if(sy==0)factor=width/original.width;
            else factor=std::max(1/std::min(original.width,original.height),(localX*sx*original.width+localY*sy*original.height)/(original.width*original.width+original.height*original.height));
            width=original.width*factor;height=original.height*factor;
        }
        out.width=width;out.height=height;const double ox=(.5-anchorUnit.x)*width,oy=(.5-anchorUnit.y)*height;
        out.x=anchor.x+ox*std::cos(r)-oy*std::sin(r)-width/2;out.y=anchor.y+ox*std::sin(r)+oy*std::cos(r)-height/2;break;
    }}return checked(out,original);
}
std::optional<Corners> Drag::movedCorners(Point point,bool shift) const{
    if(!originalCorners||!finite(point)||!finite(start))return {};
    auto out=*originalCorners;auto d=subtract(point,start);if(shift){if(std::abs(d.x)>=std::abs(d.y))d.y=0;else d.x=0;}
    if(mode.kind==ModeKind::Move){for(auto& p:out)p=add(p,d);}
    else if(mode.kind==ModeKind::Distort){checkHandle(mode.handle);const auto i=static_cast<size_t>(mode.handle/2);out[i]=add(out[i],d);if(mode.handle%2)out[(i+1)%4]=add(out[(i+1)%4],d);}
    else return {};
    return out;
}
bool usableCorners(const Corners& c){
    double sign=0;for(auto p:c)if(!finite(p)||std::abs(p.x)>1000000||std::abs(p.y)>1000000)return false;
    for(size_t i=0;i<4;++i){const auto a=c[i],b=c[(i+1)%4],d=c[(i+2)%4];const double cross=(b.x-a.x)*(d.y-b.y)-(b.y-a.y)*(d.x-b.x);
        if(std::abs(cross)<=.01)return false;if(sign==0)sign=cross<0?-1:1;else if((cross<0)!=(sign<0))return false;}
    return true;
}
SnapResult snapOffset(Rect box,const SnapTargets& targets,double tolerance){
    SnapResult result;if(!std::isfinite(tolerance)||tolerance<0)return result;
    const auto axis=[tolerance](std::array<double,3> guides,const std::vector<double>& values,double& offset,std::optional<double>& matched){
        for(double guide:guides)for(double target:values){const double move=target-guide;if(!std::isfinite(move)||std::abs(move)>tolerance)continue;if(matched&&std::abs(offset)<=std::abs(move))continue;offset=move;matched=target;}
    };
    axis({box.x,box.x+box.width/2,box.x+box.width},targets.xs,result.offset.x,result.x);
    axis({box.y,box.y+box.height/2,box.y+box.height},targets.ys,result.offset.y,result.y);return result;
}
Preview previewDrag(const Drag& drag,Point point,bool ratio,Modifiers flags,const SnapTargets& targets,double zoom){
    Preview out{drag.original,{}, {}};
    if(auto c=drag.movedCorners(point,flags.shift)){if(usableCorners(*c))out.distortion=c;else out.accepted=false;return out;}
    out.transform=roundedTransform(drag.updated(point,ratio,flags));
    if(drag.mode.kind==ModeKind::Move&&!flags.control){out.snap=snapOffset(bounds(out.transform),targets,10/std::max(safeZoom(zoom),.0001));auto t=out.transform;t.x+=out.snap.offset.x;t.y+=out.snap.offset.y;if(t.valid())out.transform=t;else out.snap={};}
    return out;
}
std::vector<Placement> visiblePlacements(const Document& doc,DisplayedTransform displayed){
    std::unordered_map<std::string,std::vector<const Layer*>> children;
    for(const auto& layer:doc.layers)children[layer.parentId].push_back(&layer);
    std::vector<Placement> result;std::unordered_set<std::string> visited;
    const auto visit=[&](auto&& self,const std::string& parent,size_t depth)->void{
        if(depth>256)throw std::invalid_argument("Transform hierarchy exceeds depth budget");
        auto it=children.find(parent);if(it==children.end())return;
        for(const auto* layer:it->second){if(!visited.insert(layer->id).second)throw std::invalid_argument("Transform hierarchy repeats layer identity");if(!layer->visible)continue;
            if(layer->group)self(self,layer->id,depth+1);else if(layer->raster)result.push_back({layer->id,displayed?displayed(*layer):layer->transform});}
    };visit(visit,"",0);return result;
}
SnapTargets collectSnapTargets(Point size,std::span<const Placement> layers,std::span<const std::string> excluded){
    SnapTargets targets{{0,size.x/2,size.x},{0,size.y/2,size.y}};
    for(const auto& layer:layers){if(std::find(excluded.begin(),excluded.end(),layer.id)!=excluded.end())continue;const auto b=bounds(layer.transform);
        for(double x:{b.x,b.x+b.width/2,b.x+b.width})targets.xs.push_back(std::round(x));
        for(double y:{b.y,b.y+b.height/2,b.y+b.height})targets.ys.push_back(std::round(y));}
    return targets;
}
std::optional<PressIntent> resolvePress(const PressContext& c,Point viewPoint,const ViewMapping& view,const std::optional<OverlayGeometry>& overlay,Modifiers flags){
    if(!c.canEdit&&!c.hasEdit)return {};
    if((c.controlsVisible||c.persistent)&&overlay){if(auto mode=overlay->hit(viewPoint)){
        if(mode->kind==ModeKind::Resize&&(flags.command||c.hasDistortion))mode->kind=ModeKind::Distort;
        return PressIntent{c.activeId,*mode,false,false};}}
    const auto pixel=view.toDocument(viewPoint);const Placement* under=nullptr;
    for(auto it=c.visibleLayers.rbegin();it!=c.visibleLayers.rend();++it)if(contains(it->transform,pixel)){under=&*it;break;}
    const auto intent=[&](std::string id,bool picked){return PressIntent{std::move(id),{ModeKind::Move},picked,flags.option};};
    const bool picks=!c.hasEdit;
    if(flags.command&&picks&&under)return intent(under->id,true);
    if(c.groupBox&&!c.activeId.empty()&&(contains(*c.groupBox,pixel)||!(picks&&c.autoSelect)||!under))return intent(c.activeId,false);
    if(c.activeTransform&&contains(*c.activeTransform,pixel))return intent(c.activeId,false);
    if(picks&&(c.autoSelect||flags.command)&&under)return intent(under->id,true);
    if(c.activeTransform&&!c.activeId.empty())return intent(c.activeId,false);
    return {};
}
}
