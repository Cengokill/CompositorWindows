#pragma once
#include "core/Document.h"
#include <algorithm>
#include <cmath>
#include <span>
#include <stdexcept>

namespace compositor::effects_tools {
inline bool validCurve(std::span<const Point> p){if(p.size()<2||p.size()>32||p.front().x!=0||p.back().x!=255)return false;for(size_t i=0;i<p.size();++i)if(!std::isfinite(p[i].x)||!std::isfinite(p[i].y)||p[i].x<0||p[i].x>255||p[i].y<0||p[i].y>255||(i&&p[i-1].x>=p[i].x))return false;return true;}
// Exact Curves.swift shape-preserving Hermite expression. Shared with raster
// effects, so the graph and rendered LUT cannot use different curve formulas.
inline double curveValueUnchecked(std::span<const Point> p,double x){size_t i=0;while(i+1<p.size()&&p[i+1].x<=x)++i;i=std::min(p.size()-2,i);auto d=[&](size_t j){return (p[j+1].y-p[j].y)/(p[j+1].x-p[j].x);};auto slope=[&](size_t j){if(j==0)return d(0);if(j==p.size()-1)return d(p.size()-2);double before=d(j-1),after=d(j);if(before*after<=0)return 0.;return 2/(1/before+1/after);};double h=p[i+1].x-p[i].x,t=std::clamp((x-p[i].x)/h,0.,1.);double y=(2*t*t*t-3*t*t+1)*p[i].y+(t*t*t-2*t*t+t)*h*slope(i)+(-2*t*t*t+3*t*t)*p[i+1].y+(t*t*t-t*t)*h*slope(i+1);return std::clamp(y,0.,255.);}
inline double curveValue(std::span<const Point> p,double x){if(!validCurve(p)||!std::isfinite(x))throw std::runtime_error("Invalid curve");return curveValueUnchecked(p,x);}
}
