// Fixed prospective envelope for in-flight adjustment cancellation. This file
// also compiles against the pre-cancellation API and records that missing path.
#include "effects/Adjustments.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>
using namespace compositor;
using Clock=std::chrono::steady_clock;
template<class F> bool invokeAdjustment(F function,std::shared_ptr<const Raster> source,const std::string& json,const std::function<bool()>& cancelled){
    if constexpr(std::is_invocable_v<F,std::shared_ptr<const Raster>,std::string_view,const GrayRaster*,effects::AdjustmentRegion,const std::function<bool()>&>){function(source,json,nullptr,{},cancelled);return true;}
    else {function(source,json,nullptr,{});return false;}
}
int main(int argc,char** argv){try{
    if(argc!=2)throw std::runtime_error("Provide HSV, Levels, Curves, Exposure, Gradient Map or Grain");
    const std::string argument=argv[1];const std::string kind=argument=="hsv"?"Hue/Saturation":argument=="levels"?"Levels":argument=="curves"?"Curves":argument=="exposure"?"Exposure":argument=="gradient"?"Gradient Map":argument=="grain"?"Grain":"";
    if(kind.empty())throw std::runtime_error("Unknown case");
    auto settings=QJsonDocument::fromJson(QByteArray::fromStdString(effects::defaultAdjustmentJson(kind))).object();
    if(argument=="hsv")settings["hue"]=120;
    else if(argument=="levels"){auto levels=settings["levels"].toObject();auto ranges=levels["ranges"].toArray();auto range=ranges[0].toObject();range["gamma"]=1.5;ranges[0]=range;levels["ranges"]=ranges;settings["levels"]=levels;}
    else if(argument=="curves"){auto curves=settings["curves"].toObject();auto channels=curves["channels"].toArray();channels[0]=QJsonArray{QJsonObject{{"x",0},{"y",255}},QJsonObject{{"x",255},{"y",0}}};curves["channels"]=channels;settings["curves"]=curves;}
    else if(argument=="exposure")settings["exposureSettings"]=QJsonObject{{"exposure",1},{"offset",0},{"gamma",1}};
    else if(argument=="gradient")settings["gradientMapSettings"]=QJsonObject{{"shadows",QJsonObject{{"red",0},{"green",0},{"blue",0}}},{"highlights",QJsonObject{{"red",1},{"green",1},{"blue",1}}},{"reversed",true}};
    else settings["grainSettings"]=QJsonObject{{"amount",70},{"size",1.5},{"roughness",50},{"seed",77}};
    const auto json=QJsonDocument(settings).toJson(QJsonDocument::Compact).toStdString();effects::validateAdjustmentJson(json);
    const auto source=Raster::filled(4096,4096,{70,100,150,255});const auto tile=source->tiles.front();
    std::atomic_bool requested{};std::atomic_uint64_t checks{};std::mutex mutex;std::condition_variable cv;bool started=false;Clock::time_point requestedAt;
    std::function<bool()> cancelled=[&]{++checks;return requested.load();};
    std::jthread canceller([&]{std::unique_lock lock(mutex);cv.wait(lock,[&]{return started;});lock.unlock();std::this_thread::sleep_for(std::chrono::milliseconds(20));requestedAt=Clock::now();requested.store(true);});
    bool stopped=false;const auto begin=Clock::now();{std::lock_guard lock(mutex);started=true;cv.notify_one();}
    try{invokeAdjustment(&effects::applyAdjustment,source,json,cancelled);}catch(const std::runtime_error& error){stopped=std::string(error.what()).find("cancelled")!=std::string::npos;if(!stopped)throw;}
    const auto finished=Clock::now();canceller.join();
    const double elapsed=std::chrono::duration<double,std::milli>(finished-begin).count(),latency=std::chrono::duration<double,std::milli>(finished-requestedAt).count();
    const bool immutable=source->tiles.front()==tile&&source->pixel(0,0)==Pixel{70,100,150,255}&&source->pixel(4095,4095)==Pixel{70,100,150,255};
    const bool passed=requested&&stopped&&latency>=0&&latency<=100&&immutable;
    std::printf("{\"case\":\"%s\",\"size\":4096,\"request_delay_ms\":20,\"gate_ms\":100,\"operation_ms\":%.4f,\"cancel_latency_ms\":%.4f,\"checks\":%llu,\"cancelled\":%s,\"source_immutable\":%s,\"passed\":%s}\n",argument.c_str(),elapsed,latency,static_cast<unsigned long long>(checks.load()),stopped?"true":"false",immutable?"true":"false",passed?"true":"false");
    return passed?0:1;
}catch(const std::exception& error){std::fprintf(stderr,"INVALID RUN %s\n",error.what());return 2;}}
