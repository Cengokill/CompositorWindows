// Fixed direct-preview corpus v1. Thresholds are independent of measured output.
#include "graphics/GrowingBrushSession.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace compositor;
using namespace compositor::graphics;
namespace {
using Clock=std::chrono::steady_clock;
double ms(Clock::time_point a,Clock::time_point b){return std::chrono::duration<double,std::milli>(b-a).count();}
QJsonArray numbers(const std::vector<double>& values){QJsonArray output;for(auto value:values)output.append(value);return output;}
QJsonObject summary(const std::vector<double>& values){auto sorted=values;std::sort(sorted.begin(),sorted.end());return { {"p50_ms",sorted[sorted.size()/2]}, {"p95_ms",sorted[size_t(std::ceil(.95*sorted.size()))-1]}, {"max_ms",sorted.back()} };}
Layer original(bool large){Layer layer;layer.id=newId();if(large){constexpr int side=4000;std::vector<Pixel> pixels(size_t(side)*side);uint32_t seed=7;for(auto& pixel:pixels){seed=seed*1664525u+1013904223u;pixel={uint8_t(unsigned(uint8_t(seed>>24))*128/255),uint8_t(unsigned(uint8_t(seed>>16))*128/255),uint8_t(unsigned(uint8_t(seed>>8))*128/255),128};}layer.raster=Raster::fromRgba(side,side,reinterpret_cast<const uint8_t*>(pixels.data()),side*4);layer.transform={500,500,side,side};}else layer.transform={0,0,30000,30000};return layer;}
QJsonObject measure(bool large,std::shared_ptr<D3D11BrushCoverage> gpu,const QString& backend){
    std::vector<double> appends,renders,totals,commits,commitRenders;size_t maxVisible=0;uint64_t created=0,rgba=0;size_t sparse=0,coverage=0;
    for(int pass=0;pass<2;++pass){auto layer=original(large);Document doc;doc.width=doc.height=large?5000:30000;doc.layers={layer};CompositeCache cache;const double x=large?450:14900,y=large?900:14900;
        BrushSessionSettings settings;settings.radius=large?10:5;settings.hardness=large?.4:1;settings.opacity=1;settings.color={0,0,255};
        GrowingBrushSession stroke(layer,settings,doc.width,doc.height,gpu);Raster::resetMaterializationCount();
        // Existing image display is ready before the first input, matching an editor.
        cache.renderViewport(doc,x,y,640,480,1,64,256);
        for(int event=0;event<24;++event){Point p=large?Point{490-double(event%3),1000+event*2.}:Point{15000+event*2.,15000};auto begin=Clock::now();if(event==0)stroke.begin(p);else stroke.append(p);auto appended=Clock::now();auto view=cache.renderViewport(doc,x,y,640,480,1,64,256,stroke.preview()->renderPreview());auto rendered=Clock::now();appends.push_back(ms(begin,appended));renders.push_back(ms(appended,rendered));totals.push_back(ms(begin,rendered));maxVisible=std::max(maxVisible,view.raster?view.raster->tiles.size():size_t(0));}
        auto begin=Clock::now();doc.layers[0]=stroke.commit()->materializeLayer();auto committed=Clock::now();cache.renderViewport(doc,x,y,640,480,1,64,256);auto drawn=Clock::now();commits.push_back(ms(begin,committed));commitRenders.push_back(ms(committed,drawn));auto metrics=stroke.metrics();created+=metrics.materializedTiles;rgba+=Raster::materializationCount();sparse=std::max(sparse,metrics.sparsePixelBytes);coverage=std::max(coverage,metrics.coverage.coverageStorageBytes);if(!stroke.acceleratorError().empty()||metrics.coverage.acceleratorFallbacks)throw std::runtime_error("Backend fallback invalidates performance evidence");
    }
    const auto combined=summary(totals);const bool passed=combined["p95_ms"].toDouble()<=16.7&&rgba==0&&maxVisible<=64;
    return { {"workload",large?"4000_seed7_unaligned_growth":"30000_blank_tiny"}, {"backend",backend}, {"seed",large?QJsonValue(7):QJsonValue()}, {"events",48}, {"passes",2}, {"viewport_width",640}, {"viewport_height",480}, {"viewport_origin",QJsonArray{large?450:14900,large?900:14900}}, {"units_per_pixel",1}, {"gate_p95_ms",16.7}, {"passed",passed}, {"append",summary(appends)}, {"canonical_viewport_render",summary(renders)}, {"combined_event",combined}, {"append_ms",numbers(appends)}, {"render_ms",numbers(renders)}, {"combined_ms",numbers(totals)}, {"commit_ms",numbers(commits)}, {"commit_render_ms",numbers(commitRenders)}, {"max_visible_tiles",qint64(maxVisible)}, {"materialized_source_tiles_including_commit",qint64(created)}, {"full_rgba_materializations",qint64(rgba)}, {"max_sparse_pixel_bytes",qint64(sparse)}, {"max_coverage_bytes",qint64(coverage)}, {"includes_gpu_present",false} };
}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);std::cout<<std::unitbuf;QJsonArray cases;int failed=0;try{for(bool warp:{false,true}){std::shared_ptr<D3D11BrushCoverage> gpu;QString backend="CPU";if(warp){gpu=std::make_shared<D3D11BrushCoverage>(std::filesystem::path(__FILE__).parent_path().parent_path().parent_path()/"shaders"/"BrushCoverage.hlsl",true);backend="WARP";std::cout<<"Adapter "<<gpu->adapterName()<<'\n';}for(bool large:{false,true}){auto row=measure(large,gpu,backend);cases.append(row);failed+=row["passed"].toBool()?0:1;std::cout<<QJsonDocument(row).toJson(QJsonDocument::Compact).constData()<<'\n';}}
    QJsonObject report{{"corpus","DIRECT_PREVIEW_V1"},{"source_trajectory","GrowingBrushSessionTests benchmarkViewport,2x24events"},{"fixture_note","4k fixture is seed7 premultiplied alpha128 noise; prior preview helper benchmark used uniform pixels and a smaller viewport."},{"gate_p95_ms",16.7},{"includes_gpu_present",false},{"cases",cases},{"failed",failed}};QFile file(argc>1?QString::fromLocal8Bit(argv[1]):"preview-performance.json");if(!file.open(QIODevice::WriteOnly)||file.write(QJsonDocument(report).toJson())<0)throw std::runtime_error("Cannot save performance evidence");return failed?1:0;
    }catch(const std::exception& error){std::cout<<"ERROR "<<error.what()<<'\n';return 2;}}
