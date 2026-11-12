// Based on RimLight.swift and MetalRimLight.swift at upstream 97c3b3f.
// Copyright (c) 2026 Wonder Assembly LLC. MIT; see Compositor-MIT.txt.
#include "RimLight.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace compositor::filters {
namespace {
double bounded(double v,double low,double high,double fallback){return std::isfinite(v)?std::clamp(v,low,high):fallback;}
void check(const std::function<bool()>& cancelled){if(cancelled&&cancelled())throw std::runtime_error("Filter cancelled");}
float smooth(float a,float b,float x){const float t=std::clamp((x-a)/(b-a),0.f,1.f);return t*t*(3-2*t);}
// Three sliding box passes approximate a Gaussian without a radius-dependent cost.
std::vector<float> blur(std::vector<float> image,int w,int h,double sigma,const std::function<bool()>& cancelled){
    if(sigma<.15)return image;
    const int radius=std::max(1,int(std::round((std::sqrt(4*sigma*sigma+1)-1)/2)));
    std::vector<float> scratch(image.size());
    const double inv=1.0/(2*radius+1);
    for(int pass=0;pass<3;++pass){
        for(int y=0;y<h;++y){check(cancelled);double sum=0;for(int x=-radius;x<=radius;++x)sum+=image[size_t(y)*w+std::clamp(x,0,w-1)];
            for(int x=0;x<w;++x){scratch[size_t(y)*w+x]=float(sum*inv);sum-=image[size_t(y)*w+std::clamp(x-radius,0,w-1)];sum+=image[size_t(y)*w+std::clamp(x+radius+1,0,w-1)];}}
        for(int x=0;x<w;++x){check(cancelled);double sum=0;for(int y=-radius;y<=radius;++y)sum+=scratch[size_t(std::clamp(y,0,h-1))*w+x];
            for(int y=0;y<h;++y){image[size_t(y)*w+x]=float(sum*inv);sum-=scratch[size_t(std::clamp(y-radius,0,h-1))*w+x];sum+=scratch[size_t(std::clamp(y+radius+1,0,h-1))*w+x];}}
    }
    return image;
}
float inverseError(float x){
    float w=-std::log((1-x)*(1+x)),p;
    if(w<5){w-=2.5f;p=2.81022636e-08f;p=3.43273939e-07f+p*w;p=-3.5233877e-06f+p*w;p=-4.39150654e-06f+p*w;p=.00021858087f+p*w;p=-.00125372503f+p*w;p=-.00417768164f+p*w;p=.246640727f+p*w;p=1.50140941f+p*w;}
    else{w=std::sqrt(w)-3;p=-.000200214257f;p=.000100950558f+p*w;p=.00134934322f+p*w;p=-.00367342844f+p*w;p=.00573950773f+p*w;p=-.0076224613f+p*w;p=.00943887047f+p*w;p=1.00167406f+p*w;p=2.83297682f+p*w;}
    return p*x;
}
float linear(float x){return x<=.04045f?x/12.92f:std::pow((x+.055f)/1.055f,2.4f);}
float gamma(float x){return x<=.0031308f?12.92f*x:1.055f*std::pow(x,1/2.4f)-.055f;}
}
RimLightSettings RimLightSettings::normalized()const{
    auto out=*this;out.count=std::clamp(count,0,3);out.glow=bounded(glow,0,100,10);
    for(auto& light:out.lights){light.angle=bounded(light.angle,-180,180,135);light.strength=bounded(light.strength,0,100,50);light.width=bounded(light.width,0,100,30);light.choke=bounded(light.choke,0,100,0);for(auto& c:light.color)c=bounded(c,0,1,1);}
    return out;
}
bool RimLightSettings::isIdentity()const{const auto s=normalized();for(int i=0;i<s.count;++i)if(s.lights[i].strength>0)return false;return true;}

