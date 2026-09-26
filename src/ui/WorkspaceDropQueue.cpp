#include "WorkspaceDropQueue.h"
#include <QFileInfo>
#include <QTimer>
#include <deque>
#include <stdexcept>

namespace compositor::ui {
struct WorkspaceDropQueue::Impl {
    Host host;std::deque<WorkspaceDropRequest> pending;std::optional<WorkspaceDropRequest> running;
    QPointer<QObject> waiting;qsizetype index{};bool waitingActive{},cancelled{},draining{},completing{};
    QTimer retry;
};
WorkspaceDropQueue::WorkspaceDropQueue(Host host,QObject* parent):QObject(parent),impl_(std::make_unique<Impl>()){
    if(!host.canBegin||!host.canAdvance||!host.managing||!host.project||!host.imageTarget||!host.image||!host.cancelImage||!host.error)throw std::invalid_argument("Incomplete workspace drop host");
    setObjectName("workspaceDropQueue");impl_->host=std::move(host);impl_->retry.setSingleShot(true);impl_->retry.setInterval(30);
    connect(&impl_->retry,&QTimer::timeout,this,[this]{drain();});
}
WorkspaceDropQueue::~WorkspaceDropQueue(){impl_->retry.stop();}
WorkspaceDropQueue* WorkspaceDropQueue::find(QObject* parent){if(parent)for(auto* child:parent->children())if(auto* queue=dynamic_cast<WorkspaceDropQueue*>(child))return queue;return nullptr;}
bool WorkspaceDropQueue::idle()const{return !impl_->running&&impl_->pending.empty()&&!impl_->completing;}
bool WorkspaceDropQueue::waitingFor(QObject* target)const{return target&&impl_->waitingActive&&impl_->waiting==target;}
void WorkspaceDropQueue::enqueue(WorkspaceDropRequest request){if(request.paths.isEmpty()&&!request.completed)return;impl_->pending.push_back(std::move(request));drain();}
void WorkspaceDropQueue::finish(){
    auto& p=*impl_;auto completed=std::move(p.running->completed);const bool cancelled=p.cancelled;
    p.running.reset();p.waiting=nullptr;p.waitingActive=false;p.cancelled=false;p.index=0;p.host.managing(false);
    if(completed){
        p.completing=true;
        QTimer::singleShot(0,this,[this,done=std::move(completed),cancelled]{
            QPointer<WorkspaceDropQueue> alive(this);
            try{done(cancelled);}catch(...){qWarning("Workspace drop completion callback threw");}
            if(alive){impl_->completing=false;impl_->retry.start(0);}
        });
    }
}
void WorkspaceDropQueue::imageFinished(QObject* target,bool cancelled){auto& p=*impl_;if(!waitingFor(target))return;p.waiting=nullptr;p.waitingActive=false;p.cancelled|=cancelled;p.retry.start(0);}
void WorkspaceDropQueue::cancel(QObject* destination){
    auto& p=*impl_;
    for(auto i=p.pending.begin();i!=p.pending.end();)if(!destination||i->destination==destination){auto completed=std::move(i->completed);i=p.pending.erase(i);if(completed)QTimer::singleShot(0,this,[done=std::move(completed)]{done(true);});}else ++i;
    if(p.running&&(!destination||p.running->destination==destination||p.waiting==destination)){p.cancelled=true;if(p.waiting)p.host.cancelImage(p.waiting);p.retry.start(0);}
}
void WorkspaceDropQueue::drain(){
    auto& p=*impl_;if(p.draining||p.completing)return;p.draining=true;
    struct Restore{bool& flag;~Restore(){flag=false;}} restore{p.draining};
    for(;;){
        if(p.waitingActive){
            // The workspace prevents normal tab closure during an import, but
            // a destroyed receiver must not leave a permanently owned queue.
            if(p.waiting||!p.host.canAdvance()){p.retry.start();return;}
            p.waitingActive=false;
        }
        if(!p.running){
            if(p.pending.empty())return;
            if(!p.host.canBegin()){p.retry.start();return;}
            p.running=std::move(p.pending.front());p.pending.pop_front();p.index=0;p.cancelled=false;p.host.managing(true);
        }
        if(p.cancelled||p.index>=p.running->paths.size()){finish();if(p.completing)return;continue;}
        if(!p.host.canAdvance()){p.retry.start();return;}
        const auto path=p.running->paths[p.index++];
        try{
            if(QFileInfo(path).suffix().compare("comp",Qt::CaseInsensitive)==0){p.host.project(path);continue;}
            auto* target=p.host.imageTarget(p.running->destination,p.running->hasDestination);if(!target)continue;
            p.waiting=target;p.waitingActive=true;
            p.host.image(target,path,p.running->point,p.running->lifetime);
            p.retry.start();return;
        }catch(const std::exception& error){p.waiting=nullptr;p.waitingActive=false;p.host.error(path,QString::fromUtf8(error.what()));}
    }
}
}
