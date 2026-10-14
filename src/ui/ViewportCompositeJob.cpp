#include "ViewportCompositeJob.h"
#include "editing_transform/TransformGeometry.h"
#include "graphics/Downsample.h"
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>

namespace compositor::ui {
namespace {
ViewportCompositeJob::Result renderSnapshot(const ViewportCompositeJob::Request& request,uint64_t epoch,const std::shared_ptr<std::atomic<uint64_t>>& liveEpoch,const std::shared_ptr<std::atomic_bool>& proceed){
    ViewportCompositeJob::Result result;
    result.owner=request.owner;result.document=request.document;result.zoom=request.zoom;result.panX=request.panX;result.panY=request.panY;
    result.canvasWidth=request.canvasWidth;result.canvasHeight=request.canvasHeight;result.epoch=epoch;result.gesture=request.gesture;
    const auto abandoned=[&]{return !liveEpoch||liveEpoch->load()!=epoch;};
    if(proceed){while(!proceed->load()){if(abandoned()){result.stale=true;return result;}std::this_thread::sleep_for(std::chrono::milliseconds(2));}}
    if(abandoned()){result.stale=true;return result;}
    try{result.view=result.cache.renderViewport(request.document,request.x,request.y,request.width,request.height,request.units,64,256);}
    catch(const std::exception& error){result.error=QString::fromUtf8(error.what());}
    if(abandoned())result.stale=true;
    return result;
}
}
struct ViewportCompositeJob::Impl {
    struct Queued { Request request; uint64_t epoch{}; };
    Receiver receiver;
    std::shared_ptr<std::atomic<uint64_t>> epoch=std::make_shared<std::atomic<uint64_t>>(1);
    std::shared_ptr<std::atomic_bool> proceed;
    std::optional<Queued> pending;
    bool running{};
    bool stopped{};
    QFutureWatcher<Result> watcher;
};
ViewportCompositeJob::ViewportCompositeJob(Receiver receiver,QObject* parent):QObject(parent),impl_(std::make_unique<Impl>()){
    if(!receiver)throw std::invalid_argument("Viewport composite job requires a receiver");
    setObjectName("viewportCompositeJob");impl_->receiver=std::move(receiver);
    connect(&impl_->watcher,&QFutureWatcher<Result>::finished,this,[this]{
        if(!impl_||impl_->stopped)return;
        auto result=impl_->watcher.result();
        impl_->running=false;
        std::optional<Impl::Queued> next;
        if(impl_->pending){next=std::move(impl_->pending);impl_->pending.reset();}
        if(next&&next->epoch==impl_->epoch->load()){start(std::move(next->request));return;}
        if(result.stale||result.epoch!=impl_->epoch->load()||!impl_->receiver)return;
        impl_->receiver(std::move(result));
    });
}
ViewportCompositeJob::~ViewportCompositeJob(){shutdown();}
void ViewportCompositeJob::setProceedGate(std::shared_ptr<std::atomic_bool> gate){if(impl_)impl_->proceed=std::move(gate);}
uint64_t ViewportCompositeJob::epoch() const{return impl_&&impl_->epoch?impl_->epoch->load():0;}
void ViewportCompositeJob::invalidate(){if(!impl_||impl_->stopped)return;impl_->epoch->fetch_add(1);impl_->pending.reset();}
void ViewportCompositeJob::shutdown(){
    if(!impl_||impl_->stopped)return;
    impl_->stopped=true;impl_->epoch->fetch_add(1);impl_->pending.reset();
    disconnect(&impl_->watcher,nullptr,this,nullptr);
    impl_->watcher.waitForFinished();
}
void ViewportCompositeJob::submit(Request request){
    if(!impl_||impl_->stopped||!request.owner)return;
    if(impl_->running){impl_->pending=Impl::Queued{std::move(request),impl_->epoch->load()};return;}
    start(std::move(request));
}
void ViewportCompositeJob::start(Request request){
    if(!impl_||impl_->stopped||!request.owner)return;
    const uint64_t epoch=impl_->epoch->load();
    impl_->running=true;
    auto live=impl_->epoch;auto proceed=impl_->proceed;
    impl_->watcher.setFuture(QtConcurrent::run([request=std::move(request),epoch,live,proceed]{return renderSnapshot(request,epoch,live,proceed);}));
}
namespace {
MovePreviewJob::Result renderPreview(const MovePreviewJob::Request& request,uint64_t epoch,const std::shared_ptr<std::atomic<uint64_t>>& liveEpoch){
    MovePreviewJob::Result result;
    result.gesture=request.gesture;result.layerId=request.layerId;result.epoch=epoch;result.masked=request.masked;result.anchorX=request.anchorX;result.anchorY=request.anchorY;
    result.viewX=request.viewX;result.viewY=request.viewY;result.units=request.units;result.movingX=request.movingX;result.movingY=request.movingY;
    const auto abandoned=[&]{return !liveEpoch||liveEpoch->load()!=epoch;};
    if(abandoned()){result.stale=true;return result;}
    try{
        result.base=SoftwareRenderer().renderScaled(request.back,request.viewX,request.viewY,request.pixelsWide,request.pixelsHigh,request.units);
        if(request.wantFront&&!abandoned())result.front=SoftwareRenderer().renderScaled(request.front,request.viewX,request.viewY,request.pixelsWide,request.pixelsHigh,request.units);
        if(!abandoned()){
            auto moving=SoftwareRenderer().renderScaled(request.movingLayer,request.movingX,request.movingY,request.movingPixelsWide,request.movingPixelsHigh,request.units);
            if(!moving)throw std::runtime_error("Move preview renderer returned no layer image");
            if(request.masked)result.plate=std::move(moving);else result.sprite=std::move(moving);
        }
    }catch(const std::exception& error){result.error=QString::fromUtf8(error.what());}
    if(abandoned())result.stale=true;
    return result;
}
}
MovePreviewBounds movingLayerPreviewBounds(const Layer& layer,double units){
    MovePreviewBounds result;
    if(!layer.raster||layer.raster->width<1||layer.raster->height<1||!layer.transform.valid()||!(units>=1./32)||units>32768||!std::isfinite(units))return result;
    const auto box=editing_transform::bounds(layer.transform);
    // Lanczos halving reads 10 source steps past the output sample. Smooth
    // sampling without a reduction still needs a couple of document pixels.
    double margin=2;
    if(layer.transform.sampling!=Transform::Sampling::Nearest){
        const int level=graphics::DownsampleCache::levelFor(layer.transform.width/(units*layer.raster->width));
        if(level>0){
            const double step=double(uint64_t(1)<<level),radius=10.*(step-1.)+step;
            const double angle=std::remainder(layer.transform.rotation,360.)*std::numbers::pi/180.,c=std::abs(std::cos(angle)),s=std::abs(std::sin(angle));
            const double hx=radius*layer.transform.width/layer.raster->width,hy=radius*layer.transform.height/layer.raster->height;
            margin+=std::max(c*hx+s*hy,s*hx+c*hy);
        }
    }
    result.x=box.x-margin;result.y=box.y-margin;result.width=box.width+margin*2;result.height=box.height+margin*2;
    if(!(result.width>0)||!(result.height>0)||!std::isfinite(result.x)||!std::isfinite(result.y)||!std::isfinite(result.width)||!std::isfinite(result.height)||std::abs(result.x)>10000000||std::abs(result.y)>10000000)return result;
    const double wide=std::ceil(result.width/units),high=std::ceil(result.height/units);
    if(!std::isfinite(wide)||!std::isfinite(high)||wide<1||high<1||wide>30000||high>30000||wide*high>100000000.)return result;
    result.pixelsWide=int(wide);result.pixelsHigh=int(high);result.valid=true;return result;
}
struct MovePreviewJob::Impl {
    struct Queued { Request request; uint64_t epoch{}; uint64_t gesture{}; };
    Receiver receiver;
    std::shared_ptr<std::atomic<uint64_t>> epoch=std::make_shared<std::atomic<uint64_t>>(1);
    std::optional<Queued> pending;
    bool running{};
    bool stopped{};
    uint64_t runningGesture{};
    uint64_t runningEpoch{};
    QFutureWatcher<Result> watcher;
};
MovePreviewJob::MovePreviewJob(Receiver receiver,QObject* parent):QObject(parent),impl_(std::make_unique<Impl>()){
    if(!receiver)throw std::invalid_argument("Move preview job requires a receiver");
    setObjectName("movePreviewJob");impl_->receiver=std::move(receiver);
    connect(&impl_->watcher,&QFutureWatcher<Result>::finished,this,[this]{
        if(!impl_||impl_->stopped)return;
        auto result=impl_->watcher.result();
        impl_->running=false;
        std::optional<Impl::Queued> next;
        if(impl_->pending){next=std::move(impl_->pending);impl_->pending.reset();}
        if(next&&next->epoch==impl_->epoch->load()){start(std::move(next->request));return;}
        if(result.stale||result.epoch!=impl_->epoch->load()||!impl_->receiver)return;
        impl_->receiver(std::move(result));
    });
}
MovePreviewJob::~MovePreviewJob(){shutdown();}
void MovePreviewJob::invalidate(){if(!impl_||impl_->stopped)return;impl_->epoch->fetch_add(1);impl_->pending.reset();}
void MovePreviewJob::shutdown(){
    if(!impl_||impl_->stopped)return;
    impl_->stopped=true;impl_->epoch->fetch_add(1);impl_->pending.reset();
    disconnect(&impl_->watcher,nullptr,this,nullptr);
    impl_->watcher.waitForFinished();
}
bool MovePreviewJob::covers(uint64_t gesture) const{
    if(!impl_||impl_->stopped)return false;
    if(impl_->pending&&impl_->pending->gesture==gesture&&impl_->pending->epoch==impl_->epoch->load())return true;
    return impl_->running&&impl_->runningGesture==gesture&&impl_->runningEpoch==impl_->epoch->load();
}
void MovePreviewJob::submit(Request request){
    if(!impl_||impl_->stopped||request.layerId.empty())return;
    if(impl_->running){const uint64_t gesture=request.gesture;impl_->pending=Impl::Queued{std::move(request),impl_->epoch->load(),gesture};return;}
    start(std::move(request));
}
void MovePreviewJob::start(Request request){
    if(!impl_||impl_->stopped||request.layerId.empty())return;
    const uint64_t epoch=impl_->epoch->load();
    impl_->running=true;impl_->runningGesture=request.gesture;impl_->runningEpoch=epoch;
    auto live=impl_->epoch;
    impl_->watcher.setFuture(QtConcurrent::run([request=std::move(request),epoch,live]{return renderPreview(request,epoch,live);}));
}
}
