// Behavior translated from Compositor a19db9011282399785dc18efcfded904627bdcc2
// Filters.swift and ContentFill.swift. Copyright (c) 2026 Wonder Assembly LLC.
// MIT license retained in dependencies/imaging/notices/Compositor-MIT.txt.
#include "PixelFilters.h"
#include "graphics/ParallelBatch.h"
#include "graphics/PixelAlgorithms.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
namespace compositor::filters {
namespace {
void cancel(const Limits& l){if(l.cancelled&&l.cancelled())throw std::runtime_error("Filter cancelled");}
std::size_t count(int w,int h,const Limits& l,std::uint64_t bytesPerPixel=24){
    if(w<1||h<1||w>30000||h>30000||std::uint64_t(w)*h>100000000)throw std::runtime_error("Filter exceeds the 100-megapixel or 30000-pixel-side budget");
    const auto n=std::uint64_t(w)*h;
    // Tiled intermediates also allocate full edge tiles, especially significant
    // for very thin images. Three grids can coexist during grow/run/final trim.
    const auto tiles=std::uint64_t((w+Raster::tileSide-1)/Raster::tileSide)*((h+Raster::tileSide-1)/Raster::tileSide);
    const auto estimated=n*bytesPerPixel+tiles*sizeof(Raster::Tile)*3;
    if(estimated>l.maxWorkingBytes)throw std::runtime_error("Filter exceeds transient allocation budget");return std::size_t(n);
}
void checkSelection(const GrayRaster& m){if(m.width<1||m.height<1||m.width>30000||m.height>30000||std::uint64_t(m.width)*m.height>100000000||m.pixels.size()!=std::size_t(m.width)*m.height)throw std::runtime_error("Invalid source selection storage");}
bool empty(const GrayRaster& m){return std::none_of(m.pixels.begin(),m.pixels.end(),[](auto p){return p!=0;});}
double normalized(double value,double low,double high,double fallback){return std::isfinite(value)?std::clamp(value,low,high):fallback;}
void kindCheck(Kind k){switch(k){case Kind::GaussianBlur:case Kind::MotionBlur:case Kind::AddNoise:case Kind::LensCorrection:case Kind::ContentAwareFill:case Kind::Vignette:case Kind::Bloom:case Kind::TonalContrast:case Kind::Dither:case Kind::Scanlines:case Kind::CameraRaw:return;}throw std::runtime_error("Unsupported filter kind");}
graphics::Rgba8View view(std::vector<std::uint8_t>& v,int w,int h){return {v,std::uint32_t(w),std::uint32_t(h),std::size_t(w)*4};}
std::uint8_t byte(double x){return std::uint8_t(std::clamp(std::floor(x+.5),0.,255.));}
std::vector<double> gaussianWeights(double sigma){int radius=std::max(1,int(std::ceil(sigma*3)));std::vector<double> weights(std::size_t(radius)*2+1);double total=0;for(int t=-radius;t<=radius;++t)total+=(weights[t+radius]=std::exp(-double(t*t)/(2*sigma*sigma)));for(auto& weight:weights)weight/=total;return weights;}
std::vector<std::uint8_t> gaussian(const std::vector<std::uint8_t>& source,int w,int h,double sigma,const Limits& limits){
    auto weights=gaussianWeights(sigma);int r=int(weights.size()/2);std::vector<float> pass(std::size_t(w)*h);std::vector<std::uint8_t> output(source.size());
    // Transparent exterior. Float intermediate prevents repeated 8-bit quantization.
    for(int c=0;c<4;++c){for(int y=0;y<h;++y){cancel(limits);for(int x=0;x<w;++x){if((x&255)==0)cancel(limits);double sum=0;for(int k=std::max(-r,-x);k<=std::min(r,w-1-x);++k)sum+=source[(std::size_t(y)*w+x+k)*4+c]*weights[k+r];pass[std::size_t(y)*w+x]=float(sum);}}
        for(int y=0;y<h;++y){cancel(limits);for(int x=0;x<w;++x){if((x&255)==0)cancel(limits);double sum=0;for(int k=std::max(-r,-y);k<=std::min(r,h-1-y);++k)sum+=pass[std::size_t(y+k)*w+x]*weights[k+r];output[(std::size_t(y)*w+x)*4+c]=byte(sum);}}}
    return output;
}
double sample(const std::vector<std::uint8_t>& p,int w,int h,double x,double y,int channel){
    if(x<=-1||y<=-1||x>=w||y>=h)return 0;int ix=int(std::floor(x)),iy=int(std::floor(y));double fx=x-ix,fy=y-iy;auto get=[&](int xx,int yy){return xx<0||yy<0||xx>=w||yy>=h?0.:double(p[(std::size_t(yy)*w+xx)*4+channel]);};return (get(ix,iy)*(1-fx)+get(ix+1,iy)*fx)*(1-fy)+(get(ix,iy+1)*(1-fx)+get(ix+1,iy+1)*fx)*fy;
}
std::vector<std::uint8_t> motion(const std::vector<std::uint8_t>& source,int w,int h,double sigma,double angle,const Limits& limits){
    auto weights=gaussianWeights(sigma);int r=int(weights.size()/2);const double a=angle*std::numbers::pi/180,dx=std::cos(a),dy=-std::sin(a);std::vector<std::uint8_t> out(source.size());
    for(int y=0;y<h;++y){cancel(limits);for(int x=0;x<w;++x){if((x&63)==0)cancel(limits);std::array<double,4> sum{};for(int k=-r;k<=r;++k)for(int c=0;c<4;++c)sum[c]+=sample(source,w,h,x+k*dx,y+k*dy,c)*weights[k+r];for(int c=0;c<4;++c)out[(std::size_t(y)*w+x)*4+c]=byte(sum[c]);}}return out;
}
double linearOf(double e){return e<=.04045?e/12.92:std::pow((e+.055)/1.055,2.4);}
double encodedOf(double l){l=std::clamp(l,0.,1.);return l<=.0031308?l*12.92:1.055*std::pow(l,1/2.4)-.055;}
bool finishingIdentity(Kind kind,const Settings& s){
    switch(kind){
    case Kind::Vignette:return s.vignette==0;
    case Kind::Bloom:return s.bloom==0;
    case Kind::TonalContrast:return s.tonal==0;
    case Kind::Dither:return s.ditherLevels<=0;
    case Kind::Scanlines:return s.scanline==0&&s.scanlineGlow==0;
    case Kind::CameraRaw:return s.exposure==0&&s.contrast==0&&s.highlights==0&&s.shadows==0&&s.temperature==0&&s.tint==0&&s.vibrance==0&&s.saturation==0&&s.clarity==0&&s.sharpen==0&&s.noiseReduction==0&&s.rawVignette==0&&s.rawRotate==0&&s.rawScale==100;
    default:return false;
    }
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
void bloom(std::vector<std::uint8_t>& pixels,int w,int h,const Settings& s,const Limits& limits){
    std::vector<std::uint8_t> bright(pixels.size());
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){auto i=(std::size_t(y)*w+x)*4;unsigned a=pixels[i+3];if(!a)continue;double yb=(.2126*pixels[i]+.7152*pixels[i+1]+.0722*pixels[i+2])/a;if(yb<=s.bloomThreshold)continue;double gain=std::clamp((yb-s.bloomThreshold)/(1-s.bloomThreshold),0.,1.)*s.bloom/100;for(int c=0;c<4;++c)bright[i+c]=byte(pixels[i+c]*gain);}
    auto blurred=gaussian(bright,w,h,8,limits);
    for(std::size_t i=0;i<pixels.size();i+=4){unsigned a=std::min(255u,unsigned(pixels[i+3])+blurred[i+3]);for(int c=0;c<3;++c)pixels[i+c]=byte(std::min(255.,double(pixels[i+c])+blurred[i+c]));pixels[i+3]=std::uint8_t(a);}
}
void tonalContrast(std::vector<std::uint8_t>& pixels,int w,int h,const Settings& s,const Limits& limits){
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
void scanlines(std::vector<std::uint8_t>& pixels,int w,int h,const Settings& s,const Limits& limits){
    auto source=pixels;double dark=s.scanline/100,glow=s.scanlineGlow/100;
    graphics::runParallelBatch(std::size_t(h),0,[&](std::size_t row){cancel(limits);int y=int(row);
        for(int x=0;x<w;++x){auto i=(std::size_t(y)*w+x)*4;for(int c=0;c<4;++c)pixels[i+c]=source[i+c];
            if(y%2&&dark>0)for(int c=0;c<3;++c)pixels[i+c]=byte(pixels[i+c]*(1-dark));
            if(glow>0&&y>0&&y+1<h){auto above=(std::size_t(y-1)*w+x)*4,below=(std::size_t(y+1)*w+x)*4;for(int c=0;c<3;++c)pixels[i+c]=byte(std::min(255.,pixels[i+c]+(source[above+c]+source[below+c])*.5*glow));}
        }});
}
void cameraRaw(std::vector<std::uint8_t>& pixels,int w,int h,const Settings& s){
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){
        auto i=(std::size_t(y)*w+x)*4;unsigned a=pixels[i+3];if(!a)continue;
        double rgb[3];for(int c=0;c<3;++c)rgb[c]=pixels[i+c]/double(a);
        rgb[0]=std::clamp(rgb[0]*(1+s.temperature/200)+s.tint/400,0.,1.);rgb[2]=std::clamp(rgb[2]*(1-s.temperature/200)-s.tint/400,0.,1.);
        for(int c=0;c<3;++c)rgb[c]=linearOf(rgb[c])*std::pow(2.,s.exposure);
        double luma=.2126*rgb[0]+.7152*rgb[1]+.0722*rgb[2];
        double contrast=s.contrast/100;for(int c=0;c<3;++c)rgb[c]=luma+(rgb[c]-luma)*(1+contrast);
        double shadow=std::clamp(1-luma*2,0.,1.)*s.shadows/100,highlight=std::clamp(luma*2-1,0.,1.)*s.highlights/100;for(int c=0;c<3;++c)rgb[c]+=shadow*(1-rgb[c])-highlight*rgb[c];
        double sat=1+s.saturation/100,vib=s.vibrance/100;for(int c=0;c<3;++c){double delta=rgb[c]-luma;rgb[c]=luma+delta*sat*(1+vib*std::clamp(1-std::abs(delta),0.,1.));}
        for(int c=0;c<3;++c)pixels[i+c]=byte(encodedOf(rgb[c])*a);
    }
    if(s.clarity!=0||s.sharpen!=0||s.noiseReduction!=0){auto blurred=gaussian(pixels,w,h,s.noiseReduction>0?1.2:2.2,{});double gain=(s.clarity+s.sharpen-s.noiseReduction)/100;for(std::size_t i=0;i<pixels.size();i+=4)if(pixels[i+3])for(int c=0;c<3;++c)pixels[i+c]=byte(pixels[i+c]+(int(pixels[i+c])-int(blurred[i+c]))*gain);}
    if(s.rawVignette!=0){Settings veil;veil.vignette=std::abs(s.rawVignette);veil.vignetteRound=true;veil.vignetteMidpoint=.55;veil.vignetteFeather=.45;if(s.rawVignette<0)vignette(pixels,w,h,veil);}
    if(s.rawRotate!=0||s.rawScale!=100){
        auto source=pixels;double radians=s.rawRotate*std::numbers::pi/180,scale=100/s.rawScale,c=std::cos(radians),sn=std::sin(radians);
        for(int y=0;y<h;++y)for(int x=0;x<w;++x){double dx=x+.5-w/2.,dy=y+.5-h/2.;double sx=(dx*c+dy*sn)*scale+w/2.-.5,sy=(-dx*sn+dy*c)*scale+h/2.-.5;auto i=(std::size_t(y)*w+x)*4;for(int ch=0;ch<4;++ch)pixels[i+ch]=byte(sample(source,w,h,sx,sy,ch));}
    }
}
std::shared_ptr<const Raster> resize(const Raster& source,int w,int h,const Limits& limits){
    count(w,h,limits);std::vector<std::uint8_t> out(std::size_t(w)*h*4);
    // Filters.prepared uses BrushRaster.draw with interpolationQuality=.none.
    // Read source tiles at destination pixel centers, preserving premultiplication
    // and avoiding a second full source RGBA allocation for the preview copy.
    for(int y=0;y<h;++y){cancel(limits);const int sy=int((std::int64_t(2*y+1)*source.height)/(2*h));
        for(int x=0;x<w;++x){const int sx=int((std::int64_t(2*x+1)*source.width)/(2*w));
            const auto p=source.pixel(sx,sy);const auto i=(std::size_t(y)*w+x)*4;
            out[i]=p.r;out[i+1]=p.g;out[i+2]=p.b;out[i+3]=p.a;}}
    return Raster::fromRgba(w,h,out.data(),std::size_t(w)*4);
}
PixelRect coverageBounds(const SourceSelection& selected){const auto&m=*selected.coverage;int loX=m.width,loY=m.height,hiX=0,hiY=0;for(int y=0;y<m.height;++y)for(int x=0;x<m.width;++x)if(m.pixel(x,y)){loX=std::min(loX,x);loY=std::min(loY,y);hiX=std::max(hiX,x+1);hiY=std::max(hiY,y+1);}return {selected.originX+loX,selected.originY+loY,hiX-loX,hiY-loY};}
}
Settings Settings::normalized()const{auto out=*this;out.radius=filters::normalized(radius,.1,250,1);out.angle=filters::normalized(angle,-90,90,0);out.distance=filters::normalized(distance,1,2000,10);out.amount=filters::normalized(amount,.1,400,10);out.distortion=filters::normalized(distortion,-100,100,0);out.vignette=filters::normalized(vignette,0,100,0);out.vignetteMidpoint=filters::normalized(vignetteMidpoint,0,1,.5);out.vignetteFeather=filters::normalized(vignetteFeather,0,1,.5);out.bloom=filters::normalized(bloom,0,100,0);out.bloomThreshold=filters::normalized(bloomThreshold,0,1,.6);out.tonal=filters::normalized(tonal,0,100,0);out.ditherLevels=int(filters::normalized(ditherLevels,0,16,0));out.ditherStyle=int(filters::normalized(ditherStyle,0,2,0));out.scanline=filters::normalized(scanline,0,100,0);out.scanlineGlow=filters::normalized(scanlineGlow,0,100,0);out.exposure=filters::normalized(exposure,-5,5,0);out.contrast=filters::normalized(contrast,-100,100,0);out.highlights=filters::normalized(highlights,-100,100,0);out.shadows=filters::normalized(shadows,-100,100,0);out.temperature=filters::normalized(temperature,-100,100,0);out.tint=filters::normalized(tint,-100,100,0);out.vibrance=filters::normalized(vibrance,-100,100,0);out.saturation=filters::normalized(saturation,-100,100,0);out.clarity=filters::normalized(clarity,-100,100,0);out.sharpen=filters::normalized(sharpen,0,100,0);out.noiseReduction=filters::normalized(noiseReduction,0,100,0);out.rawVignette=filters::normalized(rawVignette,-100,100,0);out.rawRotate=filters::normalized(rawRotate,-45,45,0);out.rawScale=filters::normalized(rawScale,50,200,100);return out;}
double blurMargin(Kind kind,const Settings& raw){kindCheck(kind);auto s=raw.normalized();return kind==Kind::GaussianBlur?s.radius*3+2:kind==Kind::MotionBlur?s.distance/2+2:0;}
Transform placedGrid(const Transform& placed,int originalWidth,int originalHeight,PixelRect rect){
    if(!placed.valid()||originalWidth<1||originalHeight<1||rect.width<1||rect.height<1)throw std::runtime_error("Invalid filter placement");auto out=placed;out.width=rect.width*placed.width/originalWidth;out.height=rect.height*placed.height/originalHeight;auto middle=placed.fromUnit({(rect.x+rect.width/2.)/originalWidth,(rect.y+rect.height/2.)/originalHeight});out.x=middle.x-out.width/2;out.y=middle.y-out.height/2;if(!out.valid())throw std::runtime_error("Expanded filter transform exceeds document limits");return out;
}
std::shared_ptr<const Raster> runPixels(Kind kind,const Raster& source,const Settings& raw,double scale,std::uint32_t seed,const GrayRaster* selection,const Limits& limits){
    kindCheck(kind);cancel(limits);count(source.width,source.height,limits,kind==Kind::ContentAwareFill?32:24);if(!std::isfinite(scale)||scale<=0||scale>1)throw std::runtime_error("Invalid filter preview scale");if(selection){checkSelection(*selection);if(selection->width!=source.width||selection->height!=source.height)throw std::runtime_error("Filter selection dimensions differ");}
    if(kind==Kind::ContentAwareFill&&!selection)throw std::runtime_error("Content-Aware Fill needs a selection");const auto s=raw.normalized();auto original=source.rgba();cancel(limits);const int w=source.width,h=source.height;if(finishingIdentity(kind,s))return Raster::fromRgba(w,h,original.data(),std::size_t(w)*4);auto result=original;cancel(limits);
    if(selection&&empty(*selection))return Raster::fromRgba(w,h,original.data(),std::size_t(w)*4);
    switch(kind){
        case Kind::GaussianBlur:result=gaussian(original,w,h,s.radius*scale,limits);break;
        case Kind::MotionBlur:result=motion(original,w,h,s.distance*scale/std::sqrt(12.),s.angle,limits);break;
        case Kind::AddNoise:graphics::addNoise(view(result,w,h),float(s.amount),s.gaussian,s.monochromatic,seed,limits.cancelled);break;
        case Kind::LensCorrection:graphics::lensDistort(graphics::readOnly(view(original,w,h)),view(result,w,h),s.distortion/100*.35,limits.cancelled);break;
        case Kind::ContentAwareFill:{graphics::ConstGray8View mask{selection->pixels,std::uint32_t(w),std::uint32_t(h),std::size_t(w)};if(!graphics::contentFill(view(result,w,h),mask,limits.cancelled))throw std::runtime_error("Not enough unselected opaque pixels to synthesize a fill");break;}
        case Kind::Vignette:vignette(result,w,h,s);break;
        case Kind::Bloom:bloom(result,w,h,s,limits);break;
        case Kind::TonalContrast:tonalContrast(result,w,h,s,limits);break;
        case Kind::Dither:dither(result,w,h,s,seed);break;
        case Kind::Scanlines:scanlines(result,w,h,s,limits);break;
        case Kind::CameraRaw:cameraRaw(result,w,h,s);break;
    }
    cancel(limits);if(selection)for(int y=0;y<h;++y){cancel(limits);for(int x=0;x<w;++x){auto i=std::size_t(y)*w+x;unsigned coverage=selection->pixels[i];for(int c=0;c<4;++c)result[i*4+c]=std::uint8_t((unsigned(result[i*4+c])*coverage+unsigned(original[i*4+c])*(255-coverage)+127)/255);}}
    graphics::clampPremultiplied(view(result,w,h));return Raster::fromRgba(w,h,result.data(),std::size_t(w)*4);
}
Result apply(const Request& request){
    kindCheck(request.kind);cancel(request.limits);if(!request.source||!request.transform.valid())throw std::runtime_error("Filter needs a valid image layer");const auto& original=*request.source;count(original.width,original.height,request.limits);if(!std::isfinite(request.retainedBlurMargin)||request.retainedBlurMargin<0||request.retainedBlurMargin>1002)throw std::runtime_error("Invalid retained blur margin");
    Result result{request.source,request.transform,{0,0,original.width,original.height},1,false,{}};auto settings=request.settings.normalized();if(request.selection){if(!request.selection->coverage)throw std::runtime_error("Present selection must have coverage");checkSelection(*request.selection->coverage);if(std::abs(std::int64_t(request.selection->originX))>30000||std::abs(std::int64_t(request.selection->originY))>30000)throw std::runtime_error("Source selection origin exceeds supported extent");if(empty(*request.selection->coverage))return result;}
    if(request.kind==Kind::ContentAwareFill&&!request.selection)throw std::runtime_error("Content-Aware Fill needs a selection");if(request.kind==Kind::LensCorrection&&settings.distortion==0)return result;if(finishingIdentity(request.kind,settings))return result;
    PixelRect bounds{0,0,original.width,original.height};const bool spreads=request.kind==Kind::GaussianBlur||request.kind==Kind::MotionBlur;
    if(spreads){int margin=int(std::ceil(std::max(request.retainedBlurMargin,blurMargin(request.kind,settings))));bounds={-margin,-margin,original.width+2*margin,original.height+2*margin};}
    if(request.kind==Kind::ContentAwareFill){auto selection=coverageBounds(*request.selection);int loX=std::min(0,selection.x),loY=std::min(0,selection.y);bounds={loX,loY,std::max(original.width,selection.x+selection.width)-loX,std::max(original.height,selection.y+selection.height)-loY};}
    count(bounds.width,bounds.height,request.limits,request.kind==Kind::ContentAwareFill?32:24);auto working=request.source;
    if(bounds!=PixelRect{0,0,original.width,original.height}){std::vector<std::uint8_t> pixels(std::size_t(bounds.width)*bounds.height*4);for(int y=0;y<original.height;++y){cancel(request.limits);for(int x=0;x<original.width;++x){auto p=original.pixel(x,y);auto i=(std::size_t(y-bounds.y)*bounds.width+x-bounds.x)*4;pixels[i]=p.r;pixels[i+1]=p.g;pixels[i+2]=p.b;pixels[i+3]=p.a;}}working=Raster::fromRgba(bounds.width,bounds.height,pixels.data(),std::size_t(bounds.width)*4);}
    auto placed=placedGrid(request.transform,original.width,original.height,bounds);double factor=request.preview&&request.kind!=Kind::AddNoise&&request.kind!=Kind::ContentAwareFill?std::min(1.,2048./std::max(working->width,working->height)):1.;
    if(factor<1){int w=std::max(1,int(working->width*factor)),h=std::max(1,int(working->height*factor));factor=double(w)/working->width;working=resize(*working,w,h,request.limits);}
    std::optional<GrayRaster> selected;if(request.selection){const auto& input=*request.selection;selected=GrayRaster{working->width,working->height,std::vector<std::uint8_t>(std::size_t(working->width)*working->height)};for(int y=0;y<working->height;++y)for(int x=0;x<working->width;++x){int sx=int(std::floor(bounds.x+(x+.5)*bounds.width/working->width))-input.originX,sy=int(std::floor(bounds.y+(y+.5)*bounds.height/working->height))-input.originY;selected->pixels[std::size_t(y)*working->width+x]=input.coverage->pixel(sx,sy);}}
    auto output=runPixels(request.kind,*working,settings,factor,request.seed,selected?&*selected:nullptr,request.limits);
    if(spreads&&!request.preview){int loX=output->width,loY=output->height,hiX=0,hiY=0;for(int y=0;y<output->height;++y){cancel(request.limits);for(int x=0;x<output->width;++x)if(output->pixel(x,y).a){loX=std::min(loX,x);loY=std::min(loY,y);hiX=std::max(hiX,x+1);hiY=std::max(hiY,y+1);}}
        if(hiX>loX&&hiY>loY&&(loX||loY||hiX!=output->width||hiY!=output->height)){const int w=hiX-loX,h=hiY-loY;std::vector<std::uint8_t> cropped(std::size_t(w)*h*4);for(int y=0;y<h;++y)for(int x=0;x<w;++x){auto p=output->pixel(x+loX,y+loY);auto i=(std::size_t(y)*w+x)*4;cropped[i]=p.r;cropped[i+1]=p.g;cropped[i+2]=p.b;cropped[i+3]=p.a;}bounds={bounds.x+loX,bounds.y+loY,w,h};placed=placedGrid(request.transform,original.width,original.height,bounds);output=Raster::fromRgba(w,h,cropped.data(),std::size_t(w)*4);}
    }
    cancel(request.limits);result.raster=output;result.transform=placed;result.sourceBounds=bounds;result.previewScale=factor;result.changed=true;result.implementation=spreads?"analytic software kernel; Mac raster comparison pending":"pinned upstream C algorithm";return result;
}
}

