#include "core/Document.h"
#include "core/DocumentExport.h"
#include "effects/Adjustments.h"
#include "imaging/wic_codec.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <functional>
#include <iostream>
#include <initializer_list>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
using namespace compositor;
#define REQUIRE(...) do{if(!(__VA_ARGS__))throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+": " #__VA_ARGS__);}while(false)
Document document(int width,int height,Pixel color={90,40,20,128}){Document d;d.id=newId();d.width=width;d.height=height;Layer layer;layer.id=newId();layer.raster=Raster::filled(width,height,color);layer.transform={0,0,double(width),double(height),0,false,false,Transform::Sampling::Nearest};layer.opacity=.75;d.layers={layer};return d;}
void equal(const Raster& a,const Raster& b,int slop=0){REQUIRE(a.width==b.width&&a.height==b.height);for(int y=0;y<a.height;++y)for(int x=0;x<a.width;++x){const auto p=a.pixel(x,y),q=b.pixel(x,y);auto delta=[&](uint8_t u,uint8_t v){return std::abs(int(u)-int(v));};if(delta(p.r,q.r)>slop||delta(p.g,q.g)>slop||delta(p.b,q.b)>slop||delta(p.a,q.a)>slop)throw std::runtime_error("pixel mismatch at "+std::to_string(x)+","+std::to_string(y)+" tiled="+std::to_string(p.r)+","+std::to_string(p.g)+","+std::to_string(p.b)+","+std::to_string(p.a)+" full="+std::to_string(q.r)+","+std::to_string(q.g)+","+std::to_string(q.b)+","+std::to_string(q.a));}}
size_t unique(const Raster& raster){std::set<const Raster::Tile*> pointers;for(auto& tile:raster.tiles){REQUIRE(tile);pointers.insert(tile.get());}return pointers.size();}
void blank_30000(){Document d;d.width=d.height=30000;CompositeCache cache;Raster::resetMaterializationCount();auto full=cache.render(d);REQUIRE(full->width==30000&&full->height==30000&&full->tiles.size()==13924&&unique(*full)==1);REQUIRE(full->pixel(29999,29999)==Pixel{}&&cache.render(d)==full);auto view=cache.renderViewport(d,0,0,30000,30000);REQUIRE(view.documentWidth==30000&&view.documentHeight==30000&&view.unitsPerPixel==16);REQUIRE(view.raster->tiles.size()==64&&unique(*view.raster)==1&&view.raster->pixel(1874,1874)==Pixel{});REQUIRE(Raster::materializationCount()==0);}
void identity_fastpath(){auto d=document(768,512);d.layers[0].opacity=1;CompositeCache cache;REQUIRE(cache.render(d)==d.layers[0].raster);auto view=cache.renderViewport(d,256,256,256,256);REQUIRE(view.documentX==256&&view.documentY==256&&view.unitsPerPixel==1&&view.raster->tiles.size()==1&&view.raster->tiles[0]==d.layers[0].raster->tiles[4]);}
void identity_fastpath_respects_effects(){
    auto d=document(768,512,{0,0,0,255});d.layers[0].opacity=1;
    d.layers[0].effects.specified=true;d.layers[0].effects.colorOverlay.enabled=true;d.layers[0].effects.colorOverlay.color={255,0,0,255};d.layers[0].effects.colorOverlay.opacity=1;
    CompositeCache cache;auto full=cache.render(d);REQUIRE(full!=d.layers[0].raster);REQUIRE(full->pixel(10,10)==Pixel{255,0,0,255});
    equal(*full,*SoftwareRenderer().render(d,0,0,d.width,d.height));
    auto view=cache.renderViewport(d,0,0,256,256);REQUIRE(view.raster->pixel(10,10)==Pixel{255,0,0,255});
}
void normal_equivalence(){auto d=document(513,259);Layer over=d.layers[0];over.id=newId();over.raster=Raster::filled(50,100,{0,160,0,200});over.transform={21.5,48.25,80,170,19,true,false,Transform::Sampling::Smooth};over.blend=Blend::Multiply;d.layers.push_back(over);CompositeCache cache;auto full=cache.render(d);equal(*full,*SoftwareRenderer().render(d,0,0,d.width,d.height));REQUIRE(cache.render(d)==full);auto view=cache.renderViewport(d,260,0,240,258);REQUIRE(view.documentX==256&&view.unitsPerPixel==1);for(int y=0;y<view.raster->height;++y)for(int x=0;x<view.raster->width;++x)REQUIRE(view.raster->pixel(x,y)==full->pixel(x+256,y));}
void offscreen_invalidation(){auto d=document(768,256);CompositeCache cache;auto fullBefore=cache.render(d);auto left=cache.renderViewport(d,0,0,256,256,1,1,3);auto right=cache.renderViewport(d,512,0,256,256,1,1,3);Pixel changed{0,0,255,255};d.layers[0].raster=d.layers[0].raster->replacing(600,50,1,1,&changed,1);auto leftAfter=cache.renderViewport(d,0,0,256,256,1,1,3);REQUIRE(leftAfter.raster->tiles[0]==left.raster->tiles[0]);auto rightAfter=cache.renderViewport(d,512,0,256,256,1,1,3);REQUIRE(rightAfter.raster->tiles[0]!=right.raster->tiles[0]&&rightAfter.raster->pixel(88,50).b==191);auto full=cache.render(d);REQUIRE(full->tiles[0]==fullBefore->tiles[0]&&full->pixel(600,50).b==191);REQUIRE(right.raster->pixel(88,50).b!=191);equal(*full,*SoftwareRenderer().render(d,0,0,768,256));}
void bounded_eviction(){auto d=document(768,256);CompositeCache cache;auto first=cache.renderViewport(d,0,0,256,256,1,1,2);auto middle=cache.renderViewport(d,256,0,256,256,1,1,2);auto last=cache.renderViewport(d,512,0,256,256,1,1,2);auto again=cache.renderViewport(d,0,0,256,256,1,1,2);REQUIRE(again.raster->tiles[0]!=first.raster->tiles[0]);equal(*again.raster,*first.raster);REQUIRE(middle.raster->tiles.size()==1&&last.raster->tiles.size()==1);}
void metadata_and_hidden_source(){auto d=document(513,1,{255,0,0,255});Layer mask=d.layers[0];mask.id=newId();mask.visible=false;mask.opacity=1;mask.raster=Raster::filled(513,1,{0,0,0,128});d.layers[0].maskSourceId=mask.id;d.layers[0].opacity=1;d.layers.push_back(mask);CompositeCache cache;auto before=cache.renderViewport(d,512,0,1,1);REQUIRE(before.raster->pixel(0,0).a==128);d.layers[1].raster=Raster::filled(513,1,{0,0,0,64});cache.renderViewport(d,0,0,1,1);auto after=cache.renderViewport(d,512,0,1,1);REQUIRE(after.raster->pixel(0,0).a==64&&before.raster->pixel(0,0).a==128);d.layers[0].visible=false;auto empty=cache.renderViewport(d,512,0,1,1);REQUIRE(empty.raster->pixel(0,0)==Pixel{});}
void full_after_partial(){auto d=document(513,257);CompositeCache cache;auto partial=cache.renderViewport(d,0,0,32,32);REQUIRE(partial.raster->width==256&&partial.raster->height==256);auto full=cache.render(d);REQUIRE(full->width==513&&full->height==257&&full->tiles.size()==6);equal(*full,*SoftwareRenderer().render(d,0,0,513,257));}
void lod_large_content(){Document d;d.width=d.height=30000;Layer layer;layer.id=newId();layer.raster=Raster::filled(1,1,{30,60,90,255});layer.transform={0,0,30000,30000};d.layers={layer};CompositeCache cache;auto view=cache.renderViewport(d,0,0,30000,30000,1,4,8);REQUIRE(view.unitsPerPixel==64&&view.raster->tiles.size()==4&&view.raster->width==469);REQUIRE(view.raster->pixel(200,200)==Pixel{30,60,90,255});auto zoomed=cache.renderViewport(d,1234,2345,30,30,1,4,8);REQUIRE(zoomed.unitsPerPixel==1&&zoomed.raster->tiles.size()<=4&&zoomed.raster->pixel(10,10)==Pixel{30,60,90,255});auto smallest=cache.renderViewport(d,0,0,30000,30000,30000,1,1);REQUIRE(smallest.raster->width==1&&smallest.unitsPerPixel==30000);}
void scaled_renderer_contract(){auto d=document(10,4);std::vector<Pixel> pixels(40);for(int y=0;y<4;++y)for(int x=0;x<10;++x)pixels[size_t(y)*10+x]={uint8_t(x*20),uint8_t(y*40),0,255};d.layers[0].raster=Raster::fromRgba(10,4,reinterpret_cast<const uint8_t*>(pixels.data()),40);d.layers[0].opacity=1;SoftwareRenderer renderer;equal(*renderer.render(d,0,0,10,4),*renderer.renderScaled(d,0,0,10,4,1));auto scaled=renderer.renderScaled(d,.25,.25,5,2,2);for(int y=0;y<2;++y)for(int x=0;x<5;++x)REQUIRE(scaled->pixel(x,y)==pixels[size_t(y*2+1)*10+x*2+1]);}
void scaled_grain_partition(){auto d=document(1032,8,{100,100,100,255});Layer grain;grain.id=newId();grain.adjustmentJson=effects::defaultAdjustmentJson("Grain");grain.transform={0,0,1032,8};d.layers.push_back(grain);SoftwareRenderer renderer;auto full=renderer.renderScaled(d,0,0,258,2,4);auto left=renderer.renderScaled(d,0,0,256,2,4),right=renderer.renderScaled(d,1024,0,2,2,4);for(int y=0;y<2;++y)for(int x=0;x<258;++x)REQUIRE(full->pixel(x,y)==(x<256?left->pixel(x,y):right->pixel(x-256,y)));}
void outside_and_rejections(){auto d=document(20,20);CompositeCache cache;REQUIRE(!cache.renderViewport(d,21,0,5,5).raster);REQUIRE(!cache.renderViewport(d,0,0,0,0).raster);for(auto action:std::vector<std::function<void()>>{[&]{cache.renderViewport(d,0,0,-1,1);},[&]{cache.renderViewport(d,NAN,0,1,1);},[&]{cache.renderViewport(d,0,0,1,1,0);},[&]{cache.renderViewport(d,0,0,1,1,1,2,1);}}){bool rejected=false;try{action();}catch(const std::invalid_argument&){rejected=true;}REQUIRE(rejected);}for(double units:{0.,-1.,double(NAN),double(INFINITY),1/64.}){bool rejected=false;try{SoftwareRenderer().renderScaled(d,0,0,1,1,units);}catch(const std::invalid_argument&){rejected=true;}REQUIRE(rejected);}auto halfDocument=d;halfDocument.layers[0].opacity=1;const std::array<Pixel,2> pair{{{255,0,0,255},{0,255,0,255}}};halfDocument.layers[0].raster=halfDocument.layers[0].raster->replacing(0,0,2,1,pair.data(),2);auto half=SoftwareRenderer().renderScaled(halfDocument,0,0,4,2,.5);REQUIRE(half->width==4&&half->height==2);for(int y=0;y<2;++y)for(int x=0;x<4;++x)REQUIRE(half->pixel(x,y)==pair[size_t(x/2)]);cache.reset();REQUIRE(cache.renderViewport(d,0,0,20,20).raster->pixel(10,10)==SoftwareRenderer().render(d,10,10,1,1)->pixel(0,0));}
void blank_culling(){Document d;d.width=d.height=30000;Layer l;l.id=newId();l.raster=Raster::filled(1,1,{255,0,0,255});l.transform={-100,-100,1,1};d.layers={l};CompositeCache cache;auto full=cache.render(d);REQUIRE(unique(*full)==1&&full->pixel(0,0)==Pixel{});Layer group;group.id=newId();group.group=true;group.visible=false;l.parentId=group.id;l.transform={0,0,30000,30000};d.layers={group,l};REQUIRE(unique(*cache.render(d))==1);}

