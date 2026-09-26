#include "ui/LevelsPreviewHistogram.h"
#include "ui/DocumentPreview.h"
#include "editing/Selection.h"
#include <QCoreApplication>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <stdexcept>
extern "C" {
#include "graphics/upstream/LevelsPixels.h"
}
using namespace compositor;
#define REQUIRE(...) do{if(!(__VA_ARGS__))throw std::runtime_error(std::string("line ")+std::to_string(__LINE__)+": " #__VA_ARGS__);}while(false)
Document fixture(int w,int h,bool varied=false){Document d;d.id="levels";d.width=w;d.height=h;Layer l;l.id="source";l.transform={0,0,double(w),double(h)};std::vector<Pixel> pixels(size_t(w)*h);for(int y=0;y<h;++y)for(int x=0;x<w;++x){const auto a=uint8_t(varied?1+(x*67+y*53)%255:255);pixels[size_t(y)*w+x]={a,0,0,a};}l.raster=Raster::fromRgba(w,h,reinterpret_cast<const uint8_t*>(pixels.data()),size_t(w)*4);d.layers.push_back(l);return d;}
LevelsPreviewHistogramResult actual(const Document& d){const auto&l=d.layers[0];
#ifdef FROZEN_BASELINE
    auto selection=editing::mappedCoverage(d,l.transform,l.raster->width,l.raster->height);return {effects_tools::levelsHistogram(*l.raster,selection.get()),l.raster->width,l.raster->height};
#else
    return levelsPreviewHistogram(*l.raster,l.transform,d);
#endif
}
LevelsPreviewHistogramResult sourceOracle(const Document&d){const auto&l=d.layers[0];const auto&r=*l.raster;const double factor=std::min(1.,8000./std::max(r.width,r.height));const int w=std::max(1,int(r.width*factor)),h=std::max(1,int(r.height*factor));std::vector<Pixel> pixels(size_t(w)*h);for(int y=0;y<h;++y)for(int x=0;x<w;++x)pixels[size_t(y)*w+x]=r.pixel(int(std::floor((x+.5)*r.width/w)),int(std::floor((y+.5)*r.height/h)));auto mask=editing::mappedCoverage(d,l.transform,w,h);LevelsPreviewHistogramResult result{{},w,h};levels_histogram(reinterpret_cast<const uint8_t*>(pixels.data()),mask?mask->pixels.data():nullptr,pixels.size(),result.bins[0].data());return result;}
void cap_at_8000(){auto d=fixture(9001,3);auto value=actual(d),expected=sourceOracle(d);REQUIRE(value.width==8000&&value.height==2&&value.bins==expected.bins);}
void odd_portrait_grid(){auto d=fixture(3,9001);auto value=actual(d),expected=sourceOracle(d);REQUIRE(value.width==2&&value.height==8000&&value.bins==expected.bins);}
void source_row_major_fractional(){auto d=fixture(513,519,true);const auto value=actual(d),expected=sourceOracle(d);REQUIRE(value.bins==expected.bins);}
void capped_selection_mapping(){auto d=fixture(9001,3,true);auto mask=std::make_shared<GrayRaster>(GrayRaster{9001,3,std::vector<uint8_t>(27003)});for(size_t i=0;i<mask->pixels.size();++i)mask->pixels[i]=uint8_t(i%256);d.selection=Selection{mask};const auto value=actual(d),expected=sourceOracle(d);REQUIRE(value.width==8000&&value.height==2&&value.bins==expected.bins);}
void native_streaming_order_preserved(){auto d=fixture(513,519,true);auto native=effects_tools::levelsHistogram(*d.layers[0].raster),streamed=documentLevelsHistogram(d);REQUIRE(native==streamed);const auto source=sourceOracle(d);REQUIRE(native!=source.bins);double maxDifference=0;for(int c=0;c<4;++c)for(int b=0;b<256;++b)maxDifference=std::max(maxDifference,std::abs(native[size_t(c)][size_t(b)]-source.bins[size_t(c)][size_t(b)]));std::cout<<"native_vs_source_max_bin_difference="<<maxDifference<<'\n';}
void input_immutable_no_flatten(){auto d=fixture(8000,2,true);const auto before=d;Raster::resetMaterializationCount();const auto value=actual(d);REQUIRE(value.width==8000&&value.height==2&&d==before&&Raster::materializationCount()==0);}
#ifndef FROZEN_BASELINE
void cancellation(){auto d=fixture(9001,3);int checks=0;bool stopped=false;try{levelsPreviewHistogram(*d.layers[0].raster,d.layers[0].transform,d,[&]{return ++checks>1;});}catch(const std::runtime_error& e){stopped=std::string(e.what())=="Histogram cancelled";}REQUIRE(stopped);}
#endif
int main(int argc,char**argv){QCoreApplication app(argc,argv);std::map<std::string,void(*)()>tests{{"cap_at_8000",cap_at_8000},{"odd_portrait_grid",odd_portrait_grid},{"source_row_major_fractional",source_row_major_fractional},{"capped_selection_mapping",capped_selection_mapping},{"native_streaming_order_preserved",native_streaming_order_preserved},{"input_immutable_no_flatten",input_immutable_no_flatten}};
#ifndef FROZEN_BASELINE
tests.emplace("cancellation",cancellation);
#endif
int failed=0;for(const auto&[name,test]:tests){if(argc>1&&argv[1]!=name)continue;try{test();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception&e){++failed;std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n';}}if(argc>1&&!tests.contains(argv[1]))return 2;return failed?1:0;}
