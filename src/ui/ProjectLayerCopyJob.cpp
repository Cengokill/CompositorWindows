#include "ProjectLayerCopyJob.h"
#include <QFutureWatcher>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>
#include <atomic>
#include <stdexcept>

namespace compositor::ui {
struct ProjectLayerCopyJob::Impl {
    struct Result{std::shared_ptr<layers::CopyResult> value;QString error;};
    std::vector<std::string> ids;size_t next{};Host host;
    std::optional<LayerCopyWork> work;
    std::shared_ptr<std::atomic_bool> cancelled=std::make_shared<std::atomic_bool>(false);
    QFutureWatcher<Result> watcher;
};
ProjectLayerCopyJob::ProjectLayerCopyJob(std::vector<std::string> ids,Host host,QObject* parent):QObject(parent),impl_(std::make_unique<Impl>()){
    if(!host.prepare||!host.commit||!host.settled||!host.error||!host.finished)throw std::invalid_argument("Incomplete layer-copy host");
    setObjectName("projectLayerCopyJob");impl_->ids=std::move(ids);impl_->host=std::move(host);
    connect(&impl_->watcher,&QFutureWatcher<Impl::Result>::finished,this,[this]{
        auto& state=*impl_;QString error;
        try{auto result=state.watcher.result();error=result.error;if(result.value&&!state.cancelled->load())state.host.commit(*state.work,std::move(*result.value));}
        catch(const std::exception& failure){error=QString::fromUtf8(failure.what());}
        state.host.settled(*state.work);state.work.reset();
        QPointer<ProjectLayerCopyJob> alive=this;
        if(!error.isEmpty())state.host.error(error);
        if(alive)QTimer::singleShot(0,this,[this]{advance();});
    });
    QTimer::singleShot(0,this,[this]{advance();});
}
ProjectLayerCopyJob::~ProjectLayerCopyJob(){impl_->cancelled->store(true);disconnect(&impl_->watcher,nullptr,this,nullptr);impl_->watcher.waitForFinished();}
ProjectLayerCopyJob* ProjectLayerCopyJob::find(QObject* parent){if(parent)for(auto* child:parent->children())if(auto* job=dynamic_cast<ProjectLayerCopyJob*>(child))return job;return nullptr;}
void ProjectLayerCopyJob::advance(){
    auto& state=*impl_;
    while(state.next<state.ids.size()){
        try{
            state.work=state.host.prepare(state.ids[state.next++]);if(!state.work)continue;
            auto source=state.work->source,destination=state.work->destination;auto root=state.work->root;const auto point=state.work->point;auto cancelled=state.cancelled;
            state.watcher.setFuture(QtConcurrent::run([source=std::move(source),destination=std::move(destination),root=std::move(root),point,cancelled]{Impl::Result result;try{layers::Limits limits;limits.cancelled=[cancelled]{return cancelled->load();};result.value=std::make_shared<layers::CopyResult>(layers::copySubtree(source,root,destination,point,limits));}catch(const std::exception& error){result.error=QString::fromUtf8(error.what());}return result;}));
            return;
        }catch(const std::exception& error){if(state.work){state.host.settled(*state.work);state.work.reset();}QPointer<ProjectLayerCopyJob> alive=this;state.host.error(QString::fromUtf8(error.what()));if(!alive)return;}
    }
    auto finished=std::move(state.host.finished);QPointer<ProjectLayerCopyJob> alive=this;finished();if(alive)deleteLater();
}
}