void lod_offscreen_invalidation(){auto d=document(768,64);d.width=2304;d.layers[0].transform.width=2304;CompositeCache cache;auto left=cache.renderViewport(d,0,0,512,64,2,1,3);auto right=cache.renderViewport(d,1536,0,512,64,2,1,3);std::vector<Pixel> blue(256*64,{0,0,255,255});d.layers[0].raster=d.layers[0].raster->replacing(512,0,256,64,blue.data(),256);auto leftAfter=cache.renderViewport(d,0,0,512,64,2,1,3);REQUIRE(leftAfter.raster->tiles[0]==left.raster->tiles[0]);auto rightAfter=cache.renderViewport(d,1536,0,512,64,2,1,3);REQUIRE(rightAfter.unitsPerPixel==2&&rightAfter.raster->tiles[0]!=right.raster->tiles[0]&&rightAfter.raster->pixel(0,0).b==191);}
void blur_adjustment_tile_seam(){Document d;d.id=newId();d.width=512;d.height=32;Layer image;image.id=newId();image.opacity=1;image.raster=Raster::filled(512,32,{255,0,0,255});image.transform={0,0,512,32,0,false,false,Transform::Sampling::Nearest};Layer blur;blur.id=newId();blur.adjustmentJson=effects::defaultAdjustmentJson("Gaussian Blur");blur.transform={0,0,512,32};d.layers={image,blur};auto full=SoftwareRenderer().render(d,0,0,512,32);CompositeCache cache;auto tiled=cache.render(d);REQUIRE(tiled->pixel(255,16)==full->pixel(255,16)&&tiled->pixel(256,16)==full->pixel(256,16));REQUIRE(tiled->pixel(256,16).a>200);}
void tonal_adjustment_tile_seam(){
    Document d;d.id=newId();d.width=512;d.height=32;std::vector<Pixel> pixels(512*32);for(int y=0;y<32;++y)for(int x=0;x<512;++x)pixels[size_t(y)*512+x]={uint8_t((x*17+y*3)%256),uint8_t((x*7+y*19)%256),uint8_t((x*13+y*11)%256),255};
    Layer image;image.id=newId();image.raster=Raster::filled(512,32,{})->replacing(0,0,512,32,pixels.data(),512);image.transform={0,0,512,32,0,false,false,Transform::Sampling::Nearest};Layer tonal;tonal.id=newId();tonal.transform={0,0,512,32};auto json=QJsonDocument::fromJson(QByteArray::fromStdString(effects::defaultAdjustmentJson("Tonal Contrast"))).object();auto group=json["filterLayerSettings"].toObject();auto settings=group["tonal"].toObject();settings["radius"]=4;settings["amount"]=100;group["tonal"]=settings;json["filterLayerSettings"]=group;tonal.adjustmentJson=QJsonDocument(json).toJson(QJsonDocument::Compact).toStdString();d.layers={image,tonal};
    auto full=SoftwareRenderer().render(d,0,0,512,32);CompositeCache cache;auto tiled=cache.render(d);equal(*tiled,*full);auto scaled=SoftwareRenderer().renderScaled(d,0,0,256,16,2);auto view=cache.renderViewport(d,0,0,512,32,2);equal(*view.raster,*scaled);
    auto overview=SoftwareRenderer().renderScaled(d,0,0,8,1,64);auto left=SoftwareRenderer().renderScaled(d,0,0,4,1,64);auto right=SoftwareRenderer().renderScaled(d,256,0,4,1,64);for(int x=0;x<8;++x)REQUIRE(overview->pixel(x,0)==(x<4?left->pixel(x,0):right->pixel(x-4,0)));
    settings["radius"]=100;group["tonal"]=settings;json["filterLayerSettings"]=group;d.layers[1].adjustmentJson=QJsonDocument(json).toJson(QJsonDocument::Compact).toStdString();
    auto largeFull=SoftwareRenderer().render(d,0,0,512,32);CompositeCache largeCache;equal(*largeCache.render(d),*largeFull);
    settings["amount"]=0;group["tonal"]=settings;json["filterLayerSettings"]=group;d.layers[1].adjustmentJson=QJsonDocument(json).toJson(QJsonDocument::Compact).toStdString();
    auto identity=SoftwareRenderer().renderScaled(d,0,0,256,256,1./32);d.layers.pop_back();equal(*identity,*SoftwareRenderer().renderScaled(d,0,0,256,256,1./32));
}
void sharpen_adjustment_tile_seam(){
    Document d;d.id=newId();d.width=512;d.height=32;std::vector<Pixel> pixels(512*32);for(int y=0;y<32;++y)for(int x=0;x<512;++x)pixels[size_t(y)*512+x]={uint8_t((x*17+y*3)%256),uint8_t((x*7+y*19)%256),uint8_t((x*13+y*11)%256),255};
    Layer image;image.id=newId();image.raster=Raster::filled(512,32,{})->replacing(0,0,512,32,pixels.data(),512);image.transform={0,0,512,32,0,false,false,Transform::Sampling::Nearest};Layer sharpen;sharpen.id=newId();sharpen.transform={0,0,512,32};auto json=QJsonDocument::fromJson(QByteArray::fromStdString(effects::defaultAdjustmentJson("Sharpen"))).object();auto group=json["filterLayerSettings"].toObject();auto settings=group["sharpen"].toObject();settings["radius"]=4;settings["amount"]=100;group["sharpen"]=settings;json["filterLayerSettings"]=group;sharpen.adjustmentJson=QJsonDocument(json).toJson(QJsonDocument::Compact).toStdString();d.layers={image,sharpen};
    auto full=SoftwareRenderer().render(d,0,0,512,32);CompositeCache cache;auto tiled=cache.render(d);equal(*tiled,*full);auto scaled=SoftwareRenderer().renderScaled(d,0,0,256,16,2);auto view=cache.renderViewport(d,0,0,512,32,2);equal(*view.raster,*scaled);
    const Pixel changed{255,255,255,255};d.layers[0].raster=d.layers[0].raster->replacing(255,16,1,1,&changed,1);equal(*cache.render(d),*SoftwareRenderer().render(d,0,0,512,32));
    auto overview=SoftwareRenderer().renderScaled(d,0,0,8,1,64);auto left=SoftwareRenderer().renderScaled(d,0,0,4,1,64);auto right=SoftwareRenderer().renderScaled(d,256,0,4,1,64);for(int x=0;x<8;++x)REQUIRE(overview->pixel(x,0)==(x<4?left->pixel(x,0):right->pixel(x-4,0)));
    settings["radius"]=20;group["sharpen"]=settings;json["filterLayerSettings"]=group;d.layers[1].adjustmentJson=QJsonDocument(json).toJson(QJsonDocument::Compact).toStdString();
    auto largeFull=SoftwareRenderer().render(d,0,0,512,32);CompositeCache largeCache;equal(*largeCache.render(d),*largeFull);
    settings["amount"]=0;group["sharpen"]=settings;json["filterLayerSettings"]=group;d.layers[1].adjustmentJson=QJsonDocument(json).toJson(QJsonDocument::Compact).toStdString();
    auto identity=SoftwareRenderer().renderScaled(d,0,0,256,256,1./32);d.layers.pop_back();equal(*identity,*SoftwareRenderer().renderScaled(d,0,0,256,256,1./32));
}
void rgb_split_adjustment_tile_seam(){
    Document d;d.id=newId();d.width=512;d.height=32;
    std::vector<Pixel> pixels(512*32);
    for(int y=0;y<32;++y)for(int x=0;x<512;++x)
        pixels[std::size_t(y)*512+x]={std::uint8_t((x*17+y*3)%256),std::uint8_t((x*7+y*19)%256),std::uint8_t((x*13+y*11)%256),255};
    Layer image;image.id=newId();image.opacity=1;image.raster=Raster::filled(512,32,{})->replacing(0,0,512,32,pixels.data(),512);image.transform={0,0,512,32,0,false,false,Transform::Sampling::Nearest};
    Layer split;split.id=newId();auto json=QJsonDocument::fromJson(QByteArray::fromStdString(effects::defaultAdjustmentJson("RGB Split"))).object();auto layer=json["filterLayerSettings"].toObject();layer["rgbSplitDistance"]=18;layer["rgbSplitBlur"]=8;layer["rgbSplitCenter"]=QJsonArray{256,16};
    for(bool radial:{false,true}){
        layer["rgbSplitRadial"]=radial;json["filterLayerSettings"]=layer;split.adjustmentJson=QJsonDocument(json).toJson(QJsonDocument::Compact).toStdString();split.transform={0,0,512,32};d.layers={image,split};
        auto full=SoftwareRenderer().render(d,0,0,512,32);CompositeCache cache;auto tiled=cache.render(d);equal(*tiled,*full);
    }
    layer["rgbSplitRadial"]=true;layer["rgbSplitDistance"]=40;layer["rgbSplitBlur"]=24;layer["rgbSplitReach"]=80;layer["rgbSplitCenter"]=QJsonObject{{"x",2},{"y",16}};
    json["filterLayerSettings"]=layer;split.adjustmentJson=QJsonDocument(json).toJson(QJsonDocument::Compact).toStdString();d.layers={image,split};
    auto full=SoftwareRenderer().render(d,0,0,512,32);CompositeCache cache;auto tiled=cache.render(d);
    equal(*tiled,*full);
    auto scaled=SoftwareRenderer().renderScaled(d,0,0,256,16,2);auto view=cache.renderViewport(d,0,0,512,32,2);
    equal(*view.raster,*scaled);
    layer.remove("rgbSplitCenter");json["filterLayerSettings"]=layer;split.adjustmentJson=QJsonDocument(json).toJson(QJsonDocument::Compact).toStdString();d.layers={image,split};
    full=SoftwareRenderer().render(d,0,0,512,32);cache.reset();tiled=cache.render(d);
    equal(*tiled,*full);
}
void filter_default_points_tiles(){
    Document d;d.id=newId();d.width=512;d.height=48;
    std::vector<Pixel> pixels(512*48);
    for(int y=0;y<48;++y)for(int x=0;x<512;++x)
        pixels[std::size_t(y)*512+x]={std::uint8_t((x*11+y)%256),std::uint8_t((x+y*5)%256),std::uint8_t((x*3)%256),255};
    Layer image;image.id=newId();image.opacity=1;image.raster=Raster::filled(512,48,{})->replacing(0,0,512,48,pixels.data(),512);image.transform={0,0,512,48,0,false,false,Transform::Sampling::Nearest};
    auto attach=[&](const char* kind,std::initializer_list<std::pair<const char*,QJsonValue>> fields){
        Layer adj;adj.id=newId();auto json=QJsonDocument::fromJson(QByteArray::fromStdString(effects::defaultAdjustmentJson(kind))).object();auto layer=json["filterLayerSettings"].toObject();
        for(const auto& field:fields)layer.insert(field.first,field.second);
        layer.remove("progressiveStart");layer.remove("progressiveEnd");layer.remove("rgbSplitCenter");
        json["filterLayerSettings"]=layer;adj.adjustmentJson=QJsonDocument(json).toJson(QJsonDocument::Compact).toStdString();adj.transform={0,0,512,48};d.layers={image,adj};
        auto full=SoftwareRenderer().render(d,0,0,512,48);CompositeCache cache;equal(*cache.render(d),*full,1);
        auto view=cache.renderViewport(d,0,0,512,48,2);
        REQUIRE(view.raster&&view.unitsPerPixel==2);
        equal(*view.raster,*SoftwareRenderer().renderScaled(d,view.documentX,view.documentY,view.raster->width,view.raster->height,view.unitsPerPixel),1);
    };
    attach("RGB Split",{{"rgbSplitDistance",18},{"rgbSplitBlur",10},{"rgbSplitRadial",true}});
    attach("Progressive Blur",{{"progressiveFrom",0},{"progressiveTo",16}});
}
void progressive_deep_scaled(){
    auto fill=[&](DeepImage::Format format,float peak){
        Document d;d.id=newId();d.width=320;d.height=48;
        auto deep=makeDeep(320,48,format);
        for(int y=0;y<48;++y)for(int x=0;x<320;++x){const float t=float(x)/319.f;deep->setPixel(x,y,{t*peak,.25f*peak,(1.f-t)*peak,1});}
        Layer image;image.id=newId();image.opacity=1;image.deep=deep;image.raster=rasterFromDeep(*deep);image.transform={0,0,320,48};
        Layer adj;adj.id=newId();auto json=QJsonDocument::fromJson(QByteArray::fromStdString(effects::defaultAdjustmentJson("Progressive Blur"))).object();auto layer=json["filterLayerSettings"].toObject();
        layer["progressiveFrom"]=2;layer["progressiveTo"]=14;layer.remove("progressiveStart");layer.remove("progressiveEnd");json["filterLayerSettings"]=layer;
        adj.adjustmentJson=QJsonDocument(json).toJson(QJsonDocument::Compact).toStdString();adj.transform={0,0,320,48};d.layers={image,adj};
        auto full=SoftwareRenderer().renderDeep(d,0,0,320,48);auto left=SoftwareRenderer().renderDeep(d,0,0,256,48);auto right=SoftwareRenderer().renderDeep(d,256,0,64,48);
        REQUIRE(full&&left&&right);
        if(format==DeepImage::Format::HalfFloat)REQUIRE(full->format==format);
        auto close=[&](const DeepImage& a,const DeepImage& b,int dx){
            for(int y=0;y<b.height;++y)for(int x=0;x<b.width;++x){const auto p=a.pixel(x+dx,y),q=b.pixel(x,y);
                for(int c=0;c<4;++c)if(std::abs(p[c]-q[c])>1.5f/255.f)throw std::runtime_error("deep seam at "+std::to_string(x+dx)+","+std::to_string(y)+" d="+std::to_string(std::abs(p[c]-q[c])));}
        };
        close(*full,*left,0);close(*full,*right,256);
        auto canvas=SoftwareRenderer().render(d,0,0,320,48);CompositeCache cache;equal(*cache.render(d),*canvas,1);
        for(double units:{1.,2.,4.}){
            auto view=cache.renderViewport(d,0,0,320,48,units);
            REQUIRE(view.raster&&view.unitsPerPixel==units);
            equal(*view.raster,*SoftwareRenderer().renderScaled(d,view.documentX,view.documentY,view.raster->width,view.raster->height,view.unitsPerPixel),1);
        }
        QTemporaryDir dir;REQUIRE(dir.isValid());
        const auto png=std::filesystem::path(dir.filePath("progressive-deep.png").toStdWString());
        imaging::ExportOptions pngOptions;pngOptions.format=imaging::ImageFormat::Png;
        exportDocumentAtomic(d,png,pngOptions);
        REQUIRE(imaging::WicCodec::decode(png).image.pixels==canvas->rgba());
    };
    fill(DeepImage::Format::Unorm16,1.f);fill(DeepImage::Format::HalfFloat,2.f);
}
Layer solid(const char* id,int width,int height,Pixel color,Transform transform){Layer layer;layer.id=id;layer.opacity=1;layer.raster=Raster::filled(width,height,color);layer.transform=transform;return layer;}
void partial_metadata_damage(){
    Document d;d.id=newId();d.width=768;d.height=256;
    d.layers.push_back(solid("bg",768,256,{20,40,80,255},{0,0,768,256,0,false,false,Transform::Sampling::Nearest}));
    Layer over=solid("over",200,80,{200,30,30,255},{20,40,200,80,0,false,false,Transform::Sampling::Nearest});
    over.effects.specified=true;over.effects.dropShadow.enabled=true;over.effects.dropShadow.opacity=1;over.effects.dropShadow.distance=80;over.effects.dropShadow.size=8;over.effects.dropShadow.angle=0;
    d.layers.push_back(over);
    CompositeCache cache;auto before=cache.renderViewport(d,0,0,768,256,1,64,256);const auto far=before.raster->tiles[2];
    d.layers[1].visible=false;auto hidden=cache.renderViewport(d,0,0,768,256,1,64,256);
    REQUIRE(hidden.raster->tiles[2]==far);REQUIRE(hidden.raster->tiles[0]!=before.raster->tiles[0]);
    equal(*hidden.raster,*SoftwareRenderer().renderScaled(d,hidden.documentX,hidden.documentY,hidden.raster->width,hidden.raster->height,1));
    d.layers[1].visible=true;auto shown=cache.renderViewport(d,0,0,768,256,1,64,256);REQUIRE(shown.raster->tiles[2]==far);
    d.layers[1].name="Renamed";auto renamed=cache.renderViewport(d,0,0,768,256,1,64,256);REQUIRE(renamed.raster->tiles==shown.raster->tiles);
    d.layers[1].opacity=.4;auto faded=cache.renderViewport(d,0,0,768,256,1,64,256);
    REQUIRE(faded.raster->tiles[2]==renamed.raster->tiles[2]);
    equal(*faded.raster,*SoftwareRenderer().renderScaled(d,faded.documentX,faded.documentY,faded.raster->width,faded.raster->height,1));
    d.layers[1].opacity=1;d.layers[1].transform.flipX=true;auto flipped=cache.renderViewport(d,0,0,768,256,1,64,256);
    REQUIRE(flipped.raster->tiles[2]==faded.raster->tiles[2]);
    equal(*flipped.raster,*SoftwareRenderer().renderScaled(d,flipped.documentX,flipped.documentY,flipped.raster->width,flipped.raster->height,1));
}
void group_visibility_damage(){
    Document d;d.id=newId();d.width=768;d.height=256;
    d.layers.push_back(solid("bg",768,256,{20,40,80,255},{0,0,768,256,0,false,false,Transform::Sampling::Nearest}));
    Layer group;group.id="group";group.group=true;group.opacity=1;
    Layer child=solid("child",180,90,{220,180,20,255},{30,20,180,90,0,false,false,Transform::Sampling::Nearest});child.parentId=group.id;
    d.layers.push_back(group);d.layers.push_back(child);
    CompositeCache cache;auto before=cache.renderViewport(d,0,0,768,256,1,64,256);const auto far=before.raster->tiles[2];
    d.layers[1].visible=false;auto hidden=cache.renderViewport(d,0,0,768,256,1,64,256);
    REQUIRE(hidden.raster->tiles[2]==far);REQUIRE(hidden.raster->tiles[0]!=before.raster->tiles[0]);
    equal(*hidden.raster,*SoftwareRenderer().renderScaled(d,hidden.documentX,hidden.documentY,hidden.raster->width,hidden.raster->height,1));
}
void flip_full_frame_matches(){
    Document d;d.id=newId();d.width=512;d.height=256;
    auto raster=Raster::filled(512,256,{10,20,30,255});const Pixel red{255,0,0,255},blue{0,0,255,255};
    raster=raster->replacing(0,4,1,1,&red,1);raster=raster->replacing(511,20,1,1,&blue,1);
    Layer layer;layer.id="photo";layer.opacity=1;layer.raster=raster;layer.transform={0,0,512,256};
    d.layers={layer};
    CompositeCache cache;cache.renderViewport(d,0,0,512,256,1,64,256);
    d.layers[0].transform.flipX=true;auto horizontal=cache.renderViewport(d,0,0,512,256,1,64,256);
    equal(*horizontal.raster,*SoftwareRenderer().renderScaled(d,horizontal.documentX,horizontal.documentY,horizontal.raster->width,horizontal.raster->height,1));
    REQUIRE(horizontal.raster->pixel(0,20).b==255);REQUIRE(horizontal.raster->pixel(511,4).r==255);
    d.layers[0].transform.flipY=true;auto both=cache.renderViewport(d,0,0,512,256,1,64,256);
    equal(*both.raster,*SoftwareRenderer().renderScaled(d,both.documentX,both.documentY,both.raster->width,both.raster->height,1));
}
void navigator_overview_isolated(){
    auto d=document(1024,512,{255,0,0,255});d.layers[0].opacity=1;
    CompositeCache view,overview;
    auto patch=view.renderViewport(d,0,0,256,256,1,4,8);const auto tile=patch.raster->tiles[0];
    const double units=std::max(1.,d.width/160.);
    auto mini=overview.renderViewport(d,0,0,d.width,d.height,units,4,16);
    REQUIRE(mini.raster&&mini.unitsPerPixel+1e-9>=units&&mini.raster->width<=256&&mini.raster->height<=256);
    equal(*mini.raster,*SoftwareRenderer().renderScaled(d,mini.documentX,mini.documentY,mini.raster->width,mini.raster->height,mini.unitsPerPixel));
    auto again=view.renderViewport(d,0,0,256,256,1,4,8);REQUIRE(again.raster->tiles[0]==tile);
    std::vector<Pixel> blue(64*64,{0,0,255,255});d.layers[0].raster=d.layers[0].raster->replacing(0,0,64,64,blue.data(),64);
    auto updated=overview.renderViewport(d,0,0,d.width,d.height,units,4,16);
    REQUIRE(updated.raster->pixel(1,1).b>200&&updated.raster->tiles[0]!=mini.raster->tiles[0]);
    auto untouched=view.renderViewport(d,256,0,256,256,1,4,8);REQUIRE(untouched.raster->pixel(0,0).r>200);
}
void unsupported_blank_adjustment(){Document d;d.width=d.height=30000;Layer adjustment;adjustment.id=newId();adjustment.adjustmentJson="{\"kind\":\"unsupported\"}";d.layers={adjustment};for(bool viewport:{false,true}){CompositeCache cache;bool rejected=false;try{if(viewport)cache.renderViewport(d,0,0,30000,30000);else cache.render(d);}catch(const std::exception&){rejected=true;}REQUIRE(rejected);}d.layers[0].visible=false;CompositeCache cache;REQUIRE(unique(*cache.render(d))==1);}

