// Behavior translated from Compositor a19db9011282399785dc18efcfded904627bdcc2
// Filters.swift and ContentFill.swift. Copyright (c) 2026 Wonder Assembly LLC.
// MIT license retained in dependencies/imaging/notices/Compositor-MIT.txt.
#include "PixelFilters.h"
#include <type_traits>
#include "CameraRaw.h"
#include "graphics/ParallelBatch.h"
#include "graphics/PixelAlgorithms.h"
extern "C" {
#include "graphics/upstream/CameraRawPixels.h"
}
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <type_traits>
#include <stdexcept>
namespace compositor::filters {
std::uint64_t estimatedWorkingBytes(int w,int h,std::uint64_t bytesPerPixel){
    const auto n=std::uint64_t(w)*h;
    const auto tiles=std::uint64_t((w+Raster::tileSide-1)/Raster::tileSide)*((h+Raster::tileSide-1)/Raster::tileSide);
    return n*bytesPerPixel+tiles*sizeof(Raster::Tile);
}
namespace {
void cancel(const Limits& l){if(l.cancelled&&l.cancelled())throw std::runtime_error("Filter cancelled");}
void gateSize(int w,int h){if(w<1||h<1||w>30000||h>30000||std::uint64_t(w)*h>100000000)throw std::runtime_error("Filter exceeds the 100-megapixel or 30000-pixel-side budget");}
std::size_t count(int w,int h,const Limits& l,std::uint64_t bytesPerPixel=24){
    gateSize(w,h);const auto estimated=estimatedWorkingBytes(w,h,bytesPerPixel);
    if(estimated>l.maxWorkingBytes)throw std::runtime_error("Filter exceeds transient allocation budget");return std::size_t(std::uint64_t(w)*h);
}
void checkSelection(const GrayRaster& m){if(m.width<1||m.height<1||m.width>30000||m.height>30000||std::uint64_t(m.width)*m.height>100000000||m.pixels.size()!=std::size_t(m.width)*m.height)throw std::runtime_error("Invalid source selection storage");}
bool empty(const GrayRaster& m){return std::none_of(m.pixels.begin(),m.pixels.end(),[](auto p){return p!=0;});}
double normalized(double value,double low,double high,double fallback){return std::isfinite(value)?std::clamp(value,low,high):fallback;}
void kindCheck(Kind k){switch(k){case Kind::GaussianBlur:case Kind::MotionBlur:case Kind::AddNoise:case Kind::LensCorrection:case Kind::ContentAwareFill:case Kind::Vignette:case Kind::Bloom:case Kind::TonalContrast:case Kind::Dither:case Kind::Scanlines:case Kind::CameraRaw:case Kind::ProgressiveBlur:case Kind::RgbSplit:case Kind::LensEffects:return;}throw std::runtime_error("Unsupported filter kind");}
bool spreads(Kind k){return k==Kind::GaussianBlur||k==Kind::MotionBlur||k==Kind::ProgressiveBlur||k==Kind::RgbSplit;}
graphics::Rgba8View view(std::vector<std::uint8_t>& v,int w,int h){return {v,std::uint32_t(w),std::uint32_t(h),std::size_t(w)*4};}
std::uint8_t byte(double x){return std::uint8_t(std::clamp(std::floor(x+.5),0.,255.));}
std::vector<double> gaussianWeights(double sigma){int radius=std::max(1,int(std::ceil(sigma*3)));std::vector<double> weights(std::size_t(radius)*2+1);double total=0;for(int t=-radius;t<=radius;++t)total+=(weights[t+radius]=std::exp(-double(t*t)/(2*sigma*sigma)));for(auto& weight:weights)weight/=total;return weights;}
template<class T> T blurValue(double value){if constexpr(std::is_same_v<T,uint8_t>)return byte(value);else return T(value);}
int along(int pos,int length,bool clampLow,bool clampHigh){
    if(pos<0)return clampLow?0:-1;
    if(pos>=length)return clampHigh?length-1:-1;
    return pos;
}
template<class T> std::vector<T> gaussian(const std::vector<T>& source,int w,int h,double sigma,const Limits& limits,LayerSides clamp={}){
    auto weights=gaussianWeights(sigma);int r=int(weights.size()/2);std::vector<float> pass(std::size_t(w)*h);std::vector<T> output(source.size());
    // Transparent exterior except on canvas-touching sides, which repeat the edge pixel.
    for(int c=0;c<4;++c){for(int y=0;y<h;++y){cancel(limits);for(int x=0;x<w;++x){if((x&255)==0)cancel(limits);double sum=0;for(int k=-r;k<=r;++k){const int xx=along(x+k,w,clamp.left,clamp.right);if(xx>=0)sum+=source[(std::size_t(y)*w+xx)*4+c]*weights[k+r];}pass[std::size_t(y)*w+x]=float(sum);}}
        for(int y=0;y<h;++y){cancel(limits);for(int x=0;x<w;++x){if((x&255)==0)cancel(limits);double sum=0;for(int k=-r;k<=r;++k){const int yy=along(y+k,h,clamp.top,clamp.bottom);if(yy>=0)sum+=pass[std::size_t(yy)*w+x]*weights[k+r];}output[(std::size_t(y)*w+x)*4+c]=blurValue<T>(sum);}}}
    return output;
}
template<class Sum=float> void boxPass(std::vector<float>& a,std::vector<float>& b,int w,int h,bool horizontal,int radius,LayerSides clamp,const Limits& limits){
    const int lines=horizontal?h:w,length=horizontal?w:h;
    const float inv=1.f/float(radius*2+1);
    const bool lo=horizontal?clamp.left:clamp.top,hi=horizontal?clamp.right:clamp.bottom;
    graphics::runParallelBatch(std::size_t(lines),0,[&](std::size_t lineIndex){
        cancel(limits);
        const int line=int(lineIndex);
        auto at=[&](int pos,int c)->float{
            const int i=along(pos,length,lo,hi);
            if(i<0)return 0.f;
            const int x=horizontal?i:line,y=horizontal?line:i;
            return a[(std::size_t(y)*w+x)*4+c];
        };
        Sum sum[4]{};
        for(int k=-radius;k<=radius;++k)for(int c=0;c<4;++c)sum[c]+=at(k,c);
        for(int pos=0;pos<length;++pos){
            float* dst=&b[(std::size_t(horizontal?line:pos)*w+(horizontal?pos:line))*4];
            for(int c=0;c<4;++c)dst[c]=float(sum[c]*inv);
            for(int c=0;c<4;++c){sum[c]-=at(pos-radius,c);sum[c]+=at(pos+radius+1,c);}
        }
    });
    a.swap(b);
}
template<class Sum=float> void fastGaussianFloat(std::vector<float>& a,std::vector<float>& b,int w,int h,double sigma,const Limits& limits,LayerSides clamp){
    if(sigma<.15)return;
    constexpr int passes=3;
    const double ideal=std::sqrt(12*sigma*sigma/passes+1);
    int small=std::max(1,int(std::floor(ideal)));if((small&1)==0)--small;
    const int large=small+2;
    const int nSmall=std::clamp(int(std::lround((12*sigma*sigma-passes*small*small-4*passes*small-3*passes)/(-4.*small-4))),0,passes);
    for(int i=0;i<passes;++i){const int radius=((i<nSmall?small:large)-1)/2;boxPass<Sum>(a,b,w,h,true,radius,clamp,limits);boxPass<Sum>(a,b,w,h,false,radius,clamp,limits);}
}
template<class T,class Sum=float> std::vector<T> fastGaussian(const std::vector<T>& source,int w,int h,double sigma,const Limits& limits,LayerSides clamp={}){
    if(sigma<.15)return source;
    std::vector<float> a(source.begin(),source.end()),b(source.size());
    fastGaussianFloat<Sum>(a,b,w,h,sigma,limits,clamp);
    std::vector<T> output(source.size());for(std::size_t i=0;i<source.size();++i)output[i]=blurValue<T>(a[i]);return output;
}
template<class T> std::vector<T> progressive(const std::vector<T>& source,int w,int h,const Settings& s,double scale,const Limits& limits,LayerSides clamp={}){
    const double strongest=std::max(s.progressiveFrom,s.progressiveTo)*scale;
    if(strongest<=0)return source;
    const Point start=s.progressiveStart.value_or(Point{w/2.,0}),end=s.progressiveEnd.value_or(Point{w/2.,double(h)});
    double dx=end.x-start.x,dy=end.y-start.y,len2=dx*dx+dy*dy;
    if(len2<1){dx=0;dy=1;len2=1;}
    const double from=s.progressiveInverted?s.progressiveTo:s.progressiveFrom;
    const double to=s.progressiveInverted?s.progressiveFrom:s.progressiveTo;
    std::vector<float> lower(source.begin(),source.end()),upper=lower,scratch(lower.size()),mixed=lower;
    if(std::abs(from-to)*scale<.15){
        fastGaussianFloat(mixed,scratch,w,h,strongest,limits,clamp);
        std::vector<T> output(source.size());for(std::size_t i=0;i<mixed.size();++i)output[i]=blurValue<T>(mixed[i]);return output;
    }
    constexpr int levels=8;
    std::vector<double> amount(std::size_t(w)*h);
    const double length=std::sqrt(len2);
    graphics::runParallelBatch(std::size_t(h),0,[&](std::size_t row){
        cancel(limits);const int y=int(row);
        for(int x=0;x<w;++x){
            const double px=x+.5-start.x,py=y+.5-start.y;
            const double t=std::clamp(s.progressiveRadial?std::hypot(px,py)/length:(px*dx+py*dy)/len2,0.,1.);
            amount[std::size_t(y)*w+x]=std::clamp((from+(to-from)*t)*scale,0.,strongest);
        }
    });
    double prev=0;
    for(int level=1;level<=levels;++level){
        cancel(limits);
        const double high=strongest*std::pow(double(level)/levels,2),low=strongest*std::pow(double(level-1)/levels,2);
        fastGaussianFloat(upper,scratch,w,h,std::sqrt(std::max(0.,high*high-prev*prev)),limits,clamp);
        graphics::runParallelBatch(std::size_t(h),0,[&](std::size_t row){
            cancel(limits);const int y=int(row);
            for(int x=0;x<w;++x){
                const double radius=amount[std::size_t(y)*w+x];
                if(radius<low||(level<levels&&radius>high)||(level>1&&radius==low))continue;
                const double span=high*high-low*low;
                const double weight=span>0?std::clamp((radius*radius-low*low)/span,0.,1.):1.;
                const auto i=(std::size_t(y)*w+x)*4;
                for(int c=0;c<4;++c)mixed[i+c]=float(lower[i+c]+(upper[i+c]-lower[i+c])*weight);
            }
        });
        lower=upper;prev=high;
    }
    std::vector<T> output(source.size());for(std::size_t i=0;i<mixed.size();++i)output[i]=blurValue<T>(mixed[i]);return output;
}
template<class T> double sample(const std::vector<T>& p,int w,int h,double x,double y,int channel,LayerSides clamp){
    int ix=int(std::floor(x)),iy=int(std::floor(y));double fx=x-ix,fy=y-iy;auto get=[&](int xx,int yy){xx=along(xx,w,clamp.left,clamp.right);yy=along(yy,h,clamp.top,clamp.bottom);return xx<0||yy<0?0.:double(p[(std::size_t(yy)*w+xx)*4+channel]);};return (get(ix,iy)*(1-fx)+get(ix+1,iy)*fx)*(1-fy)+(get(ix,iy+1)*(1-fx)+get(ix+1,iy+1)*fx)*fy;
}
template<class T> std::vector<T> motion(const std::vector<T>& source,int w,int h,double sigma,double angle,const Limits& limits,LayerSides clamp={}){
    auto weights=gaussianWeights(sigma);int r=int(weights.size()/2);const double a=angle*std::numbers::pi/180,dx=std::cos(a),dy=-std::sin(a);std::vector<T> out(source.size());
    for(int y=0;y<h;++y){cancel(limits);for(int x=0;x<w;++x){if((x&63)==0)cancel(limits);std::array<double,4> sum{};for(int k=-r;k<=r;++k)for(int c=0;c<4;++c)sum[c]+=sample(source,w,h,x+k*dx,y+k*dy,c,clamp)*weights[k+r];for(int c=0;c<4;++c)out[(std::size_t(y)*w+x)*4+c]=blurValue<T>(sum[c]);}}return out;
}
// Behavioral reference: upstream LensEffects.swift v1.5.8, blob
// 2a519f5ce8fd0e504eae78b065a1e70891fa124c (MIT, notice above).
// Software implementation of the v1.5.8 optical behavior. Fixed quadrature
// bounds work independently of radius; Core Image raster parity is unverified.
std::vector<float> lensPixels(const std::vector<float>& input,int w,int h,const LensEffectSettings& raw,const Limits& limits){
    const auto s=raw.normalized();if(s.isIdentity())return input;
    count(w,h,limits,192);const double side=std::max(w,h);
    auto linear=[](double v){return v<=.04045?v/12.92:std::pow((v+.055)/1.055,2.4);};
    auto gamma=[](double v){v=std::max(0.,v);return v<=.0031308?v*12.92:1.055*std::pow(v,1/2.4)-.055;};
    std::vector<float> light(input.size()),high(input.size()),result;
    for(int y=0;y<h;++y){cancel(limits);for(int x=0;x<w;++x){
        auto i=(std::size_t(y)*w+x)*4;double a=input[i+3];
        double lum=a>0?(.2126*input[i]+.7152*input[i+1]+.0722*input[i+2])/a:0;
        double t=s.effect==LensEffect::Bokeh?.8:s.effect==LensEffect::Mist?.75:std::min(.95,s.threshold/100);
        double weight=std::clamp((lum-t)/(1-t),0.,1.);weight=weight*weight*(3-2*weight);
        for(int c=0;c<3;++c){light[i+c]=float(a>0?linear(input[i+c]/a)*a:0);high[i+c]=float(light[i+c]*weight);}light[i+3]=high[i+3]=1;
    }}
    if(s.threshold>95&&(s.effect==LensEffect::AnamorphicFlare||s.effect==LensEffect::Starburst)){
        auto density=fastGaussian(high,w,h,side*.03*(s.threshold-95)/5,limits,{true,true,true,true});double peak=0;
        for(std::size_t i=0;i<density.size();i+=4)peak=std::max(peak,.2126*density[i]+.7152*density[i+1]+.0722*density[i+2]);
        if(peak>.001)for(int y=0;y<h;++y){cancel(limits);for(int x=0;x<w;++x){auto i=(std::size_t(y)*w+x)*4;double v=(.2126*density[i]+.7152*density[i+1]+.0722*density[i+2])/peak,cut=.25+.6*(s.threshold-95)/5;double weight=std::clamp((v-cut+.05)/.1,0.,1.);for(int c=0;c<3;++c)high[i+c]*=float(weight);}}
    }
    result=light;
    if(s.effect==LensEffect::Prism){
        double theta=s.prismAngle*std::numbers::pi/180,dx=std::cos(theta),dy=-std::sin(theta);
        for(int copy=int(s.prismCopies);copy>=1;--copy){
            std::vector<float> ghost(input.size());
            for(int way:{1,-1}){if(way==-1&&!s.prismBothWays)continue;
                for(int y=0;y<h;++y){cancel(limits);for(int x=0;x<w;++x)for(int c=0;c<3;++c){double shift=way*side*(s.prismOffset*copy/100+(1-c)*.003);auto i=(std::size_t(y)*w+x)*4+c;ghost[i]=std::max(ghost[i],float(sample(light,w,h,x-dx*shift,y-dy*shift,c,{})));}}
            }
            if(s.prismBlur>0)ghost=fastGaussian(ghost,w,h,side*s.prismBlur/10000*copy,limits,{true,true,true,true});
            double fade=std::pow(.78,copy-1)*s.prismAmount/100;
            for(int y=0;y<h;++y){cancel(limits);for(int x=0;x<w;++x)for(int c=0;c<3;++c){auto i=(std::size_t(y)*w+x)*4+c;result[i]=std::max(result[i],float(ghost[i]*fade));}}
        }
    }else if(s.effect==LensEffect::Mist){
        double size=side*(.2+s.mistSize/100*1.8)/100,strength=s.mistStrength/100,scatter=strength*.4;
        auto near=fastGaussian(light,w,h,size*.6,limits,{true,true,true,true});auto far=fastGaussian(light,w,h,size*4,limits,{true,true,true,true});auto glow=fastGaussian(high,w,h,size*1.5,limits,{true,true,true,true});
        for(int y=0;y<h;++y){cancel(limits);for(int x=0;x<w;++x)for(int c=0;c<3;++c){auto i=(std::size_t(y)*w+x)*4+c;result[i]=float(light[i]*(1-scatter)+near[i]*scatter*.6+far[i]*scatter*.4+glow[i]*strength*.35);}}
    }else if(s.effect==LensEffect::Bokeh){
        double radius=side*s.bokehRadius/100,ring=s.bokehRing/100;constexpr int samples=96;
        for(int y=0;y<h;++y){cancel(limits);for(int x=0;x<w;++x){double sum[3]{},bright[3]{},total=0;
            for(int k=0;k<samples;++k){double theta=k*2.399963229728653,rad=std::sqrt((k+.5)/samples);
                if(s.bokehShape==BokehShape::Hexagon)rad*=std::cos(std::numbers::pi/6)/std::cos(std::remainder(theta,std::numbers::pi/3));
                double xx=std::cos(theta)*rad,yy=std::sin(theta)*rad;
                if(s.bokehShape==BokehShape::Oval)xx*=.5;
                if(s.bokehShape==BokehShape::CatsEye)xx*=1-.65*std::min(1.,std::hypot((x+.5-w/2.)/(w/2.),(y+.5-h/2.)/(h/2.)));
                double weight=1+ring*4*std::pow(rad,8);total+=weight;
                for(int c=0;c<3;++c){sum[c]+=sample(light,w,h,x+xx*radius,y+yy*radius,c,{true,true,true,true})*weight;bright[c]+=sample(high,w,h,x+xx*radius,y+yy*radius,c,{true,true,true,true})*weight;}
            }
            auto i=(std::size_t(y)*w+x)*4;for(int c=0;c<3;++c){double b=bright[c]/total*(12+s.bokehBrightness*.7);result[i+c]=float(sum[c]/total+.8*(1-std::exp(-b/.8)));}
        }}
    }else{
        bool flare=s.effect==LensEffect::AnamorphicFlare;int spokes=flare?1:s.starPoints/2;double amount=(flare?s.flareAmount:s.starAmount)/50;
        for(int spoke=0;spoke<spokes;++spoke){double theta=flare?0:(s.starAngle+spoke*180./spokes)*std::numbers::pi/180,reach=side*(flare?s.flareLength:s.starLength)/100;
            std::array<double,65> weights{};std::array<std::array<double,65>,3> dx{},dy{};double total=0;
            for(int k=-32;k<=32;++k){const double t=k/32.,weight=std::exp(-4.5*t*t);weights[k+32]=weight;total+=weight;for(int c=0;c<3;++c){const double stretch=flare?1-(2-c)*.35*s.flareBlue/100:1-c*.1;dx[c][k+32]=t*reach*stretch*std::cos(theta);dy[c][k+32]=t*reach*stretch*std::sin(theta);}}
            for(int y=0;y<h;++y){cancel(limits);for(int x=0;x<w;++x)for(int c=0;c<3;++c){double sum=0;
                for(int k=0;k<65;++k)sum+=sample(high,w,h,x+dx[c][k],y-dy[c][k],c,{})*weights[k];
                double coating=flare?1-s.flareBlue/100*(c==0?.9:c==1?.62:0):1;result[(std::size_t(y)*w+x)*4+c]+=float(sum/total*amount*4*coating);
            }}
        }
    }
    for(int y=0;y<h;++y){cancel(limits);for(int x=0;x<w;++x){auto i=(std::size_t(y)*w+x)*4;double a=input[i+3];for(int c=0;c<3;++c)result[i+c]=float(a>0?std::clamp(gamma(result[i+c]/a),0.,1.)*a:0);result[i+3]=float(a);}}
    return result;
}
bool finishingIdentity(Kind kind,const Settings& s){
    switch(kind){
    case Kind::LensEffects:return s.lens.isIdentity();
    case Kind::Vignette:return s.vignetteLayer?s.vignetteLayer->amount==0:s.vignette==0;
    case Kind::Bloom:return s.bloomGlow?s.bloomAmount==0:s.bloom==0;
    case Kind::TonalContrast:return s.tonalLayer ? s.tonalLayer->amount==0 || (s.tonalLayer->shadows==0 && s.tonalLayer->midtones==0 && s.tonalLayer->highlights==0) : s.tonal==0;
    case Kind::Dither:return s.ditherLevels<=0;
    case Kind::Scanlines:return s.scanline==0&&s.scanlineGlow==0;
    case Kind::CameraRaw:return false;
    case Kind::ProgressiveBlur:return s.progressiveFrom==0&&s.progressiveTo==0;
    case Kind::RgbSplit:return s.rgbSplitDistance==0&&s.rgbSplitBlur==0;
    default:return false;
    }
}
// Layer behavior from AdjustPixels.c at upstream 02e53989 (MIT): preserve coverage.
// The standalone Windows filter below retains its earlier empty-layer behavior.
template<class T>void coloredVignette(std::vector<T>& pixels,int w,int h,const VignetteLayerSettings& s,const Limits& limits){
    if(!std::isfinite(s.frameX)||!std::isfinite(s.frameY)||!std::isfinite(s.frameWidth)||!std::isfinite(s.frameHeight)||s.frameWidth<=0||s.frameHeight<=0)throw std::runtime_error("Invalid vignette frame");
    for(int y=0;y<h;++y){cancel(limits);for(int x=0;x<w;++x){
        const auto i=(size_t(y)*w+x)*4;const double alpha=pixels[i+3];if(!(alpha>0))continue;
        const double mask=vignette_mask_at(x+.5-s.frameX,y+.5-s.frameY,s.frameWidth,s.frameHeight,s.midpoint,s.roundness,s.feather);
        const double r=pixels[i]/alpha,g=pixels[i+1]/alpha,b=pixels[i+2]/alpha;
        const double bright=std::clamp((.2126*r+.7152*g+.0722*b-.45)/.55,0.,1.);
        const double effect=s.amount/100*mask*(1-s.highlights/100*bright);
        for(size_t c=0;c<3;++c){const double value=pixels[i+c]+(s.color[c]*alpha-pixels[i+c])*effect;
            if constexpr(std::is_same_v<T,uint8_t>)pixels[i+c]=byte(value);else pixels[i+c]=float(value);}
    }}
}
void vignette(std::vector<std::uint8_t>& pixels,int w,int h,const Settings& s){
    const double strength=s.vignette/100;
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){
        double dx=(x+.5-w/2.)/(w/2.),dy=(y+.5-h/2.)/(h/2.);
        double d=s.vignetteRound?std::hypot(dx,dy):std::max(std::abs(dx),std::abs(dy));
        double t=s.vignetteFeather<=0?(d>s.vignetteMidpoint?1:0):std::clamp((d-s.vignetteMidpoint)/s.vignetteFeather,0.,1.);
        t*=strength;auto i=(std::size_t(y)*w+x)*4;unsigned a=pixels[i+3];
        if(!a){pixels[i]=pixels[i+1]=pixels[i+2]=0;pixels[i+3]=byte(t*255);continue;}
        for(int c=0;c<3;++c)pixels[i+c]=byte(pixels[i+c]*(1-t));
    }
}
template<class T> std::vector<T> rgbSplit(const std::vector<T>& source,int w,int h,const Settings& s,double scale,const Limits& limits,LayerSides clamp={}){
    const double distance=s.rgbSplitDistance*scale,blurAmt=s.rgbSplitBlur*scale;
    if(distance<=0&&blurAmt<=0)return source;
    const Point center=s.rgbSplitCenter.value_or(Point{w/2.,h/2.});
    const double reach=std::max(1e-9,s.rgbSplitReach);
    const double angle=s.rgbSplitAngle*std::numbers::pi/180,dx=std::cos(angle),dy=-std::sin(angle);
    const double spread=s.rgbSplitRadial?distance*2:distance,smear=s.rgbSplitRadial?blurAmt*2:blurAmt;
    const int spectral=distance>0?std::clamp(int(spread),3,32):1;
    const int streaks=blurAmt>0?std::clamp(int(smear/2),2,24):1;
    std::vector<T> out(source.size());
    graphics::runParallelBatch(std::size_t(h),0,[&](std::size_t row){
        cancel(limits);const int y=int(row);
        for(int x=0;x<w;++x){
            const double px=x+.5,py=y+.5;
            const double ax=s.rgbSplitRadial?(px-center.x)/reach:dx,ay=s.rgbSplitRadial?(py-center.y)/reach:dy;
            const double docX=s.rgbSplitOriginX+px*s.rgbSplitUnitsPerPixel,docY=s.rgbSplitOriginY+py*s.rgbSplitUnitsPerPixel;
            const double noise=std::sin(docX*12.9898+docY*78.233)*43758.5453;
            const float jitter=float(noise-std::floor(noise));
            float color[3]{},alpha[3]{},total[3]{};
            for(int i=0;i<spectral;++i){
                const float t=spectral==1?0.f:(float(i)+jitter)/float(spectral)*2.f-1.f;
                auto hermite=[](float e0,float e1,float x){const float u=std::clamp((x-e0)/(e1-e0),0.f,1.f);return u*u*(3.f-2.f*u);};
                const float weight[3]{hermite(-.3f,1.f,t),1.f-hermite(0.f,1.f,std::abs(t))+.05f,hermite(-.3f,1.f,-t)};
                for(int j=0;j<streaks;++j){
                    const float u=streaks==1?0.f:(float(j)+jitter)/float(streaks)-.5f;
                    const double sx=px-ax*distance*t-ax*blurAmt*u,sy=py-ay*distance*t-ay*blurAmt*u;
                    const float a=float(sample(source,w,h,sx-.5,sy-.5,3,clamp));
                    for(int c=0;c<3;++c){
                        const float rgb=float(sample(source,w,h,sx-.5,sy-.5,c,clamp));
                        color[c]+=weight[c]*rgb;alpha[c]+=weight[c]*a;total[c]+=weight[c];
                    }
                }
            }
            const auto i=(std::size_t(y)*w+x)*4;
            float a=0;
            for(int c=0;c<3;++c){if(total[c]>0){color[c]/=total[c];alpha[c]/=total[c];}a=std::max(a,alpha[c]);}
            for(int c=0;c<3;++c)out[i+c]=blurValue<T>(std::min(color[c],a));
            out[i+3]=blurValue<T>(a);
        }
    });
    return out;
}
void bloom(std::vector<std::uint8_t>& pixels,int w,int h,const Settings& s,double scale,const Limits& limits){
    const double amount=s.bloomGlow?s.bloomAmount:s.bloom,radius=s.bloomGlow?s.bloomRadius*scale:8,threshold=s.bloomGlow?0.:s.bloomThreshold;
    if(amount<=0)return;
    std::vector<std::uint8_t> bright=pixels;
    if(!s.bloomGlow){
        bright.assign(pixels.size(),0);
        for(int y=0;y<h;++y)for(int x=0;x<w;++x){auto i=(std::size_t(y)*w+x)*4;unsigned a=pixels[i+3];if(!a)continue;double yb=(.2126*pixels[i]+.7152*pixels[i+1]+.0722*pixels[i+2])/a;if(yb<=threshold)continue;double gain=std::clamp((yb-threshold)/(1-threshold),0.,1.)*amount/100;for(int c=0;c<4;++c)bright[i+c]=byte(pixels[i+c]*gain);}
    }
    auto blurred=gaussian(bright,w,h,radius,limits);
    const double mix=s.bloomGlow?amount/50:1;
    for(std::size_t i=0;i<pixels.size();i+=4){unsigned a=std::min(255u,unsigned(pixels[i+3])+unsigned(std::clamp(blurred[i+3]*mix,0.,255.)));for(int c=0;c<3;++c)pixels[i+c]=byte(std::min(255.,double(pixels[i+c])+blurred[i+c]*mix));pixels[i+3]=std::uint8_t(a);}
}
// Tone weighting follows MetalFinishing.swift at upstream 7b5ba975 (MIT).
template<class T> void tonalLayerContrast(std::vector<T>& pixels,int w,int h,const TonalLayerSettings& s,double scale,const Limits& limits){
    auto blurred=fastGaussian<T,double>(pixels,w,h,s.radius*scale,limits);
    const double maximum=std::is_same_v<T,std::uint8_t>?255.:1.;
    auto smooth=[](double x){x=std::clamp(x,0.,1.);return x*x*(3-2*x);};
    for(int y=0;y<h;++y){cancel(limits);for(int x=0;x<w;++x){
        const auto i=(std::size_t(y)*w+x)*4;const double a=pixels[i+3],ba=blurred[i+3];if(a<=0||ba<=0)continue;
        const double l=(.2126*pixels[i]+.7152*pixels[i+1]+.0722*pixels[i+2])/a;
        const double base=(.2126*blurred[i]+.7152*blurred[i+1]+.0722*blurred[i+2])/ba;
        const double sw=1-smooth((base-.15)/.35),hw=smooth((base-.5)/.35),mw=1-sw-hw;
        const double weight=(s.shadows*sw+s.midtones*mw+s.highlights*hw)/100;
        const double delta=.18*std::tanh((l-base)*6)*weight*(s.amount/50)*(4*l*(1-l));
        for(int c=0;c<3;++c)pixels[i+c]=blurValue<T>(std::clamp(double(pixels[i+c])/a+delta,0.,1.)*std::min(a,maximum));
    }}
}
void tonalContrast(std::vector<std::uint8_t>& pixels,int w,int h,const Settings& s,double scale,const Limits& limits){
    if(s.tonalLayer){tonalLayerContrast(pixels,w,h,*s.tonalLayer,scale,limits);return;}
    auto blurred=gaussian(pixels,w,h,2.5,limits);double gain=s.tonal/100;
    for(std::size_t i=0;i<pixels.size();i+=4)if(pixels[i+3])for(int c=0;c<3;++c)pixels[i+c]=byte(pixels[i+c]+(int(pixels[i+c])-int(blurred[i+c]))*gain);
}
void dither(std::vector<std::uint8_t>& pixels,int w,int h,const Settings& s,std::uint32_t seed){
    static constexpr int bayer[4][4]={{0,8,2,10},{12,4,14,6},{3,11,1,9},{15,7,13,5}};
    static constexpr std::uint8_t glyphs[5][4]={{0,0,0,0},{0,0,0,2},{0,2,0,8},{2,5,5,2},{15,15,15,15}};
    const int levels=std::max(2,s.ditherLevels);
    const double steps=double(levels-1);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){
        auto i=(std::size_t(y)*w+x)*4;unsigned a=pixels[i+3];if(!a)continue;
        // Premultiplied bytes already include alpha, so divide by a once.
        const double luma=std::clamp((.2126*pixels[i]+.7152*pixels[i+1]+.0722*pixels[i+2])/a,0.,1.);
        double quant=0;
        if(s.ditherStyle==2){int band=std::min(4,int(luma*5));std::uint8_t row=glyphs[band][(y&3)];quant=((row>>(x&3))&1)?1.:0.;}
        else{
            const double threshold=s.ditherStyle==1?double(((seed*1664525u+1013904223u+std::uint32_t(x)*747796405u+std::uint32_t(y)*2891336453u)>>16)&255)/255.:(bayer[y&3][x&3]+.5)/16.;
            const double scaled=luma*steps;const int base=int(std::floor(scaled));
            quant=std::clamp(base+(scaled-base>=threshold?1:0),0,levels-1)/steps;
        }
        const auto v=byte(quant*a);pixels[i]=pixels[i+1]=pixels[i+2]=v;
    }
}
std::vector<float> floatsOf(const DeepImage& source,const Limits& limits){
    std::vector<float> out(size_t(source.width)*source.height*4);
    for(int y=0;y<source.height;++y){cancel(limits);for(int x=0;x<source.width;++x){auto p=source.pixel(x,y);std::copy(p.begin(),p.end(),out.begin()+(size_t(y)*source.width+x)*4);}}
    return out;
}
std::shared_ptr<const DeepImage> deepFromFloats(const DeepImage& proto,const std::vector<float>& pixels,const Limits& limits){
    auto image=makeDeep(proto.width,proto.height,proto.format);
    for(int y=0;y<proto.height;++y){cancel(limits);for(int x=0;x<proto.width;++x){auto i=(size_t(y)*proto.width+x)*4;image->setPixel(x,y,{pixels[i],pixels[i+1],pixels[i+2],std::clamp(pixels[i+3],0.f,1.f)});}}
    return image;
}
float hashNoise(uint32_t seed,int x,int y){
    uint32_t h=seed^uint32_t(x)*747796405u^uint32_t(y)*2891336453u;h=h*1664525u+1013904223u;return float((h>>8)&0xffffff)/float(1<<24);
}
void addNoiseFloat(std::vector<float>& pixels,int w,int h,const Settings& s,uint32_t seed,const Limits& limits){
    const float amount=float(s.amount/400.);
    for(int y=0;y<h;++y){cancel(limits);for(int x=0;x<w;++x){auto i=(size_t(y)*w+x)*4;float a=pixels[i+3];if(!(a>0))continue;
        float n=s.gaussian?hashNoise(seed,x,y)+hashNoise(seed^0x9e3779b9,x,y)-1.f:hashNoise(seed,x,y)*2.f-1.f;
        if(s.monochromatic){for(int c=0;c<3;++c)pixels[i+c]=std::clamp(pixels[i+c]+n*amount*a,0.f,a);}
        else for(int c=0;c<3;++c){float nc=hashNoise(seed^uint32_t(c+1)*2246822519u,x,y)*2.f-1.f;pixels[i+c]=std::clamp(pixels[i+c]+nc*amount*a,0.f,a);}
    }}
}
void vignetteFloat(std::vector<float>& pixels,int w,int h,const Settings& s){
    const double strength=s.vignette/100;
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){
        double dx=(x+.5-w/2.)/(w/2.),dy=(y+.5-h/2.)/(h/2.);
        double d=s.vignetteRound?std::hypot(dx,dy):std::max(std::abs(dx),std::abs(dy));
        double t=s.vignetteFeather<=0?(d>s.vignetteMidpoint?1:0):std::clamp((d-s.vignetteMidpoint)/s.vignetteFeather,0.,1.);
        t*=strength;auto i=(size_t(y)*w+x)*4;float a=pixels[i+3];
        if(!(a>0)){pixels[i]=pixels[i+1]=pixels[i+2]=0;pixels[i+3]=float(t);continue;}
        for(int c=0;c<3;++c)pixels[i+c]=float(pixels[i+c]*(1-t));
    }
}
void bloomFloat(std::vector<float>& pixels,int w,int h,const Settings& s,double scale,const Limits& limits){
    const double amount=s.bloomGlow?s.bloomAmount:s.bloom,radius=s.bloomGlow?s.bloomRadius*scale:8,threshold=s.bloomGlow?0.:s.bloomThreshold;
    if(amount<=0)return;
    auto bright=pixels;
    if(!s.bloomGlow){
        std::fill(bright.begin(),bright.end(),0.f);
        for(int y=0;y<h;++y)for(int x=0;x<w;++x){auto i=(size_t(y)*w+x)*4;float a=pixels[i+3];if(!(a>0))continue;double yb=(.2126*pixels[i]+.7152*pixels[i+1]+.0722*pixels[i+2])/a;if(yb<=threshold)continue;double gain=std::clamp((yb-threshold)/(1-threshold),0.,1.)*amount/100;for(int c=0;c<4;++c)bright[i+c]=float(pixels[i+c]*gain);}
    }
    auto blurred=gaussian(bright,w,h,radius,limits);
    const double mix=s.bloomGlow?amount/50:1;
    for(size_t i=0;i<pixels.size();i+=4){float a=std::clamp(pixels[i+3]+float(blurred[i+3]*mix),0.f,1.f);for(int c=0;c<3;++c)pixels[i+c]=std::min(a,pixels[i+c]+float(blurred[i+c]*mix));pixels[i+3]=a;}
}
void tonalFloat(std::vector<float>& pixels,int w,int h,const Settings& s,double scale,const Limits& limits){
    if(s.tonalLayer){tonalLayerContrast(pixels,w,h,*s.tonalLayer,scale,limits);return;}
    auto blurred=gaussian(pixels,w,h,2.5,limits);double gain=s.tonal/100;
    for(size_t i=0;i<pixels.size();i+=4)if(pixels[i+3]>0)for(int c=0;c<3;++c)pixels[i+c]=std::clamp(pixels[i+c]+float((pixels[i+c]-blurred[i+c])*gain),0.f,pixels[i+3]);
}
void ditherFloat(std::vector<float>& pixels,int w,int h,const Settings& s,uint32_t seed){
    static constexpr int bayer[4][4]={{0,8,2,10},{12,4,14,6},{3,11,1,9},{15,7,13,5}};
    static constexpr uint8_t glyphs[5][4]={{0,0,0,0},{0,0,0,2},{0,2,0,8},{2,5,5,2},{15,15,15,15}};
    const int levels=std::max(2,s.ditherLevels);const double steps=double(levels-1);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){
        auto i=(size_t(y)*w+x)*4;float a=pixels[i+3];if(!(a>0))continue;
        const double luma=std::clamp((.2126*pixels[i]+.7152*pixels[i+1]+.0722*pixels[i+2])/a,0.,1.);
        double quant=0;
        if(s.ditherStyle==2){int band=std::min(4,int(luma*5));uint8_t row=glyphs[band][(y&3)];quant=((row>>(x&3))&1)?1.:0.;}
        else{
            const double threshold=s.ditherStyle==1?double(((seed*1664525u+1013904223u+uint32_t(x)*747796405u+uint32_t(y)*2891336453u)>>16)&255)/255.:(bayer[y&3][x&3]+.5)/16.;
            const double scaled=luma*steps;const int base=int(std::floor(scaled));
            quant=std::clamp(base+(scaled-base>=threshold?1:0),0,levels-1)/steps;
        }
        const float v=float(quant*a);pixels[i]=pixels[i+1]=pixels[i+2]=v;
    }
}
void scanlinesFloat(std::vector<float>& pixels,int w,int h,const Settings& s,const Limits& limits){
    auto source=pixels;double dark=s.scanline/100,glow=s.scanlineGlow/100;
    graphics::runParallelBatch(size_t(h),0,[&](size_t row){cancel(limits);int y=int(row);
        for(int x=0;x<w;++x){auto i=(size_t(y)*w+x)*4;for(int c=0;c<4;++c)pixels[i+c]=source[i+c];
            if(y%2&&dark>0)for(int c=0;c<3;++c)pixels[i+c]=float(pixels[i+c]*(1-dark));
            if(glow>0&&y>0&&y+1<h){auto above=(size_t(y-1)*w+x)*4,below=(size_t(y+1)*w+x)*4;for(int c=0;c<3;++c)pixels[i+c]=std::min(pixels[i+3],pixels[i+c]+float((source[above+c]+source[below+c])*.5*glow));}
        }});
}
void scanlines(std::vector<std::uint8_t>& pixels,int w,int h,const Settings& s,const Limits& limits){
    auto source=pixels;double dark=s.scanline/100,glow=s.scanlineGlow/100;
    graphics::runParallelBatch(std::size_t(h),0,[&](std::size_t row){cancel(limits);int y=int(row);
        for(int x=0;x<w;++x){auto i=(std::size_t(y)*w+x)*4;for(int c=0;c<4;++c)pixels[i+c]=source[i+c];
            if(y%2&&dark>0)for(int c=0;c<3;++c)pixels[i+c]=byte(pixels[i+c]*(1-dark));
            if(glow>0&&y>0&&y+1<h){auto above=(std::size_t(y-1)*w+x)*4,below=(std::size_t(y+1)*w+x)*4;for(int c=0;c<3;++c)pixels[i+c]=byte(std::min(255.,pixels[i+c]+(source[above+c]+source[below+c])*.5*glow));}
        }});
}
// Nearest sample of `source` into `w`×`h` as if the logical `bounds` grid
// (including transparent exterior padding) had been materialized first.
std::shared_ptr<const Raster> resize(const Raster& source,PixelRect bounds,int w,int h,const Limits& limits){
    count(w,h,limits);std::vector<std::uint8_t> out(std::size_t(w)*h*4);
    // Filters.prepared uses BrushRaster.draw with interpolationQuality=.none.
    for(int y=0;y<h;++y){cancel(limits);const int sy=bounds.y+int((std::int64_t(2*y+1)*bounds.height)/(2*h));
        for(int x=0;x<w;++x){const int sx=bounds.x+int((std::int64_t(2*x+1)*bounds.width)/(2*w));
            const auto p=source.pixel(sx,sy);const auto i=(std::size_t(y)*w+x)*4;
            out[i]=p.r;out[i+1]=p.g;out[i+2]=p.b;out[i+3]=p.a;}}
    return Raster::fromRgba(w,h,out.data(),std::size_t(w)*4);
}
PixelRect coverageBounds(const SourceSelection& selected){const auto&m=*selected.coverage;int loX=m.width,loY=m.height,hiX=0,hiY=0;for(int y=0;y<m.height;++y)for(int x=0;x<m.width;++x)if(m.pixel(x,y)){loX=std::min(loX,x);loY=std::min(loY,y);hiX=std::max(hiX,x+1);hiY=std::max(hiY,y+1);}return {selected.originX+loX,selected.originY+loY,hiX-loX,hiY-loY};}
}
LensEffectSettings LensEffectSettings::normalized() const {
    auto out=*this;
    switch(effect){case LensEffect::AnamorphicFlare:case LensEffect::Starburst:case LensEffect::Mist:case LensEffect::Prism:case LensEffect::Bokeh:break;default:out.effect=LensEffect::AnamorphicFlare;}
    switch(bokehShape){case BokehShape::Round:case BokehShape::Hexagon:case BokehShape::Oval:case BokehShape::CatsEye:break;default:out.bokehShape=BokehShape::Round;}
    out.threshold=filters::normalized(threshold,50,100,90);
    out.flareAmount=filters::normalized(flareAmount,0,100,50);out.flareLength=filters::normalized(flareLength,1,100,60);out.flareBlue=filters::normalized(flareBlue,0,100,80);
    out.starAmount=filters::normalized(starAmount,0,100,50);out.starLength=filters::normalized(starLength,2,40,12);out.starAngle=filters::normalized(starAngle,-180,180,15);
    if(starPoints!=4&&starPoints!=6&&starPoints!=8&&starPoints!=12&&starPoints!=14)out.starPoints=6;
    out.mistStrength=filters::normalized(mistStrength,0,100,50);out.mistSize=filters::normalized(mistSize,0,100,30);
    out.prismAmount=filters::normalized(prismAmount,0,100,80);out.prismCopies=std::round(filters::normalized(prismCopies,1,8,5));out.prismOffset=filters::normalized(prismOffset,0,50,6);out.prismAngle=filters::normalized(prismAngle,-180,180,180);out.prismBlur=filters::normalized(prismBlur,0,100,0);
    out.bokehRadius=filters::normalized(bokehRadius,.5,6,2);out.bokehBrightness=filters::normalized(bokehBrightness,0,100,50);out.bokehRing=filters::normalized(bokehRing,0,100,100);
    return out;
}
bool LensEffectSettings::isIdentity() const {
    const auto s=normalized();
    switch(s.effect){case LensEffect::AnamorphicFlare:return s.flareAmount==0;case LensEffect::Starburst:return s.starAmount==0;case LensEffect::Mist:return s.mistStrength==0;case LensEffect::Prism:return s.prismAmount==0;case LensEffect::Bokeh:return false;}
    return false;
}
Settings Settings::normalized()const{auto out=*this;out.lens=lens.normalized();if(out.vignetteLayer){auto& v=*out.vignetteLayer;v.amount=filters::normalized(v.amount,0,100,35);v.midpoint=filters::normalized(v.midpoint,0,100,50);v.roundness=filters::normalized(v.roundness,-100,100,100);v.feather=filters::normalized(v.feather,0,100,60);v.highlights=filters::normalized(v.highlights,0,100,25);for(auto& c:v.color)c=filters::normalized(c,0,1,0);}out.radius=filters::normalized(radius,.1,250,1);out.angle=filters::normalized(angle,-90,90,0);out.distance=filters::normalized(distance,1,2000,10);out.amount=filters::normalized(amount,.1,400,10);out.distortion=filters::normalized(distortion,-100,100,0);out.vignette=filters::normalized(vignette,0,100,0);out.vignetteMidpoint=filters::normalized(vignetteMidpoint,0,1,.5);out.vignetteFeather=filters::normalized(vignetteFeather,0,1,.5);out.bloom=filters::normalized(bloom,0,100,0);out.bloomThreshold=filters::normalized(bloomThreshold,0,1,.6);out.tonal=filters::normalized(tonal,0,100,0);out.ditherLevels=int(filters::normalized(ditherLevels,0,16,0));out.ditherStyle=int(filters::normalized(ditherStyle,0,2,0));out.scanline=filters::normalized(scanline,0,100,0);out.scanlineGlow=filters::normalized(scanlineGlow,0,100,0);out.progressiveFrom=filters::normalized(progressiveFrom,0,250,0);out.progressiveTo=filters::normalized(progressiveTo,0,250,20);out.bloomAmount=filters::normalized(bloomAmount,0,100,40);out.bloomRadius=filters::normalized(bloomRadius,1,150,24);out.rgbSplitDistance=filters::normalized(rgbSplitDistance,0,200,10);out.rgbSplitBlur=filters::normalized(rgbSplitBlur,0,400,0);out.rgbSplitAngle=filters::normalized(rgbSplitAngle,-180,180,0);out.rgbSplitReach=filters::normalized(rgbSplitReach,1e-9,3200000,1000);for(auto* point:{&out.progressiveStart,&out.progressiveEnd,&out.rgbSplitCenter})if(*point&&(!std::isfinite((*point)->x)||!std::isfinite((*point)->y)))point->reset();return out;}
bool supportsHdr(Kind kind){return kind!=Kind::LensEffects&&kind!=Kind::LensCorrection&&kind!=Kind::ContentAwareFill&&kind!=Kind::Bloom&&kind!=Kind::TonalContrast&&kind!=Kind::Scanlines&&kind!=Kind::RgbSplit;}
double blurMargin(Kind kind,const Settings& raw){kindCheck(kind);auto s=raw.normalized();return kind==Kind::GaussianBlur?s.radius*3+2:kind==Kind::MotionBlur?s.distance/2+2:kind==Kind::ProgressiveBlur?std::max(s.progressiveFrom,s.progressiveTo)*3+2:kind==Kind::RgbSplit?(s.rgbSplitDistance+s.rgbSplitBlur)*(s.rgbSplitRadial?2:1)+2:kind==Kind::Bloom&&s.bloomGlow?s.bloomRadius*3+2:0;}
LayerSides canvasTouchingSides(const Transform& placed,int sourceWidth,int sourceHeight,int canvasWidth,int canvasHeight){
    LayerSides sides{};
    if(placed.rotation!=0||canvasWidth<1||canvasHeight<1||sourceWidth<1||sourceHeight<1)return sides;
    const auto a=placed.toUnit({0,0}),b=placed.toUnit({double(canvasWidth),double(canvasHeight)});
    const double minX=std::min(a.x,b.x)*sourceWidth,maxX=std::max(a.x,b.x)*sourceWidth;
    const double minY=std::min(a.y,b.y)*sourceHeight,maxY=std::max(a.y,b.y)*sourceHeight;
    if(minX>=-.5)sides.left=true;if(maxX<=sourceWidth+.5)sides.right=true;if(minY>=-.5)sides.top=true;if(maxY<=sourceHeight+.5)sides.bottom=true;
    return sides;
}
PixelRect blurWorkingBounds(Kind kind,const Settings& raw,int width,int height,double retainedBlurMargin,LayerSides sides){
    const int margin=int(std::ceil(std::max(retainedBlurMargin,blurMargin(kind,raw))));
    const int left=sides.left?0:margin,top=sides.top?0:margin,right=sides.right?0:margin,bottom=sides.bottom?0:margin;
    return {-left,-top,width+left+right,height+top+bottom};
}
PixelRect filterWorkingBounds(const Request& request){
    const int width=request.source->width,height=request.source->height;
    const auto sides=canvasTouchingSides(request.transform,width,height,request.canvasWidth,request.canvasHeight);
    auto bounds=blurWorkingBounds(request.kind,request.settings,width,height,request.retainedBlurMargin,sides);
    if(request.kind!=Kind::RgbSplit||!request.settings.rgbSplitRadial)return bounds;
    if(request.canvasWidth<1||request.canvasHeight<1)throw std::runtime_error("Radial RGB Split requires canvas bounds");
    // A radial inverse sample can affect any point on the canvas, especially
    // when distance is greater than reach. Include the canvas in source space.
    double left=std::min(0,bounds.x),top=std::min(0,bounds.y),right=std::max(width,bounds.x+bounds.width),bottom=std::max(height,bounds.y+bounds.height);
    for(Point p:std::array<Point,4>{{{0,0},{double(request.canvasWidth),0},{0,double(request.canvasHeight)},{double(request.canvasWidth),double(request.canvasHeight)}}}){
        const auto u=request.transform.toUnit(p);const double x=u.x*width,y=u.y*height;
        if(!std::isfinite(x)||!std::isfinite(y))throw std::runtime_error("Radial RGB Split canvas bounds are invalid");
        left=std::min(left,x);top=std::min(top,y);right=std::max(right,x);bottom=std::max(bottom,y);
    }
    left=std::floor(left);top=std::floor(top);right=std::ceil(right);bottom=std::ceil(bottom);
    if(left < -30000||top < -30000||right-left>30000||bottom-top>30000||(right-left)*(bottom-top)>100000000)throw std::runtime_error("Radial RGB Split canvas exceeds filter budget");
    return {int(left),int(top),int(right-left),int(bottom-top)};
}
Transform placedGrid(const Transform& placed,int originalWidth,int originalHeight,PixelRect rect){
    if(!placed.valid()||originalWidth<1||originalHeight<1||rect.width<1||rect.height<1)throw std::runtime_error("Invalid filter placement");auto out=placed;out.width=rect.width*placed.width/originalWidth;out.height=rect.height*placed.height/originalHeight;auto middle=placed.fromUnit({(rect.x+rect.width/2.)/originalWidth,(rect.y+rect.height/2.)/originalHeight});out.x=middle.x-out.width/2;out.y=middle.y-out.height/2;if(!out.valid())throw std::runtime_error("Expanded filter transform exceeds document limits");return out;
}
std::shared_ptr<const Raster> runPixels(Kind kind,const Raster& source,const Settings& raw,double scale,std::uint32_t seed,const GrayRaster* selection,const Limits& limits,LayerSides clamp){
    kindCheck(kind);cancel(limits);count(source.width,source.height,limits,kind==Kind::LensEffects?192:kind==Kind::ProgressiveBlur?96:kind==Kind::ContentAwareFill?32:24);if(!std::isfinite(scale)||scale<=0||scale>64)throw std::runtime_error("Invalid filter preview scale");if(selection){checkSelection(*selection);if(selection->width!=source.width||selection->height!=source.height)throw std::runtime_error("Filter selection dimensions differ");}
    if(kind==Kind::ContentAwareFill&&!selection)throw std::runtime_error("Content-Aware Fill needs a selection");const auto s=raw.normalized();auto original=source.rgba();cancel(limits);const int w=source.width,h=source.height;if(finishingIdentity(kind,s))return Raster::fromRgba(w,h,original.data(),std::size_t(w)*4);auto result=original;cancel(limits);
    if(selection&&empty(*selection))return Raster::fromRgba(w,h,original.data(),std::size_t(w)*4);
    switch(kind){
        case Kind::GaussianBlur:result=gaussian(original,w,h,s.radius*scale,limits,clamp);break;
        case Kind::MotionBlur:result=motion(original,w,h,s.distance*scale/std::sqrt(12.),s.angle,limits,clamp);break;
        case Kind::ProgressiveBlur:result=progressive(original,w,h,s,scale,limits,clamp);break;
        case Kind::RgbSplit:result=rgbSplit(original,w,h,s,scale,limits,clamp);break;
        case Kind::AddNoise:graphics::addNoise(view(result,w,h),float(s.amount),s.gaussian,s.monochromatic,seed,limits.cancelled);break;
        case Kind::LensCorrection:graphics::lensDistort(graphics::readOnly(view(original,w,h)),view(result,w,h),s.distortion/100*.35,limits.cancelled);break;
        case Kind::ContentAwareFill:{graphics::ConstGray8View mask{selection->pixels,std::uint32_t(w),std::uint32_t(h),std::size_t(w)};if(!graphics::contentFill(view(result,w,h),mask,limits.cancelled))throw std::runtime_error("Not enough unselected opaque pixels to synthesize a fill");break;}
        case Kind::Vignette:if(s.vignetteLayer)coloredVignette(result,w,h,*s.vignetteLayer,limits);else vignette(result,w,h,s);break;
        case Kind::Bloom:bloom(result,w,h,s,scale,limits);break;
        case Kind::TonalContrast:tonalContrast(result,w,h,s,scale,limits);break;
        case Kind::Dither:dither(result,w,h,s,seed);break;
        case Kind::Scanlines:scanlines(result,w,h,s,limits);break;
        case Kind::LensEffects:{std::vector<float> input(original.size());for(std::size_t i=0;i<input.size();++i)input[i]=original[i]/255.f;auto output=lensPixels(input,w,h,s.lens,limits);for(std::size_t i=0;i<output.size();++i)result[i]=byte(output[i]*255);break;}
        case Kind::CameraRaw:break;
    }
    cancel(limits);if(selection)for(int y=0;y<h;++y){cancel(limits);for(int x=0;x<w;++x){auto i=std::size_t(y)*w+x;unsigned coverage=selection->pixels[i];for(int c=0;c<4;++c)result[i*4+c]=std::uint8_t((unsigned(result[i*4+c])*coverage+unsigned(original[i*4+c])*(255-coverage)+127)/255);}}
    graphics::clampPremultiplied(view(result,w,h));return Raster::fromRgba(w,h,result.data(),std::size_t(w)*4);
}
namespace {
std::shared_ptr<const DeepImage> blendDeepSelection(std::shared_ptr<const DeepImage> result, const DeepImage& source,
                                                     const GrayRaster* selection, const Limits& limits) {
    if (!selection) return result;
    auto mixed = std::make_shared<DeepImage>(*result);
    for (int y = 0; y < source.height; ++y) {
        cancel(limits);
        for (int x = 0; x < source.width; ++x) {
            const auto weight = selection->pixel(x, y);
            const auto a = source.pixel(x, y);
            auto b = result->pixel(x, y);
            const float amount = weight / 255.f;
            for (int c = 0; c < 4; ++c) b[c] = a[c] + (b[c] - a[c]) * amount;
            mixed->setPixel(x, y, b);
        }
    }
    return mixed;
}

Result applyCameraRaw(const Request& request, Result result) {
    const auto& source = *request.source;
    const auto settings = request.cameraRaw.normalized();
    const bool identity = settings.isIdentity() && !request.cameraRawView.paints();
    const bool eightBitOnly = cameraRawNeedsEightBit(settings);
    const auto& deep = request.deepSource;
    if (deep && (!deep->valid() || deep->width != source.width || deep->height != source.height))
        throw std::runtime_error("Camera Raw source grid changed");
    if (deep && deep->isHdr() && (eightBitOnly || settings.geometry.adjusts()))
        throw std::runtime_error("Convert HDR to 16-Bit before spatial Camera Raw corrections");
    if (deep && !request.preview && eightBitOnly)
        throw std::runtime_error("Convert this 16-bit layer to 8-bit before this spatial filter");
    const bool useDeep = deep && !eightBitOnly;
    const auto* coverage = request.selection ? request.selection->coverage.get() : nullptr;
    const auto retainedSelection = coverage ? coverage->retainedBytes() : 0;
    const auto cache = request.cameraRawCache;
    const auto limit = request.limits.maxWorkingBytes;
    const auto retainedPreview = request.cameraRawRetainedPreviewBytes;
    const PixelRect native = result.sourceBounds;
    const int edge = std::max(native.width, native.height);
    auto reducedGrid = [&](int maxEdge) {
        return PixelRect{0, 0, std::max(1, int(std::int64_t(native.width) * maxEdge / edge)),
                         std::max(1, int(std::int64_t(native.height) * maxEdge / edge))};
    };
    auto tileBytes = [](PixelRect grid) { return estimatedWorkingBytes(grid.width, grid.height, 0); };
    auto fits = [&](PixelRect grid, std::uint64_t retained) {
        // Deep intermediates and the largest 8-bit detail/effects passes have
        // different peaks. Count both the resampled input and the tiled output.
        if (identity && grid == native && request.cameraRawNativePreview) return retained <= limit;
        const std::uint64_t bytesPerPixel = useDeep ? (settings.shadows ? 100 : 80)
            : (settings.adjustsEffects() || settings.detail.adjusts() || request.cameraRawView.sharpenMask ? 32 : 24);
        const auto shadowBytes = settings.shadows ? cameraRawShadowWorkingBytes(grid.width, grid.height) : 0;
        const auto resizedBytes = grid != native ? tileBytes(grid) : 0;
        const auto retainedCache = cache ? (cache->shadows.mean.capacity() + cache->shadows.square.capacity()) * sizeof(float) : 0;
        const auto bytes = estimatedWorkingBytes(grid.width, grid.height, bytesPerPixel) + shadowBytes + resizedBytes
                         + retainedSelection + retainedCache;
        return retained <= limit && bytes <= limit - retained;
    };
    PixelRect grid = native;
    const bool preferNative = request.preview && request.cameraRawNativePreview && request.previewMaxEdge <= 0;
    if (request.preview && !preferNative)
        grid = reducedGrid(std::min(edge, request.previewMaxEdge > 0 ? request.previewMaxEdge : 2048));
    if (preferNative && !fits(native, retainedPreview)) {
        int maxEdge = std::min(edge, 2048);
        grid = reducedGrid(maxEdge);
        while (maxEdge > 1 && !fits(grid, retainedPreview)) {
            maxEdge /= 2;
            grid = reducedGrid(maxEdge);
        }
    }
    if (!fits(grid, retainedPreview)) throw std::runtime_error("Filter exceeds transient allocation budget");
    const bool reduced = grid != native;
    auto updateRefinement = [&] {
        if (!preferNative || !reduced) return;
        const auto rasterBytes = result.raster != request.source ? result.raster->retainedBytes() : 0;
        const auto deepBytes = result.deep && result.deep != deep ? result.deep->samples.size() * sizeof(std::uint16_t) : 0;
        result.cameraRawCanRefine = fits(native, std::uint64_t(rasterBytes) + deepBytes);
    };

    auto working = reduced ? resize(source, native, grid.width, grid.height, request.limits) : request.source;
    std::optional<GrayRaster> selected;
    if (coverage && (reduced || coverage->width != native.width || coverage->height != native.height ||
                     request.selection->originX || request.selection->originY)) {
        selected = GrayRaster{grid.width, grid.height, std::vector<std::uint8_t>(std::size_t(grid.width) * grid.height)};
        for (int y = 0; y < grid.height; ++y) {
            cancel(request.limits);
            for (int x = 0; x < grid.width; ++x) {
                const int sx = int((std::int64_t(2 * x + 1) * native.width) / (2 * grid.width)) - request.selection->originX;
                const int sy = int((std::int64_t(2 * y + 1) * native.height) / (2 * grid.height)) - request.selection->originY;
                selected->pixels[std::size_t(y) * grid.width + x] = coverage->pixel(sx, sy);
            }
        }
        coverage = &*selected;
    }
    auto view = request.cameraRawView;
    view.scale = double(std::max(grid.width, grid.height)) / edge;
    view.seed = request.seed;
    result.previewScale = view.scale;
    result.implementation = "camera raw";
    CameraRawShadowAnalysis* analysis = nullptr;
    if (cache && settings.shadows) {
        const CameraRawPreviewCache::ShadowInput key{request.source, useDeep ? deep : nullptr, grid.width, grid.height,
            settings.temperature, settings.tint, settings.exposure, settings.contrast, settings.highlights, settings.geometry, settings.calibration};
        if (cache->shadowInput != key) {
            cache->shadows = {};
            cache->shadowInput = key;
        }
        // Deep Alt clipping is applied after grading. The byte renderer uses
        // the shared analysis only for the clean pass.
        analysis = &cache->shadows;
    }
    if (!useDeep) {
        auto rendered = renderCameraRaw(*working, settings, view, coverage, request.limits.cancelled, analysis);
        result.raster = rendered.raster ? std::move(rendered.raster) : working;
        result.changed = rendered.changed;
        result.cameraRawScope = std::move(rendered.scope);
        updateRefinement();
        return result;
    }

    std::shared_ptr<const DeepImage> prepared = deep;
    if (reduced) {
        auto small = makeDeep(grid.width, grid.height, deep->format);
        for (int y = 0; y < small->height; ++y) {
            cancel(request.limits);
            for (int x = 0; x < small->width; ++x) {
                const int sx = int((std::int64_t(2 * x + 1) * native.width) / (2 * small->width));
                const int sy = int((std::int64_t(2 * y + 1) * native.height) / (2 * small->height));
                small->setPixel(x, y, deep->pixel(sx, sy));
            }
        }
        prepared = std::move(small);
    }
    result.deep = prepared;
    if (!identity) {
        auto graded = renderCameraRaw(*prepared, settings, view, nullptr, request.limits.cancelled, analysis);
        result.deep = blendDeepSelection(std::move(graded), *prepared, coverage, request.limits);
    }
    if (view.scopeLimit >= 0)
        result.cameraRawScope = CameraRawScope::make(*result.deep, view.scopeLimit, request.limits.cancelled);
    if (!reduced && (result.deep == deep || result.deep->samples == deep->samples)) {
        result.deep = deep;
    } else {
        result.raster = rasterFromDeep(*result.deep, request.limits.cancelled);
        result.changed = true;
    }
    updateRefinement();
    return result;
}
}
std::shared_ptr<const DeepImage> runDeepPixels(Kind kind,const DeepImage& source,const Settings& raw,double scale,uint32_t seed,const GrayRaster* selection,const Limits& limits,const CameraRawSettings& cameraRaw,const CameraRawView& cameraView,LayerSides clamp){
    if(!source.valid())throw std::runtime_error("Invalid HDR filter source");
    count(source.width,source.height,limits,kind==Kind::LensEffects?192:kind==Kind::ProgressiveBlur?160:64);cancel(limits);const auto settings=raw.normalized();
    if(selection&&(selection->width!=source.width||selection->height!=source.height||!selection->validStorage()))throw std::runtime_error("HDR selection must match source grid");
    if(source.isHdr()&&!supportsHdr(kind))throw std::runtime_error("Convert the HDR layer to SDR before this spatial filter");
    if(kind==Kind::LensCorrection||kind==Kind::ContentAwareFill)throw std::runtime_error("Convert this 16-bit layer to 8-bit before this spatial filter");
    std::shared_ptr<const DeepImage> result;
    auto rawSettings=cameraRaw.normalized();
    if(kind==Kind::CameraRaw){
        result=renderCameraRaw(source,rawSettings,cameraView,nullptr,limits.cancelled);
    }else if(kind==Kind::GaussianBlur||kind==Kind::MotionBlur||kind==Kind::ProgressiveBlur||kind==Kind::RgbSplit){
        auto input=floatsOf(source,limits);
        auto output=kind==Kind::GaussianBlur?gaussian(input,source.width,source.height,settings.radius*scale,limits,clamp):kind==Kind::ProgressiveBlur?progressive(input,source.width,source.height,settings,scale,limits,clamp):kind==Kind::RgbSplit?rgbSplit(input,source.width,source.height,settings,scale,limits,clamp):motion(input,source.width,source.height,settings.distance*scale/std::sqrt(12.),settings.angle,limits,clamp);
        result=deepFromFloats(source,output,limits);
    }else{
        auto pixels=floatsOf(source,limits);
        if(kind==Kind::AddNoise)addNoiseFloat(pixels,source.width,source.height,settings,seed,limits);
        else if(kind==Kind::Vignette){if(settings.vignetteLayer)coloredVignette(pixels,source.width,source.height,*settings.vignetteLayer,limits);else vignetteFloat(pixels,source.width,source.height,settings);}
        else if(kind==Kind::Bloom)bloomFloat(pixels,source.width,source.height,settings,scale,limits);
        else if(kind==Kind::TonalContrast)tonalFloat(pixels,source.width,source.height,settings,scale,limits);
        else if(kind==Kind::Dither)ditherFloat(pixels,source.width,source.height,settings,seed);
        else if(kind==Kind::LensEffects)pixels=lensPixels(pixels,source.width,source.height,settings.lens,limits);
        else if(kind==Kind::Scanlines)scanlinesFloat(pixels,source.width,source.height,settings,limits);
        else throw std::runtime_error("Unsupported deep filter kind");
        result=deepFromFloats(source,pixels,limits);
    }
    return blendDeepSelection(std::move(result),source,selection,limits);
}
std::array<std::shared_ptr<const Raster>,4> splitChannels(const Raster& source,const Limits& limits){
    cancel(limits);
    const int width=source.width,height=source.height;
    gateSize(width,height);
    const auto pixels=std::size_t(width)*height;
    std::array<std::shared_ptr<const Raster>,4> planes;
    const auto peakBytes=pixels*4+planes.size()*estimatedWorkingBytes(width,height,0);
    if(peakBytes>limits.maxWorkingBytes)throw std::runtime_error("Split to Layers exceeds transient allocation budget");
    std::vector<std::uint8_t> gray(pixels*4,255);
    for(std::size_t channel=0;channel<planes.size();++channel){
        for(int y=0;y<height;++y){
            cancel(limits);
            auto* row=gray.data()+std::size_t(y)*width*4;
            for(int x=0;x<width;++x){
                const auto p=source.pixel(x,y);
                const std::array values{p.r,p.g,p.b,p.a};
                auto* dst=row+std::size_t(x)*4;
                dst[0]=dst[1]=dst[2]=values[channel];
                dst[3]=channel<3?p.a:255;
            }
            if(channel<3)graphics::unpremultiplyOpaque({{row,std::size_t(width)*4},std::uint32_t(width),1,std::size_t(width)*4});
        }
        planes[channel]=Raster::fromRgba(width,height,gray.data(),std::size_t(width)*4,limits.cancelled);
    }
    return planes;
}
Result apply(const Request& request){
    kindCheck(request.kind);cancel(request.limits);if(!request.source||!request.transform.valid())throw std::runtime_error("Filter needs a valid image layer");const auto& original=*request.source;gateSize(original.width,original.height);
    // RGB Split's largest radial preview may be retained after switching back to Linear.
    const auto maxRetainedMargin=request.kind==Kind::RgbSplit?blurMargin(request.kind,Settings{.rgbSplitDistance=200,.rgbSplitBlur=400,.rgbSplitRadial=true}):1002;
    if(!std::isfinite(request.retainedBlurMargin)||request.retainedBlurMargin<0||request.retainedBlurMargin>maxRetainedMargin)throw std::runtime_error("Invalid retained blur margin");
    Result result{request.source,request.transform,{0,0,original.width,original.height},1,false,{}};auto settings=request.settings.normalized();if(request.selection){if(!request.selection->coverage)throw std::runtime_error("Present selection must have coverage");checkSelection(*request.selection->coverage);if(std::abs(std::int64_t(request.selection->originX))>30000||std::abs(std::int64_t(request.selection->originY))>30000)throw std::runtime_error("Source selection origin exceeds supported extent");if(empty(*request.selection->coverage))return result;}
    if(request.kind==Kind::CameraRaw)return applyCameraRaw(request,std::move(result));
    if(request.kind==Kind::ContentAwareFill&&!request.selection)throw std::runtime_error("Content-Aware Fill needs a selection");if(request.kind==Kind::LensCorrection&&settings.distortion==0)return result;if(finishingIdentity(request.kind,settings))return result;
    PixelRect bounds{0,0,original.width,original.height};const bool grows=spreads(request.kind);
    const auto clamp=grows?canvasTouchingSides(request.transform,original.width,original.height,request.canvasWidth,request.canvasHeight):LayerSides{};
    if(grows)bounds=filterWorkingBounds(request);
    if(request.kind==Kind::ContentAwareFill){auto selection=coverageBounds(*request.selection);int loX=std::min(0,selection.x),loY=std::min(0,selection.y);bounds={loX,loY,std::max(original.width,selection.x+selection.width)-loX,std::max(original.height,selection.y+selection.height)-loY};}
    gateSize(bounds.width,bounds.height);
    auto placed=placedGrid(request.transform,original.width,original.height,bounds);const double previewEdge=(request.kind==Kind::ProgressiveBlur||request.kind==Kind::RgbSplit||request.kind==Kind::LensEffects)&&request.previewMaxEdge>0?request.previewMaxEdge:2048.;double factor=request.preview&&request.kind!=Kind::AddNoise&&request.kind!=Kind::ContentAwareFill?std::min(1.,previewEdge/std::max(bounds.width,bounds.height)):1.;
    if(request.kind==Kind::ProgressiveBlur||request.kind==Kind::RgbSplit){
        const int w=factor<1?std::max(1,int(bounds.width*factor)):bounds.width;
        const int h=factor<1?std::max(1,int(bounds.height*factor)):bounds.height;
        count(w,h,request.limits,request.deepSource?160:request.kind==Kind::RgbSplit?48:96);
    }
    std::shared_ptr<const Raster> working;
    if(factor<1){int w=std::max(1,int(bounds.width*factor)),h=std::max(1,int(bounds.height*factor));factor=double(w)/bounds.width;working=resize(original,bounds,w,h,request.limits);}
    else{count(bounds.width,bounds.height,request.limits,request.kind==Kind::ContentAwareFill?32:24);working=request.source;
        if(bounds!=PixelRect{0,0,original.width,original.height}){std::vector<std::uint8_t> pixels(std::size_t(bounds.width)*bounds.height*4);for(int y=0;y<original.height;++y){cancel(request.limits);for(int x=0;x<original.width;++x){auto p=original.pixel(x,y);auto i=(std::size_t(y-bounds.y)*bounds.width+x-bounds.x)*4;pixels[i]=p.r;pixels[i+1]=p.g;pixels[i+2]=p.b;pixels[i+3]=p.a;}}working=Raster::fromRgba(bounds.width,bounds.height,pixels.data(),std::size_t(bounds.width)*4);}}
    std::optional<GrayRaster> selected;if(request.selection){const auto& input=*request.selection;selected=GrayRaster{working->width,working->height,std::vector<std::uint8_t>(std::size_t(working->width)*working->height)};for(int y=0;y<working->height;++y)for(int x=0;x<working->width;++x){int sx=int(std::floor(bounds.x+(x+.5)*bounds.width/working->width))-input.originX,sy=int(std::floor(bounds.y+(y+.5)*bounds.height/working->height))-input.originY;selected->pixels[std::size_t(y)*working->width+x]=input.coverage->pixel(sx,sy);}}
    if(request.kind==Kind::ProgressiveBlur||request.kind==Kind::RgbSplit){
        // The public settings are in document coordinates; kernels receive the
        // corresponding coordinates in the padded, possibly reduced grid.
        auto map=[&](Point p){auto u=placed.toUnit(p);return Point{u.x*working->width,u.y*working->height};};
        if(request.kind==Kind::ProgressiveBlur){
            settings.progressiveStart=map(settings.progressiveStart.value_or(request.transform.fromUnit({.5,0})));
            settings.progressiveEnd=map(settings.progressiveEnd.value_or(request.transform.fromUnit({.5,1})));
        }else{
            settings.rgbSplitCenter=map(settings.rgbSplitCenter.value_or(request.transform.fromUnit({.5,.5})));
            const double document=std::hypot(placed.width,placed.height);
            settings.rgbSplitReach=settings.rgbSplitReach*std::hypot(double(working->width),double(working->height))/std::max(1.,document);
        }
    }
    std::shared_ptr<const DeepImage> deepOutput;
    const bool eightBitOnly=request.kind==Kind::LensCorrection||request.kind==Kind::ContentAwareFill;
    if(!request.preview&&request.deepSource&&request.deepSource->valid()&&!request.deepSource->isHdr()&&eightBitOnly)
        throw std::runtime_error("Convert this 16-bit layer to 8-bit before this spatial filter");
    if(request.deepSource&&!eightBitOnly){
        if(!request.deepSource->valid()||request.deepSource->width!=original.width||request.deepSource->height!=original.height)throw std::runtime_error("HDR filter source grid changed");
        count(working->width,working->height,request.limits,80);
        auto prepared=makeDeep(working->width,working->height,request.deepSource->format);
        for(int y=0;y<prepared->height;++y){cancel(request.limits);for(int x=0;x<prepared->width;++x){const int sx=bounds.x+int((int64_t(2*x+1)*bounds.width)/(2*prepared->width)),sy=bounds.y+int((int64_t(2*y+1)*bounds.height)/(2*prepared->height));prepared->setPixel(x,y,request.deepSource->pixel(sx,sy));}}
        auto view=request.cameraRawView;view.scale=factor;view.seed=request.seed;
        deepOutput=runDeepPixels(request.kind,*prepared,settings,factor,request.seed,selected?&*selected:nullptr,request.limits,request.cameraRaw,view,clamp);
    }
    if(deepOutput&&bounds==PixelRect{0,0,original.width,original.height}&&deepOutput->width==request.deepSource->width&&deepOutput->height==request.deepSource->height&&deepOutput->samples==request.deepSource->samples){result.deep=request.deepSource;return result;}
    auto output=deepOutput?rasterFromDeep(*deepOutput):runPixels(request.kind,*working,settings,factor,request.seed,selected?&*selected:nullptr,request.limits,clamp);
    if(grows&&!request.preview){int loX=output->width,loY=output->height,hiX=0,hiY=0;for(int y=0;y<output->height;++y){cancel(request.limits);for(int x=0;x<output->width;++x)if(deepOutput?deepOutput->pixel(x,y)[3]>0:output->pixel(x,y).a!=0){loX=std::min(loX,x);loY=std::min(loY,y);hiX=std::max(hiX,x+1);hiY=std::max(hiY,y+1);}}
        if(hiX>loX&&hiY>loY&&(loX||loY||hiX!=output->width||hiY!=output->height)){const int w=hiX-loX,h=hiY-loY;std::vector<std::uint8_t> cropped(std::size_t(w)*h*4);for(int y=0;y<h;++y)for(int x=0;x<w;++x){auto p=output->pixel(x+loX,y+loY);auto i=(std::size_t(y)*w+x)*4;cropped[i]=p.r;cropped[i+1]=p.g;cropped[i+2]=p.b;cropped[i+3]=p.a;}if(deepOutput){auto crop=makeDeep(w,h,deepOutput->format);for(int y=0;y<h;++y)for(int x=0;x<w;++x)crop->setPixel(x,y,deepOutput->pixel(x+loX,y+loY));deepOutput=crop;}bounds={bounds.x+loX,bounds.y+loY,w,h};placed=placedGrid(request.transform,original.width,original.height,bounds);output=Raster::fromRgba(w,h,cropped.data(),std::size_t(w)*4);}
    }
    cancel(request.limits);result.deep=deepOutput;result.raster=output;result.transform=placed;result.sourceBounds=bounds;result.previewScale=factor;result.changed=true;result.implementation=request.kind==Kind::LensEffects||grows||request.kind==Kind::RgbSplit||request.kind==Kind::Bloom?"analytic software kernel; Mac raster comparison pending":"pinned upstream C algorithm";return result;
}
}

