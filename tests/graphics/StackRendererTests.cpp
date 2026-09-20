#include "graphics/StackRenderer.h"
#include "graphics/MaskSampling.h"
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
    std::cout<<"{\"suite\":\"stack_renderer\",\"passed\":"<<passed<<",\"failed\":"<<failed<<",\"mac_differential\":false}\n";return failed?1:0;
}
