#include "retouch/RetouchSession.h"
#include "graphics/SamplingSource.h"
#include "editing/Selection.h"
#include <cstdio>
#include <map>
#include <stdexcept>
using namespace compositor;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
Document fixture(Point shift,int size){
    Document d;d.width=d.height=size;Layer l;l.id="target";l.transform={shift.x+256,shift.y+256,128,128};l.opacity=.65;
    std::vector<Pixel> pixels(128*128);for(int y=0;y<128;++y)for(int x=0;x<128;++x)pixels[size_t(y)*128+x]={uint8_t(x),uint8_t(y),40,180};
    l.raster=Raster::filled(128,128)->replacing(0,0,128,128,pixels.data(),128);
    l.mask=Mask{std::make_shared<GrayRaster>(GrayRaster{128,128,std::vector<uint8_t>(128*128,160)})};d.layers.push_back(l);return d;
}
void warpPreview(retouch::Mode mode){
    const Point delta{14848,11776};auto small=fixture({},1024),large=fixture(delta,30000);const auto saved=large;
    auto outline=editing::SelectionOutline::rectangle({306,300,6,40},false);
    retouch::Settings settings;settings.mode=mode;settings.radius=9;settings.hardness=.35;settings.opacity=.6;
    retouch::RetouchSession a(small.layers[0],1024,1024,settings,{},outline.rasterize(1024,1024));
    retouch::RetouchSession b(large.layers[0],30000,30000,settings,{},outline.moved(delta).rasterize(30000,30000));
    require(a.begin({306,310})&&b.begin({delta.x+306,delta.y+310}),"warp starts");
    a.append({316,313});b.append({delta.x+316,delta.y+313});
    auto preview=b.livePreview();require(preview==b.livePreview(),"unchanged sparse preview must retain identity");
    const auto frozen=SoftwareRenderer(preview).render(large,int(delta.x)+288,int(delta.y)+288,64,64)->rgba();
    for(double step:{.5,1.,2.,4.}){
        const auto actual=SoftwareRenderer(preview).renderScaled(large,delta.x+288,delta.y+288,32,32,step);
        const auto expected=SoftwareRenderer(a.livePreview()).renderScaled(small,288,288,32,32,step);
        const auto aPixels=actual->rgba(),ePixels=expected->rgba();
        if(aPixels!=ePixels){size_t different=0;int maximum=0;for(size_t i=0;i<aPixels.size();++i)if(aPixels[i]!=ePixels[i]){if(!different)std::printf("first difference step=%g byte=%zu actual=%u expected=%u\n",step,i,unsigned(aPixels[i]),unsigned(ePixels[i]));++different;maximum=std::max(maximum,std::abs(int(aPixels[i])-int(ePixels[i])));}std::printf("different bytes=%zu maximum=%d preview=(%g,%g,%g,%g)\n",different,maximum,preview->layer.transform.x,preview->layer.transform.y,preview->layer.transform.width,preview->layer.transform.height);}
        require(aPixels==ePixels,"sparse warp live pixels differ from dense preview at display scale");
    }
    a.append({324,317});b.append({delta.x+324,delta.y+317});require(b.livePreview()!=preview,"changed warp preview must publish new identity");
    require(SoftwareRenderer(preview).render(large,int(delta.x)+288,int(delta.y)+288,64,64)->rgba()==frozen,"later dabs mutate earlier published warp preview");
    const auto beforeCancel=b.livePreview();b.cancelSnapshot();
    require(SoftwareRenderer(preview).render(large,int(delta.x)+288,int(delta.y)+288,64,64)->rgba()==frozen,"cancel invalidates published warp preview");
    require(beforeCancel->imageSource&&beforeCancel->imageSource->width<1024&&beforeCancel->imageSource->height<1024,"local warp preview scans full canvas");
    require(large==saved,"warp preview changed canonical document");
}
void cloneComposite(){
    const Point delta{14848,11776};auto small=fixture({},1024),large=fixture(delta,30000);
    Layer overlay;overlay.id="overlay";overlay.raster=Raster::filled(64,64,{80,20,10,128});overlay.transform={290,290,64,64};overlay.opacity=.5;overlay.blend=Blend::Screen;
    small.layers.push_back(overlay);overlay.transform.x+=delta.x;overlay.transform.y+=delta.y;large.layers.push_back(overlay);
    retouch::Settings settings;settings.mode=retouch::Mode::Clone;settings.radius=7;settings.opacity=.6;settings.sampleAllLayers=true;
    retouch::Sources aSource,bSource;aSource.cloneOffset=bSource.cloneOffset=Point{-8,2};aSource.allLayers=SoftwareRenderer().render(small,0,0,1024,1024);bSource.allLayersSource=retouch::compositeSource(large);
    const auto original=large;large.layers[1].raster=Raster::filled(64,64,{0,250,0,255}); // Later mutations must not enter the frozen stroke source.
    retouch::RetouchSession a(small.layers[0],1024,1024,settings,aSource),b(original.layers[0],30000,30000,settings,bSource);
    require(a.begin({314,314})&&b.begin({delta.x+314,delta.y+314}),"clone starts");a.append({320,318});b.append({delta.x+320,delta.y+318});
    auto expected=a.commitSnapshot()->materializeLayer(),actual=b.commitSnapshot()->materializeLayer();require(actual.raster->rgba()==expected.raster->rgba(),"bounded all-layer clone differs from frozen full composite");
}
void blurMaskEdge(){
    auto small=fixture({},1024),large=fixture({},30000);auto gray=std::make_shared<GrayRaster>();gray->width=gray->height=32;gray->pixels.resize(32*32);for(int y=0;y<32;++y)for(int x=0;x<32;++x)gray->pixels[size_t(y)*32+x]=uint8_t(x*7);
    for(auto* d:{&small,&large}){d->layers[0].mask=Mask{gray,true,true,Transform{0,0,32,32}};}
    retouch::Settings settings;settings.mode=retouch::Mode::Blur;settings.radius=9;settings.opacity=.6;
    retouch::RetouchSession a(small.layers[0],1024,1024,settings,{}, {},{},true),b(large.layers[0],30000,30000,settings,{}, {},{},true);
    require(a.begin({1,1})&&b.begin({1,1}),"mask blur starts");a.append({8,5});b.append({8,5});
    require(a.commitSnapshot()->materializeLayer().mask->raster->pixels==b.commitSnapshot()->materializeLayer().mask->raster->pixels,"sparse Gaussian differs at clamped document mask edge");
}
void invalidSource(){auto d=fixture({},30000);auto source=std::make_shared<graphics::SamplingSource>();source->width=30000;source->height=29999;source->identity=d.layers[0].raster;source->rgba=[](int,int){return Pixel{};};retouch::Sources sources;sources.allLayersSource=source;bool refused=false;try{retouch::RetouchSession session(d.layers[0],30000,30000,{},sources);}catch(const std::invalid_argument&){refused=true;}require(refused,"invalid lazy source extent accepted");}
}
int main(int argc,char** argv){const std::map<std::string,std::function<void()>> cases{{"smudge_preview",[]{warpPreview(retouch::Mode::Smudge);}},{"liquify_preview",[]{warpPreview(retouch::Mode::Liquify);}},{"clone_composite",cloneComposite},{"mask_edge",blurMaskEdge},{"invalid_source",invalidSource}};int failed=0;for(const auto&[name,body]:cases){if(argc>1&&argv[1]!=name)continue;try{body();std::printf("PASS %s\n",name.c_str());}catch(const std::exception&e){++failed;std::printf("FAIL %s: %s\n",name.c_str(),e.what());}}if(argc>1&&!cases.contains(argv[1]))return 2;return failed?1:0;}
