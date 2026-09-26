#include "LayerMergeJob.h"
#include <QFutureWatcher>
#include <QPointer>
#include <QProgressDialog>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>
#include <atomic>
#include <stdexcept>
namespace compositor::ui {
struct LayerMergeJob::Impl {
    struct Result {std::optional<layers::EditResult> value;QString error;};
    Host host;
    std::shared_ptr<std::atomic_bool> cancelled=std::make_shared<std::atomic_bool>(false);
    QFutureWatcher<Result> watcher;
    QProgressDialog progress;
    explicit Impl(QWidget* parent):progress("Merging layers…","Cancel",0,0,parent){
        progress.setObjectName("mergeProgress");progress.setWindowTitle("Merge Layers");
        progress.setWindowModality(Qt::NonModal);progress.setMinimumDuration(0);
        progress.setAutoClose(false);progress.setAutoReset(false);
    }
};
LayerMergeJob::LayerMergeJob(Document document,layers::SelectionState selection,Host host,QWidget* parent)
    :QObject(parent),impl_(std::make_unique<Impl>(parent)){
    if(!host.commit||!host.settled||!host.error)throw std::invalid_argument("Incomplete merge host");
    setObjectName("layerMergeJob");impl_->host=std::move(host);
    connect(&impl_->progress,&QProgressDialog::canceled,this,[this]{cancel();});
    connect(&impl_->watcher,&QFutureWatcher<Impl::Result>::finished,this,[this]{
        auto& state=*impl_;auto result=state.watcher.result();
        state.progress.hide();QPointer<LayerMergeJob> alive=this;
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
    // Queue entry so the command can return and its busy-state refresh finish.
    QTimer::singleShot(0,this,[this,document=std::move(document),selection=std::move(selection)]{
        auto token=impl_->cancelled;
        impl_->watcher.setFuture(QtConcurrent::run([document,selection,token]{
            Impl::Result result;
            try{layers::Limits limits;limits.cancelled=[token]{return token->load();};
                result.value=layers::merge(document,selection,SoftwareRenderer(),limits);
            }catch(const std::exception& error){result.error=QString::fromUtf8(error.what());}
            return result;
        }));
        if(!token->load()&&impl_->watcher.isRunning())impl_->progress.show();
    });
}
LayerMergeJob::~LayerMergeJob(){
    impl_->cancelled->store(true);disconnect(&impl_->watcher,nullptr,this,nullptr);
    impl_->watcher.waitForFinished();
}
void LayerMergeJob::cancel(){impl_->cancelled->store(true);impl_->progress.hide();}
LayerMergeJob* LayerMergeJob::find(QObject* parent){if(parent)for(auto* child:parent->children())if(auto* job=dynamic_cast<LayerMergeJob*>(child))return job;return nullptr;}
}
