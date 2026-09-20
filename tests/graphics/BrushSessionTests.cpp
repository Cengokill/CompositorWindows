#include "graphics/BrushSession.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>

using namespace compositor;
using namespace compositor::graphics;
namespace {
int passed=0,failed=0,performanceFailures=0;
void require(bool b,const char* message){if(!b)throw std::runtime_error(message);}
template<class F>void test(const char* id,F f){try{f();++passed;std::cout<<"PASS "<<id<<std::endl;}catch(const std::exception& e){++failed;std::cout<<"FAIL "<<id<<": "<<e.what()<<std::endl;}}
void samePixels(const Raster& a,const Raster& b,int tolerance=0){require(a.width==b.width&&a.height==b.height,"raster dimensions differ");for(int y=0;y<a.height;++y)for(int x=0;x<a.width;++x){auto p=a.pixel(x,y),q=b.pixel(x,y);
    require(std::abs(int(p.r)-q.r)<=tolerance&&std::abs(int(p.g)-q.g)<=tolerance&&std::abs(int(p.b)-q.b)<=tolerance&&std::abs(int(p.a)-q.a)<=tolerance,"raster pixel difference");}}
BrushSessionSettings red(double radius=20,double hardness=1,double opacity=1,bool erase=false){return{radius,hardness,opacity,{255,0,0},erase};}
void functional(const std::shared_ptr<D3D11BrushCoverage>& gpu){
    test("G-S-001-immutable-tiles-commit-next-stroke",[&]{auto base=Raster::filled(1024,768);BrushSession stroke(base,red(20,0),gpu);stroke.begin({50,50});stroke.append({400,50});auto frozen=stroke.preview();auto first=stroke.commit();
        require(base->pixel(50,50)==Pixel{},"base mutated");require(first->pixel(200,50).r>0,"stroke missing");require(first->tiles.back()==base->tiles.back(),"untouched tile copied");require(stroke.commit()==first,"commit not pointer-idempotent");
        auto old=frozen->pixel(200,50);BrushSession next(first,red(20,0),gpu);next.begin({600,500});next.append({800,500});auto second=next.commit();require(second->tiles[0]==first->tiles[0],"next stroke copied unaffected tile");require(frozen->pixel(200,50)==old,"old preview changed");
        require(stroke.metrics().fullRasterMaterializations==0&&next.metrics().fullRasterMaterializations==0,"stroke materialized raster");});
    test("G-S-002-stroke-opacity-cap-and-erase",[&]{auto base=Raster::filled(200,80);BrushSession stroke(base,red(20,0,0.5),gpu);stroke.begin({20,40});for(double x:{180.,20.,180.,20.,100.})stroke.append({x,40});auto live=stroke.preview();auto result=stroke.commit();
        require(live->pixel(100,40)==Pixel{128,0,0,128}&&result->pixel(100,40)==Pixel{128,0,0,128},"stroke opacity cap");for(int y=0;y<80;++y)for(int x=0;x<200;++x)require(result->pixel(x,y).a<=128,"crossings exceeded opacity");
        BrushSession erase(result,red(20,1,0.5,true),gpu);erase.begin({100,40});erase.append({101,40});auto erased=erase.commit();require(erased->pixel(100,40)==Pixel{64,0,0,64},"erase must scale premultiplied RGB and alpha");require(result->pixel(100,40).a==128,"eraser mutated source");});
    test("G-S-003-cancel-and-empty-selection",[&]{auto base=Raster::filled(300,120);BrushSession stroke(base,red(),gpu);stroke.begin({100,40});auto preview=stroke.preview();require(preview!=base,"preview missing");require(stroke.cancel()==base&&stroke.preview()==base,"cancel changed base");
        auto empty=std::make_shared<GrayRaster>();empty->width=300;empty->height=120;empty->pixels.resize(300*120);BrushSession none(base,red(),gpu,empty);require(!none.begin({50,50})&&none.commit()==base,"explicitly empty selection painted");});
    test("G-S-004-selection-once-and-padding",[&]{auto base=Raster::filled(300,120,Pixel{0,0,128,128});auto m=std::make_shared<GrayRaster>();m->width=300;m->height=120;m->pixels.resize(300*120);
        for(int y=0;y<120;++y)for(int x=0;x<150;++x)m->pixels[size_t(y)*300+x]=128;
        BrushSession s(base,red(100,1,0.5),gpu,m);s.begin({150,60});auto r=s.commit();require(r->pixel(140,60)==Pixel{64,0,96,160},"selection coverage applied more than once");require(r->pixel(160,60)==base->pixel(160,60),"outside selection changed");});
    test("G-S-005-adaptive-sparse-curve",[&]{auto base=Raster::filled(300,300);BrushSession s(base,red(2,1),gpu);auto circle=[](double degrees){auto a=degrees*std::numbers::pi/180.;return Point{150+std::cos(a)*100,150+std::sin(a)*100};};
        s.begin(circle(0));for(int d=30;d<=180;d+=30)s.append(circle(d));auto r=s.commit();for(double d:{45.,75.,105.,135.}){auto p=circle(d);require(r->pixel(int(p.x),int(p.y)).a>0,"sparse curve missed arc");}
        require(s.metrics().settledSegments>6,"curve remained straight chords");});
    test("G-S-006-tail-reaches-cursor-and-no-ghost",[&]{auto base=Raster::filled(300,120);BrushSession s(base,red(4,1),gpu);s.begin({20,60});s.append({150,20});s.append({280,60});auto live=s.preview();require(live->pixel(278,60).a==255,"tail lags cursor");
        auto r=s.commit();require(r->pixel(215,40).a==0&&r->pixel(278,60).a==255,"straight tail ghost after curve commit");require(live->pixel(215,40).a>0,"frozen tail snapshot mutated");});
    test("G-S-007-transform-circle-and-flips",[&]{auto base=Raster::filled(100,100);Transform t{25,-50,50,200,90,true,true,Transform::Sampling::Nearest};auto geometry=BrushSessionGeometry::forLayer(t,100,100,100,100);
        BrushSession s(base,{8,1,1,{0,255,0},false},gpu,{},geometry);s.begin({50,50});auto raster=s.commit();
        auto source=[&](Point document){auto u=t.toUnit(document);return raster->pixel(int(u.x*100),int(u.y*100));};
        require(source({50.5,50.5}).g>240&&source({54.5,50.5}).g>240&&source({50.5,54.5}).g>240,"transformed circular interior");
        require(source({61.5,50.5}).a==0&&source({50.5,61.5}).a==0,"transformed tip stretched");});
    test("G-S-008-800px-no-periodic-ridges",[&]{auto base=Raster::filled(4000,2200);BrushSession s(base,red(400,0),gpu);s.begin({600,1600});s.append({3400,1600});auto r=s.commit();
        for(int y:{1600,1700,1800,1900,1980}){int lo=255,hi=0;for(int x=1000;x<=3000;++x){int a=r->pixel(x,y).a;lo=std::min(lo,a);hi=std::max(hi,a);}require(hi-lo<=1,"800px brush periodic ridges");}});
}
void benchmark(const std::shared_ptr<D3D11BrushCoverage>& accelerator,const char* backend,bool opaque){
    Raster::resetMaterializationCount();
    auto raster=Raster::filled(4000,4000,opaque?Pixel{0,0,0,255}:Pixel{});std::vector<double> frameTimes;
    uint64_t outputAllocations=0,coverageAllocations=0,materializations=0;size_t maxCoverageBytes=0;
    for(int pass=0;pass<2;++pass){auto totalStart=std::chrono::steady_clock::now();BrushSession session(raster,{400,0,1,{255,255,255},false},accelerator);session.begin({700,3200});
        for(int i=1;i<=120;++i){auto start=std::chrono::steady_clock::now();Point p=i<=60?Point{700.,3200.-i*40.}:Point{700.+(i-60)*40.,800.};
            session.append(p);auto visible=session.preview();require(visible!=nullptr,"missing frame snapshot");
            frameTimes.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());}
        auto beforeCommit=std::chrono::steady_clock::now();auto next=session.commit();auto committed=std::chrono::steady_clock::now();
        require(next->pixel(700,2000).a==255&&next->pixel(2000,800).a==255,"benchmark stroke missing");
        require(Raster::materializationCount()==0,"core instrumentation observed full raster materialization");
        const auto& m=session.metrics();outputAllocations+=m.outputTileAllocations;coverageAllocations+=m.coverageTileAllocations;materializations+=m.fullRasterMaterializations;maxCoverageBytes=std::max(maxCoverageBytes,m.coverageStorageBytes);
        size_t unchanged=0;for(size_t i=0;i<next->tiles.size();++i)if(next->tiles[i]==raster->tiles[i])++unchanged;
        std::cout<<"{\"benchmark\":\"4000x4000_800px_120_events\",\"backend\":\""<<backend<<"\",\"opaque\":"<<(opaque?"true":"false")<<",\"pass\":"<<pass<<",\"draw_ms\":"<<std::chrono::duration<double,std::milli>(beforeCommit-totalStart).count()
            <<",\"commit_ms\":"<<std::chrono::duration<double,std::milli>(committed-beforeCommit).count()<<",\"touched_tiles\":"<<m.touchedTiles<<",\"unchanged_tiles_shared\":"<<unchanged<<"}"<<std::endl;
        std::cout<<"{\"profile\":\"brush_session\",\"backend\":\""<<backend<<"\",\"pass\":"<<pass<<",\"preparation_ms\":"<<m.tilePreparationMilliseconds<<",\"coverage_ms\":"<<m.coverageRenderMilliseconds<<",\"composition_ms\":"<<m.tileCompositionMilliseconds<<"}"<<std::endl;
        raster=std::move(next); // immediately becomes the next pass's immutable base
    }
    std::sort(frameTimes.begin(),frameTimes.end());std::cout<<"{\"benchmark\":\"brush_session_2x120_summary\",\"backend\":\""<<backend<<"\",\"opaque\":"<<(opaque?"true":"false")
        <<",\"samples\":"<<frameTimes.size()<<",\"p50_ms\":"<<frameTimes[120]<<",\"p95_ms\":"<<frameTimes[228]<<",\"max_ms\":"<<frameTimes.back()<<",\"coverage_tile_allocations\":"<<coverageAllocations<<",\"output_tile_allocations\":"<<outputAllocations
        <<",\"max_live_coverage_bytes\":"<<maxCoverageBytes<<",\"session_materializations\":"<<materializations<<",\"core_rgba_materializations\":"<<Raster::materializationCount()<<",\"includes_UI_present\":false}"<<std::endl;
    if(accelerator){const bool targetMet=frameTimes[228]<=16.7;if(!targetMet)++performanceFailures;
        std::cout<<"{\"performance_gate\":\"WARP_session_p95\",\"target_ms\":16.7,\"observed_ms\":"<<frameTimes[228]<<",\"met\":"<<(targetMet?"true":"false")<<"}"<<std::endl;}
}
}
int main(int argc,char**argv){
    std::cout<<"BACKEND CPU"<<std::endl;functional({});
    std::shared_ptr<D3D11BrushCoverage> warp;
    if(argc>=2){try{warp=std::make_shared<D3D11BrushCoverage>(argv[1],true);std::cout<<"BACKEND "<<warp->adapterName()<<std::endl;functional(warp);
        test("G-S-009-CPU-WARP-session-agreement",[&]{auto base=Raster::filled(520,360,Pixel{10,20,30,64});BrushSession cpu(base,red(27,0.3,0.47)),gpu(base,red(27,0.3,0.47),warp);
            cpu.begin({20,40});gpu.begin({20,40});for(Point p:{Point{210,160},{320,30},{490,260},{40,310}}){cpu.append(p);gpu.append(p);}samePixels(*cpu.commit(),*gpu.commit(),1);});
    }catch(const std::exception&e){++failed;std::cout<<"FAIL WARP session initialization: "<<e.what()<<std::endl;}}
    if(argc>=3&&(std::string(argv[2])=="--benchmark"||std::string(argv[2])=="--benchmark-warp")){try{for(bool opaque:{false,true}){if(std::string(argv[2])=="--benchmark")benchmark({},"CPU",opaque);if(warp)benchmark(warp,"WARP",opaque);}}catch(const std::exception&e){++failed;std::cout<<"FAIL benchmark: "<<e.what()<<std::endl;}}
    std::cout<<"{\"suite\":\"brush_session\",\"passed\":"<<passed<<",\"failed\":"<<failed<<",\"performance_failures\":"<<performanceFailures<<",\"mac_differential\":false}"<<std::endl;return failed||performanceFailures?1:0;
}
