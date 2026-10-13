#include "ViewportCompositeJob.h"
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <chrono>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>

namespace compositor::ui {
namespace {
ViewportCompositeJob::Result renderSnapshot(const ViewportCompositeJob::Request& request,uint64_t epoch,const std::shared_ptr<std::atomic<uint64_t>>& liveEpoch,const std::shared_ptr<std::atomic_bool>& proceed){
    ViewportCompositeJob::Result result;
    result.owner=request.owner;result.document=request.document;result.zoom=request.zoom;result.panX=request.panX;result.panY=request.panY;
    result.canvasWidth=request.canvasWidth;result.canvasHeight=request.canvasHeight;result.epoch=epoch;
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
}
