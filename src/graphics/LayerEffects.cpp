#include "LayerEffects.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace compositor::graphics {
namespace {
uint8_t byte(double n){return uint8_t(std::clamp(std::lround(n),0L,255L));}
bool near(const Transform& t,Point p,double pad){
    if(!(t.width>0)||!(t.height>0))return false;
    auto u=t.toUnit(p);double px=pad/t.width,py=pad/t.height;
    return u.x>=-px&&u.x<=1+px&&u.y>=-py&&u.y<=1+py;
}
Point offsetOf(double angle,double distance){double a=angle*std::numbers::pi/180;return {std::cos(a)*distance,std::sin(a)*distance};}
double blurredAlpha(const std::function<Pixel(Point)>& sample,Point p,double radius){
    int r=std::clamp(int(std::ceil(radius)),0,12);
    if(r==0)return sample(p).a/255.;
    double sum=0,weight=0;
    for(int y=-r;y<=r;++y)for(int x=-r;x<=r;++x){double d=std::hypot(double(x),double(y));if(d>r)continue;double w=1-d/(r+1.);sum+=sample({p.x+x,p.y+y}).a/255.*w;weight+=w;}
    return weight>0?sum/weight:0;
}
Pixel colored(Pixel color,double alpha){double a=std::clamp(alpha,0.,1.);return {byte(color.r*a),byte(color.g*a),byte(color.b*a),byte(255*a)};}
}
EffectSample shadeLayer(const Layer& layer,Point p,Pixel content,double coverage,const std::function<Pixel(Point)>& shaped){
    EffectSample out;out.content=content;if(!layer.effects.active())return out;
    if(!near(layer.transform,p,80)&&content.a==0)return out;
    const auto& fx=layer.effects;coverage=std::clamp(coverage,0.,1.);
    if(fx.dropShadow.enabled){auto delta=offsetOf(fx.dropShadow.angle,fx.dropShadow.distance);double a=blurredAlpha(shaped,{p.x-delta.x,p.y-delta.y},fx.dropShadow.size)*fx.dropShadow.opacity*coverage;out.under=colored(fx.dropShadow.color,a);out.underBlend=fx.dropShadow.blend;out.hasUnder=out.under.a>0;}
    if(fx.outerGlow.enabled){double outside=1-content.a/255.;double a=blurredAlpha(shaped,p,fx.outerGlow.size)*fx.outerGlow.opacity*coverage*outside;out.glow=colored(fx.outerGlow.color,a);out.glowBlend=fx.outerGlow.blend;out.hasGlow=out.glow.a>0;}
    if(fx.stroke.enabled&&fx.stroke.size>0){
        int r=std::clamp(int(std::ceil(fx.stroke.size)),1,24);double here=shaped(p).a/255.,around=0;
        int step=std::max(1,r/2);for(int y=-r;y<=r;y+=step)for(int x=-r;x<=r;x+=step)around=std::max(around,shaped({p.x+x,p.y+y}).a/255.);
        double edge=fx.stroke.position==2?std::clamp(here*(1-around),0.,1.):fx.stroke.position==0?std::clamp(around-here,0.,1.):std::clamp(std::abs(around-here),0.,1.);
        auto stroke=colored(fx.stroke.color,edge*coverage);
        if(fx.stroke.position==0){out.under=blendPixel(out.under,stroke,Blend::Normal);out.hasUnder=out.under.a>0;}
        else out.content=blendPixel(out.content,stroke,Blend::Normal);
    }
    if(fx.colorOverlay.enabled&&content.a){auto tint=colored(fx.colorOverlay.color,content.a/255.*fx.colorOverlay.opacity);out.content=blendPixel(out.content,tint,fx.colorOverlay.blend);}
    if(fx.innerShadow.enabled&&content.a){auto delta=offsetOf(fx.innerShadow.angle,fx.innerShadow.distance);double inside=shaped(p).a/255.;double toward=blurredAlpha(shaped,{p.x-delta.x,p.y-delta.y},fx.innerShadow.size);double a=inside*(1-toward)*fx.innerShadow.opacity*coverage;out.content=blendPixel(out.content,colored(fx.innerShadow.color,a),fx.innerShadow.blend);}
    if(fx.innerGlow.enabled&&content.a){double a=(1-blurredAlpha(shaped,p,std::max(1.,fx.innerGlow.size)))*fx.innerGlow.opacity*(content.a/255.)*coverage;out.content=blendPixel(out.content,colored(fx.innerGlow.color,a),fx.innerGlow.blend);}
    return out;
}
}
