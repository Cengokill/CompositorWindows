#include "DocumentPreview.h"
#include "graphics/Downsample.h"
#include "graphics/SamplingSource.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
extern "C" {
#include "graphics/upstream/LevelsPixels.h"
}
namespace compositor {
QImage fittedDocumentPreview(const Document& document,QSize maximum){
    validateDocument(document);
    if(maximum.width()<1||maximum.height()<1||maximum.width()>4096||maximum.height()>4096)
        throw std::invalid_argument("Invalid document thumbnail dimensions");
    const QSize full(document.width,document.height),fitted=full.scaled(maximum,Qt::KeepAspectRatio);
    const double units=std::max({1.,double(document.width)/std::max(1,fitted.width()),double(document.height)/std::max(1,fitted.height())});
    const int width=std::max(1,std::min(document.width,fitted.width())),height=std::max(1,std::min(document.height,fitted.height()));
    auto raster=SoftwareRenderer().renderScaled(document,(document.width-width*units)/2.,(document.height-height*units)/2.,width,height,units);
    const auto bytes=raster->rgba();
    QImage image(bytes.data(),width,height,width*4,QImage::Format_RGBA8888_Premultiplied);
    return image.scaled(fitted.expandedTo({1,1}),Qt::KeepAspectRatio,Qt::SmoothTransformation).copy();
}
effects_tools::LevelsHistogram documentLevelsHistogram(const Document& document,const std::function<bool()>& cancelled,DocumentHistogramStats* stats){
    if(stats)*stats={};
    auto check=[&]{if(cancelled&&cancelled())throw std::runtime_error("Histogram cancelled");};check();validateDocument(document);
    const int columns=(document.width+255)/256,rows=(document.height+255)/256;
    SoftwareRenderer renderer;
    // Validate effects through their ordinary visibility/clip routing, including
    // empty stacks. Supported adjustments preserve alpha. Hidden metadata must
    // retain the renderer's existing behavior rather than becoming a new error.
    (void)renderer.render(document,0,0,1,1);
    std::vector<bool> occupied(size_t(columns)*rows);
    for(const auto& layer:document.layers){
        check();
        if(!layer.raster)continue;
        auto source=graphics::samplingSource(layer.raster);
        const int level=layer.transform.sampling==Transform::Sampling::Nearest?0:graphics::DownsampleCache::levelFor(layer.transform.width/source->width);
        const auto grid=graphics::samplingGrid(*source,level);
        const double left=double(grid.x)/source->width,top=double(grid.y)/source->height;
        const double right=double(grid.x+grid.width*grid.step)/source->width,bottom=double(grid.y+grid.height*grid.step)/source->height;
        double x0=double(document.width),y0=double(document.height),x1=0,y1=0;
        for(Point unit:std::array<Point,4>{{{left,top},{right,top},{right,bottom},{left,bottom}}}){
            const auto p=layer.transform.fromUnit(unit);x0=std::min(x0,p.x);y0=std::min(y0,p.y);x1=std::max(x1,p.x);y1=std::max(y1,p.y);
        }
        // Round out by one pixel to keep floating transform boundaries conservative.
        const int beginX=int(std::clamp(std::floor(x0)-1.,0.,double(document.width)))/256;
        const int beginY=int(std::clamp(std::floor(y0)-1.,0.,double(document.height)))/256;
        const int endX=(int(std::clamp(std::ceil(x1)+1.,0.,double(document.width)))+255)/256;
        const int endY=(int(std::clamp(std::ceil(y1)+1.,0.,double(document.height)))+255)/256;
        for(int y=beginY;y<endY;++y)for(int x=beginX;x<endX;++x)occupied[size_t(y)*columns+x]=true;
    }
    effects_tools::LevelsHistogram bins{};
    for(int ty=0;ty<rows;++ty)for(int tx=0;tx<columns;++tx){
        check();if(!occupied[size_t(ty)*columns+tx]){if(stats)++stats->skippedTiles;continue;}
        const int x=tx*256,y=ty*256,w=std::min(256,document.width-x),h=std::min(256,document.height-y);
        const auto raster=renderer.render(document,x,y,w,h);
        if(stats){++stats->renderedTiles;stats->renderedPixels+=uint64_t(w)*h;stats->maxTilePixels=std::max(stats->maxTilePixels,uint64_t(w)*h);}
        for(int row=0;row<h;++row)levels_histogram(reinterpret_cast<const uint8_t*>(raster->tiles[0]->pixels.data()+size_t(row)*256),nullptr,size_t(w),bins[0].data());
    }
    return bins;
}
effects_tools::LevelsHistogram documentLevelsPreviewHistogram(const Document& document,const std::function<bool()>& cancelled,DocumentPreviewHistogramStats* stats){
    if(stats)*stats={};
    const auto check=[&]{if(cancelled&&cancelled())throw std::runtime_error("Histogram cancelled");};check();validateDocument(document);
    const double factor=std::min(1.,8000./std::max(document.width,document.height));
    const int width=std::max(1,int(document.width*factor)),height=std::max(1,int(document.height*factor));
    if(stats){stats->width=width;stats->height=height;stats->logicalPreviewPixels=uint64_t(width)*height;}
    SoftwareRenderer renderer;
    // Preserve the canonical renderer's invalid/hidden-adjustment rules even
    // when all input pixels are transparent. Supported adjustments retain alpha.
    (void)renderer.render(document,0,0,1,1);check();
    const int columns=(document.width+255)/256,rows=(document.height+255)/256;
    std::vector<bool> occupied(size_t(columns)*rows),occupiedRows(size_t(rows),false);
    for(const auto& layer:document.layers){
        check();if(!layer.raster)continue;
        const auto source=graphics::samplingSource(layer.raster);
        const int level=layer.transform.sampling==Transform::Sampling::Nearest?0:graphics::DownsampleCache::levelFor(layer.transform.width/source->width);
        const auto grid=graphics::samplingGrid(*source,level);
        const double left=double(grid.x)/source->width,top=double(grid.y)/source->height;
        const double right=double(grid.x+grid.width*grid.step)/source->width,bottom=double(grid.y+grid.height*grid.step)/source->height;
        double x0=double(document.width),y0=double(document.height),x1=0,y1=0;
        for(Point unit:std::array<Point,4>{{{left,top},{right,top},{right,bottom},{left,bottom}}}){
            const auto p=layer.transform.fromUnit(unit);x0=std::min(x0,p.x);y0=std::min(y0,p.y);x1=std::max(x1,p.x);y1=std::max(y1,p.y);
        }
        // Match the conservative canonical source coverage used by the full
        // histogram. Keep hidden raster records: they can supply live masks.
        const int beginX=int(std::clamp(std::floor(x0)-1.,0.,double(document.width)))/256;
        const int beginY=int(std::clamp(std::floor(y0)-1.,0.,double(document.height)))/256;
        const int endX=(int(std::clamp(std::ceil(x1)+1.,0.,double(document.width)))+255)/256;
        const int endY=(int(std::clamp(std::ceil(y1)+1.,0.,double(document.height)))+255)/256;
        for(int y=beginY;y<endY;++y)for(int x=beginX;x<endX;++x){occupied[size_t(y)*columns+x]=true;occupiedRows[size_t(y)]=true;}
    }
    std::vector<int> sourceColumns(size_t(width),0);
    for(int x=0;x<width;++x)sourceColumns[size_t(x)]=std::min(document.width-1,int(std::floor((x+.5)*document.width/width)));
    std::vector<Pixel> row(size_t(width),Pixel{});effects_tools::LevelsHistogram bins{};
    std::vector<std::shared_ptr<const Raster>> band(size_t(columns),nullptr);int bandY=-1;uint64_t retainedPixels=0;
    for(int y=0;y<height;++y){
        check();const int sourceY=std::min(document.height-1,int(std::floor((y+.5)*document.height/height))),tileY=sourceY/256;
        if(!occupiedRows[size_t(tileY)]){if(stats)++stats->skippedRows;continue;}
        if(bandY!=tileY){std::fill(band.begin(),band.end(),nullptr);bandY=tileY;retainedPixels=0;}
        std::fill(row.begin(),row.end(),Pixel{});
        for(int x=0;x<width;){
            check();const int tileX=sourceColumns[size_t(x)]/256;int end=x+1;
            while(end<width&&sourceColumns[size_t(end)]/256==tileX)++end;
            if(occupied[size_t(tileY)*columns+tileX]){
                const int left=tileX*256,top=tileY*256,span=std::min(256,document.width-left),bandHeight=std::min(256,document.height-top);
                // Integer document centers preserve layer LOD, global Grain
                // coordinates, masks and stack order before preview decimation.
                auto& tile=band[size_t(tileX)];
                if(!tile){
                    tile=renderer.render(document,left,top,span,bandHeight);
                    const auto pixels=uint64_t(tile->tiles.size())*256*256;retainedPixels+=pixels;
                    if(stats){++stats->renderedRegions;stats->renderedPixels+=uint64_t(span)*bandHeight;stats->maxRegionPixels=std::max(stats->maxRegionPixels,uint64_t(span)*bandHeight);stats->maxOutputTilePixels=std::max(stats->maxOutputTilePixels,pixels);stats->maxRetainedTilePixels=std::max(stats->maxRetainedTilePixels,retainedPixels);}
                }
                for(int i=x;i<end;++i)row[size_t(i)]=tile->pixel(sourceColumns[size_t(i)]-left,sourceY-top);
            }else if(stats)++stats->skippedStrips;
            x=end;
        }
        check();levels_histogram(reinterpret_cast<const uint8_t*>(row.data()),nullptr,size_t(width),bins[0].data());
    }
    return bins;
}
std::optional<std::array<double,3>> sampleDocumentOriginalRGB(const Document& document,Point point){
    if(!std::isfinite(point.x)||!std::isfinite(point.y)||point.x<0||point.y<0||point.x>=document.width||point.y>=document.height)return {};
    const auto pixel=SoftwareRenderer().render(document,int(std::floor(point.x)),int(std::floor(point.y)),1,1)->pixel(0,0);
    if(!pixel.a)return {};
    return std::array<double,3>{std::min(1.,double(pixel.r)/pixel.a),std::min(1.,double(pixel.g)/pixel.a),std::min(1.,double(pixel.b)/pixel.a)};
}
}