void move_translation_damage(){
    Document document;document.id=newId();document.width=1920;document.height=1080;
    document.layers.push_back(solid("background",4,4,{20,40,80,255},{0,0,1920,1080,0,false,false,Transform::Sampling::Smooth}));
    Layer photo=solid("photo",960,700,{180,40,20,255},{0,0,960,700,0,false,false,Transform::Sampling::Smooth});
    const Pixel mark{0,220,255,255};photo.raster=photo.raster->replacing(24,36,1,1,&mark,1);document.layers.push_back(photo);
    const double units=4;CompositeCache cache;const auto started=std::chrono::steady_clock::now();
    auto before=cache.renderViewport(document,0,0,1920,1080,units,64,256);
    const auto firstMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    const auto firstTiles=cache.lastViewportComposedTiles();const auto far=before.raster->tiles.at(1);
    document.layers[1].transform.x=10;document.layers[1].transform.y=12;
    const auto moveStarted=std::chrono::steady_clock::now();
    auto moved=cache.renderViewport(document,0,0,1920,1080,units,64,256);
    const auto moveMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-moveStarted).count();
    const auto moveTiles=cache.lastViewportComposedTiles();
    std::cout<<"move_translation_damage zoom_units="<<units<<" first_ms="<<firstMs<<" first_tiles="<<firstTiles<<" translation_ms="<<moveMs<<" translation_tiles="<<moveTiles<<"\n";
    REQUIRE(firstTiles>moveTiles&&moveTiles>0);REQUIRE(moved.raster->tiles.at(1)==far);
    CompositeCache fresh;equal(*moved.raster,*fresh.renderViewport(document,0,0,1920,1080,units,64,256).raster);
    Document shadowed=document;shadowed.layers[1].effects.specified=true;shadowed.layers[1].effects.dropShadow.enabled=true;shadowed.layers[1].effects.dropShadow.opacity=1;shadowed.layers[1].effects.dropShadow.distance=12;shadowed.layers[1].effects.dropShadow.size=4;shadowed.layers[1].effects.dropShadow.angle=0;shadowed.layers[1].transform={0,0,800,500};
    CompositeCache shadowCache;auto shadowBefore=shadowCache.renderViewport(shadowed,0,0,1920,1080,units,64,256);const auto shadowFar=shadowBefore.raster->tiles.at(1);
    shadowed.layers[1].transform.x=8;shadowed.layers[1].transform.y=6;auto shadowMoved=shadowCache.renderViewport(shadowed,0,0,1920,1080,units,64,256);
    REQUIRE(shadowMoved.raster->tiles.at(1)==shadowFar&&shadowCache.lastViewportComposedTiles()<shadowBefore.raster->tiles.size());
    equal(*shadowMoved.raster,*CompositeCache{}.renderViewport(shadowed,0,0,1920,1080,units,64,256).raster);
    Document dependent=document;Layer clipped=document.layers[1];clipped.id="clipped";clipped.maskSourceId=document.layers[1].id;clipped.opacity=.6;dependent.layers.push_back(clipped);
    CompositeCache dependentCache;auto dependentBefore=dependentCache.renderViewport(dependent,0,0,1920,1080,units,64,256);const auto dependentFar=dependentBefore.raster->tiles.at(1);
    dependent.layers[1].transform.x=40;auto dependentMoved=dependentCache.renderViewport(dependent,0,0,1920,1080,units,64,256);
    REQUIRE(dependentMoved.raster->tiles.at(1)!=dependentFar);REQUIRE(dependentCache.lastViewportComposedTiles()>moveTiles);
    equal(*dependentMoved.raster,*CompositeCache{}.renderViewport(dependent,0,0,1920,1080,units,64,256).raster);
    Document native=document;CompositeCache fullScale;const auto nativeStarted=std::chrono::steady_clock::now();
    fullScale.renderViewport(native,0,0,1920,1080,1,64,256);
    const auto nativeMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-nativeStarted).count();
    const auto nativeTiles=fullScale.lastViewportComposedTiles();
    native.layers[1].transform.x=48;const auto nativeMoveStarted=std::chrono::steady_clock::now();
    auto nativeMoved=fullScale.renderViewport(native,0,0,1920,1080,1,64,256);
    const auto nativeMoveMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-nativeMoveStarted).count();
    std::cout<<"move_translation_damage zoom_units=1 first_ms="<<nativeMs<<" first_tiles="<<nativeTiles<<" translation_ms="<<nativeMoveMs<<" translation_tiles="<<fullScale.lastViewportComposedTiles()<<"\n";
    equal(*nativeMoved.raster,*CompositeCache{}.renderViewport(native,0,0,1920,1080,1,64,256).raster);
}

