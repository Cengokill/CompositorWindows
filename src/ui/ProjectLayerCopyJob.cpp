#include "ProjectLayerCopyJob.h"
#include <QFutureWatcher>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
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
            auto source=state.work->source,destination=state.work->destination;auto root=state.work->root;auto roots=state.work->listRoots;const auto point=state.work->point;const auto placement=state.work->listPlacement;auto cancelled=state.cancelled;
            state.watcher.setFuture(QtConcurrent::run([source=std::move(source),destination=std::move(destination),root=std::move(root),roots=std::move(roots),point,placement,cancelled]() mutable {Impl::Result result;try{layers::Limits limits;limits.cancelled=[cancelled]{return cancelled->load();};if(!placement){result.value=std::make_shared<layers::CopyResult>(layers::copySubtree(source,root,destination,point,limits));return result;}if(!placement->atBottom&&placement->above.empty())std::reverse(roots.begin(),roots.end());layers::CopyResult combined;std::vector<std::string> selected;for(const auto& id:roots){const auto it=std::find_if(source.layers.begin(),source.layers.end(),[&](const Layer& layer){return layer.id==id;});if(it==source.layers.end())throw std::runtime_error("Dragged layer no longer exists");auto copied=layers::copySubtree(source,id,destination,it->transform.fromUnit({.5,.5}),limits);const auto newId=copied.ids.at(id);auto placed=layers::place(copied.edit.document,{},newId,*placement);destination=std::move(placed.document);selected.push_back(newId);combined.ids.insert(copied.ids.begin(),copied.ids.end());}if(selected.empty())throw std::runtime_error("No layers to copy");combined.edit={std::move(destination),{selected,selected.back()},true,"Copy Layers from Project"};result.value=std::make_shared<layers::CopyResult>(std::move(combined));}catch(const std::exception& error){result.error=QString::fromUtf8(error.what());}return result;}));
            return;
        }catch(const std::exception& error){if(state.work){state.host.settled(*state.work);state.work.reset();}QPointer<ProjectLayerCopyJob> alive=this;state.host.error(QString::fromUtf8(error.what()));if(!alive)return;}
    }
    auto finished=std::move(state.host.finished);QPointer<ProjectLayerCopyJob> alive=this;finished();if(alive)deleteLater();
}
}
