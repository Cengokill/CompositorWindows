#include "graphics/SamplingSource.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>
using namespace compositor;
using namespace compositor::graphics;
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{if(argc!=2)throw std::runtime_error("An unused output directory is required");QDir output(QString::fromLocal8Bit(argv[1]));if(output.exists())throw std::runtime_error("Reference output already exists");if(!output.mkpath("."))throw std::runtime_error("Cannot create reference output");QJsonArray records;int index=0;
    for(auto [width,height]:std::array<std::pair<int,int>,6>{{{513,259},{777,321},{65,2049},{2049,65},{1,2047},{2047,1}}}){
        std::vector<Pixel> pixels(size_t(width)*height);std::vector<uint8_t> gray(pixels.size());uint32_t state=29;
        const auto next=[&](){state=state*1664525u+1013904223u;return state;};for(size_t i=0;i<pixels.size();++i){const auto alpha=uint8_t(next()>>24);pixels[i]={uint8_t(next()%(uint32_t(alpha)+1)),uint8_t(next()%(uint32_t(alpha)+1)),uint8_t(next()%(uint32_t(alpha)+1)),alpha};gray[i]=uint8_t(next()>>24);}
        auto color=std::make_shared<Raster>(*Raster::fromRgba(width,height,reinterpret_cast<const uint8_t*>(pixels.data()),size_t(width)*4));color->samplingOriginX=-3;color->samplingOriginY=5;auto mask=std::make_shared<GrayRaster>(GrayRaster{width,height,std::move(gray)});mask->samplingOriginX=7;mask->samplingOriginY=-11;
        for(bool monochrome:{false,true})for(int level:{1,2,3,4,6,16}){ReducedSourceCache cache;auto input=monochrome?samplingSource(std::shared_ptr<const GrayRaster>(mask)):samplingSource(std::shared_ptr<const Raster>(color));auto reduced=cache.resolve(input,level);const auto grid=reduced->grid();std::vector<Pixel> result(size_t(grid.width)*grid.height);for(int y=0;y<grid.height;++y)for(int x=0;x<grid.width;++x)result[size_t(y)*grid.width+x]=reduced->pixel(x,y);const auto name=QString("case-%1-%2-level%3.rgba").arg(index).arg(monochrome?"gray":"rgba").arg(level);QFile file(output.filePath(name));const auto bytes=qint64(result.size()*sizeof(Pixel));if(!file.open(QIODevice::WriteOnly)||file.write(reinterpret_cast<const char*>(result.data()),bytes)!=bytes)throw std::runtime_error("Cannot write reference pixels");records.append(QJsonObject{{"name",name},{"width",grid.width},{"height",grid.height},{"grid_x",grid.x},{"grid_y",grid.y},{"bytes",bytes}});}
        ++index;
    }
    QFile manifest(output.filePath("manifest.json"));if(!manifest.open(QIODevice::WriteOnly)||manifest.write(QJsonDocument(QJsonObject{{"schema","REDUCTION_REFERENCE_PIXELS_V1"},{"cases",records}}).toJson())<0)throw std::runtime_error("Cannot write reference manifest");std::cout<<records.size()<<" reduction pixel buffers captured\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
