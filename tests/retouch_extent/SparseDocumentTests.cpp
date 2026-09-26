#include "retouch/RetouchSession.h"
#include "editing/Selection.h"
#include <cstdio>
#include <map>
#include <stdexcept>
using namespace compositor;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
Layer source(Point shift){std::vector<uint8_t> bytes(64*64*4);for(int y=0;y<64;++y)for(int x=0;x<64;++x){size_t i=(size_t(y)*64+x)*4;bytes[i]=uint8_t((x*3+y)%180);bytes[i+1]=uint8_t((y*2+x)%180);bytes[i+2]=uint8_t((x+y*3)%180);bytes[i+3]=200;}Layer l;l.id="retouch-sparse";l.transform={shift.x+40,shift.y+40,64,64};l.raster=Raster::fromRgba(64,64,bytes.data(),64*4);return l;}
void exercise(retouch::Mode mode){
    // Keep the healing context away from both canvas edges. Its random grain
    // is keyed by the clipped context width, so unequal context is not a
    // translation control (BrushStroke.swift 774-778, HealPixels.c 235).
    const Point offset{14848,11776};auto small=source({256,256}),large=source({offset.x+256,offset.y+256});auto original=large;
    retouch::Settings settings;settings.mode=mode;settings.radius=9;settings.hardness=.35;settings.opacity=.6;settings.healingSeed=77;
    retouch::Sources sources;if(mode==retouch::Mode::Clone)sources.cloneOffset=Point{-20,0};
    auto clip=editing::SelectionOutline::rectangle({316,316,40,40},false);
    retouch::RetouchSession reference(small,1024,1024,settings,sources,clip.rasterize(1024,1024));
    retouch::RetouchSession session(large,30000,30000,settings,sources,clip.moved(offset).rasterize(30000,30000));
    require(reference.begin({330,326})&&session.begin({offset.x+330,offset.y+326}),"both retouch strokes begin");
    reference.append({339,330});session.append({offset.x+339,offset.y+330});
    const auto actual=session.commitSnapshot()->materializeLayer(),expected=reference.commitSnapshot()->materializeLayer();
    require(actual.raster&&expected.raster,"retouch result pixels");require(actual.raster->width==expected.raster->width&&actual.raster->height==expected.raster->height,"retouch source extent matches translated control");
    require(actual.transform.x-offset.x==expected.transform.x&&actual.transform.y-offset.y==expected.transform.y&&actual.transform.width==expected.transform.width&&actual.transform.height==expected.transform.height,"retouch placement matches translated control");
    require(session.metrics().healingRegionPixels==reference.metrics().healingRegionPixels,"translated healing context must have equal extents");
    require(actual.raster->rgba()==expected.raster->rgba(),"large retouch pixels differ from exact small translated control");
    require(large==original,"retouch must retain original layer");const auto& metrics=session.metrics();require(metrics.workingBufferPixels<=1024*1024&&metrics.gaussianScratchPixels<=1024*1024&&metrics.healingRegionPixels<=1024*1024,"local retouch materialized too much of large document");
}
}
int main(int argc,char**argv){const std::map<std::string,retouch::Mode> cases{{"clone",retouch::Mode::Clone},{"blur",retouch::Mode::Blur},{"heal_content",retouch::Mode::HealContentAware},{"heal_texture",retouch::Mode::HealCreateTexture},{"heal_proximity",retouch::Mode::HealProximity},{"smudge",retouch::Mode::Smudge},{"liquify",retouch::Mode::Liquify}};int failed=0;for(const auto&[name,mode]:cases){if(argc>1&&argv[1]!=name)continue;try{exercise(mode);std::printf("PASS %s\n",name.c_str());}catch(const std::exception&e){++failed;std::printf("FAIL %s: %s\n",name.c_str(),e.what());}}if(argc>1&&!cases.contains(argv[1]))return 2;return failed?1:0;}