void lens_global_cache(){
    auto d=document(512,32,{0,0,0,255});d.layers[0].opacity=1;
    std::vector<Pixel> light(12*12,{255,255,255,255});
    d.layers[0].raster=d.layers[0].raster->replacing(4,10,12,12,light.data(),12);
    Layer lens;lens.id=newId();lens.name="Lens Effects";lens.transform={0,0,512,32};
    lens.adjustmentJson=effects::defaultAdjustmentJson("Lens Effects");d.layers.push_back(lens);
    CompositeCache cache;auto full=SoftwareRenderer().render(d,0,0,512,32);
    equal(*cache.render(d),*full);
    auto distant=cache.renderViewport(d,256,0,256,32);
    for(int y=0;y<32;++y)for(int x=0;x<256;++x)REQUIRE(distant.raster->pixel(x,y)==full->pixel(x+256,y));
    const double beforeOrigin=std::nextafter(-1.,0.);
    auto nearBoundary=cache.renderViewport(d,beforeOrigin,0,512,32);
    REQUIRE(nearBoundary.raster);
    const int offset=int(std::lround(-nearBoundary.documentX/nearBoundary.unitsPerPixel));
    REQUIRE(nearBoundary.raster->pixel(offset+12,14)==full->pixel(12,14));
    SoftwareRenderer draft({},256);
    constexpr double units=.9,phase=-.23;
    auto first=draft.renderScaled(d,phase,0,256,32,units);
    // A cancellation probe would abort a second lens calculation. Both tiles
    // belong to one image even when their floating-point phases round apart.
    auto second=draft.renderScaled(d,phase+256*units,0,256,32,units,[]{return true;});
    auto combined=SoftwareRenderer({},256).renderScaled(d,phase,0,512,32,units);
    for(int y=0;y<32;++y)for(int x=0;x<256;++x){
        REQUIRE(first->pixel(x,y)==combined->pixel(x,y));
        REQUIRE(second->pixel(x,y)==combined->pixel(x+256,y));
    }
    auto portrait=document(1366,2048,{16,16,16,255});portrait.layers[0].opacity=1;
    std::vector<Pixel> highlight(128*128,{255,255,255,255});
    portrait.layers[0].raster=portrait.layers[0].raster->replacing(160,256,128,128,highlight.data(),128);
    lens.transform={0,0,1366,2048};portrait.layers.push_back(lens);
    CompositeCache portraitPreview;portraitPreview.setLensPreviewMaxEdge(256);
    auto zoomed=portraitPreview.renderViewport(portrait,0,0,800,700,1./1.5);
    REQUIRE(zoomed.raster&&zoomed.unitsPerPixel>=1.);
    REQUIRE(zoomed.raster->pixel(int(350/zoomed.unitsPerPixel),int(320/zoomed.unitsPerPixel)).b>16);
    std::vector<Pixel> dark(12*12,{0,0,0,255});
    d.layers[0].raster=d.layers[0].raster->replacing(4,10,12,12,dark.data(),12);
    auto changed=SoftwareRenderer().render(d,0,0,512,32);
    REQUIRE(changed->rgba()!=full->rgba());equal(*cache.render(d),*changed);
    auto after=cache.renderViewport(d,256,0,256,32);
    for(int y=0;y<32;++y)for(int x=0;x<256;++x)REQUIRE(after.raster->pixel(x,y)==changed->pixel(x+256,y));
    bool rejected=false;try{SoftwareRenderer().renderScaled(d,0,0,1,1,0);}catch(const std::invalid_argument&){rejected=true;}REQUIRE(rejected);
    d.width=3000;d.height=2000;d.layers[0].transform={0,0,3000,2000};d.layers[1].transform={0,0,3000,2000};
    rejected=false;try{SoftwareRenderer().render(d,0,0,1,1);}catch(const std::runtime_error&){rejected=true;}REQUIRE(rejected);
}
void vignette_cache(){
    for(auto size:{std::pair{1026,146},std::pair{146,1026}}){
        auto d=document(size.first,size.second,{40,70,30,128});Layer v;v.id=newId();v.name="Vignette";v.transform={0,0,double(d.width),double(d.height)};v.adjustmentJson=effects::defaultAdjustmentJson("Vignette");v.opacity=.6;d.layers.push_back(v);
        const auto started=std::chrono::steady_clock::now();auto full=SoftwareRenderer().render(d,0,0,d.width,d.height);CompositeCache cache;equal(*cache.render(d),*full);
        auto scaled=SoftwareRenderer().renderScaled(d,0,0,d.width/2,d.height/2,2);auto view=cache.renderViewport(d,0,0,d.width,d.height,2);equal(*view.raster,*scaled);
        auto patch=cache.renderViewport(d,64,32,64,64,1);REQUIRE(patch.raster);auto expected=SoftwareRenderer().renderScaled(d,patch.documentX,patch.documentY,patch.raster->width,patch.raster->height,patch.unitsPerPixel);equal(*patch.raster,*expected);
        std::cout<<"vignette "<<d.width<<"x"<<d.height<<" full/cache/scaled/patch_ms="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count()<<"\n";
    }
}

