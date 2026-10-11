#include "LastFilterJob.h"
#include <optional>
#include <QFutureWatcher>
#include <QPointer>
#include <QProgressDialog>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>
#include <atomic>
#include <stdexcept>
namespace compositor::ui {
struct LastFilterJob::Impl {
    struct Result {std::optional<Layer> value;QString error;};
    Host host;
    EditorProject* owner{};
    std::shared_ptr<std::atomic_bool> cancelled=std::make_shared<std::atomic_bool>(false);
    QFutureWatcher<Result> watcher;
    QProgressDialog progress;
    explicit Impl(QWidget* parent):progress("Applying last filter…","Cancel",0,0,parent){
        progress.setObjectName("lastFilterProgress");progress.setWindowTitle("Last Filter");
        progress.setWindowModality(Qt::NonModal);progress.setMinimumDuration(0);
        progress.setAutoClose(false);progress.setAutoReset(false);
    }
};
LastFilterJob::LastFilterJob(std::function<Layer(std::function<bool()>)> work,Host host,QWidget* parent,EditorProject* owner)
    :QObject(parent),impl_(std::make_unique<Impl>(parent)){
    if(!work||!host.commit||!host.settled||!host.error||!owner)throw std::invalid_argument("Incomplete last-filter job");
    setObjectName("lastFilterJob");impl_->host=std::move(host);impl_->owner=owner;
    connect(&impl_->progress,&QProgressDialog::canceled,this,[this]{cancel();});
    connect(&impl_->watcher,&QFutureWatcher<Impl::Result>::finished,this,[this]{
        auto& state=*impl_;auto result=state.watcher.result();
        state.progress.hide();QPointer<LastFilterJob> alive=this;
        const bool cancelled=state.cancelled->load();
        auto host=state.host;
        host.settled();if(!alive)return;
        if(!cancelled){
            try{if(result.value)host.commit(std::move(*result.value));}
            catch(const std::exception& error){result.error=QString::fromUtf8(error.what());}
            if(!alive)return;
            if(!result.error.isEmpty())host.error(result.error);
        }
        if(alive)deleteLater();
    });
    QTimer::singleShot(0,this,[this,work=std::move(work)]{
        auto token=impl_->cancelled;
        impl_->watcher.setFuture(QtConcurrent::run([work,token]{
            Impl::Result result;
            try{
                auto cancelled=[token]{return token->load();};
                if(cancelled())throw std::runtime_error("Filter cancelled");
                result.value=work(std::move(cancelled));
            }catch(const std::exception& error){result.error=QString::fromUtf8(error.what());}
            return result;
        }));
        if(!token->load()&&impl_->watcher.isRunning())impl_->progress.show();
    });
}
LastFilterJob::~LastFilterJob(){
    impl_->cancelled->store(true);disconnect(&impl_->watcher,nullptr,this,nullptr);
    impl_->watcher.waitForFinished();
}
void LastFilterJob::cancel(){impl_->cancelled->store(true);impl_->progress.hide();}
EditorProject* LastFilterJob::owner()const{return impl_->owner;}
LastFilterJob* LastFilterJob::find(QObject* parent){if(parent)for(auto* child:parent->children())if(auto* job=dynamic_cast<LastFilterJob*>(child))return job;return nullptr;}
LastFilterJob* LastFilterJob::find(QObject* parent,EditorProject* owner){
    if(!parent||!owner)return nullptr;
    for(auto* child:parent->children())if(auto* job=dynamic_cast<LastFilterJob*>(child);job&&job->owner()==owner)return job;
    return nullptr;
}
}
