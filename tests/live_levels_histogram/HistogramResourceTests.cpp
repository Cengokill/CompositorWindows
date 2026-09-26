#include "ui/DocumentPreview.h"
#include "effects/Adjustments.h"
#include <QCoreApplication>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <stdexcept>
extern "C" {
#include "graphics/upstream/LevelsPixels.h"
}
using namespace compositor;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
Document sparse(){Document d;d.id="sparse-live-histogram";d.width=d.height=30000;Layer l;l.id="pixels";l.transform={14984,14992,32,16};l.transform.sampling=Transform::Sampling::Nearest;l.raster=Raster::filled(32,16,{70,180,120,255});d.layers={l};return d;}
void large_sparse(){
    const auto document=sparse(),before=document;DocumentPreviewHistogramStats stats;Raster::resetMaterializationCount();const auto bins=documentLevelsPreviewHistogram(document,{},&stats);
    int columns=0,rows=0;for(int i=0;i<8000;++i){const int sample=int(std::floor((i+.5)*30000/8000));columns+=sample>=14984&&sample<15016;rows+=sample>=14992&&sample<15008;}
    require(bins[1][70]==columns*rows&&bins[2][180]==columns*rows&&bins[3][120]==columns*rows,"Sparse large canvas retains exact nearest-selected document pixels");
    require(stats.width==8000&&stats.height==8000&&stats.logicalPreviewPixels==64000000,"Large canvas uses bounded source preview grid");
    require(stats.maxRegionPixels<=65536&&stats.maxOutputTilePixels<=65536&&stats.maxRetainedTilePixels<=uint64_t(118)*65536&&stats.renderedPixels<70000&&stats.skippedRows>7900,"Large sparse input retains one tile band and skips transparent rows");
    require(Raster::materializationCount()==0&&document==before,"Large sparse histogram avoids full raster materialization and mutation");
    std::printf("rendered_pixels=%llu regions=%llu max_output_tile_pixels=%llu skipped_rows=%llu sampled_opaque=%d\n",static_cast<unsigned long long>(stats.renderedPixels),static_cast<unsigned long long>(stats.renderedRegions),static_cast<unsigned long long>(stats.maxOutputTilePixels),static_cast<unsigned long long>(stats.skippedRows),columns*rows);
}
void large_blank(){
    auto d=sparse();d.layers.clear();DocumentPreviewHistogramStats stats;require(documentLevelsPreviewHistogram(d,{},&stats)==effects_tools::LevelsHistogram{}&&stats.renderedRegions==0&&stats.skippedRows==8000,"Blank30000 square needs no histogram raster strips");
    Layer invalid;invalid.id="invalid";invalid.adjustmentJson="{}";d.layers.push_back(invalid);bool failed=false;try{(void)documentLevelsPreviewHistogram(d);}catch(const std::exception&){failed=true;}require(failed,"Visible invalid adjustment still reports error on blank input");d.layers[0].visible=false;require(documentLevelsPreviewHistogram(d)==effects_tools::LevelsHistogram{},"Hidden invalid adjustment preserves canonical routing");
}
void cancellation(){
    auto d=sparse();d.layers[0].transform={0,0,30000,30000};DocumentPreviewHistogramStats stats;bool early=false,late=false;
    try{(void)documentLevelsPreviewHistogram(d,[]{return true;},&stats);}catch(const std::runtime_error& e){early=std::string(e.what())=="Histogram cancelled";}
    require(early&&stats.renderedRegions==0,"Cancellation before any canonical strip");
    try{(void)documentLevelsPreviewHistogram(d,[&]{return stats.renderedRegions>=1;},&stats);}catch(const std::runtime_error& e){late=std::string(e.what())=="Histogram cancelled";}
    require(late&&stats.renderedRegions==1&&stats.maxRegionPixels<=65536&&stats.maxOutputTilePixels==65536,"Cancellation checked before another bounded source tile");
}
void full_stack(){
    Document d;d.id="histogram-stack";d.width=701;d.height=523;Layer group;group.id="group";group.group=true;group.transform={0,0,701,523};group.mask=Mask{std::make_shared<GrayRaster>(GrayRaster{1,1,{128}})};d.layers.push_back(group);
    Layer base;base.id="base";base.parentId=group.id;std::vector<Pixel> input(size_t(517)*263);for(int y=0;y<263;++y)for(int x=0;x<517;++x){const auto a=uint8_t(1+(x*67+y*53)%255);input[size_t(y)*517+x]={a,uint8_t(a/2),uint8_t(a/3),a};}base.raster=Raster::fromRgba(517,263,reinterpret_cast<const uint8_t*>(input.data()),517*4);base.transform={251.25,241.75,127,70,27,true,false,Transform::Sampling::High};d.layers.push_back(base);
    Layer clipped;clipped.id="clipped";clipped.parentId=group.id;clipped.maskSourceId=base.id;clipped.raster=Raster::filled(17,19,{30,90,50,128});clipped.transform={240,220,200,200};d.layers.push_back(clipped);
    Layer grain;grain.id="grain";grain.parentId=group.id;grain.adjustmentJson=effects::defaultAdjustmentJson("Grain");d.layers.push_back(grain);
    const auto pixels=SoftwareRenderer().render(d,0,0,d.width,d.height)->rgba();effects_tools::LevelsHistogram expected{};levels_histogram(pixels.data(),nullptr,size_t(d.width)*d.height,expected[0].data());
    DocumentPreviewHistogramStats stats;const auto actual=documentLevelsPreviewHistogram(d,{},&stats);require(actual==expected,"Integer strip composition matches full canonical stack with rotation/downsampling/group mask/clipping and global Grain");require(stats.maxRegionPixels<=65536&&stats.maxOutputTilePixels<=65536&&stats.maxRetainedTilePixels<=uint64_t(3)*65536,"Complex stack retains bounded output tiles");
}
void full_histogram_retained(){
    Document d;d.id="retained-histogram";d.width=9001;d.height=3;Layer l;l.id="pixels";l.transform={0,0,9001,3};l.raster=Raster::filled(9001,3,{255,0,0,255});d.layers={l};const auto full=documentLevelsHistogram(d),preview=documentLevelsPreviewHistogram(d);require(full[1][255]==27003&&preview[1][255]==16000,"Existing full histogram keeps its contract while live preview uses capped grid");
}
void partial_right_tile(){
    Document d;d.id="right-partial-tile";d.width=30000;d.height=1;std::vector<Pixel> pixels(30000,Pixel{70,180,120,255});std::fill(pixels.begin()+29952,pixels.end(),Pixel{30,90,200,255});Layer l;l.id="pixels";l.transform={0,0,30000,1};l.transform.sampling=Transform::Sampling::Nearest;l.raster=Raster::fromRgba(30000,1,reinterpret_cast<const uint8_t*>(pixels.data()),120000);d.layers={l};
    DocumentPreviewHistogramStats stats;const auto bins=documentLevelsPreviewHistogram(d,{},&stats);int tail=0;for(int x=0;x<8000;++x)tail+=int(std::floor((x+.5)*30000/8000))>=29952;
    require(bins[1][30]==tail&&bins[2][90]==tail&&bins[3][200]==tail&&bins[1][70]==8000-tail,"Fractional8000 grid includes the unique partial-right-tile pixels exactly");
    require(stats.width==8000&&stats.height==1&&stats.renderedRegions==118&&stats.renderedPixels==30000&&stats.maxRetainedTilePixels==uint64_t(118)*65536,"30000-wide canvas retains118 tiles including the partial right edge");
    std::printf("retained_output_bytes=%llu right_edge_samples=%d\n",static_cast<unsigned long long>(stats.maxRetainedTilePixels*4),tail);
}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);const std::map<std::string,void(*)()> cases{{"large_sparse",large_sparse},{"large_blank",large_blank},{"cancellation",cancellation},{"full_stack",full_stack},{"full_histogram_retained",full_histogram_retained},{"partial_right_tile",partial_right_tile}};try{require(argc==2&&cases.contains(argv[1]),"Specify histogram resource contract");cases.at(argv[1])();std::printf("PASS %s\n",argv[1]);return 0;}catch(const std::exception& error){std::fprintf(stderr,"FAIL %s\n",error.what());return 1;}}
