#include "editing/PixelEdits.h"
#ifndef FROZEN_BASELINE
#include "editing/GradientPreview.h"
#endif
#include <iostream>
#include <set>
#include <string>
using namespace compositor;
using namespace compositor::editing;
Document fixture(int w,int h){Document d;d.id="gradient-document";d.width=w;d.height=h;Layer l;l.id="gradient-target";l.transform={0,0,double(w),double(h)};d.layers={l};return d;}
GradientSettings settings(){GradientSettings s;s.style=GradientStyle::ForegroundToBackground;return s;}
uint64_t preview(const Document& d){
#ifdef FROZEN_BASELINE
    auto layer=gradientLayer(d.layers.front(),d,{.5,.5},{double(d.width)-.5,.5},settings());
    std::set<const Raster::Tile*> unique;if(layer.raster)for(const auto& tile:layer.raster->tiles)unique.insert(tile.get());return unique.size()*sizeof(Raster::Tile);
#else
    GradientPreview p(d.layers.front(),d,{.5,.5},{double(d.width)-.5,.5},settings());return p.previewOwnedPixelBytes();
#endif
}
int main(){int failed=0;auto run=[&](const char* name,auto body){try{const auto ok=body();std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n';failed+=!ok;}catch(const std::exception& e){std::cout<<"FAIL "<<name<<" unexpected="<<e.what()<<'\n';++failed;}};
run("aggregate_image_budget_rejected_before_preview",[]{auto d=fixture(128,128);d.layers[0].transform={0,0,4,4};d.layers[0].raster=Raster::filled(4,4);Layer other;other.id="other";other.raster=Raster::filled(10000,9999);d.layers.push_back(other);validateDocument(d);try{(void)preview(d);return false;}catch(const std::exception&){return true;}});
run("owned_mask_budget_couples_image_growth",[]{auto d=fixture(128,128);d.layers[0].transform={0,0,4,4};d.layers[0].raster=Raster::filled(4,4);auto own=std::make_shared<GrayRaster>(GrayRaster{1,1,{255}});d.layers[0].mask=Mask{own};Layer other;other.id="other";auto gray=std::make_shared<GrayRaster>();gray->width=10000;gray->height=9999;gray->pixels.resize(size_t(gray->width)*gray->height,255);other.mask=Mask{gray};d.layers.push_back(other);validateDocument(d);try{(void)preview(d);return false;}catch(const std::exception&){return true;}});
run("preview_pixels_bounded_before_viewport_request",[]{auto d=fixture(2048,2048);const auto bytes=preview(d);std::cout<<"owned_preview_pixel_bytes="<<bytes<<" maximum="<<16*sizeof(Raster::Tile)<<'\n';return bytes<=16*sizeof(Raster::Tile);});
run("unselected_blank_30000_square_still_rejected",[]{auto d=fixture(30000,30000);try{(void)preview(d);return false;}catch(const std::exception&){return true;}});
std::cout<<"cases=4 failed="<<failed<<" mac_differential=false\n";return failed?1:0;}
