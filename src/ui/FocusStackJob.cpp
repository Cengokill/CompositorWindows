#include "FocusStackJob.h"
#include "editing/FocusStack.h"
#include <QFutureWatcher>
#include <QPointer>
#include <QProgressDialog>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>
#include <atomic>
#include <stdexcept>
namespace compositor::ui {
std::shared_ptr<const Raster> runFocusStack(const std::vector<std::filesystem::path>& files,const ImportDecoder& decode,
    const std::function<bool()>& cancelled,std::uint64_t maxWorkingBytes){
    if(!decode)throw std::invalid_argument("Image decoder is required");
    if(files.size()<2)throw std::runtime_error("Select at least two images to focus stack");
    if(files.size()>editing::maxFocusStackFrames)throw std::runtime_error("Focus stacking accepts at most 16 images");
    imaging::ImportOptions options;options.cancelled=cancelled;options.maxWorkingBytes=maxWorkingBytes;
    std::vector<imaging::RgbaImage> frames;frames.reserve(files.size());
    for(const auto& file:files){
        const auto name=QString::fromStdWString(file.filename().wstring()).toStdString();
        if(cancelled&&cancelled())throw std::runtime_error("Focus stack cancelled");
        imaging::DecodedImage decoded;
        try{decoded=decode(file,options);imaging::validate(decoded.image);}
        catch(const std::exception& error){
            if(cancelled&&cancelled())throw std::runtime_error("Focus stack cancelled");
            throw std::runtime_error(name+": "+error.what());
        }
        if(frames.empty()){
            // Bound the whole stack from the first frame before decoding the others.
            if(editing::focusStackWorkingBytes(decoded.image.width,decoded.image.height,files.size())>maxWorkingBytes)
                throw std::runtime_error("These images are too large to focus stack together");
        }else if(decoded.image.width!=frames.front().width||decoded.image.height!=frames.front().height){
            throw std::runtime_error(name+": size "+std::to_string(decoded.image.width)+"x"+std::to_string(decoded.image.height)
                +" differs from the first image ("+std::to_string(frames.front().width)+"x"+std::to_string(frames.front().height)+")");
        }
        frames.push_back(std::move(decoded.image));
    }
    editing::FocusStackLimits limits;limits.maxWorkingBytes=maxWorkingBytes;limits.cancelled=cancelled;
    auto stacked=editing::focusStack(frames,limits);
    frames.clear();frames.shrink_to_fit();
    const auto& image=stacked.image;
    return Raster::fromRgba(int(image.width),int(image.height),image.pixels.data(),image.stride);
}
struct FocusStackJob::Impl {
    struct Result {std::shared_ptr<const Raster> raster;QString error;};
    Host host;
    std::shared_ptr<std::atomic_bool> cancelled=std::make_shared<std::atomic_bool>(false);
    QFutureWatcher<Result> watcher;
    QProgressDialog progress;
    explicit Impl(QWidget* parent):progress("Focus stacking images…","Cancel",0,0,parent){
        progress.setObjectName("focusStackProgress");progress.setWindowTitle("Focus Stack");
        progress.setWindowModality(Qt::NonModal);progress.setMinimumDuration(0);
        progress.setAutoClose(false);progress.setAutoReset(false);
    }
};
FocusStackJob::FocusStackJob(std::vector<std::filesystem::path> files,Host host,QWidget* parent,ImportDecoder decode)
    :QObject(parent),impl_(std::make_unique<Impl>(parent)){
    if(!host.commit||!host.settled||!host.error||!decode)throw std::invalid_argument("Incomplete focus stack host");
    setObjectName("focusStackJob");impl_->host=std::move(host);
    connect(&impl_->progress,&QProgressDialog::canceled,this,[this]{cancel();});
    connect(&impl_->watcher,&QFutureWatcher<Impl::Result>::finished,this,[this]{
        auto& state=*impl_;auto result=state.watcher.result();
        state.progress.hide();QPointer<FocusStackJob> alive=this;
        const bool cancelled=state.cancelled->load();
        auto host=state.host;
        host.settled();if(!alive)return;
        if(!cancelled){
            try{if(result.raster)host.commit(std::move(result.raster));}
            catch(const std::exception& error){result.error=QString::fromUtf8(error.what());}
            if(!alive)return;
            if(!result.error.isEmpty())host.error(result.error);
        }
        if(alive)deleteLater();
    });
    // Queue entry so the command can return and its busy-state refresh finish.
    QTimer::singleShot(0,this,[this,files=std::move(files),decode=std::move(decode)]{
        auto token=impl_->cancelled;
        impl_->watcher.setFuture(QtConcurrent::run([files,decode,token]{
            Impl::Result result;
            try{result.raster=runFocusStack(files,decode,[token]{return token->load();});}
            catch(const std::exception& error){result.error=QString::fromUtf8(error.what());}
            return result;
        }));
        if(!token->load()&&impl_->watcher.isRunning())impl_->progress.show();
    });
}
FocusStackJob::~FocusStackJob(){
    impl_->cancelled->store(true);disconnect(&impl_->watcher,nullptr,this,nullptr);
    impl_->watcher.waitForFinished();
}
void FocusStackJob::cancel(){impl_->cancelled->store(true);impl_->progress.hide();}
FocusStackJob* FocusStackJob::find(QObject* parent){if(parent)for(auto* child:parent->children())if(auto* job=dynamic_cast<FocusStackJob*>(child))return job;return nullptr;}
}
