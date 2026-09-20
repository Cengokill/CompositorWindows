// Guided filter arithmetic and refinement ordering translated from Compositor
// a19db9011282399785dc18efcfded904627bdcc2, GuidedMatte.swift and SubjectRemoval.swift.
// Copyright (c) 2026 Wonder Assembly LLC. MIT; see dependencies/imaging/notices/Compositor-MIT.txt.
#include "subject_matte.h"
#include <cmath>
namespace compositor::imaging {
namespace {
void shape(std::span<const float> p,std::uint32_t w,std::uint32_t h,int r){if(p.size()!=checkedBytes(w,h,1)||r<0||r>30000)throw std::runtime_error("Invalid guided filter shape or radius");}
std::vector<float> resize(std::span<const float> src,std::uint32_t sw,std::uint32_t sh,std::uint32_t w,std::uint32_t h){
    std::vector<float> out(std::size_t(w)*h);
    for(std::uint32_t y=0;y<h;++y)for(std::uint32_t x=0;x<w;++x){double sx=std::clamp((x+.5)*sw/w-.5,0.,double(sw-1)),sy=std::clamp((y+.5)*sh/h-.5,0.,double(sh-1));auto x0=std::uint32_t(sx),y0=std::uint32_t(sy),x1=std::min(x0+1,sw-1),y1=std::min(y0+1,sh-1);float fx=float(sx-x0),fy=float(sy-y0);out[std::size_t(y)*w+x]=(src[std::size_t(y0)*sw+x0]*(1-fx)+src[std::size_t(y0)*sw+x1]*fx)*(1-fy)+(src[std::size_t(y1)*sw+x0]*(1-fx)+src[std::size_t(y1)*sw+x1]*fx)*fy;}return out;
}
std::vector<float> gaussian(std::span<const float> p,std::uint32_t w,std::uint32_t h,double sigma){
    const int r=std::max(1,int(std::ceil(3*sigma)));std::vector<double> kernel(2*r+1);double sum=0;for(int i=-r;i<=r;++i)sum+=(kernel[i+r]=std::exp(-double(i*i)/(2*sigma*sigma)));for(auto& v:kernel)v/=sum;
    std::vector<float> pass(p.size()),out(p.size());for(std::uint32_t y=0;y<h;++y)for(std::uint32_t x=0;x<w;++x){double v=0;for(int i=-r;i<=r;++i)v+=p[std::size_t(y)*w+std::clamp(int(x)+i,0,int(w)-1)]*kernel[i+r];pass[std::size_t(y)*w+x]=float(v);}
    for(std::uint32_t y=0;y<h;++y)for(std::uint32_t x=0;x<w;++x){double v=0;for(int i=-r;i<=r;++i)v+=pass[std::size_t(std::clamp(int(y)+i,0,int(h)-1))*w+x]*kernel[i+r];out[std::size_t(y)*w+x]=float(v);}return out;
}
double clampSetting(double v,double lo,double hi,double fallback){return std::isfinite(v)?std::clamp(v,lo,hi):fallback;}
}
std::vector<float> boxMean(std::span<const float> src,std::uint32_t w,std::uint32_t h,int r){
    shape(src,w,h,r);const float span=float(r*2+1);std::vector<float> pass(src.size()),out(src.size());
    for(int y=0;y<int(h);++y){auto row=std::size_t(y)*w;float sum=0;for(int x=-r;x<=r;++x)sum+=src[row+std::clamp(x,0,int(w)-1)];for(int x=0;x<int(w);++x){pass[row+x]=sum/span;sum-=src[row+std::clamp(x-r,0,int(w)-1)];sum+=src[row+std::clamp(x+r+1,0,int(w)-1)];}}
    for(int x=0;x<int(w);++x){float sum=0;for(int y=-r;y<=r;++y)sum+=pass[std::size_t(std::clamp(y,0,int(h)-1))*w+x];for(int y=0;y<int(h);++y){out[std::size_t(y)*w+x]=sum/span;sum-=pass[std::size_t(std::clamp(y-r,0,int(h)-1))*w+x];sum+=pass[std::size_t(std::clamp(y+r+1,0,int(h)-1))*w+x];}}return out;
}
std::vector<float> guidedFilter(std::span<const float> mask,std::span<const float> guide,std::uint32_t w,std::uint32_t h,int r,float epsilon){
    shape(mask,w,h,r);shape(guide,w,h,r);if(!std::isfinite(epsilon)||epsilon<=0)throw std::runtime_error("Guided filter epsilon must be positive");
    auto meanGuide=boxMean(guide,w,h,r),meanMask=boxMean(mask,w,h,r);std::vector<float>squares(mask.size()),products(mask.size());
    for(std::size_t i=0;i<mask.size();++i){squares[i]=guide[i]*guide[i];products[i]=guide[i]*mask[i];}
    auto meanSquares=boxMean(squares,w,h,r),meanProducts=boxMean(products,w,h,r);std::vector<float>slope(mask.size()),offset(mask.size());
    for(std::size_t i=0;i<mask.size();++i){auto variance=meanSquares[i]-meanGuide[i]*meanGuide[i],covariance=meanProducts[i]-meanGuide[i]*meanMask[i];slope[i]=covariance/(variance+epsilon);offset[i]=meanMask[i]-slope[i]*meanGuide[i];}
    auto meanSlope=boxMean(slope,w,h,r),meanOffset=boxMean(offset,w,h,r);std::vector<float>out(mask.size());for(std::size_t i=0;i<mask.size();++i)out[i]=std::clamp(meanSlope[i]*guide[i]+meanOffset[i],0.F,1.F);return out;
}
GrayMask refineSubjectMask(const GrayMask& mask,const RgbaImage& image,const MatteSettings& settings,bool preview,const GrayMask* existing,const ImportOptions& options){
    validate(mask);validate(image);if(mask.width!=image.width||mask.height!=image.height)throw std::runtime_error("Mask and guide dimensions differ");
    if(existing){validate(*existing);if(existing->width!=mask.width||existing->height!=mask.height)throw std::runtime_error("Existing mask dimensions differ");}
    checkCancelled(options);const auto count=checkedBytes(mask.width,mask.height,1,options);if(count>options.maxWorkingBytes/72)throw std::runtime_error("Guided matte exceeds transient memory budget");
    GrayMask out{mask.width,mask.height,mask.width,std::vector<std::uint8_t>(count)};std::vector<float>p(count);
    for(std::uint32_t y=0;y<mask.height;++y)for(std::uint32_t x=0;x<mask.width;++x)p[std::size_t(y)*mask.width+x]=mask.pixels[y*mask.stride+x]/255.F;
    if(settings.advanced){
        const auto refine=clampSetting(settings.refineEdges,0,40,12),contrast=clampSetting(settings.contrast,0,100,25),shift=clampSetting(settings.shiftEdge,-10,10,0);
        if(refine>0){
            const auto factor=preview?std::min(1.,1400./std::max(mask.width,mask.height)):1.;auto w=std::max(1U,std::uint32_t(std::round(mask.width*factor))),h=std::max(1U,std::uint32_t(std::round(mask.height*factor)));
            // DeviceGray conversion must still be measured against the Mac fixture; explicit Rec.709 gamma-domain guide here.
            std::vector<float>guide(count);for(std::uint32_t y=0;y<image.height;++y)for(std::uint32_t x=0;x<image.width;++x){const auto* q=&image.pixels[y*image.stride+x*4];guide[std::size_t(y)*image.width+x]=(.2126F*q[0]+.7152F*q[1]+.0722F*q[2])/255.F;}
            auto small=guidedFilter(resize(p,mask.width,mask.height,w,h),resize(guide,mask.width,mask.height,w,h),w,h,std::max(1,int(std::round(refine*factor))),1e-4F);
            for(auto& value:small)value=std::floor(std::clamp(value*255.F+.5F,0.F,255.F))/255.F;
            p=resize(small,w,h,mask.width,mask.height);
        }
        checkCancelled(options);
        // Actual source order: guided filter, shift (blur/threshold), contrast.
        if(shift!=0){p=gaussian(p,mask.width,mask.height,std::abs(shift)/2);const float level=shift<0?.75F:.25F;for(auto& v:p)v=std::clamp((v-level)*1000.F,0.F,1.F);}
        if(contrast>0){const float slope=float(1/std::max(.02,1-contrast/100*.98));for(auto& v:p)v=std::clamp(v*slope+(1-slope)/2,0.F,1.F);}
    }
    for(std::uint32_t y=0;y<mask.height;++y){checkCancelled(options);for(std::uint32_t x=0;x<mask.width;++x){const auto i=std::size_t(y)*mask.width+x;auto value=std::uint8_t(std::clamp(p[i]*255.F+.5F,0.F,255.F));out.pixels[i]=existing?std::uint8_t((unsigned(value)*existing->pixels[y*existing->stride+x]+127)/255):value;}}return out;
}
RgbaImage applySubjectMask(const RgbaImage& image,const GrayMask& mask){validate(image);validate(mask);if(image.width!=mask.width||image.height!=mask.height)throw std::runtime_error("Image/mask size mismatch");auto out=image;for(std::uint32_t y=0;y<image.height;++y)for(std::uint32_t x=0;x<image.width;++x)for(int c=0;c<4;++c){auto i=y*image.stride+x*4+c;out.pixels[i]=std::uint8_t((unsigned(image.pixels[i])*mask.pixels[y*mask.stride+x]+127)/255);}return out;}
}
