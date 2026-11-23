#include "retouch/RetouchSession.h"
#include "core/DeepImage.h"
#include "graphics/PixelAlgorithms.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace compositor;
using namespace compositor::retouch;
namespace {
int passed=0,failed=0;
void require(bool b,const char* message){if(!b)throw std::runtime_error(message);}
template<class F>void test(const char* name,F body){try{body();++passed;std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& e){++failed;std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n';}}
std::shared_ptr<const Raster> raster(int w,int h,const std::function<Pixel(int,int)>& pixel){std::vector<Pixel> values(size_t(w)*h);for(int y=0;y<h;++y)for(int x=0;x<w;++x)values[size_t(y)*w+x]=pixel(x,y);return Raster::filled(w,h)->replacing(0,0,w,h,values.data(),w);}
Transform transform(int w,int h){return {0,0,double(w),double(h),0,false,false,Transform::Sampling::Nearest};}
std::shared_ptr<const Raster> colors(){return raster(80,40,[](int x,int y){if(x>=10&&x<20&&y>=15&&y<25)return Pixel{0,255,0,255};return x<40?Pixel{255,0,0,255}:Pixel{0,0,255,255};});}
std::shared_ptr<const GrayRaster> selection(int w,int h,const std::function<uint8_t(int,int)>& pixel){auto result=std::make_shared<GrayRaster>();result->width=w;result->height=h;result->pixels.resize(size_t(w)*h);for(int y=0;y<h;++y)for(int x=0;x<w;++x)result->pixels[size_t(y)*w+x]=pixel(x,y);return result;}
}
int main(int argc,char** argv){
    std::shared_ptr<graphics::D3D11BrushCoverage> gpu;
    if(argc>1){try{gpu=std::make_shared<graphics::D3D11BrushCoverage>(argv[1],true);std::cout<<"WARP adapter: "<<gpu->adapterName()<<'\n';}catch(const std::exception&e){std::cout<<"WARP initialization failed: "<<e.what()<<'\n';return 1;}}
    test("warp_document_preview_precedes_selection_commit_clip",[&]{auto original=raster(32,16,[](int x,int){return Pixel{uint8_t(x*7),0,0,255};});Settings settings;settings.mode=Mode::Smudge;settings.radius=3;settings.hardness=1;auto clip=selection(32,16,[](int x,int){return uint8_t(x<5?255:0);});RetouchSession stroke(original,transform(32,16),32,16,settings,{},clip,gpu);stroke.begin({10,8});stroke.append({11,8});auto document=stroke.warpDocumentPreview();require(document->pixel(11,8).r==70,"Live document smudge was selection clipped");require(stroke.preview()->pixel(11,8).r==77,"Commit preview ignored selection");require(stroke.warpDocumentPreview()==document,"Unchanged live document preview not cached");});
    test("source_clone_alignment_rounding_and_sample_crosshair",[]{CloneAlignment alignment;require(!alignment.strokeOffset({60,20}),"Missing source accepted");alignment.setSource({15,20});require(alignment.beginStroke({60,20})==Point{-45,0},"First aligned offset");require(alignment.beginStroke({66,20})==Point{-45,0},"Aligned offset reset");require(alignment.samplePoint({66,20})==Point{21,20},"Aligned source crosshair");alignment.aligned=false;require(alignment.beginStroke({50,10})==Point{-35,10},"Nonaligned stroke source");require(alignment.samplePoint({60,20})==Point{15,20},"Idle nonaligned crosshair");require(alignment.samplePoint({60,20},true)==Point{25,30},"Active nonaligned crosshair");alignment.setSource({15.5,20.5});require(alignment.beginStroke({17,19})==Point{-2,2},"Wholepixel offset rounding");});
    test("source_clone_stamp_three_strokes_and_immutable_source",[&]{auto original=colors();CloneAlignment alignment;alignment.setSource({15,20});Settings settings;settings.radius=3;settings.hardness=1;
        Sources source;source.cloneOffset=alignment.beginStroke({60,20});RetouchSession first(original,transform(80,40),80,40,settings,source,{},gpu);require(first.begin({60,20}),"First clone did not begin");first.append({60.5,20});auto preview=first.preview();auto after=first.commit();require(after->pixel(60,20)==Pixel{0,255,0,255},"Green source not copied");require(original->pixel(60,20)==Pixel{0,0,255,255},"Original mutated");require(after->pixel(70,5)==original->pixel(70,5),"Outside clone changed");require(first.commit()==after,"Commit not idempotent");require(preview->pixel(60,20)==Pixel{0,255,0,255},"Preview mutated");
        source.cloneOffset=alignment.beginStroke({66,20});RetouchSession second(after,transform(80,40),80,40,settings,source,{},gpu);second.begin({66,20});after=second.commit();require(after->pixel(66,20)==Pixel{255,0,0,255},"Second aligned stroke did not copy red");
        alignment.aligned=false;source.cloneOffset=alignment.beginStroke({50,10});RetouchSession third(after,transform(80,40),80,40,settings,source,{},gpu);third.begin({50,10});require(third.commit()->pixel(50,10)==Pixel{0,255,0,255},"Nonaligned stroke source differs");});
    test("clone_all_layers_frozen_sample_and_source_over_opacity_cap",[&]{auto original=Raster::filled(32,16,{0,0,255,255});Settings settings;settings.radius=5;settings.hardness=1;settings.opacity=.5;settings.sampleAllLayers=true;Sources source;source.allLayers=Raster::filled(32,16,{128,0,0,128});source.cloneOffset=Point{};
        RetouchSession stroke(original,transform(32,16),32,16,settings,source,{},gpu);stroke.begin({10,8});for(Point p:std::array<Point,4>{{{20,8},{10,8},{20,8},{10,8}}})stroke.append(p);auto result=stroke.commit();require(result->pixel(10,8)==Pixel{64,0,191,255},"Clone opacity accumulated or wrong premultiplication");require(source.allLayers->pixel(10,8)==Pixel{128,0,0,128},"Frozen sample changed");
        settings.sampleAllLayers=false;RetouchSession current(original,transform(32,16),32,16,settings,source);current.begin({10,8});require(current.commit()->pixel(10,8)==original->pixel(10,8),"Current-layer clone used all-layer sample");});
    test("selection_multiplication_once_empty_and_cancel",[&]{auto original=Raster::filled(32,16,{0,0,255,255});Settings settings;settings.radius=5;settings.hardness=1;settings.opacity=.5;Sources source;source.currentLayer=Raster::filled(32,16,{255,0,0,255});source.cloneOffset=Point{};auto clip=selection(32,16,[](int x,int){return uint8_t(x<16?128:0);});
        RetouchSession stroke(original,transform(32,16),32,16,settings,source,clip,gpu);stroke.begin({10,8});stroke.append({22,8});auto preview=stroke.preview();require(preview->pixel(10,8)==Pixel{64,0,191,255},"Selection applied twice or omitted");require(preview->pixel(22,8)==original->pixel(22,8),"Selection exterior edited");require(stroke.cancel()==original,"Cancel did not return original");require(preview->pixel(10,8)==Pixel{64,0,191,255},"Cancel mutated published preview");
        RetouchSession empty(original,transform(32,16),32,16,settings,source,selection(32,16,[](int,int){return uint8_t{0};}),gpu);require(!empty.begin({10,8})&&empty.commit()==original,"Explicit empty selection edited");});
    for(Mode mode:{Mode::HealContentAware,Mode::HealCreateTexture,Mode::HealProximity})test(mode==Mode::HealContentAware?"source_heal_content_aware":mode==Mode::HealCreateTexture?"source_heal_create_texture":"source_heal_proximity",[&,mode]{
        auto original=raster(120,80,[](int x,int y){const bool blemish=x>=55&&x<65&&y>=35&&y<45;const auto gray=uint8_t(x%4<2?100:112);return blemish?Pixel{230,20,20,255}:Pixel{gray,gray,gray,255};});Settings settings;settings.mode=mode;settings.radius=12;settings.hardness=1;settings.healingSeed=0x12345678;
        RetouchSession stroke(original,transform(120,80),120,80,settings,{}, {},gpu);stroke.begin({60,40});auto wash=stroke.preview();require(wash->pixel(60,40).r<original->pixel(60,40).r,"Healing wash missing");stroke.append({60.5,40});auto healed=stroke.commit();
        for(Point p:std::array<Point,3>{{{60,40},{56,36},{64,44}}}){auto value=healed->pixel(int(p.x),int(p.y));require(int(value.r)-value.g<30&&value.g>=80&&value.g<=130&&value.a==255,"Source blemish not healed into gray texture");}
        for(Point p:std::array<Point,5>{{{10,10},{90,40},{30,40},{60,10},{60,70}}})require(healed->pixel(int(p.x),int(p.y))==original->pixel(int(p.x),int(p.y)),"Opaque pixels outside brush changed");
        require(original->pixel(60,40)==Pixel{230,20,20,255},"Healing mutated source");require(stroke.metrics().healingRegionPixels>0,"Healing region not measured");
    });
    test("healing_translucent_source_copy_preserves_outside_coverage",[&]{auto original=Raster::filled(80,80,{50,50,50,128});Settings settings;settings.mode=Mode::HealProximity;settings.radius=3;settings.hardness=1;RetouchSession stroke(original,transform(80,80),80,80,settings,{}, {},gpu);stroke.begin({40,40});auto after=stroke.commit();require(after->pixel(10,10)==Pixel{50,50,50,128},"BrushRaster.draw copy semantics lost");require(original->pixel(10,10).a==128,"Source mutated input");});
    test("blur_distinct_soft_sample_frozen_within_stroke",[&]{auto original=raster(64,32,[](int x,int){return x<32?Pixel{0,0,0,255}:Pixel{255,255,255,255};});Settings settings;settings.mode=Mode::Blur;settings.radius=10;settings.hardness=1;RetouchSession stroke(original,transform(64,32),64,32,settings,{}, {},gpu);stroke.begin({31,16});auto once=stroke.preview()->pixel(31,16);require(once.r>50&&once.r<128,"Blur did not soften edge");stroke.append({32,16});stroke.append({31,16});auto result=stroke.commit();require(result->pixel(31,16)==once,"Blur compounds within stroke");require(result->pixel(2,16)==original->pixel(2,16),"Blur outside tip changed");RetouchSession second(result,transform(64,32),64,32,settings);second.begin({31,16});require(second.commit()->pixel(31,16).r>once.r,"New blur stroke failed to compound");});
    test("blur_mask_edges_selection_and_gray_snapshots",[&]{auto original=selection(40,20,[](int x,int){return uint8_t(x<20?0:255);});Settings settings;settings.mode=Mode::Blur;settings.radius=8;settings.hardness=1;RetouchSession stroke(original,transform(40,20),40,20,settings,{},gpu);stroke.begin({19,10});auto preview=stroke.previewMask();require(preview->pixel(19,10)>50&&preview->pixel(19,10)<128,"Gray blur edge");require(stroke.previewMask()==preview,"Gray preview cache missed");auto result=stroke.commitMask();require(result->pixel(2,2)==0&&result->pixel(38,18)==255,"Gray untouched area changed");require(stroke.cancelMask()==original&&original->pixel(19,10)==0,"Gray cancel mutated source");
        bool refused=false;try{settings.mode=Mode::Clone;RetouchSession rejected(original,transform(40,20),40,20,settings);}catch(const std::invalid_argument&){refused=true;}require(refused,"Clone mask target accepted");});
    test("smudge_and_liquify_first_dab_match_distinct_source_math",[&]{auto original=raster(32,24,[](int x,int){return Pixel{uint8_t(x*7),0,0,255};});
        for(Mode mode:{Mode::Smudge,Mode::Liquify}){Settings settings;settings.mode=mode;settings.radius=4;settings.hardness=1;settings.opacity=.5;RetouchSession stroke(original,transform(32,24),32,24,settings,{}, {},gpu);stroke.begin({10,12});require(stroke.preview()==original,"Warp click painted");stroke.append({11,12});auto result=stroke.commit();
            // Smudge follows the 1.4.2 smear: carried color mixes into the destination. At 50% opacity the center is 77+(70-77)*0.5, which rounds to 74. Liquify stays bilinear.
            require(result->pixel(11,12).r==74,"Warp source center formula differs");require(stroke.metrics().warpDabs==1,"Warp spacing source threshold");require(result->pixel(25,12)==original->pixel(25,12),"Warp changed outside footprint");require(original->pixel(11,12).r==77,"Warp source mutated");}
    });
    test("warp_spacing_accumulates_short_events_and_narrow_canvas_safe",[&]{Settings settings;settings.mode=Mode::Liquify;settings.radius=20;settings.hardness=.5;auto original=Raster::filled(1,12,{50,20,10,128});RetouchSession stroke(original,transform(1,12),1,12,settings,{}, {},gpu);stroke.begin({.5,4});require(!stroke.append({.5,4.5}),"Subspacing event advanced");require(stroke.append({.5,5}),"Accumulated spacing not reached");auto result=stroke.commit();for(int y=0;y<12;++y)require(result->pixel(0,y)==original->pixel(0,y),"One-column source guard violated");});
    test("warp_selection_commit_clears_alpha_via_copy",[&]{auto original=raster(40,20,[](int x,int){return x<10?Pixel{}:Pixel{255,0,0,255};});Settings settings;settings.mode=Mode::Smudge;settings.radius=4;settings.hardness=1;settings.opacity=1;auto clip=selection(40,20,[](int x,int){return uint8_t(x<16?255:0);});RetouchSession stroke(original,transform(40,20),40,20,settings,{},clip,gpu);stroke.begin({6,10});stroke.append({12,10});stroke.append({20,10});auto result=stroke.commit();require(result->pixel(12,10).a==0,"Warp copy retained original alpha");require(result->pixel(20,10)==original->pixel(20,10),"Warp selection commit escaped");});
    test("transformed_clone_document_sampling_and_tile_sharing",[&]{auto original=Raster::filled(768,32,{0,0,255,255});auto placement=transform(768,32);placement.x=20;placement.y=10;Sources source;source.currentLayer=Raster::filled(800,64,{255,0,0,255});source.cloneOffset=Point{};Settings settings;settings.radius=4;settings.hardness=1;RetouchSession stroke(original,placement,800,64,settings,source,{},gpu);Raster::resetMaterializationCount();stroke.begin({30,20});auto result=stroke.commit();require(result->pixel(10,10)==Pixel{255,0,0,255},"Document transform sample location");require(result->tiles[1]==original->tiles[1]&&result->tiles[2]==original->tiles[2],"Untouched source tiles not shared");require(Raster::materializationCount()==0,"Retouch called Raster::rgba");});
    test("deep_clone_sub_byte_difference",[]{Layer layer;layer.id="sub-byte";layer.transform=transform(32,16);auto deep=makeDeep(32,16,DeepImage::Format::Unorm16);for(int y=0;y<16;++y)for(int x=0;x<32;++x){auto p=deep->at(x,y);p[0]=x<16?10000:10001;p[1]=p[2]=0;p[3]=65535;}layer.deep=deep;layer.raster=Raster::filled(32,16,{39,0,0,255});Settings settings;settings.mode=Mode::Clone;settings.radius=3;settings.hardness=1;Sources source;source.cloneOffset=Point{-16,0};RetouchSession stroke(layer,32,16,settings,source);stroke.begin({24,8});auto snapshot=stroke.commitSnapshot();require(snapshot->changed(),"Sub-byte clone discarded");auto result=stroke.committedLayer();require(result.deep->at(24,8)[0]==10000,"Sub-byte clone sample lost");});
    test("retouch_invalid_lifecycle_and_missing_sources",[]{auto original=Raster::filled(16,16,{255,0,0,255});Settings settings;RetouchSession missing(original,transform(16,16),16,16,settings);bool refused=false;try{missing.begin({8,8});}catch(const std::invalid_argument&){refused=true;}require(refused,"Clone missing source not refused");settings.sampleAllLayers=true;Sources sources;sources.cloneOffset=Point{};RetouchSession allMissing(original,transform(16,16),16,16,settings,sources);refused=false;try{allMissing.begin({8,8});}catch(const std::invalid_argument&){refused=true;}require(refused,"Missing all-layer sample not refused");settings.mode=Mode::Liquify;RetouchSession cancelled(original,transform(16,16),16,16,settings);cancelled.cancel();refused=false;try{cancelled.begin({8,8});}catch(const std::logic_error&){refused=true;}require(refused,"Cancelled session restarted");});
    test("unorm16_clone_blur_smudge_liquify_and_heal_conversion",[&]{
        Layer layer;layer.id="deep";layer.transform=transform(32,16);auto deep=makeDeep(32,16,DeepImage::Format::Unorm16);
        for(int y=0;y<16;++y)for(int x=0;x<32;++x){auto p=deep->at(x,y);p[0]=uint16_t(x<16?10001:50021);p[1]=20003;p[2]=30007;p[3]=65535;}
        layer.deep=deep;layer.raster=rasterFromDeep(*deep);
        auto deepAt=[&](const Layer& result,int sx,int sy){const int x=int(std::lround(result.transform.x));const int y=int(std::lround(result.transform.y));const int px=sx-x,py=sy-y;require(result.deep&&px>=0&&py>=0&&px<result.deep->width&&py<result.deep->height,"deep sample outside committed crop");return result.deep->at(px,py)[0];};
        auto retains=[&](const Layer& result,int sx,int sy,uint16_t value){const int x=int(std::lround(result.transform.x));const int y=int(std::lround(result.transform.y));const int px=sx-x,py=sy-y;if(!result.deep||px<0||py<0||px>=result.deep->width||py>=result.deep->height)return;require(result.deep->at(px,py)[0]==value,"unpainted 16-bit pixel changed");};
        Settings clone;clone.radius=4;clone.hardness=1;Sources source;source.cloneOffset=Point{-16,0};
        RetouchSession stamp(layer,32,16,clone,source,{},gpu);
        require(stamp.begin({24,8}),"clone did not begin");
        require(stamp.commitSnapshot()->changed(),"clone no-op");
        auto cloned=stamp.committedLayer();
        require(cloned.deep&&cloned.deep->format==DeepImage::Format::Unorm16,"clone dropped 16-bit pixels");
        require(deepAt(cloned,24,8)==10001,"clone copied 8-bit quantized pixels");
        retains(cloned,0,8,10001);
        Settings blur;blur.mode=Mode::Blur;blur.radius=8;blur.hardness=1;
        RetouchSession soften(layer,32,16,blur,{},{},gpu);soften.begin({16,8});require(soften.commitSnapshot()->changed(),"blur no-op");auto blurredLayer=soften.committedLayer();
        retains(blurredLayer,0,8,10001);
        require(deepAt(blurredLayer,16,8)>10001&&deepAt(blurredLayer,16,8)<50021,"blur ignored 16-bit source");
        for(Mode mode:{Mode::Smudge,Mode::Liquify}){Settings warp;warp.mode=mode;warp.radius=4;warp.hardness=1;warp.opacity=.5;
            RetouchSession stroke(layer,32,16,warp,{},{},gpu);stroke.begin({14,8});stroke.append({18,8});require(stroke.commitSnapshot()->changed(),"warp no-op");auto warped=stroke.committedLayer();
            retains(warped,0,8,10001);
            require(deepAt(warped,16,8)!=10001&&deepAt(warped,16,8)!=50021,"warp quantized through 8-bit");}
        RetouchSession cancelled(layer,32,16,blur,{},{},gpu);cancelled.begin({16,8});require(cancelled.cancelSnapshot()->materializeLayer()==layer,"blur cancel mutated the layer");
        bool refused=false;try{Settings heal;heal.mode=Mode::HealProximity;RetouchSession stroke(layer,32,16,heal);}catch(const std::runtime_error&){refused=true;}
        require(refused,"16-bit healing accepted a silent 8-bit kernel");
    });
    // Issue 221: a Blur stroke must not filter the whole canvas before its first dab.
    test("blur_begin_filters_only_touched_tiles",[&]{
        Layer layer;layer.id="blur-start";layer.transform=transform(2048,2048);layer.raster=raster(2048,2048,[](int x,int y){return Pixel{uint8_t(x),uint8_t(y),uint8_t(x^y),255};});
        Settings settings;settings.mode=Mode::Blur;settings.radius=20;RetouchSession stroke(layer,2048,2048,settings,{},{},gpu);
        require(stroke.begin({1000,1000}),"Blur stroke did not begin");
        const auto& metrics=stroke.metrics();
        require(metrics.gaussianScratchPixels<=uint64_t(256+2*90)*256&&metrics.workingBufferPixels<=32ull*256*256,"Blur filtered the whole canvas at stroke start");
    });
    // Tile-by-tile Blur keeps the exact separable Gaussian: double kernel, float horizontal pass, double vertical pass.
    test("blur_tiles_match_independent_gaussian",[&]{
        const int w=400,h=300;
        const auto value=[](int x,int y){return uint8_t((x*7+y*13+(x*y)%29)%256);};
        const auto reference=[&](int px,int py,double sigma,bool clamp,const std::function<uint8_t(int,int)>& channel){
            const int r=int(std::ceil(sigma*3));std::vector<double> kernel;double total=0;for(int i=-r;i<=r;++i){kernel.push_back(std::exp(-double(i*i)/(2*sigma*sigma)));total+=kernel.back();}for(auto& k:kernel)k/=total;
            const auto at=[&](int x,int y)->double{if(clamp){x=std::clamp(x,0,w-1);y=std::clamp(y,0,h-1);}return x<0||y<0||x>=w||y>=h?0.:channel(x,y);};
            double sum=0;for(int k=-r;k<=r;++k){double row=0;for(int j=-r;j<=r;++j)row+=at(px+j,py+k)*kernel[size_t(j+r)];sum+=double(float(row))*kernel[size_t(k+r)];}
            return uint8_t(std::clamp(std::round(sum),0.,255.));
        };
        for(double radius:{20.,100.}){
            const double sigma=std::clamp(radius*2/10,1.5,30.);
            Layer image;image.id="blur-image";image.transform=transform(w,h);image.raster=raster(w,h,[&](int x,int y){const auto v=value(x,y);return Pixel{v,uint8_t(255-v),uint8_t(v/2),255};});
            Settings settings;settings.mode=Mode::Blur;settings.radius=radius;settings.hardness=1;
            RetouchSession stroke(image,w,h,settings,{},{},gpu);require(stroke.begin({230,150}),"Image blur did not begin");stroke.append({260,150});stroke.append({290,150});stroke.commitSnapshot();
            const auto result=stroke.committedLayer().raster;
            for(int x=240;x<=280;++x){
                const auto red=reference(x,150,sigma,false,[&](int xx,int yy){return value(xx,yy);});
                const auto alpha=reference(x,150,sigma,false,[](int,int){return uint8_t(255);});
                if(alpha!=255)continue;
                require(result->pixel(x,150).r==red,"Tiled image blur differs from the reference Gaussian");
                require(result->pixel(x,150).g==reference(x,150,sigma,false,[&](int xx,int yy){return uint8_t(255-value(xx,yy));}),"Tiled image blur green differs");
            }
            Layer masked=image;masked.id="blur-mask";auto gray=selection(w,h,[&](int x,int y){return value(x,y);});masked.mask=Mask{};masked.mask->raster=gray;
            RetouchSession maskStroke(masked,w,h,settings,{},{},gpu,true);require(maskStroke.begin({4,150}),"Mask blur did not begin");maskStroke.append({4,170});maskStroke.commitSnapshot();
            const auto mask=maskStroke.committedLayer().mask->raster;
            for(int y=152;y<=168;++y)for(int x=0;x<=8;++x)require(mask->pixel(x,y)==reference(x,y,sigma,true,[&](int xx,int yy){return value(xx,yy);}),"Tiled mask blur differs from the clamped reference Gaussian");
        }
    });
    // A warp stroke appends brush coverage once per pointer event, while every dab still moves pixels.
    test("warp_coverage_once_per_event_matches_fine_events",[&]{
        Layer layer;layer.id="warp-events";layer.transform=transform(200,100);layer.raster=raster(200,100,[](int x,int y){return Pixel{uint8_t(x),uint8_t(y*2),uint8_t((x*y)%256),255};});
        for(Mode mode:{Mode::Smudge,Mode::Liquify}){
            Settings settings;settings.mode=mode;settings.radius=20;settings.hardness=.5;settings.opacity=.8;
            RetouchSession coarse(layer,200,100,settings,{},{},gpu);coarse.begin({50,50});require(coarse.append({90,50}),"Coarse warp move was refused");
            require(coarse.metrics().warpDabs==40,"Warp dab spacing changed");
            require(coarse.metrics().coverageSamples==2,"Warp appended coverage for every dab");
            RetouchSession fine(layer,200,100,settings,{},{},gpu);fine.begin({50,50});for(int x=51;x<=90;++x)fine.append({double(x),50});
            require(fine.metrics().warpDabs==40,"Fine warp dab count changed");
            coarse.commitSnapshot();fine.commitSnapshot();
            const auto a=coarse.committedLayer().raster,b=fine.committedLayer().raster;
            for(int x=55;x<=85;++x)require(a->pixel(x,50)==b->pixel(x,50),"Per-event coverage changed fully covered warp pixels");
        }
    });
    std::cout<<"{\"suite\":\"retouch\",\"passed\":"<<passed<<",\"failed\":"<<failed<<",\"mac_differential\":false,\"warp\":"<<(gpu?"true":"false")<<"}\n";return failed?1:0;
}
