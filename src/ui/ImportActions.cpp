#include "ImportActions.h"
#include "imaging/wic_codec.h"
#include "imaging/heif_codec.h"
#include "imaging/svg_raster.h"
#include "imaging/raw_develop.h"
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>
#include <cmath>
#include <deque>
#include <limits>

namespace compositor::ui {
imaging::DecodedImage decodeImportImage(const std::filesystem::path& path,const imaging::ImportOptions& options){
    auto extension=QString::fromStdWString(path.extension().wstring()).toLower();
    if(extension==".svg")return imaging::rasterizeSvg(path,options);
    if(extension==".cr2"||extension==".nef"||extension==".arw"||extension==".dng"||extension==".raw"||extension==".rw2")return imaging::decodeRaw(path,{},options);
    QFile file(QString::fromStdWString(path.wstring()));
    if(!file.open(QIODevice::ReadOnly))throw std::runtime_error("Cannot open image");
    const auto signature=file.read(16);file.close();
    return signature.size()>=12&&signature.mid(4,4)=="ftyp"
        ?imaging::HeifCodec::decode(path,options):imaging::WicCodec::decode(path,options);
}
ImportResult prepareImportBatch(ImportState state,const ImportBatch& batch,const ImportDecoder& decode,imaging::ImportOptions options){
    if(!decode)throw std::invalid_argument("Image decoder is required");
    if(state.document)validateDocument(*state.document);
    if(batch.point&&(!std::isfinite(batch.point->x)||!std::isfinite(batch.point->y)))throw std::invalid_argument("Invalid image drop point");
    ImportResult result;result.before=state;result.after=std::move(state);
    const auto point=result.before.document?batch.point:std::nullopt;
    const auto budget=std::min<uint64_t>(options.remainingPixels,100000000);
    auto cancelled=[&]{return options.cancelled&&options.cancelled();};
    for(const auto& path:batch.files){
        if(cancelled()){result.cancelled=true;break;}
        try{
            uint64_t used=0;
            if(result.after.document)for(const auto& layer:result.after.document->layers)if(layer.raster){
                const auto pixels=uint64_t(layer.raster->width)*layer.raster->height;
                if(pixels>budget||used>budget-pixels)throw std::runtime_error("This project exceeds the 100-megapixel import budget");
                used+=pixels;
            }
            options.remainingPixels=budget-used;
            auto decoded=decode(path,options);imaging::checkCancelled(options);
            imaging::checkedBytes(decoded.image.width,decoded.image.height,4,options);imaging::validate(decoded.image);
            auto raster=Raster::fromRgba(int(decoded.image.width),int(decoded.image.height),decoded.image.pixels.data(),decoded.image.stride);
            if(!result.after.document){Document document;document.id=newId();document.width=raster->width;document.height=raster->height;result.after.document=std::move(document);}
            auto& document=*result.after.document;
            const Point center=point.value_or(Point{document.width/2.,document.height/2.});
            Layer layer;layer.id=newId();layer.name=QFileInfo(QString::fromStdWString(path.wstring())).completeBaseName().toUtf8().toStdString();layer.raster=std::move(raster);
            layer.transform={std::floor(center.x-layer.raster->width/2.),std::floor(center.y-layer.raster->height/2.),double(layer.raster->width),double(layer.raster->height)};
            for(const auto& active:document.layers)if(active.id==result.after.active){layer.parentId=active.group?active.id:active.parentId;break;}
            if(!layer.parentId.empty())result.expandedGroups.push_back(layer.parentId);
            result.after.active=layer.id;document.layers.push_back(std::move(layer));++result.imported;
        }catch(const std::exception& error){
            if(cancelled()){result.cancelled=true;break;}
            result.errors.append(QString::fromStdWString(path.filename().wstring())+": "+QString::fromUtf8(error.what()));
        }
    }
    if(cancelled())result.cancelled=true;
    if(result.cancelled){result.after=result.before;result.imported=0;result.expandedGroups.clear();result.errors.clear();}
    if(result.after.document)validateDocument(*result.after.document);
    return result;
}

struct ImportQueue::Impl {
    struct Request {uint64_t id{};QPointer<QObject> target;ImportBatch batch;};
    Host host;ImportDecoder decoder;std::deque<Request> pending;
    std::optional<Request> running;std::shared_ptr<std::atomic_bool> cancellation;
    QFutureWatcher<ImportResult> watcher;QTimer retry;uint64_t next{1};bool finishing{};
};
ImportQueue::ImportQueue(Host host,QObject* parent,ImportDecoder decoder):QObject(parent),impl_(std::make_unique<Impl>()){
    if(!host.snapshot||!host.blocked||!host.busy||!host.commit||!host.completed||!decoder)throw std::invalid_argument("Incomplete import host");
    setObjectName("imageImportQueue");impl_->host=std::move(host);impl_->decoder=std::move(decoder);impl_->retry.setInterval(30);impl_->retry.setSingleShot(true);
    connect(&impl_->retry,&QTimer::timeout,this,[this]{drain();});
    connect(&impl_->watcher,&QFutureWatcher<ImportResult>::finished,this,[this]{
        auto& p=*impl_;if(!p.running)return;p.finishing=true;auto request=std::move(*p.running);p.running.reset();
        auto result=p.watcher.result();
        if(p.cancellation->load()){result.cancelled=true;result.after=result.before;result.imported=0;result.errors.clear();}
        if(request.target){
            if(result.changed())try{p.host.commit(request.target,result);}catch(const std::exception& error){result.imported=0;result.after=result.before;result.errors.append(QString::fromUtf8(error.what()));}
            p.host.busy(request.target,false);p.host.completed(request.target,result);
        }
        p.cancellation.reset();p.finishing=false;QTimer::singleShot(0,this,[this]{drain();});
    });
}
ImportQueue::~ImportQueue(){
    impl_->retry.stop();if(impl_->cancellation)impl_->cancellation->store(true);
    disconnect(&impl_->watcher,nullptr,this,nullptr);impl_->watcher.waitForFinished();
}
uint64_t ImportQueue::enqueue(QObject* target,ImportBatch batch){
    if(!target||batch.files.empty())return 0;
    auto& p=*impl_;const auto id=p.next++;p.pending.push_back({id,target,std::move(batch)});drain();return id;
}
void ImportQueue::cancel(QObject* target){
    auto& p=*impl_;if(p.running&&(!target||p.running->target==target))p.cancellation->store(true);
    for(auto i=p.pending.begin();i!=p.pending.end();)if(!target||i->target==target){auto receiver=i->target;i=p.pending.erase(i);if(receiver){ImportResult result;result.cancelled=true;p.host.completed(receiver,result);}}else ++i;
}
bool ImportQueue::busy(QObject* target)const{return impl_->running&&(!target||impl_->running->target==target);}
bool ImportQueue::contains(QObject* target)const{if(busy(target))return true;for(const auto& request:impl_->pending)if(request.target==target)return true;return false;}
bool ImportQueue::idle()const{return !impl_->running&&impl_->pending.empty();}
ImportQueue* ImportQueue::find(QObject* parent){if(parent)for(auto* child:parent->children())if(auto* queue=dynamic_cast<ImportQueue*>(child))return queue;return nullptr;}
void ImportQueue::drain(){
    auto& p=*impl_;if(p.running||p.finishing)return;
    while(!p.pending.empty()){
        auto& next=p.pending.front();
        if(!next.target){p.pending.pop_front();continue;}
        if(p.host.blockedRequest?p.host.blockedRequest(next.target,next.batch):p.host.blocked(next.target)){p.retry.start();return;}
        auto before=p.host.snapshot(next.target);if(!before){p.pending.pop_front();continue;}
        p.running=std::move(next);p.pending.pop_front();p.cancellation=std::make_shared<std::atomic_bool>(false);
        auto cancelled=p.cancellation;auto batch=p.running->batch;auto decoder=p.decoder;
        p.host.busy(p.running->target,true);
        p.watcher.setFuture(QtConcurrent::run([state=std::move(*before),batch=std::move(batch),decoder=std::move(decoder),cancelled]()mutable{
            imaging::ImportOptions options;options.cancelled=[cancelled]{return cancelled->load();};
            try{return prepareImportBatch(std::move(state),batch,decoder,options);}
            catch(const std::exception& error){ImportResult result;result.errors.append(QString::fromUtf8(error.what()));return result;}
        }));return;
    }
}
}
