#include "graphics/BrushCoverage.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <numeric>
#include <stdexcept>

using namespace compositor::graphics;
namespace {
int passed=0,failed=0;int maxByteDifference=0;float maxDensityDifference=0;
// Declared before first execution. CPU/HLSL math libraries differ in exp/log and
// floating arithmetic. One gray8 unit and 3e-4 density are feasibility gates,
// independent from the still-blocked Mac differential acceptance contract.
constexpr int byteTolerance=1;constexpr float densityTolerance=3e-4f;
void require(bool b,const char* s){if(!b)throw std::runtime_error(s);}
template<class F>void test(const char* id,F f){try{f();++passed;std::cout<<"PASS "<<id<<'\n';}catch(const std::exception& e){++failed;std::cout<<"FAIL "<<id<<": "<<e.what()<<'\n';}}
BrushUniforms settings(float radius=60,float hardness=0,float ox=0,float oy=0){BrushUniforms u;u.geometry={ox,oy,radius,hardness};u.canvas={4000,4000,1,std::max(0.25f,radius*2*(hardness>=1?0.015f:0.025f))};return u;}
void compare(const BrushTile& cpu,const BrushTile& gpu){for(size_t i=0;i<cpu.preview.size();++i){maxByteDifference=std::max(maxByteDifference,std::abs(int(cpu.preview[i])-int(gpu.preview[i])));
    maxDensityDifference=std::max(maxDensityDifference,std::abs(cpu.permanent[i]-gpu.permanent[i]));}
    require(maxByteDifference<=byteTolerance,"CPU/HLSL coverage tolerance exceeded");require(maxDensityDifference<=densityTolerance,"CPU/HLSL density tolerance exceeded");}
void pgm(const std::filesystem::path& path,const BrushTile& tile){std::ofstream out(path,std::ios::binary);out<<"P5\n"<<tile.width<<' '<<tile.height<<"\n255\n";out.write(reinterpret_cast<const char*>(tile.preview.data()),std::streamsize(tile.preview.size()));}
}
int main(int argc,char**argv){
    test("G-B-001-hard-tip-analytic-AA",[]{BrushTile t(16,16);auto u=settings(2,1);std::array<BrushSegment,1> click{{{4.5f,4.5f,4.5f,4.5f}}};renderBrushCpu(t,u,click);
        require(t.preview[4*16+4]==255&&t.preview[4*16+6]==128&&t.preview[4*16+7]==0,"hard analytic AA coverage");});
    test("G-B-002-tail-replacement-and-flush",[]{BrushTile t;auto u=settings();std::array<BrushSegment,1> old{{{30,40,220,40}}},next{{{30,200,220,200}}};renderBrushCpu(t,u,{},old);auto snapshot=t.preview;
        renderBrushCpu(t,u,{},old);require(t.preview==snapshot,"tail accumulated on repeated preview");require(std::all_of(t.permanent.begin(),t.permanent.end(),[](float f){return f==0;}),"tail contaminated permanent");
        renderBrushCpu(t,u,{},next);require(t.preview[40*256+128]==0&&t.preview[200*256+128]>0,"tail left ghost");renderBrushCpu(t,u,next);auto committed=t.preview;renderBrushCpu(t,u,{});require(t.preview==committed,"flush not idempotent");require(snapshot[40*256+128]>0,"snapshot mutated");});
    test("G-B-003-soft-intersection-density",[]{BrushTile vertical,horizontal,cross;auto u=settings();std::array<BrushSegment,1> v{{{128,0,128,256}}},h{{{0,128,256,128}}};
        renderBrushCpu(vertical,u,v);renderBrushCpu(horizontal,u,h);renderBrushCpu(cross,u,v);renderBrushCpu(cross,u,h);for(int offset:{45,48,51}){size_t i=size_t(128+offset)*256+128+offset;
            int a=vertical.preview[i],b=horizontal.preview[i],c=cross.preview[i],expected=255-(255-a)*(255-b)/255;require(c>std::max(a,b)+10&&std::abs(c-expected)<=3,"soft crossings failed source-over relation");}});
    test("G-B-004-distance-not-event-count",[]{for(float diameter:{12.0f,120.0f,520.0f}){BrushTile a,b;auto u=settings(diameter/2,0,300,300);std::array<BrushSegment,1> sparse{{{60,400,740,400}}};std::vector<BrushSegment> dense;
        for(int x=60;x<740;x+=5)dense.push_back({float(x),400,float(x+5),400});renderBrushCpu(a,u,sparse);renderBrushCpu(b,u,dense);
        for(int y=100;y<std::min(256,100+int(diameter/2));++y)require(std::abs(int(a.preview[size_t(y)*256+100])-int(b.preview[size_t(y)*256+100]))<=2,"sparse/dense exceeds upstream two-byte criterion");}});
    test("G-B-005-256-tile-boundary-invariance",[]{std::array<BrushSegment,1> line{{{10,70,500,150}}};for(float hardness:{0.0f,1.0f})for(int column=0;column<2;++column){BrushTile full;auto u=settings(60,hardness,float(column*256),0);renderBrushCpu(full,u,line);
        for(int half=0;half<2;++half){BrushTile piece(128,256);auto pu=u;pu.geometry[0]+=float(half*128);renderBrushCpu(piece,pu,line);
            for(unsigned y=0;y<256;++y)for(unsigned x=0;x<128;++x)require(piece.preview[y*128+x]==full.preview[y*256+x+half*128],"tile seam changed coverage");}}});
    test("G-B-006-canvas-clipping-and-30k",[]{BrushTile t;auto u=settings(100,0,-100,-100);u.canvas={30000,30000,1,5};std::array<BrushSegment,1> line{{{-100,0,300,0}}};renderBrushCpu(t,u,line);
        for(unsigned y=0;y<100;++y)for(unsigned x=0;x<256;++x)require(t.preview[y*256+x]==0,"outside canvas leaked");BrushTile end;u.geometry={29744,0,60,0};std::array<BrushSegment,1> wide{{{29700,50,30000,50}}};renderBrushCpu(end,u,wide);require(end.preview[50*256+255]>0,"30k canvas truncated");});
    test("G-B-007-invalid-state-rejected-without-mutation",[]{BrushTile t;auto u=settings();u.geometry[2]=0;auto before=t.preview;bool threw=false;try{renderBrushCpu(t,u,{});}catch(const std::invalid_argument&){threw=true;}require(threw&&before==t.preview,"invalid brush mutated state");});
    if(argc<2){std::cout<<"SKIP WARP: shader path required\n";return failed?1:0;}
    try{
        D3D11BrushCoverage warp(argv[1],true);std::cout<<"ADAPTER "<<warp.adapterName()<<'\n';
        test("G-W-001-HLSL-quadrature-permanent-tail",[&]{for(float hardness:{0.0f,0.35f,1.0f}){BrushTile cpu,gpu;auto u=settings(60,hardness);
            std::array<BrushSegment,2> committed{{{10,40,240,180},{80,240,140,10}}};std::array<BrushSegment,1> tail{{{140,10,240,50}}};
            renderBrushCpu(cpu,u,committed,tail);warp.render(gpu,u,committed,tail);compare(cpu,gpu);auto snapshot=gpu.preview;
            renderBrushCpu(cpu,u,{},tail);warp.render(gpu,u,{},tail);compare(cpu,gpu);require(snapshot==gpu.preview,"GPU tail accumulated");
            renderBrushCpu(cpu,u,{});warp.render(gpu,u,{});compare(cpu,gpu);
            renderBrushCpu(cpu,u,tail);warp.render(gpu,u,tail);compare(cpu,gpu);
        }});
        test("G-W-002-odd-tiles-transform-clipping",[&]{for(auto wh:{std::array<uint32_t,2>{1,1},{17,31},{255,239}}){BrushTile cpu(wh[0],wh[1]),gpu(wh[0],wh[1]);auto u=settings(27,0.1f,-3,4);u.mapping={0.96f,0.28f,-0.28f,0.96f};u.canvas[0]=167;u.canvas[1]=149;
            std::array<BrushSegment,3> s{{{15,20,170,180},{40,4,40,4},{160,15,10,160}}};renderBrushCpu(cpu,u,s);warp.render(gpu,u,s);compare(cpu,gpu);}});
        test("G-W-003-hard-analytic-tie-rounding",[&]{BrushTile cpu(16,16),gpu(16,16);auto u=settings(2,1);std::array<BrushSegment,1> s{{{4.5f,4.5f,4.5f,4.5f}}};renderBrushCpu(cpu,u,s);warp.render(gpu,u,s);require(gpu.preview==cpu.preview&&gpu.preview[4*16+6]==128,"GPU analytic rounding");});
        test("G-W-004-30k-document-tile",[&]{BrushTile cpu,gpu;auto u=settings(120,0,29744,0);u.canvas={30000,10,1,6};std::array<BrushSegment,1> s{{{29700,5,30000,5}}};renderBrushCpu(cpu,u,s);warp.render(gpu,u,s);compare(cpu,gpu);});
        test("G-W-005-batch-chunks-and-reuse",[&]{std::vector<BrushTile> cpu,gpu;for(unsigned i=0;i<19;++i){cpu.emplace_back(i%2?17:256,i%2?31:256);gpu.emplace_back(i%2?17:256,i%2?31:256);}
            std::array<BrushSegment,2> s{{{0,80,800,120},{200,30,100,180}}};std::vector<BrushTileRender> jobs;
            for(size_t i=0;i<cpu.size();++i){auto u=settings(60,0,float(i)*16,0);jobs.push_back({&gpu[i],u});renderBrushCpu(cpu[i],u,s);}
            warp.renderBatch(jobs,s);for(size_t i=0;i<cpu.size();++i)compare(cpu[i],gpu[i]);
            for(size_t i=0;i<cpu.size();++i)renderBrushCpu(cpu[i],jobs[i].uniforms,{},s);warp.renderBatch(jobs,{},s);for(size_t i=0;i<cpu.size();++i)compare(cpu[i],gpu[i]);
            auto before=gpu[0].preview;std::array<BrushTileRender,2> invalid{jobs[0],jobs[0]};bool rejected=false;try{warp.renderBatch(invalid,s);}catch(const std::invalid_argument&){rejected=true;}require(rejected&&gpu[0].preview==before,"duplicate batch tile mutated state");});
        if(argc>2){std::filesystem::create_directories(argv[2]);BrushTile t;auto u=settings();std::array<BrushSegment,2> crossing{{{128,0,128,256},{0,128,256,128}}};warp.render(t,u,crossing);pgm(std::filesystem::path(argv[2])/"warp-crossing.pgm",t);}
        // Feasibility microbenchmark: one newly settled line segment on one 256px
        // tile in a 4K document. Includes allocation/upload/dispatch/readback.
        std::vector<double> times;for(int i=0;i<25;++i){BrushTile tile;auto u=settings(260,0,1792,1792);std::array<BrushSegment,1> line{{{1800,1800,2100,2050}}};
            auto start=std::chrono::steady_clock::now();warp.render(tile,u,line);auto end=std::chrono::steady_clock::now();if(i>=5)times.push_back(std::chrono::duration<double,std::milli>(end-start).count());}
        std::sort(times.begin(),times.end());std::cout<<"{\"benchmark\":\"one_256_tile_in_4K_canvas\",\"samples\":20,\"p50_ms\":"<<times[times.size()/2]<<",\"p95_ms\":"<<times[18]<<",\"includes_allocation_and_readback\":true}\n";
    }catch(const std::exception& e){++failed;std::cout<<"FAIL WARP initialization/execution: "<<e.what()<<'\n';}
    std::cout<<"{\"suite\":\"brush_native_cpu_vs_HLSL_WARP\",\"passed\":"<<passed<<",\"failed\":"<<failed<<",\"max_gray8_difference\":"<<maxByteDifference<<",\"max_density_difference\":"<<maxDensityDifference<<",\"mac_differential\":false}\n";
    return failed?1:0;
}