std::shared_ptr<const Raster> renderRimLight(const Raster& source,const GrayRaster& mask,const RimLightSettings& raw,std::uint64_t maxBytes,const std::function<bool()>& cancelled){
    const int w=source.width,h=source.height;
    if(w<1||h<1||w>30000||h>30000)throw std::runtime_error("Invalid Rim Light image or subject mask");
    const auto n=std::uint64_t(w)*h;
    if(n>100000000||mask.width!=w||mask.height!=h||mask.pixels.size()!=n)throw std::runtime_error("Invalid Rim Light image or subject mask");
    if(n>maxBytes/160)throw std::runtime_error("Rim Light exceeds transient allocation budget");
    check(cancelled);const auto s=raw.normalized();auto bytes=source.rgba();if(s.isIdentity())return Raster::fromRgba(w,h,bytes.data(),size_t(w)*4);
    const float side=float(std::max(w,h));std::vector<float> outline(n),gray(n),open(n);
    for(size_t i=0;i<n;++i){outline[i]=std::clamp(mask.pixels[i]/255.f*1.6f-.3f,0.f,1.f);open[i]=1-outline[i];const auto a=bytes[4*i+3];gray[i]=a?(.2126f*bytes[4*i]+.7152f*bytes[4*i+1]+.0722f*bytes[4*i+2])/a:0;}
    auto fine=blur(gray,w,h,1,cancelled),coarse=blur(gray,w,h,std::max(2.f,side/400),cancelled);
    auto near=blur(outline,w,h,side*.015,cancelled),weight=blur(open,w,h,side*.01,cancelled);
    std::array<std::vector<float>,3> backdrop,variance;
    for(int c=0;c<3;++c){std::vector<float> sum(n),squares(n);for(size_t i=0;i<n;++i){const float a=bytes[4*i+3]/255.f;const float v=a>0?bytes[4*i+c]/255.f/a:0;sum[i]=v*open[i];squares[i]=v*v*open[i];}sum=blur(std::move(sum),w,h,side*.01,cancelled);squares=blur(std::move(squares),w,h,side*.01,cancelled);backdrop[c].resize(n);variance[c].resize(n);for(size_t i=0;i<n;++i){const float mean=sum[i]/(weight[i]+.001f);const float a=bytes[4*i+3]/255.f;const float v=a>0?bytes[4*i+c]/255.f/a:0;backdrop[c][i]=std::abs(v-mean);variance[c][i]=std::max(0.f,squares[i]/(weight[i]+.001f)-mean*mean);}}
    std::array<std::vector<float>,3> light{std::vector<float>(n),std::vector<float>(n),std::vector<float>(n)};
    auto at=[w,h](const std::vector<float>& v,int x,int y){return v[size_t(std::clamp(y,0,h-1))*w+std::clamp(x,0,w-1)];};
    for(int l=0;l<s.count;++l){const auto& v=s.lights[l];if(v.strength==0)continue;const float width=side*float(.002+v.width/100*.03),reach=std::max(1.f,width/1.5f);auto form=blur(outline,w,h,reach,cancelled);const double angle=v.angle*std::numbers::pi/180;const float dx=float(std::cos(angle)),dy=float(std::sin(angle));
        for(int y=0;y<h;++y){check(cancelled);for(int x=0;x<w;++x){const size_t i=size_t(y)*w+x;const float alpha=bytes[4*i+3]/255.f;if(alpha<=0)continue;
            float ix=at(form,x+1,y)-at(form,x-1,y),iy=at(form,x,y-1)-at(form,x,y+1),len=std::hypot(ix,iy);if(len>1e-6f){ix/=len;iy/=len;}
            const float depth=reach*1.41421356f*inverseError(2*std::clamp(form[i],.001f,.999f)-1),t=std::max(depth,0.f)/width;
            const float choke=float(v.choke/100),held=1-smooth(std::max(0.f,(choke-.3f)/.7f)*.95f,1,t),tail=std::exp(-2.2f*t),reaches=(choke<.3f?tail+(held-tail)*choke/.3f:held)*(1-smooth(1.5f,1.9f,t));
            const auto detail=[&](int sx,int sy){return at(fine,sx,sy)-at(coarse,sx,sy);};
            const float nx=-ix*2.75f-(detail(x+1,y)-detail(x-1,y))*.5f*side/250,ny=-iy*2.75f-(detail(x,y-1)-detail(x,y+1))*.5f*side/250;
            const float facing=std::pow(std::max(0.f,(nx*dx+ny*dy-.2f)/std::sqrt(nx*nx+ny*ny+1)/std::sqrt(1.04f)),1.6f);
            const float inside=smooth(-1.5f,1.5f,depth),catches=std::clamp(1+(fine[i]-coarse[i])*6,.3f,1.8f);float differs=0,varies=0;for(int c=0;c<3;++c){differs=std::max(differs,backdrop[c][i]);varies=std::max(varies,variance[c][i]);}
            const float strand=std::clamp((differs-.03f-2.5f*std::sqrt(varies))/.1f,0.f,1.f)*smooth(0,.1f,near[i])*(1-inside);
            float ax=at(near,x+1,y)-at(near,x-1,y),ay=at(near,x,y-1)-at(near,x,y+1),alen=std::hypot(ax,ay);if(alen>1e-6f){ax/=alen;ay/=alen;}ax*=-2.75f;ay*=-2.75f;
            const float strandFacing=std::pow(std::max(0.f,(ax*dx+ay*dy-.2f)/std::sqrt(ax*ax+ay*ay+1)/std::sqrt(1.04f)),1.6f);
            const float strength=float(v.strength/100*10)*(facing*reaches*catches*(mask.pixels[i]/255.f)*inside+strand*strandFacing);
            for(int c=0;c<3;++c){const float base=bytes[4*i+c]/255.f/alpha;light[c][i]+=linear(float(v.color[c]))*strength*(linear(base)*.7f+.45f)*alpha;}
        }}
    }
    if(s.glow>0)for(int c=0;c<3;++c){auto halo=blur(light[c],w,h,side*.012,cancelled),haze=blur(light[c],w,h,side*.035,cancelled);auto around=blur(outline,w,h,side*.008,cancelled);for(size_t i=0;i<n;++i)light[c][i]+=(halo[i]*.6f+haze[i])*float(s.glow/100*4)*std::clamp(2-2*around[i],0.f,1.f);}
    for(int y=0;y<h;++y){check(cancelled);for(int x=0;x<w;++x){size_t i=size_t(y)*w+x;const float a=bytes[4*i+3]/255.f;if(a<=0)continue;for(int c=0;c<3;++c){float base=linear(bytes[4*i+c]/255.f/a),mixed=1-(1-base)*std::exp(-std::max(0.f,light[c][i]));bytes[4*i+c]=std::uint8_t(std::clamp(std::lround(gamma(mixed)*a*255),0L,long(bytes[4*i+3])));}}}
    return Raster::fromRgba(w,h,bytes.data(),size_t(w)*4,cancelled);
}
}
