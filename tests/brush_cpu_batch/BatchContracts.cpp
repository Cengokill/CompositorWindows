#define compositor reference_compositor
#include "reference41/BrushCoverage.h"
#undef compositor
#include "Fixtures.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <atomic>
#include <bit>
#include <cfenv>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <thread>
#include <windows.h>
using namespace compositor::graphics;
namespace reference=reference_compositor::graphics;
namespace {
std::atomic<int> hookMode{},active{},peak{},entries{},departures{};
struct WorkerFailure:std::runtime_error{WorkerFailure():std::runtime_error("Injected brush worker failure") {}};
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int maximumGray=0;uint64_t differentFloatBits=0,comparedPixels=0;
reference::BrushUniforms oldUniform(const BrushUniforms& u){return{u.mapping,u.geometry,u.canvas,u.counts};}
std::vector<reference::BrushSegment> oldSegments(std::span<const BrushSegment> segments){std::vector<reference::BrushSegment> result;for(auto s:segments)result.push_back({s.x0,s.y0,s.x1,s.y1});return result;}
reference::BrushTile oldTile(const BrushTile& t){reference::BrushTile result(t.width,t.height);result.permanent=t.permanent;result.preview=t.preview;return result;}
void compare(const BrushTile& current,const reference::BrushTile& before){require(current.width==before.width&&current.height==before.height,"Reference dimensions differ");for(size_t i=0;i<current.preview.size();++i){maximumGray=std::max(maximumGray,std::abs(int(current.preview[i])-int(before.preview[i])));differentFloatBits+=std::bit_cast<uint32_t>(current.permanent[i])!=std::bit_cast<uint32_t>(before.permanent[i]);++comparedPixels;}require(maximumGray==0&&differentFloatBits==0,"Batched preview/density must match frozen sequential bytes exactly");}
struct Set {
    std::vector<BrushTile> tiles;std::vector<reference::BrushTile> original;std::vector<BrushTileRender> jobs;
    explicit Set(int count=19){tiles.reserve(size_t(count));original.reserve(size_t(count));for(int i=0;i<count;++i){tiles.emplace_back(i%3?17:256,i%3?31:256);auto& t=tiles.back();for(size_t p=0;p<t.permanent.size();++p)t.permanent[p]=float(p%17)/32;original.push_back(oldTile(t));}for(int i=0;i<count;++i)jobs.push_back({&tiles[size_t(i)],batch_fixture::uniform(60,0,float(i)*7-10,0)});}
    void render(std::span<const BrushSegment> settled,std::span<const BrushSegment> tail={},uint32_t workers=0){const auto a=oldSegments(settled),b=oldSegments(tail);for(size_t i=0;i<original.size();++i)reference::renderBrushCpu(original[i],oldUniform(jobs[i].uniforms),a,b);renderBrushCpuBatch(jobs,settled,tail,workers);for(size_t i=0;i<tiles.size();++i)compare(tiles[i],original[i]);}
};
bool equal(const BrushTile& a,const BrushTile& b){return a.width==b.width&&a.height==b.height&&a.preview==b.preview&&a.permanent.size()==b.permanent.size()&&std::memcmp(a.permanent.data(),b.permanent.data(),a.permanent.size()*sizeof(float))==0;}
template<class Error,class F>void rejects(F call){bool caught=false;try{call();}catch(const Error&){caught=true;}require(caught,"Expected validation exception was not propagated");}
QByteArray hash(const std::map<int,BrushTile>& tiles,bool density){QCryptographicHash sha(QCryptographicHash::Sha256);for(const auto& [key,tile]:tiles){(void)key;sha.addData(density?QByteArrayView(reinterpret_cast<const char*>(tile.permanent.data()),qsizetype(tile.permanent.size()*sizeof(float))):QByteArrayView(reinterpret_cast<const char*>(tile.preview.data()),qsizetype(tile.preview.size())));}return sha.result().toHex();}
}
namespace compositor::graphics {
void brushCpuBatchTestHook(size_t index){
    if(!hookMode.load())return;
    struct Leave{~Leave(){--active;++departures;}} leave;
    const auto concurrent=++active;++entries;auto old=peak.load();while(old<concurrent&&!peak.compare_exchange_weak(old,concurrent)){}
    if(hookMode==1&&index==2)throw WorkerFailure();
    Sleep(hookMode==2?4:8);
}
}
int main(int argc,char**argv){QCoreApplication app(argc,argv);std::cout<<std::unitbuf;if(argc!=3)return 2;const std::string key=argv[1];const std::filesystem::path out(argv[2]);if(std::filesystem::exists(out))return 2;std::filesystem::create_directories(out);bool passed=false;std::string error;QJsonObject extra;
    try{
        const std::array<BrushSegment,2> settled{{{10,40,240,180},{80,240,140,10}}};const std::array<BrushSegment,1> tail{{{140,10,240,50}}};
        if(key=="hard_analytic_batch"){
            Set data;for(auto& job:data.jobs){job.uniforms=batch_fixture::uniform(2,1);std::fill(job.tile->permanent.begin(),job.tile->permanent.end(),0);}for(size_t i=0;i<data.tiles.size();++i)data.original[i]=oldTile(data.tiles[i]);std::array<BrushSegment,1> click{{{4.5f,4.5f,4.5f,4.5f}}};data.render(click);require(data.tiles[0].preview[4*256+4]==255&&data.tiles[0].preview[4*256+6]==128&&data.tiles[0].preview[4*256+7]==0,"Hard tip analytic AA changed");
        }else if(key=="soft_tail_replace_commit"){
            Set data;data.render(settled,tail);auto permanent=data.tiles[0].permanent;auto preview=data.tiles[0].preview;data.render({},tail);require(data.tiles[0].permanent==permanent&&data.tiles[0].preview==preview,"Repeated tail accumulated");const std::array<BrushSegment,1> moved{{{10,210,220,205}}};data.render({},moved);require(data.tiles[0].permanent==permanent,"Replacing tail changed permanent density");data.render(moved);auto committed=data.tiles[0].preview;data.render({});require(data.tiles[0].preview==committed,"Empty tail flush changed committed coverage");
        }else if(key=="odd_rotated_clipped_tiles"){
            Set data;for(size_t i=0;i<data.jobs.size();++i){auto& u=data.jobs[i].uniforms;u.mapping={.96f,.28f,-.28f,.96f};u.geometry={i%2?29744.f:-100.f,i%2?0.f:-100.f,120,.35f};u.canvas={30000,149,1,6};}const std::array<BrushSegment,3> lines{{{-100,0,300,0},{29700,50,30000,50},{40,4,40,4}}};data.render(lines);require(data.tiles[0].preview[0]==0,"Outside-canvas coverage leaked");
        }else if(key=="explicit_worker_counts"){
            for(uint32_t workers:{1u,2u,4u,16u}){Set data;data.render(settled,tail,workers);}
        }else if(key=="floating_environment"){
            std::fenv_t saved{};require(std::fegetenv(&saved)==0,"Capture caller floating environment");struct Restore{std::fenv_t value;~Restore(){std::fesetenv(&value);}} restore{saved};for(int mode:{FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO}){require(std::fesetround(mode)==0,"Set caller rounding mode");Set data;data.render(settled,tail,4);require(std::fegetround()==mode,"Batch changed caller rounding mode");}
        }else if(key=="whole_batch_preflight"){
            Set data(3);const auto before=data.tiles;data.jobs.back().uniforms.geometry[2]=0;rejects<std::invalid_argument>([&]{renderBrushCpuBatch(data.jobs,settled);});for(size_t i=0;i<before.size();++i)require(equal(data.tiles[i],before[i]),"Invalid late job changed an earlier tile");data.jobs.back().uniforms.geometry[2]=60;data.tiles.back().permanent[0]=std::numeric_limits<float>::quiet_NaN();const auto invalid=data.tiles;rejects<std::invalid_argument>([&]{renderBrushCpuBatch(data.jobs,settled);});for(size_t i=0;i<invalid.size();++i)require(equal(data.tiles[i],invalid[i]),"Invalid density mutated batch");data.tiles.back().permanent[0]=0;data.tiles.back().preview.pop_back();const auto malformed=data.tiles;rejects<std::invalid_argument>([&]{renderBrushCpuBatch(data.jobs,settled);});for(size_t i=0;i<malformed.size();++i)require(equal(data.tiles[i],malformed[i]),"Malformed storage mutated batch");
        }else if(key=="null_duplicate_and_limits"){
            Set data(3);const auto before=data.tiles;auto jobs=data.jobs;jobs.back().tile=nullptr;rejects<std::invalid_argument>([&]{renderBrushCpuBatch(jobs,settled);});jobs=data.jobs;jobs.back().tile=jobs.front().tile;rejects<std::invalid_argument>([&]{renderBrushCpuBatch(jobs,settled);});rejects<std::invalid_argument>([&]{renderBrushCpuBatch(data.jobs,settled,{},17);});std::vector<BrushTileRender> huge(4097);rejects<std::length_error>([&]{renderBrushCpuBatch(huge,settled);});std::vector<BrushSegment> many(65537);rejects<std::length_error>([&]{renderBrushCpuBatch(data.jobs,many);});renderBrushCpuBatch({},{});for(size_t i=0;i<before.size();++i)require(equal(data.tiles[i],before[i]),"Rejected metadata or empty batch changed tile bytes");
        }else if(key=="worker_exception_joins_and_recovers"){
            Set data;hookMode=1;rejects<WorkerFailure>([&]{renderBrushCpuBatch(data.jobs,settled,tail,4);});require(active==0&&entries==departures,"Worker exception returned before every callback stopped");const auto after=data.tiles;Sleep(30);for(size_t i=0;i<after.size();++i)require(equal(after[i],data.tiles[i]),"Worker changed caller storage after exception returned");hookMode=0;Set healthy;healthy.render(settled,tail,4);
        }else if(key=="bounded_concurrent_calls"){
            Set first(64),second(64);for(auto* data:{&first,&second})for(auto& job:data->jobs){job.tile->width=1;job.tile->height=1;job.tile->permanent.resize(1);job.tile->preview.resize(1);}hookMode=2;std::exception_ptr errors[2];std::thread a([&]{try{renderBrushCpuBatch(first.jobs,settled,tail,16);}catch(...){errors[0]=std::current_exception();}}),b([&]{try{renderBrushCpuBatch(second.jobs,settled,tail,16);}catch(...){errors[1]=std::current_exception();}});a.join();b.join();hookMode=0;require(!errors[0]&&!errors[1]&&active==0&&entries==128&&entries==departures,"Concurrent batches did not join all requested work");require(peak>=2&&peak<=int(brushCpuMaxWorkers),"Private pool exceeded16 native workers or did not run concurrently");extra["observed_peak_workers"]=peak.load();
        }else if(key=="executor_nested_and_validation"){
            std::array<int,8> counts{};runParallelBatch(counts.size(),4,[&](size_t row){runParallelBatch(7,4,[&](size_t){++counts[row];});});for(auto count:counts)require(count==7,"Nested batch lost work or raced an independent output");runParallelBatch(0,0,{});rejects<std::invalid_argument>([]{runParallelBatch(1,0,{});});rejects<std::invalid_argument>([]{runParallelBatch(0,17,{});});rejects<std::length_error>([]{runParallelBatch(std::numeric_limits<size_t>::max(),0,[](size_t){});});
            std::atomic<int> running{},finished{};rejects<WorkerFailure>([&]{runParallelBatch(32,4,[&](size_t index){++running;struct End{std::atomic<int>& running;std::atomic<int>& finished;~End(){--running;++finished;}} end{running,finished};if(index==2)throw WorkerFailure();Sleep(8);});});require(running==0&&finished>0,"Generic executor returned before failing batch joined");const int after=finished;Sleep(20);require(finished==after,"Generic callback survived synchronous return");
        }else if(key=="row_range_odd_tails"){
            for(uint32_t height:{31u,32u,33u,63u,64u,65u,255u,256u}){Set data(3);for(size_t i=0;i<data.tiles.size();++i){data.tiles[i]=BrushTile(127,height);data.original[i]=oldTile(data.tiles[i]);data.jobs[i].uniforms.mapping={.96f,.28f,-.28f,.96f};}data.render(settled,tail);data.render({},tail);data.render(tail);}
        }else if(key=="batch_count_exact_boundary"){
            std::vector<BrushTile> tiles;tiles.reserve(4096);for(int i=0;i<4096;++i)tiles.emplace_back(1,1);std::vector<BrushTileRender> jobs;for(auto& tile:tiles)jobs.push_back({&tile,batch_fixture::uniform(2,1)});std::array<BrushSegment,1> click{{{.5f,.5f,.5f,.5f}}};renderBrushCpuBatch(jobs,click);for(const auto& tile:tiles)require(tile.preview[0]==255&&tile.permanent[0]==1,"Exact4096-job boundary lost an analytic pixel");
        }else if(key=="real_800px_two_strokes"){
            const auto events=batch_fixture::corpus();QByteArray preview,density;size_t peakTiles=0;for(int stroke=0;stroke<2;++stroke){std::map<int,BrushTile> current;std::map<int,reference::BrushTile> original;for(size_t event=0;event<events.size();++event){const auto& input=events[event];auto jobs=batch_fixture::requests(input,current);peakTiles=std::max(peakTiles,jobs.size());const auto a=oldSegments(input.settled),b=oldSegments(input.tail);for(size_t i=0;i<jobs.size();++i){const auto& job=jobs[i];auto [it,created]=original.try_emplace(input.keys[i],job.tile->width,job.tile->height);(void)created;reference::renderBrushCpu(it->second,oldUniform(job.uniforms),a,b);}renderBrushCpuBatch(jobs,input.settled,input.tail);for(size_t i=0;i<jobs.size();++i)compare(*jobs[i].tile,original.at(input.keys[i]));if(event%40==0)std::cout<<"PROGRESS stroke="<<stroke<<" event="<<event<<'\n';}const auto p=hash(current,false),d=hash(current,true);if(stroke){require(preview==p&&density==d,"Identical second stroke changed final coverage/density");}preview=p;density=d;}extra["preview_sha256"]=QString::fromLatin1(preview);extra["density_sha256"]=QString::fromLatin1(density);extra["max_event_tiles"]=int(peakTiles);extra["events_without_press"]=240;
        }else throw std::runtime_error("Unknown case");passed=true;
    }catch(const std::exception& e){hookMode=0;error=e.what();}
    extra["schema"]="CPU_BRUSH_BATCH_CONTRACTS_V1";extra["case"]=QString::fromStdString(key);extra["passed"]=passed;extra["failed_predicate"]=QString::fromStdString(error);extra["max_gray8_difference"]=maximumGray;extra["different_density_float_bits"]=qint64(differentFloatBits);extra["compared_pixels"]=qint64(comparedPixels);extra["mac_differential"]=false;QFile file(QString::fromStdWString((out/L"results.json").wstring()));if(!file.open(QIODevice::WriteOnly))return 3;file.write(QJsonDocument(extra).toJson());std::cout<<(passed?"PASS ":"FAIL ")<<key<<" "<<error<<'\n';return passed?0:1;
}
