#include "ImportActions.h"
#include "imaging/wic_codec.h"
#include "imaging/heif_codec.h"
#include "imaging/svg_raster.h"
#include "imaging/raw_develop.h"
#include "imaging/hdr_codec.h"
#include "imaging/gif_codec.h"
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QProgressBar>
#include <QProgressDialog>
#include <QPushButton>
#include <QVBoxLayout>
#include <QMessageBox>
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
    if(extension==".exr"||extension==".hdr"){
        auto deep=extension==".exr"?imaging::decodeExr(path,options):imaging::decodeRadiance(path,options);
        auto raster=rasterFromDeep(*deep);imaging::DecodedImage decoded;
        decoded.image={uint32_t(deep->width),uint32_t(deep->height),size_t(deep->width)*4,raster->rgba()};
        decoded.metadata.decoder=extension==".exr"?"OpenEXR":"Radiance RGBE";decoded.deep=std::move(deep);return decoded;
    }
    if(extension==".svg")return imaging::rasterizeSvg(path,options);
    if(imaging::isRawFileExtension(path))return imaging::decodeRaw(path,options);
    QFile file(QString::fromStdWString(path.wstring()));
    if(!file.open(QIODevice::ReadOnly))throw std::runtime_error("Cannot open image");
    const auto signature=file.read(16);file.close();
    return signature.size()>=12&&signature.mid(4,4)=="ftyp"
        ?imaging::HeifCodec::decode(path,options):imaging::WicCodec::decode(path,options);
}
bool prepareHdrImports(QWidget* parent,const std::vector<std::filesystem::path>& files,PreparedImages& prepared){
    std::vector<std::filesystem::path> hdr;for(const auto& path:files){auto ext=QString::fromStdWString(path.extension().wstring()).toLower();if(ext==".exr"||ext==".hdr")hdr.push_back(path);}if(hdr.empty())return true;
    struct Result {PreparedImages images;QString error;};
    QProgressDialog progress("Reading HDR images…","Cancel",0,0,parent);progress.setProperty("studioChrome",true);progress.setObjectName("hdrImportPreflight");progress.setWindowModality(Qt::ApplicationModal);progress.setMinimumDuration(0);progress.setAutoClose(false);progress.setAutoReset(false);
    auto cancelled=std::make_shared<std::atomic_bool>(false);QFutureWatcher<Result> watcher;
    QObject::connect(&progress,&QProgressDialog::canceled,&progress,[cancelled]{cancelled->store(true);});
    QObject::connect(&watcher,&QFutureWatcher<Result>::finished,&progress,&QDialog::accept);
    watcher.setFuture(QtConcurrent::run([hdr,cancelled]{Result result;uint64_t pixels=0,bytes=0;try{for(const auto& path:hdr){imaging::ImportOptions options;options.remainingPixels=100000000-pixels;options.maxWorkingBytes=1200000000-bytes;options.cancelled=[cancelled]{return cancelled->load();};auto image=std::make_shared<const imaging::DecodedImage>(decodeImportImage(path,options));pixels+=uint64_t(image->image.width)*image->image.height;bytes+=image->deep->samples.size()*2+image->image.pixels.size();result.images.emplace(path,std::move(image));}}catch(const std::exception& e){result.error=QString::fromUtf8(e.what());}return result;}));
    progress.exec();cancelled->store(cancelled->load()||watcher.isRunning());watcher.waitForFinished();
    if(cancelled->load())return false;auto result=watcher.result();if(!result.error.isEmpty()){QMessageBox message(QMessageBox::Critical,"Import HDR Image",result.error,QMessageBox::Ok,parent);message.setProperty("studioChrome",true);message.exec();return false;}prepared=std::move(result.images);return true;
}
bool prepareCameraRawImport(QWidget* parent,const std::optional<std::filesystem::path>& path,
                            const std::map<std::filesystem::path,imaging::RawAdjustments>& settings,PreparedImages& prepared){
    if(!path)return true;
    struct Result {std::shared_ptr<const imaging::DecodedImage> image;QString error;};
    QProgressDialog progress("Developing RAW image…","Cancel",0,0,parent);
    progress.setProperty("studioChrome",true);progress.setWindowModality(Qt::ApplicationModal);
    progress.setMinimumDuration(0);progress.setAutoClose(false);progress.setAutoReset(false);
    auto cancelled=std::make_shared<std::atomic_bool>(false);QFutureWatcher<Result> watcher;
    QObject::connect(&progress,&QProgressDialog::canceled,&progress,[cancelled]{cancelled->store(true);});
    QObject::connect(&watcher,&QFutureWatcher<Result>::finished,&progress,&QDialog::accept);
    auto entry=settings.find(*path);auto adjustments=entry==settings.end()?imaging::RawAdjustments{}:entry->second;
    watcher.setFuture(QtConcurrent::run([path=*path,adjustments,cancelled]{Result result;try{
        imaging::ImportOptions options;options.raw=adjustments;options.cancelled=[cancelled]{return cancelled->load();};
        result.image=std::make_shared<const imaging::DecodedImage>(imaging::decodeRaw(path,options));
    }catch(const std::exception& error){result.error=QString::fromUtf8(error.what());}return result;}));
    progress.exec();cancelled->store(cancelled->load()||watcher.isRunning());watcher.waitForFinished();
    if(cancelled->load())return false;
    auto result=watcher.result();if(!result.error.isEmpty()){QMessageBox message(QMessageBox::Critical,"Import RAW Image",result.error,QMessageBox::Ok,parent);message.setProperty("studioChrome",true);message.exec();return false;}
    prepared[*path]=std::move(result.image);return true;
}
bool isPdfImport(const std::filesystem::path& path) {
    return QString::fromStdWString(path.extension().wstring()).compare(".pdf", Qt::CaseInsensitive) == 0;
}

