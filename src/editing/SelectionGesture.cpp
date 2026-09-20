// Adapted from Selection.swift:128-228 and EditorCanvas.swift:1583-1690.
// Copyright (c) 2026 Wonder Assembly LLC; MIT notice in graphics/upstream/LICENSE.
#include "SelectionGesture.h"
#include <algorithm>
#include <cmath>

namespace compositor::editing {
namespace {
bool finite(Point p){return std::isfinite(p.x)&&std::isfinite(p.y);}
Rect pointBounds(const std::vector<Point>& points){
    if(points.empty())return {};
    double x0=points.front().x,x1=x0,y0=points.front().y,y1=y0;
    for(auto p:points){x0=std::min(x0,p.x);x1=std::max(x1,p.x);y0=std::min(y0,p.y);y1=std::max(y1,p.y);}
    return {x0,y0,x1-x0,y1-y0};
}
SelectionOutline draftOutline(const LassoDraft& draft,bool aa){
    if(draft.kind==LassoKind::Ellipse&&draft.points.size()==4)return SelectionOutline::ellipse(pointBounds(draft.points),aa);
    return SelectionOutline::polygon(draft.points,aa);
}
}
void SelectionGesture::begin(Point point,LassoKind kind,SelectionMode mode,bool shift){
    if(!finite(point))return;
    const bool box=kind==LassoKind::Rectangle||kind==LassoKind::Ellipse;
    if(box)point={std::round(point.x),std::round(point.y)};
    LassoDraft draft;draft.kind=kind;draft.mode=mode;if(box)draft.anchor=point;
    appendLassoPoint(draft.points,point);draft_=std::move(draft);
    constrainArmed_=!shift;marqueeDragPoint_.reset();
}
bool SelectionGesture::marquee()const{return draft_&&(draft_->kind==LassoKind::Rectangle||draft_->kind==LassoKind::Ellipse);}
SelectionMode SelectionGesture::cursorMode(bool shift,bool option,SelectionMode choice)const{return draft_?draft_->mode:selectionMode(shift,option,choice);}
void SelectionGesture::move(Point point,bool shift){
    if(!draft_||!finite(point))return;
    if(marquee()){
        if(!shift)constrainArmed_=true;
        marqueeDragPoint_=point;dragMarquee(point,constrainArmed_&&shift,false);
    }else if(draft_->kind==LassoKind::Polygonal)moveCursor(point);
    else extend(point);
}
void SelectionGesture::moveCursor(std::optional<Point> point){if(draft_&&(!point||finite(*point)))draft_->cursor=point;}
void SelectionGesture::modifiersChanged(bool shift){if(marquee()&&marqueeDragPoint_)move(*marqueeDragPoint_,shift);}
PolygonPressResult SelectionGesture::polygonPress(Point point,double distance,int clicks){
    if(!draft_||draft_->kind!=LassoKind::Polygonal||!finite(point))return PolygonPressResult::Ignored;
    if(clicks>=2||(draft_->points.size()>=3&&std::isfinite(distance)&&distance>=0&&distance<=8))return PolygonPressResult::FinishRequested;
    return extend(point)?PolygonPressResult::Extended:PolygonPressResult::Ignored;
}
bool SelectionGesture::extend(Point point){return draft_&&appendLassoPoint(draft_->points,point);}
void SelectionGesture::dragMarquee(Point point,bool square,bool fromCenter){
    if(!marquee()||!draft_->anchor||!finite(point))return;
    const auto r=dragBox(*draft_->anchor,point,square,fromCenter);
    draft_->points={{r.x,r.y},{r.x+r.width,r.y},{r.x+r.width,r.y+r.height},{r.x,r.y+r.height}};
}
void SelectionGesture::removeLast(){if(!draft_)return;draft_->points.pop_back();if(draft_->points.empty())cancel();}
void SelectionGesture::cancel(){draft_.reset();marqueeDragPoint_.reset();constrainArmed_=false;}
void SelectionGesture::interrupt(){if(draft_&&draft_->kind!=LassoKind::Polygonal)cancel();}
SelectionOutline SelectionGesture::outline(bool aa)const{return draft_?draftOutline(*draft_,aa):SelectionOutline();}
std::optional<SelectionGestureResult> SelectionGesture::finish(const std::optional<SelectionOutline>& current,int width,int height,bool aa){
    if(!draft_)return {};
    const auto saved=std::move(*draft_);cancel();
    const auto shape=draftOutline(saved,aa);
    if((saved.points.size()<3&&saved.kind!=LassoKind::Ellipse)||shape.bounds().empty()){
        if(saved.mode==SelectionMode::Replace)return SelectionGestureResult{{},"Deselect",current.has_value()};
        return SelectionGestureResult{current,{},false};
    }
    if(!current&&(saved.mode==SelectionMode::Subtract||saved.mode==SelectionMode::Intersect))return SelectionGestureResult{current,{},false};
    const auto name=saved.kind==LassoKind::Freehand?"Lasso":saved.kind==LassoKind::Polygonal?"Polygonal Lasso":saved.kind==LassoKind::Ellipse?"Elliptical Marquee":"Rectangular Marquee";
    return SelectionGestureResult{applySelection(current,shape,saved.mode,width,height,aa),name,true};
}
Point selectionMoveOffset(Point start,Point current,bool shift){
    if(!finite(start)||!finite(current))return {};
    Point offset{current.x-start.x,current.y-start.y};
    if(shift){if(std::abs(offset.x)>=std::abs(offset.y))offset.y=0;else offset.x=0;}
    return offset;
}
Point selectionAutoscrollDelta(Rect visible,Point point){
    if(!finite(point)||!finite({visible.x,visible.y})||!finite({visible.width,visible.height})||visible.empty())return {};
    const auto speed=[](double past){return past<=0?0:std::min(40.,2+past*.4);};
    return {speed(visible.x+12-point.x)-speed(point.x-(visible.x+visible.width-12)),
        speed(visible.y+12-point.y)-speed(point.y-(visible.y+visible.height-12))};
}
}
