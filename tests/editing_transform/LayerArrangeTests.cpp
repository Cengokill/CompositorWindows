#include "editing_transform/LayerArrange.h"
#include "editing_transform/TransformGeometry.h"
#include "editing/Selection.h"
#include "graphics/StackRenderer.h"
#include <iostream>
#include <cmath>
#include <stdexcept>
using namespace compositor;
using namespace compositor::editing_transform;
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
Layer layer(const char* id,double x,double y,double w,double h){Layer l;l.id=id;l.name=id;l.transform={x,y,w,h};l.raster=Raster::filled(2,2,{1,2,3,255});return l;}
Document sample(){Document d;d.id=newId();d.width=400;d.height=300;d.layers={layer("a",10,20,40,30),layer("b",100,60,60,20),layer("c",200,10,20,50)};return d;}
int main(){try{
 auto d=sample();layers::SelectionState all{{"a","b","c"},"a"};
 auto left=arrange(d,all,Arrange::Left);check(left.changed&&left.document.layers[1].transform.x==10&&left.document.layers[2].transform.x==10,"left union");check(left.document.layers[1].raster==d.layers[1].raster,"pixels changed");
 auto right=arrange(d,all,Arrange::Right);check(right.document.layers[0].transform.x==180&&right.document.layers[1].transform.x==160,"right union");
 auto middle=arrange(d,all,Arrange::CenterX);check(middle.document.layers[0].transform.x==95&&middle.document.layers[1].transform.x==85,"horizontal center union");
 auto top=arrange(d,all,Arrange::Top);check(top.document.layers[0].transform.y==10&&top.document.layers[1].transform.y==10,"top union");
 auto vertical=arrange(d,all,Arrange::CenterY);check(vertical.document.layers[0].transform.y==30&&vertical.document.layers[1].transform.y==35,"vertical center union");
 auto bottom=arrange(d,all,Arrange::Bottom);check(bottom.document.layers[0].transform.y==50&&bottom.document.layers[1].transform.y==60&&bottom.document.layers[2].transform.y==30,"bottom union");
 auto centered=arrange(d,{{"a"},"a"},Arrange::CenterX);check(centered.document.layers[0].transform.x==180,"canvas center");
 d.selection=editing::rasterSelection(editing::SelectionOutline::rectangle({300,0,50,50},false),400,300);auto selection=arrange(d,all,Arrange::Right);check(selection.document.layers[0].transform.x==310&&selection.document.selection==d.selection,"selection target");d.selection.reset();
 d.layers[0].transform={0,0,10,10};d.layers[1].transform={60,70,30,10};d.layers[2].transform={100,100,10,10};auto centers=arrange(d,all,Arrange::CentersX);check(centers.document.layers[1].transform.x==40,"centers");auto spacing=arrange(d,all,Arrange::SpacingX);check(spacing.document.layers[1].transform.x==40,"spacing");auto two=arrange(d,{{"a","b"},"a"},Arrange::SpacingX);check(!two.changed&&two.document==d,"two no-op");
 auto yc=arrange(d,all,Arrange::CentersY);check(yc.document.layers[1].transform.y==50,"vertical centers");auto ys=arrange(d,all,Arrange::SpacingY);check(ys.document.layers[1].transform.y==50,"vertical spacing");
 auto mask=std::make_shared<GrayRaster>();mask->width=mask->height=2;mask->pixels={255,255,255,255};d.layers[1].mask=Mask{mask,true,false};d.layers[2].mask=Mask{mask,true,true,Transform{100,100,10,10}};auto moved=arrange(d,all,Arrange::Left);check(moved.document.layers[1].mask->placement==d.layers[1].transform,"unlinked stays");check(moved.document.layers[2].mask->placement->x==0,"linked follows");
 Layer group;group.id="g";group.name="g";group.group=true;group.transform={0,0,400,300};d.layers.push_back(group);d.layers[0].parentId="g";d.layers[1].parentId="g";d.layers[1].visible=false;d.layers[0].transform.rotation=90;auto grouped=arrange(d,{{"g","a","c"},"g"},Arrange::Left);check(arrangeItemCount(d,{{"g","a","c"},"g"})==2,"selected child duplicated");check(grouped.document.layers[1]==d.layers[1],"hidden child moved");
 Document masked;masked.id=newId();masked.width=30;masked.height=10;auto child=layer("child",10,0,10,10);child.parentId="inner";Layer inner;inner.id="inner";inner.name="inner";inner.group=true;inner.parentId="outer";inner.transform={10,0,10,10};auto half=std::make_shared<GrayRaster>();half->width=2;half->height=1;half->pixels={255,0};inner.mask=Mask{half};Layer outer=inner;outer.id="outer";outer.name="outer";outer.parentId.clear();masked.layers={child,inner,outer};
 graphics::StackRenderer renderer;auto before=renderer.render(masked,0,0,30,10);auto aligned=arrange(masked,{{"outer"},"outer"},Arrange::Left);auto after=renderer.render(aligned.document,0,0,30,10);check(before->pixel(11,5).a==after->pixel(1,5).a&&before->pixel(11,5).a>0,"folder mask did not move with content");check(aligned.document.layers[1].transform.x==0&&aligned.document.layers[2].transform.x==0,"nested folder masks did not move");
 auto fractional=sample();fractional.layers[0].transform.x=.25;auto rounded=arrange(fractional,{{"a"},"a"},Arrange::Left);check(!rounded.changed&&rounded.document.layers[0].transform.x==.25,"subpixel offset should round to zero");fractional.layers[0].transform.rotation=23;auto rotated=arrange(fractional,{{"a"},"a"},Arrange::Left);check(std::abs(bounds(rotated.document.layers[0].transform).x)<=.5,"rotated alignment rounding");
 fractional=sample();fractional.layers[0].transform={0,0,10,10};fractional.layers[1].transform={20,0,10,10};fractional.layers[2].transform={41,0,10,10};auto distributed=arrange(fractional,all,Arrange::CentersX);check(distributed.document.layers[1].transform.x==21,"fractional distribution rounding");
 std::cout<<"PASS arrange geometry and masks\n";return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