namespace {
uint64_t deepBytes(const imaging::DecodedImage& image) {
    return image.deep ? uint64_t(image.deep->samples.size()) * sizeof(uint16_t) : 0;
}

uint64_t remainingAfterPreparedImages(uint64_t budget, const PreparedImages& images) {
    for (const auto& [path, image] : images) {
        if (!image) continue;
        const auto bytes = image->image.pixels.size() + deepBytes(*image);
        budget -= std::min<uint64_t>(budget, bytes);
    }
    return budget;
}

class PdfImportProgress final : public QDialog {
public:
    PdfImportProgress(QWidget* parent, std::shared_ptr<std::atomic_bool> cancelled)
        : QDialog(parent), cancelled_(std::move(cancelled)) {
        setProperty("studioChrome", true);
        setObjectName("pdfImportPreflight");
        setWindowTitle("Import PDF");
        setWindowModality(Qt::ApplicationModal);
        auto* layout = new QVBoxLayout(this);
        label_ = new QLabel("Reading PDF pages…", this);
        layout->addWidget(label_);
        auto* bar = new QProgressBar(this);
        bar->setRange(0, 0);
        layout->addWidget(bar);
        buttons_ = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
        layout->addWidget(buttons_);
        connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    }

    void reject() override {
        cancelled_->store(true);
        label_->setText("Cancelling PDF import…");
        buttons_->setEnabled(false);
    }

private:
    std::shared_ptr<std::atomic_bool> cancelled_;
    QLabel* label_{};
    QDialogButtonBox* buttons_{};
};
}

