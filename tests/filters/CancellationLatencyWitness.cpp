// Fixed release-only cancellation envelope; build/input setup is outside timing.
// Request after20ms of operation work, then allow100ms to observe cancellation.
#include "filters/PixelFilters.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <stdexcept>
#include <thread>
using namespace compositor;
using Clock=std::chrono::steady_clock;
int main(int argc,char**argv){try{
    if(argc!=2)throw std::runtime_error("provide gaussian, motion, noise, lens or fill");
    const std::string name=argv[1];filters::Kind kind;int size=4096;
    if(name=="noise")kind=filters::Kind::AddNoise;else if(name=="lens")kind=filters::Kind::LensCorrection;
    else if(name=="fill"){kind=filters::Kind::ContentAwareFill;size=512;}
    else if(name=="gaussian")kind=filters::Kind::GaussianBlur;else if(name=="motion")kind=filters::Kind::MotionBlur;else throw std::runtime_error("unknown case");
    const auto source=Raster::filled(size,size,{70,100,150,255});const auto tile=source->tiles.front();
    GrayRaster selection;selection.width=selection.height=size;
    if(kind==filters::Kind::ContentAwareFill){selection.pixels.resize(size_t(size)*size);for(int y=128;y<384;++y)for(int x=128;x<384;++x)selection.pixels[size_t(y)*size+x]=255;}
    filters::Settings settings;settings.gaussian=true;settings.amount=80;settings.distortion=85;settings.radius=100;settings.distance=100;settings.angle=37;
    std::atomic_bool requested{};std::atomic_uint64_t checks{};std::mutex mutex;std::condition_variable cv;bool started=false;
    Clock::time_point requestedAt;filters::Limits limits;limits.cancelled=[&]{if(++checks==1){std::lock_guard lock(mutex);started=true;cv.notify_one();}return requested.load();};
    std::jthread canceller([&]{std::unique_lock lock(mutex);if(!cv.wait_for(lock,std::chrono::seconds(5),[&]{return started;}))return;lock.unlock();std::this_thread::sleep_for(std::chrono::milliseconds(20));requestedAt=Clock::now();requested.store(true);});
    bool cancelled=false;std::string error;const auto begin=Clock::now();
    try{filters::runPixels(kind,*source,settings,1,77,kind==filters::Kind::ContentAwareFill?&selection:nullptr,limits);}catch(const std::exception& e){error=e.what();cancelled=error.find("cancelled")!=std::string::npos;}
    const auto finished=Clock::now();canceller.join();
    const double elapsed=std::chrono::duration<double,std::milli>(finished-begin).count(),latency=std::chrono::duration<double,std::milli>(finished-requestedAt).count();
    const bool immutable=source->tiles.front()==tile&&source->pixel(0,0)==Pixel{70,100,150,255}&&source->pixel(size-1,size-1)==Pixel{70,100,150,255};
    const bool passed=requested&&cancelled&&latency>=0&&latency<=100&&immutable;
    std::printf("{\"case\":\"%s\",\"size\":%d,\"request_delay_ms\":20,\"gate_ms\":100,\"operation_ms\":%.4f,\"cancel_latency_ms\":%.4f,\"checks\":%llu,\"cancelled\":%s,\"source_immutable\":%s,\"passed\":%s}\n",name.c_str(),size,elapsed,latency,static_cast<unsigned long long>(checks.load()),cancelled?"true":"false",immutable?"true":"false",passed?"true":"false");
    if(!cancelled)std::printf("operation diagnostic: %s\n",error.c_str());return passed?0:1;
}catch(const std::exception& e){std::fprintf(stderr,"INVALID RUN %s\n",e.what());return 2;}}
