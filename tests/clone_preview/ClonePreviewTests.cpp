#include "graphics/CloneCursorPreview.h"
#include "graphics/BrushSession.h"
#include <algorithm>
#include <iostream>
#include <limits>
#include <map>
using namespace compositor;
namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
Document fixture(){Document d;d.id="clone-helper";d.width=d.height=100;Layer l;l.id="active";l.transform={0,0,100,100};l.raster=Raster::filled(100,100,{200,50,30,255});d.layers={l};return d;}
Pixel center(const std::shared_ptr<const Raster>& value){check(bool(value),"Missing preview");return value->pixel(value->width/2,value->height/2);}
template<class F>void rejects(F f){bool rejected=false;try{f();}catch(const std::invalid_argument&){rejected=true;}check(rejected,"Expected invalid argument");}
void run(const std::string& name){auto d=fixture();graphics::CloneCursorPreview cache;
 if(name=="raw_current_appearance_ignored"){
  auto m=std::make_shared<GrayRaster>();m->width=m->height=1;m->pixels={0};d.layers[0].mask=Mask{m};d.layers[0].opacity=.25;d.layers[0].blend=Blend::Multiply;d.layers[0].visible=false;
  check(center(cache.render(d,"active",{50,50},20,1,4,false))==Pixel{200,50,30,255},"Raw current asset inherited appearance");
 }else if(name=="all_layers_composite"){
  Layer top;top.id="top";top.transform={0,0,100,100};top.raster=Raster::filled(100,100,{20,200,80,255});d.layers.push_back(top);check(center(cache.render(d,"active",{50,50},20,1,4,true))==Pixel{20,200,80,255},"All Layers ignored composite");check(center(cache.render(d,"active",{50,50},20,1,4,false))==Pixel{200,50,30,255},"Sample mode cache stale");
 }else if(name=="transformed_raw_source"){
  const Pixel pixels[]{Pixel{200,50,30,255},Pixel{20,200,80,255}};auto& l=d.layers[0];l.raster=Raster::fromRgba(2,1,reinterpret_cast<const uint8_t*>(pixels),8);l.transform={15,20,60,40,31,true,false,Transform::Sampling::Nearest};const auto point=l.transform.fromUnit({.75,.5});check(center(cache.render(d,"active",point,8,1,4,false))==Pixel{20,200,80,255},"Transform/flip source coordinates differ");
 }else if(name=="one_click_tip_exact"){
  d.layers[0].raster=Raster::filled(100,100,{255,255,255,255});for(double hardness:{0.,.5,1.}){auto preview=cache.render(d,"active",{50,50},20,hardness,1,false);graphics::BrushSessionSettings settings;settings.radius=10;settings.hardness=hardness;settings.color={255,255,255};graphics::BrushSession stroke(Raster::filled(20,20),settings);stroke.begin({10,10});auto expected=stroke.commit();for(int y=0;y<20;++y)for(int x=0;x<20;++x)check(preview->pixel(x,y)==expected->pixel(x,y),"Tip differs from one real click");}
 }else if(name=="cache_snapshot_invalidation"){
  const auto original=d;auto first=cache.render(d,"active",{50,50},20,1,4,false);check(cache.render(d,"active",{50,50},20,1,4,false)==first,"Unchanged hover did not reuse result");d.layers[0].raster=Raster::filled(100,100,{20,200,80,255});auto second=cache.render(d,"active",{50,50},20,1,4,false);check(second!=first&&center(second)==Pixel{20,200,80,255},"Changed source reused old result");check(original.layers[0].raster->pixel(0,0)==Pixel{200,50,30,255},"Cached source mutated");cache.reset();check(cache.render(d,"active",{50,50},20,1,4,false)!=second,"Reset retained result identity");
 }else if(name=="live_preview_identity"){
  auto preview=std::make_shared<LayerRenderPreview>();preview->layer=d.layers[0];preview->identity=std::make_shared<int>(1);preview->image=[](Point,Transform::Sampling){return Pixel{40,80,120,255};};const auto original=d;check(center(cache.render(d,"active",{50,50},20,1,4,true,preview))==Pixel{40,80,120,255},"Live source override ignored");check(d==original,"Live preview mutated document");
 }else if(name=="large_canvas_bounded_roi"){
  d.width=d.height=30000;d.layers[0].transform={14950,14950,100,100};Raster::resetMaterializationCount();auto preview=cache.render(d,"active",{15000,15000},40,1,32,true);check(preview->width==1024&&preview->height==1024&&preview->tiles.size()==16,"Source1024 cap not retained");check(center(preview)==Pixel{200,50,30,255},"Large ROI sampled wrong source");check(Raster::materializationCount()==0,"Preview flattened a source document");
 }else if(name=="fractional_ceil_sampling"){
  auto preview=cache.render(d,"active",{50,50},1.1,1,32,false);check(preview&&preview->width==36&&preview->height==36,"Fractional ceil preview grid");rejects([&]{SoftwareRenderer().renderScaled(d,0,0,36,36,1.1/36);});auto valid=SoftwareRenderer().renderCursorRegion(d,0,0,36,36,1.1/36);check(valid->width==36,"Bounded cursor entry missing");
 }else if(name=="invalid_bounds_and_missing_source"){
  check(!cache.render(d,"missing",{50,50},20,1,4,false),"Missing current source yielded image");for(double diameter:{0.,-1.,2001.,std::numeric_limits<double>::infinity()})rejects([&]{cache.render(d,"active",{50,50},diameter,1,4,false);});for(double step:{0.,-1.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})rejects([&]{SoftwareRenderer().renderCursorRegion(d,0,0,1,1,step);});rejects([&]{SoftwareRenderer().renderCursorRegion(d,0,0,1025,1,1);});rejects([&]{SoftwareRenderer().renderCursorRegion(d,0,0,1,1025,1);});
 }else throw std::runtime_error("Unknown case");
}
}
int main(int argc,char**argv){const std::array<const char*,9> names{"raw_current_appearance_ignored","all_layers_composite","transformed_raw_source","one_click_tip_exact","cache_snapshot_invalidation","live_preview_identity","large_canvas_bounded_roi","fractional_ceil_sampling","invalid_bounds_and_missing_source"};if(argc>1&&std::find(names.begin(),names.end(),std::string(argv[1]))==names.end())return 2;int failed=0;for(auto name:names)if(argc==1||name==std::string(argv[1]))try{run(name);std::cout<<"PASS "<<name<<'\n';}catch(const std::exception&e){++failed;std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}return failed?1:0;}