bool preparePdfImports(QWidget* parent, const std::vector<std::filesystem::path>& files,
                       const std::optional<Document>& document, PreparedPdfs& prepared,
                       const PreparedImages& images,
                       const PdfRasterizer& rasterize) {
    std::vector<std::filesystem::path> pdfs;
    for (const auto& path : files)
        if (isPdfImport(path)) pdfs.push_back(path);
    if (pdfs.empty()) return true;

    const double dpi = document ? document->resolution : 72.;
    size_t remainingLayers = 10000;
    imaging::ImportOptions options;
    options.maxWorkingBytes = remainingAfterPreparedImages(options.maxWorkingBytes, images);
    if (document) {
        remainingLayers -= document->layers.size();
        for (const auto& layer : document->layers) {
            auto charge=[&](const std::shared_ptr<const Raster>& raster){if(!raster)return;const auto pixels=uint64_t(raster->width)*raster->height;options.remainingPixels-=std::min(options.remainingPixels,pixels);};
            if(layer.animation){for(const auto& frame:layer.animation->frames)charge(frame.raster);}else charge(layer.raster);
        }
    }

    auto cancelled = std::make_shared<std::atomic_bool>(false);
    PdfImportProgress progress(parent, cancelled);
    options.cancelled = [cancelled] { return cancelled->load(); };
    QFutureWatcher<PreparedPdfs> watcher;
    QObject::connect(&watcher, &QFutureWatcher<PreparedPdfs>::finished, &progress, &QDialog::accept);
    watcher.setFuture(QtConcurrent::run(
        [pdfs = std::move(pdfs), dpi, options, remainingLayers, rasterize]() mutable {
            PreparedPdfs result;
            for (const auto& path : pdfs) {
                if (options.cancelled()) break;
                auto [entry, inserted] = result.try_emplace(path);
                if (!inserted) continue;
                try {
                    auto pages = rasterize(path, dpi, options);
                    if (pages.size() > remainingLayers)
                        throw std::runtime_error("PDF pages exceed the remaining project layer limit");
                    remainingLayers -= pages.size();
                    for (const auto& page : pages) {
                        options.remainingPixels -= uint64_t(page->width) * page->height;
                        options.maxWorkingBytes -= page->retainedBytes();
                    }
                    entry->second = {dpi, std::move(pages)};
                } catch (const std::exception& error) {
                    entry->second.error = QString::fromUtf8(error.what());
                }
            }
            return result;
        }));
    if (progress.exec() != QDialog::Accepted || cancelled->load()) {
        cancelled->store(true);
        return false;
    }
    prepared = watcher.result();
    return true;
}

namespace {
void appendPdfPages(ImportResult& result, const PreparedPdf& pdf, const std::filesystem::path& path,
                    std::optional<Point> point, imaging::ImportOptions options) {
    const auto& current = result.after.document;
    if (pdf.dpi != (current ? current->resolution : 72.))
        throw std::runtime_error("The destination resolution changed during PDF import");
    if (pdf.pages.empty() || pdf.pages.size() > 10000 - (current ? current->layers.size() : 0))
        throw std::runtime_error("PDF pages exceed the remaining project layer limit");

    // Build every layer before publishing any of them to the batch.
    const auto& first = pdf.pages.front();
    const int width = current ? current->width : first->width;
    const int height = current ? current->height : first->height;
    const Point center = point.value_or(Point{width / 2., height / 2.});
    std::string parentId;
    if (current) {
        const auto active = std::find_if(current->layers.begin(), current->layers.end(),
            [&](const Layer& layer) { return layer.id == result.after.active; });
        if (active != current->layers.end()) parentId = active->group ? active->id : active->parentId;
    }
    const auto name = QFileInfo(QString::fromStdWString(path.wstring())).completeBaseName().toStdString();
    std::vector<Layer> layers;
    layers.reserve(pdf.pages.size());
    for (size_t index = pdf.pages.size(); index > 0; --index) {
        imaging::checkCancelled(options);
        const auto& raster = pdf.pages[index - 1];
        imaging::checkedBytes(raster->width, raster->height, 4, options);
        options.remainingPixels -= uint64_t(raster->width) * raster->height;
        const double scale = std::min({1., double(width) / raster->width, double(height) / raster->height});
        const double placedWidth = std::max(1., raster->width * scale);
        const double placedHeight = std::max(1., raster->height * scale);
        Layer layer;
        layer.id = newId();
        layer.name = pdf.pages.size() == 1 ? name : "Page " + std::to_string(index);
        layer.parentId = parentId;
        layer.raster = raster;
        layer.transform = {std::floor(center.x - placedWidth / 2.), std::floor(center.y - placedHeight / 2.),
                           placedWidth, placedHeight};
        if (!layer.transform.valid()) throw std::runtime_error("Invalid PDF page placement");
        layers.push_back(std::move(layer));
    }
    auto active = layers.back().id;
    if (!parentId.empty()) result.expandedGroups.push_back(parentId);
    if (!current) {
        Document document;
        document.id = newId();
        document.width = width;
        document.height = height;
        result.after.document = std::move(document);
    }
    auto& destination = result.after.document->layers;
    destination.reserve(destination.size() + layers.size());
    for (auto& layer : layers) destination.push_back(std::move(layer));
    result.after.active = std::move(active);
    result.imported += pdf.pages.size();
    result.importedPdf = true;
}
}

