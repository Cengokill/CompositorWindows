#include "effects/Adjustments.h"
#include "core/DeepImage.h"
#include "filters/PixelFilters.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>
#include <cmath>
#include <set>
#include <stdexcept>
using namespace compositor;
using namespace compositor::effects;
void check(bool ok,const char*message){if(!ok)throw std::runtime_error(message);}
template<class F>void rejects(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected,"Expected settings rejection");}
QJsonObject settings(const char*kind){return QJsonDocument::fromJson(QByteArray::fromStdString(defaultAdjustmentJson(kind))).object();}
std::string encode(QJsonObject o){return QJsonDocument(o).toJson(QJsonDocument::Compact).toStdString();}
std::shared_ptr<const Raster> apply(std::shared_ptr<const Raster> r,QJsonObject o,const GrayRaster*mask=nullptr,AdjustmentRegion region={}){return applyAdjustment(r,encode(o),mask,region);}
QJsonObject changedLevels(double black=0,double gamma=1,double white=255,double outputBlack=0,double outputWhite=255){return {{"black",black},{"gamma",gamma},{"white",white},{"outputBlack",outputBlack},{"outputWhite",outputWhite}};}
QJsonObject levelSettings(int index,QJsonObject range){auto o=settings("Levels");auto l=o["levels"].toObject();auto a=l["ranges"].toArray();a[index]=range;l["ranges"]=a;o["levels"]=l;return o;}
int main(int argc,char**argv){QCoreApplication application(argc,argv);try{check(argc==2,"Case required");std::string test=argv[1];
 if(test=="identity_handles"){auto r=Raster::filled(259,257,{12,24,36,128});for(auto kind:{"Hue/Saturation","Levels","Curves","Exposure"})check(apply(r,settings(kind))==r,"Identity allocated changed snapshot");auto g=settings("Grain");g["grainSettings"]=QJsonObject{{"amount",0},{"size",1.5},{"roughness",50},{"seed",7}};check(apply(r,g)==r,"Zero grain changed snapshot");}
 else if(test=="exposure_formula"){auto gray=Raster::filled(1,1,{128,128,128,255});auto o=settings("Exposure");o["exposureSettings"]=QJsonObject{{"exposure",1},{"offset",0},{"gamma",1}};auto p=apply(gray,o)->pixel(0,0);check(std::abs(int(p.r)-176)<=2&&p.r==p.b,"Exposure does not double linear light");o["exposureSettings"]=QJsonObject{{"exposure",0},{"offset",0},{"gamma",2}};check(std::abs(int(apply(gray,o)->pixel(0,0).r)-181)<=2,"Exposure gamma formula differs");o["exposureSettings"]=QJsonObject{{"exposure",0},{"offset",.1},{"gamma",1}};check(std::abs(int(apply(Raster::filled(1,1,{0,0,0,255}),o)->pixel(0,0).r)-89)<=2,"Exposure offset not linear");check(apply(Raster::filled(1,1,{64,64,64,128}),o)->pixel(0,0).a==128,"Exposure changed alpha");}
 else if(test=="curves_alpha"){auto o=settings("Curves");auto c=o["curves"].toObject();auto a=c["channels"].toArray();a[0]=QJsonArray{QJsonObject{{"x",0},{"y",255}},QJsonObject{{"x",255},{"y",255}}};c["channels"]=a;o["curves"]=c;for(uint8_t alpha:{uint8_t(255),uint8_t(128),uint8_t(32),uint8_t(0)}){auto p=apply(Raster::filled(1,1,{uint8_t(alpha/4),uint8_t(alpha/2),0,alpha}),o)->pixel(0,0);check(p==Pixel{alpha,alpha,alpha,alpha},"Curves did not retain premultiplied alpha");}}
 else if(test=="gradient_endpoints"){auto o=settings("Gradient Map");QJsonObject map{{"shadows",QJsonObject{{"red",1},{"green",0},{"blue",0}}},{"highlights",QJsonObject{{"red",0},{"green",0},{"blue",1}}},{"reversed",false}};o["gradientMapSettings"]=map;check(apply(Raster::filled(1,1,{0,0,0,255}),o)->pixel(0,0)==Pixel{255,0,0,255},"Gradient shadow endpoint wrong");check(apply(Raster::filled(1,1,{128,128,128,128}),o)->pixel(0,0)==Pixel{0,0,128,128},"Gradient transparent highlight wrong");auto p=apply(Raster::filled(1,1,{128,128,128,255}),o)->pixel(0,0);check(p==Pixel{127,0,128,255},"Gradient luminance midpoint wrong");map["reversed"]=true;o["gradientMapSettings"]=map;check(apply(Raster::filled(1,1,{0,0,0,255}),o)->pixel(0,0)==Pixel{0,0,255,255},"Gradient reverse failed");}
 else if(test=="grain_global_tiles"){auto o=settings("Grain");o["grainSettings"]=QJsonObject{{"amount",60},{"size",2},{"roughness",40},{"seed",7}};auto input=Raster::filled(513,259,{128,128,128,255});auto whole=apply(input,o);auto part=apply(Raster::filled(257,19,{128,128,128,255}),o,nullptr,{250,240,1});std::set<uint8_t> values;for(int y=0;y<19;++y)for(int x=0;x<257;++x){auto p=whole->pixel(x+250,y+240);check(part->pixel(x,y)==p,"Grain moved across a tile or subregion");check(p.r==p.g&&p.r==p.b,"Grain is not neutral");values.insert(p.r);}check(values.size()>5,"Grain did not vary brightness");auto g=o["grainSettings"].toObject();g["seed"]=8;o["grainSettings"]=g;check(apply(input,o)->rgba()!=whole->rgba(),"Grain ignores seed");auto transparent=Raster::filled(1,1,{});check(apply(transparent,o)==transparent,"Grain altered transparency");}
 else if(test=="hsv_primary_alpha"){auto o=settings("Hue/Saturation");o["hue"]=120;for(uint8_t a:{uint8_t(255),uint8_t(128),uint8_t(32)})check(apply(Raster::filled(1,1,{a,0,0,a}),o)->pixel(0,0)==Pixel{0,a,0,a},"HSV primary hue/alpha changed");o["hue"]=0;o["saturation"]=-100;auto p=apply(Raster::filled(1,1,{255,0,0,255}),o)->pixel(0,0);check(p.r==p.g&&p.g==p.b,"HSV desaturation not gray");o["lightness"]=100;check(apply(Raster::filled(1,1,{255,0,0,255}),o)->pixel(0,0)==Pixel{255,255,255,255},"HSV lightness endpoint wrong");}
 else if(test=="hsv_bands_colorize"){auto o=settings("Hue/Saturation");QJsonObject hsv{{"range","Reds"},{"colorize",false},{"invertRange",false},{"adjustments",QJsonArray{"Reds",QJsonObject{{"hue",60},{"saturation",0},{"lightness",0}},"Blues",QJsonObject{{"hue",0},{"saturation",-100},{"lightness",0}}}},{"bands",QJsonArray{}}};o["hsvSettings"]=hsv;check(apply(Raster::filled(1,1,{255,0,0,255}),o)->pixel(0,0)==Pixel{255,255,0,255},"Reds range failed");auto blue=apply(Raster::filled(1,1,{0,0,255,255}),o)->pixel(0,0);check(blue.r==blue.g&&blue.g==blue.b,"Blue range independent adjustment failed");hsv["colorize"]=true;hsv["adjustments"]=QJsonArray{"Reds",QJsonObject{{"hue",240},{"saturation",100},{"lightness",0}}};o["hsvSettings"]=hsv;check(apply(Raster::filled(1,1,{255,0,0,255}),o)->pixel(0,0)==Pixel{0,0,255,255},"Colorize selected range failed");}
 else if(test=="selected_immutable_tiles"){auto r=Raster::filled(513,257,{128,128,128,255});auto o=settings("Exposure");o["exposureSettings"]=QJsonObject{{"exposure",1},{"offset",0},{"gamma",1}};GrayRaster mask{513,257,std::vector<uint8_t>(513*257,0)};check(apply(r,o,&mask)==r,"Empty selection changed input");mask.pixels[10*513+10]=255;auto result=apply(r,o,&mask);check(result!=r&&result->tiles[0]!=r->tiles[0]&&result->tiles[1]==r->tiles[1]&&result->pixel(9,10)==r->pixel(9,10),"Selection failed immutable tile sharing");mask.pixels[10*513+10]=128;auto soft=apply(r,o,&mask)->pixel(10,10);auto changed=result->pixel(10,10);check(soft.r==uint8_t((128*127+int(changed.r)*128+127)/255)&&soft.a==255,"Soft selection blend differs");check(r->pixel(10,10)==Pixel{128,128,128,255},"Source mutated");}
 else if(test=="malformed_json"){auto r=Raster::filled(1,1,{});rejects([&]{applyAdjustment(r,"{}");});rejects([&]{defaultAdjustmentJson("Fake");});auto o=settings("Exposure");o["exposureSettings"]=QJsonObject{{"exposure",0},{"offset",0},{"gamma",0}};rejects([&]{apply(r,o);});o=settings("Hue/Saturation");o["hsvSettings"]=QJsonObject{{"range","Reds"},{"colorize",false},{"invertRange",false},{"adjustments",QJsonObject{}},{"bands",QJsonArray{}}};rejects([&]{apply(r,o);});GrayRaster mismatch{2,1,{0,0}};rejects([&]{apply(r,settings("Exposure"),&mismatch);});auto layer=settings("RGB Split");auto g=layer["filterLayerSettings"].toObject();g["rgbSplitDistance"]=-1;layer["filterLayerSettings"]=g;rejects([&]{apply(r,layer);});}
 else if(test=="filter_layer_kernels"){
    std::vector<uint8_t> bytes(48*24*4);for(int y=0;y<24;++y)for(int x=0;x<48;++x){auto i=size_t(y*48+x)*4;bytes[i]=uint8_t(x<24?200:40);bytes[i+1]=80;bytes[i+2]=uint8_t(x<24?40:200);bytes[i+3]=255;}
    auto source=Raster::fromRgba(48,24,bytes.data(),48*4);
    auto rgb=settings("RGB Split");auto layer=rgb["filterLayerSettings"].toObject();layer["rgbSplitDistance"]=16;layer["rgbSplitBlur"]=0;layer["rgbSplitAngle"]=0;layer["rgbSplitRadial"]=false;layer["rgbSplitCenter"]=QJsonArray{24,12};rgb["filterLayerSettings"]=layer;
    auto adjusted=apply(source,rgb);
    filters::Settings filter;filter.rgbSplitDistance=16;filter.rgbSplitCenter=Point{24,12};
    auto direct=filters::runPixels(filters::Kind::RgbSplit,*source,filter,1,0);
    check(adjusted->rgba()==direct->rgba(),"RGB Split adjustment must reuse the filter kernel");
    auto glow=settings("Bloom / Glow");auto glowLayer=glow["filterLayerSettings"].toObject();glowLayer["bloomAmount"]=50;glowLayer["bloomRadius"]=6;glow["filterLayerSettings"]=glowLayer;
    auto glowAdjusted=apply(source,glow);
    filters::Settings glowFilter;glowFilter.bloomGlow=true;glowFilter.bloomAmount=50;glowFilter.bloomRadius=6;
    check(glowAdjusted->rgba()==filters::runPixels(filters::Kind::Bloom,*source,glowFilter,1,0)->rgba(),"Bloom / Glow adjustment must reuse the glow kernel");
    auto progressive=settings("Progressive Blur");auto p=progressive["filterLayerSettings"].toObject();p["progressiveFrom"]=0;p["progressiveTo"]=0;progressive["filterLayerSettings"]=p;
    check(apply(source,progressive)==source,"Neutral Progressive Blur adjustment is identity");
    // The same sampled grid must use document-space distances at every zoom.
    for(double units:{.5,2.}){
        const AdjustmentRegion region{10,20,units,96,48};
        auto rgbSettings=layer;rgbSettings["rgbSplitRadial"]=true;rgbSettings["rgbSplitReach"]=20;rgbSettings.remove("rgbSplitCenter");rgb["filterLayerSettings"]=rgbSettings;
        filters::Settings expected;expected.rgbSplitDistance=16;expected.rgbSplitRadial=true;expected.rgbSplitReach=20/units;expected.rgbSplitCenter=Point{(48-10)/units,(24-20)/units};expected.rgbSplitOriginX=10;expected.rgbSplitOriginY=20;expected.rgbSplitUnitsPerPixel=units;
        check(apply(source,rgb,nullptr,region)->rgba()==filters::runPixels(filters::Kind::RgbSplit,*source,expected,1/units,0)->rgba(),"RGB Split scale or default center differs");
        check(apply(source,glow,nullptr,region)->rgba()==filters::runPixels(filters::Kind::Bloom,*source,glowFilter,1/units,0)->rgba(),"Bloom radius does not follow render scale");
        p["progressiveFrom"]=2;p["progressiveTo"]=8;progressive["filterLayerSettings"]=p;
        filters::Settings expectedBlur;expectedBlur.progressiveFrom=2;expectedBlur.progressiveTo=8;expectedBlur.progressiveStart=Point{(48-10)/units,(0-20)/units};expectedBlur.progressiveEnd=Point{(48-10)/units,(48-20)/units};
        check(apply(source,progressive,nullptr,region)->rgba()==filters::runPixels(filters::Kind::ProgressiveBlur,*source,expectedBlur,1/units,0)->rgba(),"Progressive Blur scale or default points differ");
        auto deep=makeDeep(source->width,source->height);for(int y=0;y<source->height;++y)for(int x=0;x<source->width;++x){auto q=source->pixel(x,y);deep->setPixel(x,y,{q.r/255.f,q.g/255.f,q.b/255.f,q.a/255.f});}
        check(applyAdjustment(deep,encode(progressive),nullptr,region)->samples==filters::runDeepPixels(filters::Kind::ProgressiveBlur,*deep,expectedBlur,1/units,0)->samples,"Deep Progressive Blur scale or defaults differ");
    }
 }
 else if(test=="levels_opaque_order"){auto r=Raster::filled(1,1,{64,64,64,255});auto o=levelSettings(1,changedLevels(0,2));auto l=o["levels"].toObject();auto a=l["ranges"].toArray();a[0]=changedLevels(40,1,210);l["ranges"]=a;o["levels"]=l;auto p=apply(r,o)->pixel(0,0);double expected=(std::sqrt(64./255)*255-40)/(210-40)*255;check(std::abs(p.r-expected)<=1&&p.r>p.g&&p.g==p.b,"Levels order differs from source");}
 else if(test=="levels_partial_alpha_selection"){
    const Pixel original{64,32,0,128};auto input=Raster::filled(3,1,original);GrayRaster selection{3,1,{0,128,255}};
    check(apply(input,settings("Levels"),&selection)==input,"Identity Levels changed selected partial-alpha pixels");
    auto output=apply(input,levelSettings(0,changedLevels(0,2)),&selection);
    check(output->pixel(0,0)==original,"Levels changed an unselected partial-alpha pixel");
    check(output->pixel(1,0)==Pixel{78,48,0,128},"Levels soft selection blended incorrect partial-alpha colors");
    check(output->pixel(2,0)==Pixel{91,64,0,128},"Levels gamma applied alpha conversion more than once");
    for(int x=0;x<3;++x)check(input->pixel(x,0)==original,"Levels changed immutable input");
 }
 else if(test=="levels_upstream_conflict"){auto o=levelSettings(0,changedLevels(0,1,255,255,0));auto result=apply(Raster::filled(1,1,{64,32,0,128}),o)->pixel(0,0);const bool passed=result==Pixel{64,96,128,128};QJsonObject evidence{{"id","SOURCE-TEST-CONFLICT-LEVELS-ALPHA"},{"result",passed?"pass":"fail"},{"source","Levels.swift:80-94 and LevelsPixels.c:3-16"},{"test","LevelsTests.inputClippingGammaOutputInversionAndAlpha"},{"input",QJsonArray{64,32,0,128}},{"expected_upstream_assertion",QJsonArray{64,96,128,128}},{"actual_windows_output",QJsonArray{result.r,result.g,result.b,result.a}},{"mac_reference","blocked_reference"}};std::cout<<QJsonDocument(evidence).toJson(QJsonDocument::Indented).toStdString();return passed?0:1;}
 else if(test=="unorm16_exposure_and_conversion"){
    auto deep=makeDeep(3,1,DeepImage::Format::Unorm16);deep->at(0,0)[0]=10001;deep->at(1,0)[0]=10031;deep->at(2,0)[0]=50021;
    for(int x=0;x<3;++x){deep->at(x,0)[1]=20003;deep->at(x,0)[2]=30007;deep->at(x,0)[3]=65535;}
    auto o=settings("Exposure");o["exposureSettings"]=QJsonObject{{"exposure",1},{"offset",0},{"gamma",1}};
    auto graded=applyAdjustment(std::shared_ptr<const DeepImage>(deep),encode(o),nullptr,{});
    check(graded&&graded->format==DeepImage::Format::Unorm16,"Exposure dropped 16-bit pixels");
    check(graded->at(0,0)[0]!=graded->at(1,0)[0]&&graded->at(0,0)[0]>10001,"Exposure quantized 16-bit neighbors");
    auto lens=settings("RGB Split");auto g=lens["filterLayerSettings"].toObject();g["rgbSplitDistance"]=8;lens["filterLayerSettings"]=g;
    auto split=applyAdjustment(std::shared_ptr<const DeepImage>(deep),encode(lens),nullptr,{0,0,1,3,1});
    check(split&&split->format==DeepImage::Format::Unorm16&&split->samples!=deep->samples,"RGB Split ignored 16-bit pixels");
 }
 else throw std::runtime_error("Unknown case");std::cout<<"PASS "<<test<<"\n";return 0;
 }catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
