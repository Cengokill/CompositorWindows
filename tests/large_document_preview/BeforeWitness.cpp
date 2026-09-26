#include "core/Document.h"
#include "effects_tools/Levels.h"
#include "filters/PixelFilters.h"
#ifndef FROZEN_BASELINE
#include "ui/DocumentPreview.h"
#endif
#include <QCoreApplication>
#include <QImage>
#include <iostream>
#include <stdexcept>
using namespace compositor;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
Document fixture(){Document d;d.id="large-preview";d.width=d.height=30000;Layer l;l.id="small-source";l.raster=Raster::filled(32,16,{70,180,120,255});l.transform={14984,14992,32,16};d.layers.push_back(l);return d;}
QImage thumbnail(const Document& d,QSize fit){
#ifdef FROZEN_BASELINE
    auto raster=SoftwareRenderer().render(d,0,0,d.width,d.height);auto bytes=raster->rgba();return QImage(bytes.data(),d.width,d.height,d.width*4,QImage::Format_RGBA8888_Premultiplied).scaled(fit,Qt::KeepAspectRatio,Qt::SmoothTransformation);
#else
    return fittedDocumentPreview(d,fit);
#endif
}
int main(int argc,char**argv){QCoreApplication app(argc,argv);int failed=0;auto run=[&](const char*name,auto fn){try{fn();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception&e){++failed;std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n';}};
run("filter_thumbnail_30000_small_source",[]{auto d=fixture();filters::Request request;request.kind=filters::Kind::AddNoise;request.source=d.layers[0].raster;request.transform=d.layers[0].transform;request.seed=42;request.settings.amount=20;auto out=filters::apply(request);require(out.raster->width==32&&out.raster->height==16&&out.transform==request.transform,"Filter output grid changed");require(out.changed,"Noise fixture did not change");d.layers[0].raster=out.raster;auto image=thumbnail(d,{640,420});require(image.size()==QSize(420,420),"Wrong fitted filter size");require(out.raster->pixel(0,0).a==255,"Source alpha changed");});
run("adjustment_thumbnail_30000_small_source",[]{auto d=fixture();auto image=thumbnail(d,{600,460});require(image.size()==QSize(460,460),"Wrong fitted adjustment size");require(image.pixelColor(0,0).alpha()==0,"Transparent exterior changed");});
run("levels_histogram_30000_small_source",[]{auto d=fixture();effects_tools::LevelsHistogram bins;
#ifdef FROZEN_BASELINE
bins=effects_tools::levelsHistogram(*SoftwareRenderer().render(d,0,0,d.width,d.height));
#else
DocumentHistogramStats stats;bins=documentLevelsHistogram(d,{},&stats);require(stats.maxTilePixels<=65536&&stats.renderedPixels<=262144,"Histogram did not stay within frozen small-source bound");
#endif
require(bins[1][70]==512&&bins[2][180]==512&&bins[3][120]==512,"Full-resolution alpha counts changed");});return failed?1:0;}