int main(int argc,char** argv){std::map<std::string,void(*)()> tests{{"sharpen_adjustment_tile_seam",sharpen_adjustment_tile_seam},{"lens_global_cache",lens_global_cache},{"tonal_adjustment_tile_seam",tonal_adjustment_tile_seam},{"vignette_cache",vignette_cache},{"lod_offscreen_invalidation",lod_offscreen_invalidation},{"partial_metadata_damage",partial_metadata_damage},{"group_visibility_damage",group_visibility_damage},{"flip_full_frame_matches",flip_full_frame_matches},{"unsupported_blank_adjustment",unsupported_blank_adjustment},{"navigator_overview_isolated",navigator_overview_isolated},{"blur_adjustment_tile_seam",blur_adjustment_tile_seam},{"rgb_split_adjustment_tile_seam",rgb_split_adjustment_tile_seam},{"filter_default_points_tiles",filter_default_points_tiles},{"progressive_deep_scaled",progressive_deep_scaled},{"blank_30000",blank_30000},{"identity_fastpath",identity_fastpath},{"identity_fastpath_respects_effects",identity_fastpath_respects_effects},{"normal_equivalence",normal_equivalence},{"offscreen_invalidation",offscreen_invalidation},{"bounded_eviction",bounded_eviction},{"metadata_and_hidden_source",metadata_and_hidden_source},{"full_after_partial",full_after_partial},{"lod_large_content",lod_large_content},{"scaled_renderer_contract",scaled_renderer_contract},{"scaled_grain_partition",scaled_grain_partition},{"outside_and_rejections",outside_and_rejections},{"blank_culling",blank_culling},{"move_translation_damage",move_translation_damage}};try{if(argc!=2||!tests.contains(argv[1]))throw std::runtime_error("Specify cache test");tests.at(argv[1])();std::cout<<"PASS "<<argv[1]<<"\n";return 0;}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<"\n";return 1;}}
