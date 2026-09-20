#pragma once
// Source: Rendering/CanvasViewport.swift (MIT, graphics/upstream/LICENSE).
#include "core/Document.h"
#include <algorithm>
#include <cmath>

namespace compositor::graphics {
struct CanvasViewport {
    Point viewSize{},pan{};
    double backingScale{1},zoom{1}; // Zoom1 = one physical display pixel per image pixel.
    bool followsFit{true};
    double pointsPerPixel()const{return zoom/backingScale;}
    Point center()const{return {viewSize.x/2,viewSize.y/2};}
    Point origin(Point documentSize)const{const auto c=center();const auto scale=pointsPerPixel();return {c.x-documentSize.x*scale/2+pan.x,c.y-documentSize.y*scale/2+pan.y};}
    Point documentPoint(Point point,Point documentSize)const{const auto o=origin(documentSize);const auto scale=pointsPerPixel();return {(point.x-o.x)/scale,(point.y-o.y)/scale};}
    Point viewPoint(Point point,Point documentSize)const{const auto o=origin(documentSize);const auto scale=pointsPerPixel();return {o.x+point.x*scale,o.y+point.y*scale};}
    void fit(Point documentSize){
        if(viewSize.x<=0||viewSize.y<=0||documentSize.x<=0||documentSize.y<=0){followsFit=true;return;}
        zoom=std::clamp(std::min(std::max(1.,viewSize.x-96)/documentSize.x,std::max(1.,viewSize.y-96)/documentSize.y)*backingScale,.001,32.);
        pan={};followsFit=true;
    }
    void resize(Point size,double newBackingScale,std::optional<Point> documentSize={}){
        const auto oldScale=pointsPerPixel();viewSize=size;backingScale=std::isfinite(newBackingScale)?std::max(1.,newBackingScale):1;
        if(followsFit&&documentSize)fit(*documentSize);
        else {const double ratio=pointsPerPixel()/oldScale;pan.x*=ratio;pan.y*=ratio;}
    }
    void setZoom(double value,Point anchor,Point documentSize){
        if(!std::isfinite(value))return;
        const auto pixel=documentPoint(anchor,documentSize);zoom=std::clamp(value,.001,32.);
        const auto moved=viewPoint(pixel,documentSize);pan.x+=anchor.x-moved.x;pan.y+=anchor.y-moved.y;followsFit=false;
    }
    void translate(Point delta){pan.x+=delta.x;pan.y+=delta.y;followsFit=false;}
};
}