ImportResult prepareImportBatch(ImportState state,const ImportBatch& batch,const ImportDecoder& decode,imaging::ImportOptions options){
    if(!decode)throw std::invalid_argument("Image decoder is required");
    if(state.document)validateDocument(*state.document);
    if(batch.point&&(!std::isfinite(batch.point->x)||!std::isfinite(batch.point->y)))throw std::invalid_argument("Invalid image drop point");
    ImportResult result;result.before=state;result.after=std::move(state);
    const auto point=result.before.document?batch.point:std::nullopt;
    const auto budget=std::min<uint64_t>(options.remainingPixels,100000000);
    const bool hasPdf = std::any_of(batch.files.begin(), batch.files.end(), isPdfImport);
    auto workingBytes = options.maxWorkingBytes;
    if (hasPdf) {
        workingBytes = remainingAfterPreparedImages(workingBytes, batch.prepared);
        for (const auto& [path, pdf] : batch.pdfs)
            for (const auto& page : pdf.pages)
                workingBytes -= std::min<uint64_t>(workingBytes, page->retainedBytes());
    }
    const auto fallback=options.cmykFallback;
    auto cancelled=[&]{return options.cancelled&&options.cancelled();};
    for(const auto& path:batch.files){
        if(cancelled()){result.cancelled=true;break;}
        try{
            uint64_t used=0;
            if(result.after.document)for(const auto& layer:result.after.document->layers){
                auto count=[&](const std::shared_ptr<const Raster>& raster){if(!raster)return;const auto pixels=uint64_t(raster->width)*raster->height;if(pixels>budget||used>budget-pixels)throw std::runtime_error("This project exceeds the 100-megapixel import budget");used+=pixels;};
                if(layer.animation){for(const auto& frame:layer.animation->frames)count(frame.raster);}else count(layer.raster);
            }
            options.remainingPixels=budget-used;
            if (isPdfImport(path)) {
                const auto prepared = batch.pdfs.find(path);
                if (prepared != batch.pdfs.end()) {
                    if (!prepared->second.error.isEmpty()) {
                        result.errors.append(QString::fromStdWString(path.filename().wstring())
                                             + ": " + prepared->second.error);
                        continue;
                    }
                    appendPdfPages(result, prepared->second, path, point, options);
                } else {
                    const double dpi = result.after.document ? result.after.document->resolution : 72.;
                    auto pdfOptions = options;
                    pdfOptions.maxWorkingBytes = workingBytes;
                    PreparedPdf pdf{dpi, imaging::rasterizePdf(path, dpi, pdfOptions)};
                    appendPdfPages(result, pdf, path, point, options);
                    for (const auto& page : pdf.pages) workingBytes -= page->retainedBytes();
                }
                continue;
            }
            const auto adjustments=batch.rawAdjustments.find(path);
            options.raw=adjustments==batch.rawAdjustments.end()?imaging::RawAdjustments{}:adjustments->second;
            const auto sourceProfile=batch.cmykProfiles.find(path);options.cmykFallback=sourceProfile==batch.cmykProfiles.end()?fallback:sourceProfile->second;
            const auto prepared=batch.prepared.find(path);
            const bool needsDecode = prepared == batch.prepared.end();
            auto decodeOptions = options;
            decodeOptions.maxWorkingBytes = workingBytes;
            // A shared immutable preflight result avoids decoding the HDR file twice.
            std::optional<imaging::GifAnimation> gif;
            std::shared_ptr<const imaging::DecodedImage> owned;
            if(needsDecode && QString::fromStdWString(path.extension().wstring()).compare(".gif",Qt::CaseInsensitive)==0){
                gif=imaging::decodeGif(path,decodeOptions);
                if(gif->frames.empty())throw std::runtime_error("GIF has no frames");
                auto first=std::make_shared<imaging::DecodedImage>();first->image=std::move(gif->frames.front().image);first->metadata.decoder="GIF";owned=std::move(first);
            }else owned=needsDecode?std::make_shared<const imaging::DecodedImage>(decode(path,decodeOptions)):prepared->second;
            if(!owned)throw std::runtime_error("Prepared image is missing");
            const auto& decoded=*owned;imaging::checkCancelled(options);
            imaging::checkedBytes(decoded.image.width,decoded.image.height,4,needsDecode?decodeOptions:options);imaging::validate(decoded.image);
            uint64_t retained = 0;
            if (hasPdf) {
                const auto columns = (decoded.image.width + Raster::tileSide - 1) / Raster::tileSide;
                const auto rows = (decoded.image.height + Raster::tileSide - 1) / Raster::tileSide;
                retained = uint64_t(columns) * rows * sizeof(Raster::Tile);
                if (needsDecode) retained += deepBytes(decoded);
                // Prepared buffers are already charged; fresh RGBA pixels coexist with the new tiles.
                if (retained + (needsDecode ? decoded.image.pixels.size() : 0) > workingBytes)
                    throw std::runtime_error("Image exceeds the remaining import allocation budget");
            }
            auto raster=Raster::fromRgba(int(decoded.image.width),int(decoded.image.height),decoded.image.pixels.data(),decoded.image.stride);
            std::optional<GifAnimation> animation;
            if(gif){
                GifAnimation frames;frames.frames.reserve(gif->frames.size());
                const auto pixels=uint64_t(decoded.image.width)*decoded.image.height;
                if(pixels && gif->frames.size()>options.remainingPixels/pixels)throw std::runtime_error("GIF frames exceed the remaining project pixel budget");
                uint64_t tileBytes=uint64_t((decoded.image.width+Raster::tileSide-1)/Raster::tileSide)*((decoded.image.height+Raster::tileSide-1)/Raster::tileSide)*sizeof(Raster::Tile);
                if(tileBytes && gif->frames.size()>workingBytes/tileBytes)throw std::runtime_error("GIF frames exceed the import allocation budget");
                const auto tiles=tileBytes*gif->frames.size();const auto bytes=uint64_t(decoded.image.width)*decoded.image.height*4;
                if(bytes && gif->frames.size()>(workingBytes-tiles)/bytes)throw std::runtime_error("GIF frames exceed the import allocation budget");
                retained=tiles;
                for(size_t i=0;i<gif->frames.size();++i){
                    imaging::checkCancelled(options);
                    auto& frame=gif->frames[i];
                    auto frameRaster=raster;
                    if(i){imaging::validate(frame.image);if(frame.image.width!=decoded.image.width||frame.image.height!=decoded.image.height)throw std::runtime_error("GIF frame dimensions differ");frameRaster=Raster::fromRgba(int(frame.image.width),int(frame.image.height),frame.image.pixels.data(),frame.image.stride,options.cancelled);}
                    frames.frames.push_back({std::move(frameRaster),int(frame.delayCentiseconds)*10});
                }
                animation=std::move(frames);
            }
            if(!result.after.document){Document document;document.cmykProfile=decoded.metadata.cmykProfile;document.id=newId();document.width=raster->width;document.height=raster->height;result.after.document=std::move(document);}
            auto& document=*result.after.document;
            const Point center=point.value_or(Point{document.width/2.,document.height/2.});
            Layer layer;layer.id=newId();layer.name=QFileInfo(QString::fromStdWString(path.wstring())).completeBaseName().toUtf8().toStdString();layer.raster=std::move(raster);layer.animation=std::move(animation);
            if(decoded.deep){if(!decoded.deep->valid()||decoded.deep->width!=layer.raster->width||decoded.deep->height!=layer.raster->height)throw std::runtime_error("HDR import grid is invalid");layer.deep=decoded.deep;}
            layer.transform={std::floor(center.x-layer.raster->width/2.),std::floor(center.y-layer.raster->height/2.),double(layer.raster->width),double(layer.raster->height)};
            for(const auto& active:document.layers)if(active.id==result.after.active){layer.parentId=active.group?active.id:active.parentId;break;}
            if(!layer.parentId.empty())result.expandedGroups.push_back(layer.parentId);
            result.after.active=layer.id;if(batch.openCameraRaw&&*batch.openCameraRaw==path)result.cameraRawLayer=layer.id;
            if(decoded.deep&&imaging::isRawFileExtension(path))result.importedDeepRaw=true;
            document.layers.push_back(std::move(layer));++result.imported;
            workingBytes -= retained;
        }catch(const std::exception& error){
            if(cancelled()){result.cancelled=true;break;}
            result.errors.append(QString::fromStdWString(path.filename().wstring())+": "+QString::fromUtf8(error.what()));
        }
    }
    if(cancelled())result.cancelled=true;
    if(result.cancelled){result.after=result.before;result.imported=0;result.cameraRawLayer.clear();result.expandedGroups.clear();result.errors.clear();}
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
    connect(&impl_->watcher, &QFutureWatcher<ImportResult>::finished, this,
            [this] { finish(impl_->watcher.result()); });
}
void ImportQueue::finish(ImportResult result) {
    auto& p = *impl_;
    if (!p.running) return;
    p.finishing = true;
    auto request = std::move(*p.running);
    p.running.reset();
    if (p.cancellation->load()) {
        result.cancelled = true;
        result.after = result.before;
        result.imported = 0;
        result.cameraRawLayer.clear();
        result.errors.clear();
    }
    if (request.target) {
        if (result.changed()) {
            try { p.host.commit(request.target, result); }
            catch (const std::exception& error) {
                result.imported = 0;
                result.cameraRawLayer.clear();
                result.after = result.before;
                result.errors.append(QString::fromUtf8(error.what()));
            }
        }
        p.host.busy(request.target, false);
        p.host.completed(request.target, result);
    }
    p.cancellation.reset();
    p.finishing = false;
    QTimer::singleShot(0, this, [this] { drain(); });
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
        const bool preparedPdfOnly = std::all_of(batch.files.begin(), batch.files.end(),
            [&](const auto& file) { return batch.pdfs.contains(file); });
        auto prepare = [state=std::move(*before),batch=std::move(batch),decoder=std::move(decoder),cancelled]() mutable {
            imaging::ImportOptions options;options.cancelled=[cancelled]{return cancelled->load();};
            try{return prepareImportBatch(std::move(state),batch,decoder,options);}
            catch(const std::exception& error){ImportResult result;result.errors.append(QString::fromUtf8(error.what()));return result;}
        };
        if (preparedPdfOnly) {
            // Rasterization and cancellation already finished before UI edits and tab creation.
            finish(prepare());
        } else {
            p.host.busy(p.running->target, true);
            p.watcher.setFuture(QtConcurrent::run(std::move(prepare)));
        }
        return;
    }
}
}
