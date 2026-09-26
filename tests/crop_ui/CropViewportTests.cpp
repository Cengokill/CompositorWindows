#include "ui/CropViewport.h"
#include "effects/Adjustments.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <array>
#include <iostream>
#include <limits>
#include <string>
using namespace compositor;
namespace {
void check(bool v,const char*m){if(!v)throw std::runtime_error(m);}
Document fixture(){Document d;d.id="crop-viewport";d.width=100;d.height=80;Layer l;l.id="base";l.transform={-30,-20,160,120};l.raster=Raster::filled(160,120,{80,120,160,255});d.layers={l};return d;}
void same(const CompositeViewport& actual,const Document& document,std::shared_ptr<const LayerRenderPreview> preview={}){check(bool(actual.raster),"Missing cropped viewport");auto expected=SoftwareRenderer(std::move(preview)).renderScaled(document,actual.documentX,actual.documentY,actual.raster->width,actual.raster->height,actual.unitsPerPixel);for(int y=0;y<expected->height;++y)for(int x=0;x<expected->width;++x)check(actual.raster->pixel(x,y)==expected->pixel(x,y),"Canonical stack pixels differ");}
void run(const std::string& name){auto d=fixture();const editing::Rect crop{-20,-10,140,100};
 if(name=="negative_fractional_phase"){
  const auto original=d;auto patch=ui::renderCropViewport(d,crop,-7.25,-4.125,20,10,.75);check(patch.documentX==-7.25&&patch.documentY==-4.125&&patch.unitsPerPixel==.75,"Physical sample origin changed");check(patch.raster&&patch.raster->width==27&&patch.raster->height==14,"Physical sample dimensions changed");check(patch.documentWidth==100&&patch.documentHeight==80&&d==original,"Canonical document extent changed");same(patch,d);
 }else if(name=="union_intersection_phase"){
  auto patch=ui::renderCropViewport(d,crop,-21.25,-12.125,20,15,.75);check(patch.documentX==-20.5&&patch.documentY==-10.625,"Intersection reset source phase");check(patch.raster&&patch.raster->width==26&&patch.raster->height==18,"Union intersection dimensions changed");same(patch,d);
 }else if(name=="rotated_mask_clip_stack"){
  Layer source;source.id="clip-source";source.transform={-25,0,40,60,18};source.raster=Raster::filled(40,60,{120,30,20,200});auto mask=std::make_shared<GrayRaster>();mask->width=2;mask->height=2;mask->pixels={255,0,255,255};source.mask=Mask{mask,true,false,Transform{-25,0,40,60,18},{}};d.layers.push_back(source);Layer clipped;clipped.id="clipped";clipped.transform={-25,-10,50,80};clipped.raster=Raster::filled(50,80,{20,100,30,160});clipped.maskSourceId=source.id;d.layers.push_back(clipped);auto patch=ui::renderCropViewport(d,crop,-20,-10,140,100,1);same(patch,d);check(patch.raster->pixel(10,40)!=Pixel{80,120,160,255},"Outside mask/clip fixture did not exercise overlay");
 }else if(name=="global_adjustment_origin"){
  Layer effect;effect.id="grain";auto settings=QJsonDocument::fromJson(QByteArray::fromStdString(effects::defaultAdjustmentJson("Grain"))).object();settings["grainSettings"]=QJsonObject{{"amount",60},{"size",2},{"roughness",40},{"seed",7}};effect.adjustmentJson=QJsonDocument(settings).toJson(QJsonDocument::Compact).toStdString();d.layers.push_back(effect);auto patch=ui::renderCropViewport(d,crop,-20,-10,140,100,1);same(patch,d);auto full=SoftwareRenderer().renderScaled(d,-30,-20,160,120,1);for(int y=0;y<patch.raster->height;++y)for(int x=0;x<patch.raster->width;++x)check(patch.raster->pixel(x,y)==full->pixel(x+10,y+10),"Adjustment global phase moved");
 }else if(name=="immutable_live_source"){
  const auto original=d;auto preview=std::make_shared<LayerRenderPreview>();preview->layer=d.layers[0];preview->identity=std::make_shared<int>(7);preview->image=[](Point,Transform::Sampling){return Pixel{150,90,40,255};};auto patch=ui::renderCropViewport(d,crop,-20,-10,140,100,1,preview);same(patch,d,preview);check(patch.raster->pixel(10,40)==Pixel{150,90,40,255},"Transient source was ignored outside document");check(d==original&&d.layers[0].raster->pixel(0,0)==Pixel{80,120,160,255},"Preview mutated canonical pixels");
 }else if(name=="large_bounded_surface"){
  d.width=30000;d.height=30000;d.layers.clear();Raster::resetMaterializationCount();auto patch=ui::renderCropViewport(d,{-15000,-15000,30000,30000},-15000,-15000,45000,45000,1);check(patch.raster&&patch.raster->tiles.size()<=64,"Crop allocated more than64 output tiles");check(patch.documentWidth==30000&&patch.documentHeight==30000,"Large document metadata changed");check(patch.unitsPerPixel>1&&patch.documentX==-15000&&patch.documentY==-15000,"Bounded step/phase incorrect");check(Raster::materializationCount()==0,"Crop flattened a full raster");
 }else if(name=="empty_and_invalid_requests"){
  check(!ui::renderCropViewport(d,crop,1000,1000,5,5,1).raster,"Empty union intersection rendered");check(!ui::renderCropViewport(d,crop,0,0,0,0,1).raster,"Empty request rendered");for(double value:std::array<double,5>{0,-1,1./64,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){bool rejected=false;try{ui::renderCropViewport(d,crop,0,0,1,1,value);}catch(const std::invalid_argument&){rejected=true;}check(rejected,"Invalid sampling step accepted");}
 }else throw std::runtime_error("Unknown case");
}
}
int main(int argc,char**argv){const std::array<const char*,7> names{"negative_fractional_phase","union_intersection_phase","rotated_mask_clip_stack","global_adjustment_origin","immutable_live_source","large_bounded_surface","empty_and_invalid_requests"};int failed=0;for(auto name:names)if(argc==1||name==std::string(argv[1]))try{run(name);std::cout<<"PASS "<<name<<'\n';}catch(const std::exception&e){++failed;std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}return failed?1:0;}
