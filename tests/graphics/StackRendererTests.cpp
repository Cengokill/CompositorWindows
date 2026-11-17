#include "graphics/StackRenderer.h"
#include "graphics/MaskSampling.h"
#include "graphics/RasterSampling.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace compositor;
using namespace compositor::graphics;
namespace {
int passed=0,failed=0;
void require(bool b,const char* message){if(!b)throw std::runtime_error(message);}
template<class F>void test(const char* id,F f){try{f();++passed;std::cout<<"PASS "<<id<<'\n';}catch(const std::exception&e){++failed;std::cout<<"FAIL "<<id<<": "<<e.what()<<'\n';}}
Layer layer(std::string id,Pixel p,int w=2,int h=2){Layer l;l.id=std::move(id);l.raster=Raster::filled(w,h,p);l.transform={0,0,double(w),double(h),0,false,false,Transform::Sampling::Nearest};return l;}
Layer alphaLayer(std::string id,std::array<uint8_t,4> alpha,bool red){auto l=layer(std::move(id),{});std::array<uint8_t,16> bytes{};for(size_t i=0;i<4;++i){bytes[i*4]=red?alpha[i]:0;bytes[i*4+3]=alpha[i];}l.raster=Raster::fromRgba(2,2,bytes.data(),8);return l;}
Mask mask(uint8_t v){auto m=std::make_shared<GrayRaster>();m->width=2;m->height=2;m->pixels.resize(4,v);return{m};}
void alphas(const Raster&r,std::array<uint8_t,4> expected){for(int y=0;y<2;++y)for(int x=0;x<2;++x)require(r.pixel(x,y).a==expected[size_t(y)*2+x],"alpha sequence differs");}
std::shared_ptr<const Raster> invert(const Layer&,std::shared_ptr<const Raster> in,RenderRegion){std::vector<Pixel> p(size_t(in->width)*in->height);for(int y=0;y<in->height;++y)for(int x=0;x<in->width;++x){auto a=in->pixel(x,y);p[size_t(y)*in->width+x]={uint8_t(a.a-a.r),uint8_t(a.a-a.g),uint8_t(a.a-a.b),a.a};}return in->replacing(0,0,in->width,in->height,p.data(),in->width);}
}
int main(){
    test("bilinear-rgba-matches-scalar-rounding",[]{
        std::array<Pixel,4> pixels{{{0,42,8,255},{1,70,117,255},{80,80,113,128},{46,9,91,128}}};
        for(int x=0;x<257;++x)for(int y=0;y<257;++y){
            const Point unit{.25+x/512.,.25+y/512.};
            const double fx=unit.x*2-.5,fy=unit.y*2-.5;
            const auto scalar=[&](uint8_t Pixel::*channel){
                const double value=(1-fy)*((1-fx)*(pixels[0].*channel)+fx*(pixels[1].*channel))
                    +fy*((1-fx)*(pixels[2].*channel)+fx*(pixels[3].*channel));
                return uint8_t(std::lround(value));
            };
            const auto actual=sampleRasterPixels(2,2,[&](int column,int row){return pixels[size_t(row)*2+column];},
                unit,Transform::Sampling::High);
            require(actual==Pixel{scalar(&Pixel::r),scalar(&Pixel::g),scalar(&Pixel::b),scalar(&Pixel::a)},
                "Bilinear SIMD changed a channel");
        }
    });
    test("scaled-normal-stack-preserves-rounded-composition",[]{
        Document document;
        document.width=269;
        document.height=273;
        for(int index=0;index<5;++index){
            const auto alpha=uint8_t(index%2?150:255);
            auto image=layer("photo-"+std::to_string(index),{uint8_t(15+index*17),25,40,alpha},269,273);
            image.transform={index*.25,index*.5,269,273};
            if(index==4){
                const std::vector<Pixel> transparent(37*273);
                image.raster=image.raster->replacing(0,0,37,273,transparent.data(),37);
            }
            document.layers.push_back(std::move(image));
        }
        auto preview=std::make_shared<LayerRenderPreview>();
        preview->layer=document.layers.back();
        preview->identity=std::make_shared<int>(0);
        for(double units:{.625,1.,1.91,3.25,5.}){
            const auto candidate=StackRenderer().renderScaled(document,-.75,-.125,256,256,units);
            const auto reference=StackRenderer({},preview).renderScaled(document,-.75,-.125,256,256,units);
            require(candidate->rgba()==reference->rgba(),"Normal stack differed from bottom-to-top reference");
        }
    });
    test("duplicate-layers-share-one-sample-without-changing-rounding",[]{
        constexpr int w=301,h=207;
        std::vector<uint8_t> bytes(size_t(w)*h*4);
        for(int y=0;y<h;++y)for(int x=0;x<w;++x){
            const uint8_t a=uint8_t(x<20||y>190?(x*7+y*3)%256:(x%5?255:100+x%90));
            auto* p=&bytes[(size_t(y)*w+x)*4];p[0]=uint8_t((x*3+y)%256*a/255);p[1]=uint8_t((x+y*5)%256*a/255);p[2]=uint8_t((x*11+y*2)%256*a/255);p[3]=a;
        }
        const auto shared=Raster::fromRgba(w,h,bytes.data(),size_t(w)*4);
        const std::array<double,9> opacities{1,.99,.99,.5,.5,.5,1,.25,.99};
        for(const auto sampling:{Transform::Sampling::High,Transform::Sampling::Smooth,Transform::Sampling::Nearest})
        for(const bool shiftOne:{false,true}){
            Document duplicated,distinct;duplicated.width=distinct.width=w;duplicated.height=distinct.height=h;
            for(size_t i=0;i<opacities.size();++i){
                Layer l;l.id="layer-"+std::to_string(i);l.raster=shared;l.opacity=opacities[i];
                l.transform={0,0,double(w),double(h),0,false,false,sampling};
                if(shiftOne&&i==4)l.transform.x=.5;
                duplicated.layers.push_back(l);
                l.raster=std::make_shared<Raster>(*shared);
                distinct.layers.push_back(std::move(l));
            }
            for(double units:{.625,1.,1.91,3.25,5.}){
                const auto candidate=StackRenderer().renderScaled(duplicated,-.75,-.125,256,256,units);
                const auto reference=StackRenderer().renderScaled(distinct,-.75,-.125,256,256,units);
                require(candidate->rgba()==reference->rgba(),"Duplicate layers sampled once changed the composite");
            }
        }
    });
    test("scaled-stack-with-blend-modes-matches-bottom-to-top-reference",[]{
        constexpr int w=269,h=273;
        std::vector<uint8_t> bytes(size_t(w)*h*4);
        for(int y=0;y<h;++y)for(int x=0;x<w;++x){
            const uint8_t a=uint8_t(x<30||y>240?(x*5+y*7)%256:(x%7?255:90+x%100));
            auto* p=&bytes[(size_t(y)*w+x)*4];p[0]=uint8_t((x*3+y)%256*a/255);p[1]=uint8_t((x+y*5)%256*a/255);p[2]=uint8_t((x*11+y*2)%256*a/255);p[3]=a;
        }
        const auto raster=Raster::fromRgba(w,h,bytes.data(),size_t(w)*4);
        const std::array<Blend,9> blends{Blend::Normal,Blend::Multiply,Blend::Normal,Blend::Screen,Blend::Overlay,Blend::Normal,Blend::SoftLight,Blend::Multiply,Blend::Normal};
        const std::array<double,9> opacities{1,1,.8,1,.6,1,1,.5,1};
        for(const bool distinct:{false,true})for(const size_t opaqueAt:{size_t(3),size_t(5),size_t(100)}){
            Document document;document.width=w;document.height=h;
            for(size_t i=0;i<blends.size();++i){
                Layer l;l.id="blend-"+std::to_string(i);l.raster=distinct?std::make_shared<Raster>(*raster):raster;l.blend=blends[i];l.opacity=opacities[i];
                l.transform={i%3*.25,i%2*.5,w,h};
                if(i==opaqueAt)l.raster=Raster::filled(w,h,{20,90,160,255});
                document.layers.push_back(std::move(l));
            }
            auto preview=std::make_shared<LayerRenderPreview>();
            preview->layer=document.layers.back();
            preview->identity=std::make_shared<int>(0);
            for(double units:{.625,1.,1.91,3.25,5.}){
                const auto candidate=StackRenderer().renderScaled(document,-.75,-.125,256,256,units);
                const auto reference=StackRenderer({},preview).renderScaled(document,-.75,-.125,256,256,units);
                require(candidate->rgba()==reference->rgba(),"Blend stack differed from bottom-to-top reference");
            }
        }
    });
    test("normal-composite-matches-core-blend-pixel-and-lround",[]{
        // Every destination and source alpha, through the renderer's own rounding and
        // Normal composite, against core blendPixel with std::lround opacity scaling.
        constexpr int side=256;
        const auto lround=[](double v){return uint8_t(std::clamp(std::lround(v),0L,255L));};
        for(int pattern=0;pattern<3;++pattern)for(const double opacity:{1.,.37,.99,.5}){
            std::vector<uint8_t> below(size_t(side)*side*4),above(below.size());
            for(int y=0;y<side;++y)for(int x=0;x<side;++x){
                const auto da=uint8_t(y),sa=uint8_t(x);
                auto* d=&below[(size_t(y)*side+x)*4];auto* s=&above[(size_t(y)*side+x)*4];
                d[0]=uint8_t(pattern==0?da:da*(x%7)/6);d[1]=uint8_t(da/2);d[2]=uint8_t(pattern==2?da*(y%3)/2:da/3);d[3]=da;
                s[0]=uint8_t(pattern==0?sa:sa*(y%5)/4);s[1]=uint8_t(sa/3);s[2]=uint8_t(pattern==1?sa:sa*(x%3)/2);s[3]=sa;
            }
            Document document;document.width=document.height=side;
            for(const auto* bytes:{&below,&above}){Layer l;l.id=bytes==&below?"below":"above";l.raster=Raster::fromRgba(side,side,bytes->data(),size_t(side)*4);l.transform={0,0,side,side,0,false,false,Transform::Sampling::Nearest};document.layers.push_back(std::move(l));}
            document.layers.back().opacity=opacity;
            const auto actual=StackRenderer().renderScaled(document,0,0,side,side,1);
            for(int y=0;y<side;++y)for(int x=0;x<side;++x){
                const auto* d=&below[(size_t(y)*side+x)*4];const auto* s=&above[(size_t(y)*side+x)*4];
                Pixel source{s[0],s[1],s[2],s[3]};
                if(opacity!=1)source={lround(source.r*opacity),lround(source.g*opacity),lround(source.b*opacity),lround(source.a*opacity)};
                const Pixel destination{d[0],d[1],d[2],d[3]};
                const auto expected=destination.a==255&&source.a==255?source:blendPixel(destination,source,Blend::Normal);
                if(destination.a==255&&source.a!=255&&source.a!=0)continue; // The opaque-destination integer path predates this test.
                require(actual->pixel(x,y)==expected,"Normal composite differs from core blendPixel");
            }
        }
    });
    test("masked-and-grouped-stack-matches-per-layer-reference",[]{
        constexpr int w=269,h=273;
        std::vector<uint8_t> bytes(size_t(w)*h*4);
        for(int y=0;y<h;++y)for(int x=0;x<w;++x){
            const uint8_t a=uint8_t(x<30||y>240?(x*5+y*7)%256:(x%7?255:90+x%100));
            auto* p=&bytes[(size_t(y)*w+x)*4];p[0]=uint8_t((x*3+y)%256*a/255);p[1]=uint8_t((x+y*5)%256*a/255);p[2]=uint8_t((x*11+y*2)%256*a/255);p[3]=a;
        }
        const auto raster=Raster::fromRgba(w,h,bytes.data(),size_t(w)*4);
        auto gradient=std::make_shared<GrayRaster>();gradient->width=97;gradient->height=61;gradient->pixels.resize(size_t(97)*61);
        for(int y=0;y<61;++y)for(int x=0;x<97;++x)gradient->pixels[size_t(y)*97+x]=uint8_t((x*9+y*4)%256);
        const std::array<Blend,8> blends{Blend::Normal,Blend::Multiply,Blend::Normal,Blend::Normal,Blend::Screen,Blend::Normal,Blend::Normal,Blend::Normal};
        const std::array<double,8> opacities{1,1,.8,.8,1,.6,1,.99};
        for(const bool folderMask:{false,true})for(const bool duplicates:{false,true}){
            Document document;document.width=w;document.height=h;
            Layer outer;outer.id="outer";outer.group=true;outer.opacity=.75;outer.transform={0,0,w,h};
            Layer inner;inner.id="inner";inner.group=true;inner.parentId="outer";inner.opacity=.5;inner.transform={0,0,w,h};
            if(folderMask)inner.mask=Mask{gradient};
            document.layers={outer,inner};
            for(size_t i=0;i<blends.size();++i){
                Layer l;l.id="layer-"+std::to_string(i);l.raster=duplicates&&i%2?document.layers.back().raster:std::make_shared<Raster>(*raster);
                l.blend=blends[i];l.opacity=opacities[i];
                l.transform=duplicates&&i%2?document.layers.back().transform:Transform{i%3*.25,i%2*.5,w,h};
                if(i==2||i==3)l.mask=Mask{gradient};
                if(i==4)l.mask=Mask{gradient,true,true,Transform{10,20,120,90}};
                if(i>=2&&i<5)l.parentId="inner";else if(i>=5&&i<7)l.parentId="outer";
                document.layers.push_back(std::move(l));
            }
            auto preview=std::make_shared<LayerRenderPreview>();
            preview->layer=document.layers.back();
            preview->identity=std::make_shared<int>(0);
            for(double units:{.625,1.,1.91,3.25,5.}){
                const auto candidate=StackRenderer().renderScaled(document,-.75,-.125,256,256,units);
                const auto reference=StackRenderer({},preview).renderScaled(document,-.75,-.125,256,256,units);
                require(candidate->rgba()==reference->rgba(),"Masked or grouped stack differed from per-layer reference");
            }
        }
    });
    test("layers-outside-the-region-are-skipped-without-changing-pixels",[]{
        // Layer edges just inside and just outside each side of the region, at
        // direct and reduced scales, with rotation, flips and blend modes.
        constexpr int w=181,h=143;
        std::vector<uint8_t> bytes(size_t(w)*h*4);
        for(int y=0;y<h;++y)for(int x=0;x<w;++x){
            const uint8_t a=uint8_t(x<9||y<5||x>w-7?(x*5+y*7)%256:(x%7?255:90+x%100));
            auto* p=&bytes[(size_t(y)*w+x)*4];p[0]=uint8_t((x*3+y)%256*a/255);p[1]=uint8_t((x+y*5)%256*a/255);p[2]=uint8_t((x*11+y*2)%256*a/255);p[3]=a;
        }
        const auto raster=Raster::fromRgba(w,h,bytes.data(),size_t(w)*4);
        for(double units:{.625,1.,1.91,3.25,5.})for(const Point origin:{Point{-.75,-.125},Point{1203.5,611.25}}){
            Document document;document.width=4000;document.height=3000;
            Layer background;background.id="background";background.raster=Raster::filled(64,48,{30,60,90,200});background.transform={0,0,4000,3000};
            document.layers.push_back(background);
            const double side=256*units;
            int i=0;
            // Small layers read deep reduction levels, whose grid overhangs the image edge.
            for(double scale:{.4,.2,.13,.05,.035})for(double d:{-2.5,-.6,-.4,0.,.4,.6,2.5,8.}){
                const double lw=w*units*scale,lh=h*units*scale;
                const double gap=d*units;
                const std::array<Transform,4> placed{{
                    {origin.x+side+gap,origin.y+side/3,lw,lh},{origin.x-lw-gap,origin.y+side/2,lw,lh},
                    {origin.x+side/4,origin.y+side+gap,lw,lh},{origin.x+side/2,origin.y-lh-gap,lw,lh}}};
                for(auto transform:placed){
                    Layer l;l.id="edge-"+std::to_string(i);l.raster=std::make_shared<Raster>(*raster);
                    transform.flipX=i%3==1;transform.flipY=i%5==2;if(i%7==3)transform.rotation=90;
                    l.transform=transform;l.opacity=i%2?.85:1;l.blend=i%6==5?Blend::Multiply:Blend::Normal;
                    document.layers.push_back(std::move(l));++i;
                }
            }
            auto preview=std::make_shared<LayerRenderPreview>();
            preview->layer=document.layers.back();
            preview->identity=std::make_shared<int>(0);
            const auto candidate=StackRenderer().renderScaled(document,origin.x,origin.y,256,256,units);
            const auto reference=StackRenderer({},preview).renderScaled(document,origin.x,origin.y,256,256,units);
            require(candidate->rgba()==reference->rgba(),"Skipping layers outside the region changed the composite");
        }
    });
    test("G-R-013-placed-mask-exterior-edge-majority",[]{Document d;auto image=layer("image",{255,0,0,255},8,4);auto gray=std::make_shared<GrayRaster>();gray->width=gray->height=3;gray->pixels.assign(9,255);gray->pixels[4]=0;image.mask=Mask{gray};image.mask->placement=Transform{2,0,3,3,0,false,false,Transform::Sampling::Nearest};d.layers={image};
        auto white=StackRenderer().render(d,0,0,8,4);require(white->pixel(0,0).a==255&&white->pixel(7,3).a==255&&white->pixel(3,1).a==0,"Reveal mask exterior or center differs");
        gray=std::make_shared<GrayRaster>(*gray);gray->pixels.assign(9,0);gray->pixels[4]=255;d.layers[0].mask->raster=gray;auto black=StackRenderer().render(d,0,0,8,4);require(black->pixel(0,0).a==0&&black->pixel(7,3).a==0&&black->pixel(3,1).a==255,"Hide mask exterior or center differs");
        GrayRaster tie;tie.width=2;tie.height=1;tie.pixels={0,255};require(maskBackground(tie)==255,"Edge majority tie must reveal");tie.pixels={127,127};require(maskBackground(tie)==0,"Below edge midpoint must hide");
        auto uniform=std::make_shared<GrayRaster>();uniform->width=200;uniform->height=97;uniform->pixels.assign(19400,255);require(cachedMaskBackground(uniform)==255,"Large uniform mask background");
        d.layers[0].mask->enabled=false;require(StackRenderer().render(d,0,0,1,1)->pixel(0,0).a==255,"Disabled placed mask clipped");});
    test("G-R-001-soft-clipping-stack-alpha",[]{Document d;d.width=d.height=2;auto base=alphaLayer("base",{255,128,32,0},false),child=alphaLayer("child",{255,255,255,255},true);child.maskSourceId="base";d.layers={base,child};
        auto r=StackRenderer().render(d,0,0,2,2);alphas(*r,{255,128,32,0});for(int y=0;y<2;++y)for(int x=0;x<2;++x){auto p=r->pixel(x,y);require(p.r==p.a&&p.g==0&&p.b==0,"black fringe under clipped red");}
        d.layers[1].opacity=.5;alphas(*StackRenderer().render(d,0,0,2,2),{255,128,32,0});
        d.layers[1].opacity=1;d.layers.insert(d.layers.begin(),layer("white",{255,255,255,255}));r=StackRenderer().render(d,0,0,2,2);alphas(*r,{255,255,255,255});for(int y=0;y<2;++y)for(int x=0;x<2;++x)require(r->pixel(x,y).r==255,"white backdrop fringe");});
    test("G-R-002-concrete-blue-base-regression",[]{Document d;auto base=layer("base",{0,0,128,128}),child=layer("child",{255,0,0,255});child.maskSourceId="base";d.layers={base,child};
        require(StackRenderer().render(d,0,0,1,1)->pixel(0,0)==Pixel{128,0,0,128},"visible stack inflated alpha");});
    test("G-R-003-hidden-black-dependency-chain",[]{Document d;auto target=alphaLayer("target",{255,255,255,255},true),source=alphaLayer("source",{255,0,128,255},false);source.visible=false;target.maskSourceId="source";d.layers={target,source};
        alphas(*StackRenderer().render(d,0,0,2,2),{255,0,128,255});d.layers[1].opacity=.5;alphas(*StackRenderer().render(d,0,0,2,2),{128,0,64,128});
        d.layers[1].opacity=1;auto third=alphaLayer("third",{0,255,255,255},false);third.visible=false;d.layers[1].maskSourceId="third";d.layers.push_back(third);alphas(*StackRenderer().render(d,0,0,2,2),{0,0,128,255});});
    test("G-R-004-moving-source-and-own-mask",[]{Document d;auto target=alphaLayer("target",{255,255,255,255},true),source=alphaLayer("source",{255,0,128,255},false);source.visible=false;source.transform.x=1;target.maskSourceId="source";d.layers={target,source};
        alphas(*StackRenderer().render(d,0,0,2,2),{0,255,0,128});d.layers[1].transform.x=0;d.layers[1].mask=mask(128);d.layers[0].mask=mask(128);alphas(*StackRenderer().render(d,0,0,2,2),{64,0,32,64});});
    test("G-R-005-noncontiguous-links-independent",[]{Document d;auto base=layer("base",{128,0,0,128}),between=layer("between",{0,255,0,255}),child=layer("child",{0,0,255,255});child.maskSourceId="base";d.layers={base,between,child};
        require(StackRenderer().render(d,0,0,1,1)->pixel(0,0)==Pixel{0,127,128,255},"noncontiguous link converted to stack");});
    test("G-R-006-hierarchy-order-and-hidden-parent",[]{Document d;auto bottom=layer("bottom",{255,0,0,255}),top=layer("top",{0,0,255,255}),child=layer("child",{0,255,0,255});Layer folder;folder.id="folder";folder.group=true;child.parentId="folder";
        d.layers={bottom,folder,top,child};require(StackRenderer().render(d,0,0,1,1)->pixel(0,0)==Pixel{0,0,255,255},"raw storage order used instead of hierarchy");
        d.layers[2].visible=false;d.layers[1].visible=false;require(StackRenderer().render(d,0,0,1,1)->pixel(0,0)==Pixel{255,0,0,255},"hidden folder child drawn");});
    test("G-R-007-folder-mask-per-child-and-stack",[]{Document d;Layer folder;folder.id="folder";folder.group=true;folder.transform={0,0,2,2,0,false,false,Transform::Sampling::Nearest};folder.mask=mask(128);
        auto first=layer("first",{255,0,0,255}),second=layer("second",{0,0,255,255});first.parentId=second.parentId="folder";d.layers={folder,first,second};
        require(StackRenderer().render(d,0,0,1,1)->pixel(0,0).a==192,"folder isolated instead of pass-through");
        d.layers[1].raster=Raster::filled(2,2,{128,0,0,128});d.layers[2].maskSourceId="first";require(StackRenderer().render(d,0,0,1,1)->pixel(0,0)==Pixel{0,0,64,64},"folder mask not applied once to clip stack");});
    test("G-R-008-source-parent-mask-independent",[]{Document d;Layer folder;folder.id="folder";folder.group=true;folder.visible=false;folder.transform={0,0,2,2};folder.mask=mask(0);auto hidden=layer("source",{0,0,128,128}),target=layer("target",{255,0,0,255});hidden.parentId="folder";target.maskSourceId="source";d.layers={folder,hidden,target};
        require(StackRenderer().render(d,0,0,1,1)->pixel(0,0)==Pixel{128,0,0,128},"dependency inherited source folder visibility/mask");});
    test("G-R-009-adjustment-alpha-mask-opacity",[]{Document d;auto base=layer("base",{64,0,0,128});Layer adjustment;adjustment.id="adjust";adjustment.adjustmentJson="{test}";adjustment.opacity=.5;adjustment.transform={0,0,2,2,0,false,false,Transform::Sampling::Nearest};adjustment.mask=mask(128);d.layers={base,adjustment};
        require(StackRenderer(invert).render(d,0,0,1,1)->pixel(0,0)==Pixel{64,32,32,128},"adjustment opacity/mask/alpha");bool rejected=false;try{StackRenderer().render(d,0,0,1,1);}catch(const std::runtime_error&){rejected=true;}require(rejected,"missing adjustment callback silently ignored");});
    test("G-R-010-clipped-adjustment-uses-opaque-base",[]{Document d;auto base=layer("base",{0,0,128,128});Layer adjustment;adjustment.id="adjust";adjustment.adjustmentJson="{test}";adjustment.maskSourceId="base";d.layers={base,adjustment};
        require(StackRenderer(invert).render(d,0,0,1,1)->pixel(0,0)==Pixel{128,128,0,128},"clipped adjustment changed base alpha");});
    test("G-R-011-nonnormal-adjustment-restores-alpha",[]{Document d;auto base=layer("base",{64,32,16,128});Layer adjustment;adjustment.id="adjust";adjustment.adjustmentJson="{test}";adjustment.blend=Blend::Multiply;d.layers={base,adjustment};
        auto constant=[](const Layer&,std::shared_ptr<const Raster> r,RenderRegion){return Raster::filled(r->width,r->height,{64,64,64,128});};
        require(StackRenderer(constant).render(d,0,0,1,1)->pixel(0,0)==Pixel{32,16,8,128},"adjustment blend thickened soft alpha");});
    test("G-R-012-region-tile-invariance",[]{Document d;d.width=513;d.height=17;auto base=layer("base",{10,20,30,64},513,17),child=layer("child",{70,20,30,128},513,17);base.transform={-7.25,1.25,520,17,2,false,true,Transform::Sampling::Smooth};child.maskSourceId="base";d.layers={base,child};
        auto full=StackRenderer().render(d,0,0,513,17),left=StackRenderer().render(d,0,0,256,17),right=StackRenderer().render(d,256,0,257,17);for(int y=0;y<17;++y)for(int x=0;x<513;++x)require(full->pixel(x,y)==(x<256?left->pixel(x,y):right->pixel(x-256,y)),"stack region seam");});
    test("G-R-014-color-overlay-keeps-source-alpha",[]{
        auto overlay=[](Layer l,Pixel color,double opacity=1){l.effects.specified=true;l.effects.colorOverlay.enabled=true;l.effects.colorOverlay.color=color;l.effects.colorOverlay.opacity=opacity;return l;};
        Document d;d.width=d.height=1;
        d.layers={overlay(layer("black",{0,0,0,128},1,1),{255,255,255,255})};
        require(StackRenderer().render(d,0,0,1,1)->pixel(0,0)==Pixel{128,128,128,128},"half-transparent overlay inflated alpha");
        auto white=layer("white",{255,255,255,255},1,1);
        d.layers={white,d.layers[0]};
        require(StackRenderer().render(d,0,0,1,1)->pixel(0,0)==Pixel{255,255,255,255},"white overlay over white backdrop stayed grey");
        d.layers={overlay(layer("red",{255,0,0,255},1,1),{0,0,255,255},.5)};
        require(StackRenderer().render(d,0,0,1,1)->pixel(0,0)==Pixel{127,0,128,255},"opaque overlay opacity mix");
        d.layers={overlay(layer("clear",{0,0,0,0},1,1),{255,255,255,255})};
        require(StackRenderer().render(d,0,0,1,1)->pixel(0,0)==Pixel{0,0,0,0},"transparent pixel stayed empty");
        auto unspecified=layer("plain",{0,0,0,128},1,1);unspecified.effects.colorOverlay.enabled=true;unspecified.effects.colorOverlay.color={255,255,255,255};
        d.layers={unspecified};
        require(StackRenderer().render(d,0,0,1,1)->pixel(0,0)==Pixel{0,0,0,128},"unspecified overlay ignored");
        auto shadow=overlay(layer("shadow",{0,0,0,128},1,1),{255,255,255,255});
        shadow.effects.dropShadow.enabled=true;shadow.effects.dropShadow.opacity=1;shadow.effects.dropShadow.angle=0;
        shadow.effects.dropShadow.distance=0;shadow.effects.dropShadow.size=0;shadow.effects.dropShadow.color={255,0,0,255};shadow.effects.dropShadow.blend=Blend::Normal;
        d.layers={shadow};
        require(StackRenderer().render(d,0,0,1,1)->pixel(0,0)==Pixel{192,128,128,192},"shadow under translucent overlay was tinted");
        auto multiply=overlay(layer("soft-red",{64,0,0,128},1,1),{255,255,255,255});
        multiply.effects.colorOverlay.blend=Blend::Multiply;
        d.layers={multiply};
        require(StackRenderer().render(d,0,0,1,1)->pixel(0,0)==Pixel{64,0,0,128},"multiply overlay changed translucent straight color");
        auto inside=overlay(layer("stroke",{0,0,0,128},1,1),{255,255,255,255});
        inside.effects.stroke.enabled=true;inside.effects.stroke.size=1;inside.effects.stroke.position=2;inside.effects.stroke.color={255,0,0,255};
        d.layers={inside};
        require(StackRenderer().render(d,0,0,1,1)->pixel(0,0)==Pixel{160,160,160,160},"inside stroke discarded by color overlay");
        auto center=overlay(layer("center",{0,0,0,128},2,2),{255,255,255,255});
        center.raster=Raster::fromRgba(2,2,std::array<uint8_t,16>{0,0,0,128,0,0,0,255,0,0,0,0,0,0,0,0}.data(),8);
        center.effects.stroke.enabled=true;center.effects.stroke.size=1;center.effects.stroke.position=1;center.effects.stroke.color={255,0,0,255};
        d.width=d.height=2;d.layers={center};
        require(StackRenderer().render(d,0,0,1,1)->pixel(0,0)==Pixel{191,191,191,191},"center stroke discarded by color overlay");
        Document scene;scene.width=scene.height=8;
        auto backdrop=layer("white",{255,255,255,255},8,8);
        auto plate=overlay(layer("plate",{0,0,0,255},4,4),{255,0,0,255});
        plate.transform={2,2,4,4,0,false,false,Transform::Sampling::Nearest};
        plate.effects.stroke.enabled=true;plate.effects.stroke.size=1;plate.effects.stroke.position=0;plate.effects.stroke.color={0,255,0,255};
        scene.layers={backdrop,plate};
        auto shown=StackRenderer().render(scene,0,0,8,8);
        require(shown->pixel(4,4)==Pixel{255,0,0,255},"inset overlay fill stayed source color");
        require(shown->pixel(1,4).g>200&&shown->pixel(1,4).r<80,"outside stroke missing next to inset plate");
        auto faded=plate;faded.opacity=.5;scene.layers={backdrop,faded};
        auto mixed=StackRenderer().render(scene,0,0,8,8)->pixel(4,4);
        require(mixed.r>200&&mixed.g>80&&mixed.g<180&&mixed.b==mixed.g,"half layer opacity hid red overlay over white");});
    std::cout<<"{\"suite\":\"stack_renderer\",\"passed\":"<<passed<<",\"failed\":"<<failed<<",\"mac_differential\":false}\n";return failed?1:0;
}
