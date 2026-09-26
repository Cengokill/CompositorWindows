#include "graphics/GrowingBrushSession.h"
#include <algorithm>
#include <iostream>
#include <map>
#include <stdexcept>
using namespace compositor;
using namespace compositor::graphics;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void exact(const CompositeViewport& a,const CompositeViewport& b){require(a.documentX==b.documentX&&a.documentY==b.documentY&&a.unitsPerPixel==b.unitsPerPixel,"viewport geometry differs");require(a.raster->width==b.raster->width&&a.raster->height==b.raster->height,"viewport dimensions differ");for(int y=0;y<a.raster->height;++y)for(int x=0;x<a.raster->width;++x)require(a.raster->pixel(x,y)==b.raster->pixel(x,y),"partial viewport pixel differs from fresh full-tile render");}
void run(bool mask,double rotation,bool growth,bool dependent){Document document;document.width=2400;document.height=1200;Layer layer;layer.id="target";layer.transform={growth?500.:0,0,1800,900,rotation,rotation!=0,false};std::vector<Pixel> pixels(1800*900);for(int y=0;y<900;++y)for(int x=0;x<1800;++x)pixels[size_t(y)*1800+x]={uint8_t(x%256),uint8_t(y%256),uint8_t((x*7+y*13)%256),255};auto raster=std::make_shared<Raster>(*Raster::fromRgba(1800,900,reinterpret_cast<const uint8_t*>(pixels.data()),1800*4));raster->samplingOriginX=-3;raster->samplingOriginY=5;layer.raster=raster;if(mask)layer.mask=Mask{std::make_shared<GrayRaster>(GrayRaster{1800,900,std::vector<uint8_t>(1800*900,255)})};document.layers={layer};if(dependent){auto child=layer;child.id="clipped";child.maskSourceId=layer.id;child.opacity=.6;document.layers.push_back(child);}BrushSessionSettings settings{17,.3,.71,{31,240,5},false};GrowingBrushSession stroke(layer,settings,2400,1200,{},{},mask);CompositeCache retained;size_t shared=0;std::shared_ptr<const Raster> last;
    for(int i=0;i<9;++i){auto point=layer.transform.fromUnit({growth?-.02+i*.004:.32+i*.005,.44+i*.002});if(i)stroke.append(point);else stroke.begin(point);auto preview=stroke.preview()->renderPreview();auto actual=retained.renderViewport(document,-13.1,-7.4,2450,1250,3.3,64,256,preview);CompositeCache fresh;auto expected=fresh.renderViewport(document,-13.1,-7.4,2450,1250,3.3,64,256,preview);exact(actual,expected);if(last)for(size_t tile=0;tile<last->tiles.size();++tile)if(last->tiles[tile]==actual.raster->tiles[tile])++shared;last=actual.raster;}
    if(!dependent&&!growth)require(shared>0,"unaffected viewport tiles were regenerated");auto cancelled=retained.renderViewport(document,-13.1,-7.4,2450,1250,3.3);CompositeCache fresh;exact(cancelled,fresh.renderViewport(document,-13.1,-7.4,2450,1250,3.3));
}
void image(){run(false,0,false,false);}void mask(){run(true,0,false,false);}void rotated(){run(false,23,false,false);}void growth(){run(false,13,true,false);}void dependent(){run(false,0,false,true);}
}
int main(int argc,char** argv){std::map<std::string,void(*)()> cases{{"image",image},{"mask",mask},{"rotated",rotated},{"growth",growth},{"dependent",dependent}};int passed=0,failed=0;for(auto [name,body]:cases){if(argc>1&&name!=argv[1])continue;try{body();++passed;std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& e){++failed;std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n';}}return failed||!passed?1:0;}
