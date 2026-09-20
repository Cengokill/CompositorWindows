// Geometry, bounds and mask policies from Distort.swift. MIT: LICENSE.
#include "Distortion.h"
#include "graphics/RasterSampling.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace compositor::editing_transform {
namespace {
void cancel(const CancelCheck& test){if(test&&test())throw TransformCancelled();}
uint8_t byte(double value){return uint8_t(std::clamp(std::round(value),0.,255.));}
bool inside(Point p){return std::isfinite(p.x)&&std::isfinite(p.y)&&p.x>=0&&p.y>=0&&p.x<1&&p.y<1;}
struct Geometry {Transform placed;int width,height;double factor;Homography inverse;};
Geometry geometry(const Transform& original,const Corners& c,const WarpOptions& options){
    if(!original.valid()||!usableCorners(c))throw std::invalid_argument("Invalid distortion geometry");
    if(options.longestSide<0||(options.trim&&options.longestSide))throw std::invalid_argument("Trim requires full resolution");
    double x0=c[0].x,x1=x0,y0=c[0].y,y1=y0;for(auto p:c){x0=std::min(x0,p.x);x1=std::max(x1,p.x);y0=std::min(y0,p.y);y1=std::max(y1,p.y);}
    x0=std::floor(x0);y0=std::floor(y0);x1=std::ceil(x1);y1=std::ceil(y1);double w=x1-x0,h=y1-y0;
    if(w<1||h<1||w>30000||h>30000||w*h>100000000)throw std::invalid_argument("Distortion bounds exceed pixel budget");
    Transform placed{x0,y0,w,h,0,false,false,original.sampling};
    const double factor=options.longestSide?std::min(1.,options.longestSide/std::max(w,h)):1;
    return {placed,std::max(1,int(std::ceil(w*factor))),std::max(1,int(std::ceil(h*factor))),factor,Homography::fromCorners(c).inverse()};
}
Point imageUnit(Point p,const Transform& t){if(t.flipX)p.x=1-p.x;if(t.flipY)p.y=1-p.y;return p;}
// A fixed4x4 boundary quadrature retains thin visible slivers when trimming.
// Interior pixels use their center sample, preserving integer identity warps.
template<class Sample>Pixel warpPixel(const Geometry& g,const Transform& original,int x,int y,Sample sample,Pixel exterior={}){
    const auto at=[&](double dx,double dy){return g.inverse.map({g.placed.x+(x+dx)/g.factor,g.placed.y+(y+dy)/g.factor});};
    const auto middle=at(.5,.5);
    if(inside(at(0,0))&&inside(at(1,0))&&inside(at(1,1))&&inside(at(0,1)))return sample(imageUnit(middle,original));
    unsigned hits=0;std::array<double,4> sum{};
    for(int sy=0;sy<4;++sy)for(int sx=0;sx<4;++sx){auto unit=at((sx+.5)/4,(sy+.5)/4);const auto p=inside(unit)?(++hits,sample(imageUnit(unit,original))):exterior;sum[0]+=p.r;sum[1]+=p.g;sum[2]+=p.b;sum[3]+=p.a;}
    if(hits==16&&inside(middle))return sample(imageUnit(middle,original));
    return {byte(sum[0]/16),byte(sum[1]/16),byte(sum[2]/16),byte(sum[3]/16)};
}
std::shared_ptr<const Raster> cropRaster(const Raster& source,int x0,int y0,int width,int height){
    auto out=std::make_shared<Raster>();out->width=width;out->height=height;
    for(int ty=0;ty<(height+255)/256;++ty)for(int tx=0;tx<(width+255)/256;++tx){auto tile=std::make_shared<Raster::Tile>();for(int y=0;y<std::min(256,height-ty*256);++y)for(int x=0;x<std::min(256,width-tx*256);++x)tile->pixels[size_t(y)*256+x]=source.pixel(x0+tx*256+x,y0+ty*256+y);out->tiles.push_back(std::move(tile));}return out;
}
std::shared_ptr<const GrayRaster> cropGray(const GrayRaster& source,const Rect& crop){
    auto out=std::make_shared<GrayRaster>();out->width=int(crop.width);out->height=int(crop.height);out->pixels.resize(size_t(out->width)*out->height);
    for(int y=0;y<out->height;++y)for(int x=0;x<out->width;++x)out->pixels[size_t(y)*out->width+x]=source.pixel(int(crop.x)+x,int(crop.y)+y);return out;
}
}
Point Homography::map(Point p)const{const auto&m=matrix;const double w=m[6]*p.x+m[7]*p.y+m[8];if(std::abs(w)<1e-14)return {std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::quiet_NaN()};return {(m[0]*p.x+m[1]*p.y+m[2])/w,(m[3]*p.x+m[4]*p.y+m[5])/w};}
Homography Homography::inverse()const{const auto&m=matrix;const double a=m[4]*m[8]-m[5]*m[7],b=m[2]*m[7]-m[1]*m[8],c=m[1]*m[5]-m[2]*m[4],d=m[5]*m[6]-m[3]*m[8],e=m[0]*m[8]-m[2]*m[6],f=m[2]*m[3]-m[0]*m[5],g=m[3]*m[7]-m[4]*m[6],h=m[1]*m[6]-m[0]*m[7],i=m[0]*m[4]-m[1]*m[3];const double determinant=m[0]*a+m[1]*d+m[2]*g;if(!std::isfinite(determinant)||std::abs(determinant)<1e-14)throw std::invalid_argument("Singular perspective mapping");Homography out{{a,b,c,d,e,f,g,h,i}};for(auto& value:out.matrix)value/=determinant;return out;}
Homography Homography::fromCorners(const Corners& c){
    if(!usableCorners(c))throw std::invalid_argument("Invalid perspective corners");const double sx=c[0].x-c[1].x+c[2].x-c[3].x,sy=c[0].y-c[1].y+c[2].y-c[3].y;double g=0,h=0;
    if(std::abs(sx)>1e-9||std::abs(sy)>1e-9){const double dx1=c[1].x-c[2].x,dx2=c[3].x-c[2].x,dy1=c[1].y-c[2].y,dy2=c[3].y-c[2].y,denominator=dx1*dy2-dx2*dy1;if(std::abs(denominator)>1e-12){g=(sx*dy2-dx2*sy)/denominator;h=(dx1*sy-sx*dy1)/denominator;}}
    return {{c[1].x-c[0].x+g*c[1].x,c[3].x-c[0].x+h*c[3].x,c[0].x,c[1].y-c[0].y+g*c[1].y,c[3].y-c[0].y+h*c[3].y,c[0].y,g,h,1}};
}
Corners carriedCorners(const Transform& placement,const Transform& enclosing,const Corners& target){auto noFlips=enclosing;noFlips.flipX=noFlips.flipY=false;const auto map=Homography::fromCorners(target);auto result=corners(placement);for(auto& p:result)p=map.map(noFlips.toUnit(p));return result;}
RasterWarp warpRaster(std::shared_ptr<const Raster> source,const Transform& original,const Corners& c,WarpOptions options){
    cancel(options.cancelled);if(!source||source->width<1||source->height<1||source->width>30000||source->height>30000||uint64_t(source->width)*source->height>100000000||source->tiles.size()!=size_t((source->width+255)/256)*size_t((source->height+255)/256))throw std::invalid_argument("Invalid distortion raster");for(auto& tile:source->tiles)if(!tile)throw std::invalid_argument("Missing distortion tile");const auto g=geometry(original,c,options);
    auto image=std::make_shared<Raster>();image->width=g.width;image->height=g.height;int minX=g.width,minY=g.height,maxX=0,maxY=0;
    for(int ty=0;ty<(g.height+255)/256;++ty)for(int tx=0;tx<(g.width+255)/256;++tx){auto tile=std::make_shared<Raster::Tile>();
        for(int y=0;y<std::min(256,g.height-ty*256);++y){cancel(options.cancelled);for(int x=0;x<std::min(256,g.width-tx*256);++x){const int xx=tx*256+x,yy=ty*256+y;auto p=warpPixel(g,original,xx,yy,[&](Point unit){return graphics::sampleRaster(*source,unit,Transform::Sampling::Smooth);});tile->pixels[size_t(y)*256+x]=p;if(p.a){minX=std::min(minX,xx);minY=std::min(minY,yy);maxX=std::max(maxX,xx+1);maxY=std::max(maxY,yy+1);}}}image->tiles.push_back(std::move(tile));}
    RasterWarp out{image,g.placed,{0,0,double(g.width),double(g.height)}};
    if(options.trim&&minX<maxX&&minY<maxY&&(minX!=0||minY!=0||maxX!=g.width||maxY!=g.height)){out.crop={double(minX),double(minY),double(maxX-minX),double(maxY-minY)};out.raster=cropRaster(*image,minX,minY,maxX-minX,maxY-minY);out.transform.x+=minX;out.transform.y+=minY;out.transform.width=maxX-minX;out.transform.height=maxY-minY;}
    cancel(options.cancelled);return out;
}
GrayWarp warpGray(std::shared_ptr<const GrayRaster> source,const Transform& original,const Corners& c,uint8_t exterior,WarpOptions options){
    cancel(options.cancelled);if(!source||source->width<1||source->height<1||source->width>30000||source->height>30000||uint64_t(source->width)*source->height>100000000||source->pixels.size()!=size_t(source->width)*source->height)throw std::invalid_argument("Invalid distortion mask");const auto g=geometry(original,c,options);
    if(source->width==1&&source->height==1)return {source,g.placed};
    auto out=std::make_shared<GrayRaster>();out->width=g.width;out->height=g.height;out->pixels.resize(size_t(g.width)*g.height);
    for(int y=0;y<g.height;++y){cancel(options.cancelled);for(int x=0;x<g.width;++x){auto p=warpPixel(g,original,x,y,[&](Point unit){const auto gray=byte(graphics::sampleGray(*source,unit,Transform::Sampling::Smooth)*255);return Pixel{gray,gray,gray,255};},{exterior,exterior,exterior,255});out->pixels[size_t(y)*g.width+x]=p.r;}}
    return {out,g.placed};
}
Layer distortLayer(const Layer& original,const Transform& draft,const Corners& c,WarpOptions options){
    if(!original.raster||original.group)throw std::invalid_argument("Distortion requires image pixels");auto imageOptions=options;imageOptions.trim=options.longestSide==0;auto warped=warpRaster(original.raster,draft,c,imageOptions);auto result=original;result.raster=warped.raster;result.transform=warped.transform;
    if(original.mask&&original.mask->raster){const auto& mask=*original.mask;auto maskOptions=options;maskOptions.trim=false;
        if(mask.linked&&!mask.placement){auto moved=warpGray(mask.raster,draft,c,0,maskOptions);result.mask->raster=moved.raster==mask.raster?mask.raster:cropGray(*moved.raster,warped.crop);}
        else if(mask.linked&&mask.placement){auto placed=following(*mask.placement,original.transform,draft);const auto carried=carriedCorners(placed,draft,c);if(usableCorners(carried)){auto moved=warpGray(mask.raster,placed,carried,graphics::cachedMaskBackground(mask.raster),maskOptions);result.mask->raster=moved.raster;result.mask->placement=moved.transform;}}
        else result.mask->placement=mask.placement.value_or(original.transform);
    }
    cancel(options.cancelled);return result;
}
DistortionSession::DistortionSession(Layer original):original_(std::move(original)),result_(original_),draft_(original_.transform),corners_(corners(draft_)){if(!original_.raster||original_.group||!draft_.valid())throw std::invalid_argument("Invalid distortion target");}
bool DistortionSession::update(const Transform& draft,const Corners& c){if(finished_)throw std::logic_error("Distortion is finished");if(!draft.valid()||!usableCorners(c))return false;draft_=draft;corners_=c;return true;}
Layer DistortionSession::preview(int limit,CancelCheck cancellation)const{if(finished_)return result_;return distortLayer(original_,draft_,corners_,{limit,false,std::move(cancellation)});}
Layer DistortionSession::apply(CancelCheck cancellation){if(finished_)return result_;auto result=distortLayer(original_,draft_,corners_,{0,true,std::move(cancellation)});result_=std::move(result);finished_=true;return result_;}
Layer DistortionSession::cancel(){result_=original_;finished_=true;return original_;}
}
