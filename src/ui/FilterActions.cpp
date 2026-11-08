#include "MainWindow.h"
#include "LastFilterJob.h"
#include "CameraRawPanel.h"
#include "CropViewport.h"
#include "DocumentPreview.h"
#include "EditPanelSession.h"
#include "NativeCanvas.h"
#include "PropertyControls.h"
#include "graphics/MaskSampling.h"
#include "graphics/Downsample.h"
#include <map>
#include "filters/PixelFilters.h"
#include "editing/Selection.h"
#include <QDialog>
#include <QDialogButtonBox>
#include <QPointer>
#include <QEvent>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QTimer>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <QRandomGenerator>
#include <QImage>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QMessageBox>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <utility>

namespace compositor {
namespace {
std::optional<filters::SourceSelection> filterSelection(const Document&doc,const Layer&layer,filters::Kind kind,const std::function<bool()>& cancelled={}){
    if(!doc.selection||!layer.raster)return {};
    filters::PixelRect rect{0,0,layer.raster->width,layer.raster->height};
    if(kind==filters::Kind::ContentAwareFill&&doc.selection->coverage){
        auto box=editing::coverageBounds(*doc.selection->coverage);double x0=1e100,y0=1e100,x1=-1e100,y1=-1e100;
        for(Point p:std::array<Point,4>{{{box.x,box.y},{box.x+box.width,box.y},{box.x+box.width,box.y+box.height},{box.x,box.y+box.height}}}){auto u=layer.transform.toUnit(p);x0=std::min(x0,u.x*rect.width);x1=std::max(x1,u.x*rect.width);y0=std::min(y0,u.y*rect.height);y1=std::max(y1,u.y*rect.height);}
        if(!box.empty()){rect={int(std::floor(x0)),int(std::floor(y0)),int(std::ceil(x1)-std::floor(x0)),int(std::ceil(y1)-std::floor(y0))};}
    }
    if(rect.width<1||rect.height<1||rect.width>30000||rect.height>30000||uint64_t(rect.width)*rect.height>100000000)throw std::runtime_error("Selection exceeds source allocation limits");
    auto mapping=filters::placedGrid(layer.transform,layer.raster->width,layer.raster->height,rect);
    return filters::SourceSelection{editing::mappedCoverage(doc,mapping,rect.width,rect.height,false,editing::CoverageSampling::Linear,cancelled),rect.x,rect.y};
}
// Filters.swift414-424 resamples owned masks over the padded transform before
// assigning the final trimmed layer transform. Keep those two grids distinct.
std::optional<Transform> paddedPlacement(const filters::Request& job){
    const int width=job.source->width,height=job.source->height;
    filters::PixelRect bounds{0,0,width,height};
    if(job.kind==filters::Kind::GaussianBlur||job.kind==filters::Kind::MotionBlur||job.kind==filters::Kind::ProgressiveBlur||job.kind==filters::Kind::RgbSplit){
        bounds=filters::filterWorkingBounds(job);
    }else if(job.kind==filters::Kind::ContentAwareFill&&job.selection&&job.selection->coverage){
        const auto box=editing::coverageBounds(*job.selection->coverage);
        if(!box.empty()){
            const int x=int(box.x)+job.selection->originX,y=int(box.y)+job.selection->originY;
            const int left=std::min(0,x),top=std::min(0,y);
            bounds={left,top,std::max(width,x+int(box.width))-left,std::max(height,y+int(box.height))-top};
        }
    }
    if(bounds==filters::PixelRect{0,0,width,height})return {};
    return filters::placedGrid(job.transform,width,height,bounds);
}
Layer carryFilterMask(const Layer& original,Layer layer,const filters::Request& job,bool changed){
    layer.mask=original.mask;
    if(!changed||!layer.mask)return layer;
    auto padded=paddedPlacement(job);
    if(!padded){
        if(!layer.raster||!original.raster)return layer;
        if(layer.transform==original.transform&&layer.raster->width==original.raster->width&&layer.raster->height==original.raster->height)return layer;
        padded=layer.transform;
    }
    auto& mask=*layer.mask;
    if(job.preview){if(!mask.placement)mask.placement=original.transform;return layer;}
    if(mask.placement||!mask.raster||(mask.raster->width==1&&mask.raster->height==1))return layer;
    const auto width=layer.raster->width,height=layer.raster->height;
    const auto pixels=uint64_t(width)*height;
    if(width<1||height<1||width>30000||height>30000||pixels>100000000||pixels>job.limits.maxWorkingBytes)
        throw std::runtime_error("The expanded filter mask exceeds its allocation budget");
    auto grown=std::make_shared<GrayRaster>();grown->width=width;grown->height=height;grown->pixels.resize(size_t(pixels));
    const auto exterior=graphics::maskBackground(*mask.raster);
    graphics::DownsampleCache reduction;
    const double covered=original.transform.width/std::max(1.,padded->width)*width;
    const auto reduced=reduction.image(mask.raster,covered/mask.raster->width);
    for(int y=0;y<height;++y){
        if(job.limits.cancelled&&job.limits.cancelled())throw std::runtime_error("Filter cancelled");
        for(int x=0;x<width;++x){
            const auto point=padded->fromUnit({(x+.5)/width,(y+.5)/height});
            const auto value=graphics::sampleMask(*reduced,original.transform.toUnit(point),Transform::Sampling::High,exterior);
            grown->pixels[size_t(y)*width+x]=uint8_t(std::clamp(std::lround(value*255),0L,255L));
        }
    }
    mask.raster=std::move(grown);return layer;
}
struct PreparedFilter {
    Layer layer;
    std::optional<filters::CameraRawScope> scope;
    bool reduced{},canRefine{};
};
PreparedFilter filteredLayer(const Layer& original,const filters::Request& job){
    auto result=filters::apply(job);auto layer=original;
    layer.raster=result.raster;if(result.raster!=original.raster)layer.deep=result.deep;layer.transform=result.transform;if(result.changed){layer.shapeJson.clear();layer.text.reset();}
    return {carryFilterMask(original,std::move(layer),job,result.changed),std::move(result.cameraRawScope),
            result.previewScale<1,result.cameraRawCanRefine};
}
std::function<Layer(const Layer&)> filterMerge(const Layer& rendered,const Layer& original,const filters::Request& job){
    return [rendered,original,job](const Layer& current){
        auto layer=current;layer.raster=rendered.raster;if(rendered.raster!=current.raster)layer.deep=rendered.deep;layer.transform=rendered.transform;layer.shapeJson=rendered.shapeJson;
        if(current.mask==original.mask&&current.transform==original.transform){layer.mask=rendered.mask;return layer;}
        return carryFilterMask(current,std::move(layer),job,rendered.raster!=original.raster||rendered.transform!=original.transform);
    };
}
QStringList filterKindNames(){return {"Gaussian Blur","Motion Blur","Add Noise","Lens Correction","Content-Aware Fill","Vignette","Bloom","Tonal Contrast","Dither","Scanlines","Camera Raw Filter","Progressive Blur","RGB Split","Lens Effects"};}
filters::Request buildFilterRequest(EditorProject& project,const Document& before,const Layer& layer,filters::Kind kind){
    filters::Request request;request.settings=project.toolState.filterSettings.pixels;request.cameraRaw=project.toolState.filterSettings.cameraRaw.normalized();
    request.kind=kind;request.deepSource=layer.deep;request.source=layer.raster?layer.raster:Raster::filled(before.width,before.height);
    request.transform=layer.raster?layer.transform:Transform{0,0,double(before.width),double(before.height)};
    request.canvasWidth=before.width;request.canvasHeight=before.height;
    request.seed=QRandomGenerator::global()->generate();
    if(kind!=filters::Kind::CameraRaw)request.selection=filterSelection(before,layer,kind);
    if(kind==filters::Kind::Dither&&request.settings.ditherLevels<=0)request.settings.ditherLevels=4;
    if(kind==filters::Kind::Vignette&&request.settings.vignette==0)request.settings.vignette=40;
    if(kind==filters::Kind::Bloom&&request.settings.bloom==0)request.settings.bloom=30;
    if(kind==filters::Kind::Scanlines&&request.settings.scanline==0)request.settings.scanline=35;
    return request;
}
struct FilterOutput {
    std::optional<Document> document;
    std::array<std::shared_ptr<const Raster>,4> channels;
    QImage thumbnail;
    QString error;
    QString viewportError;
    std::optional<filters::CameraRawScope> scope;
    std::optional<CompositeViewport> viewport;
    std::optional<CompositeViewport> navigator;
    std::uint64_t retainedBytes{};
    bool full{},reduced{},canRefine{};
};
class DockFollow final:public QObject {
    QPointer<QWidget> window_;
    QDialog* dialog_;
public:
    DockFollow(QDialog* dialog,QWidget* window):QObject(dialog),window_(window),dialog_(dialog){}
    void place(){
        if(!window_||!dialog_)return;
        const QRect frame=window_->frameGeometry();
        dialog_->setFixedWidth(440);
        const int decoration=std::max(0,dialog_->frameGeometry().height()-dialog_->height());
        dialog_->setFixedHeight(std::max(1,frame.height()-decoration));
        const QRect docked=dialog_->frameGeometry();
        dialog_->move(dialog_->pos()+QPoint(frame.right()-docked.right(),frame.top()-docked.top()));
    }
    bool eventFilter(QObject* watched,QEvent* event)override{
        if(watched==window_&&(event->type()==QEvent::Move||event->type()==QEvent::Resize||event->type()==QEvent::Show||event->type()==QEvent::WindowStateChange))place();
        return QObject::eventFilter(watched,event);
    }
};
class FilterDialog final:public QDialog {
public:
    using QDialog::QDialog;std::function<bool()> mayReject;
    void reject()override{if(!mayReject||mayReject())QDialog::reject();}
};
class FilterPanel final:public ui::EditPanelSession {
    struct RawViewport {
        NativeCanvas::ViewportBounds bounds;
        Document canonical;
        std::optional<editing::Rect> crop;
        bool operator==(const RawViewport&)const=default;
    };
    Document before_;
    Layer original_;
    filters::Request request_;
    std::function<void(const filters::Settings&)> remember_;
    std::function<void(const filters::CameraRawSettings&)> rememberRaw_;
    std::function<void(filters::Kind)> rememberLast_;
    std::function<std::optional<editing::Rect>()> currentCrop_;
    ui::CameraRawPanel* raw_{};
    QFutureWatcher<std::optional<std::pair<double,double>>> balance_;
    std::shared_ptr<std::atomic_bool> balanceCancelled_;
    std::shared_ptr<std::optional<filters::SourceSelection>> rawSelection_;
    ui::CameraRawPanel::Tool dragKind_{ui::CameraRawPanel::Tool::None};
    FilterDialog dialog_;
    QVBoxLayout layout_;
    QFormLayout fields_;
    QLabel thumbnail_,status_;
    QCheckBox preview_{"Preview"};
    QDialogButtonBox buttons_{QDialogButtonBox::Apply|QDialogButtonBox::Cancel};
    QPushButton* apply_{};
    QTimer debounce_;
    QFutureWatcher<FilterOutput> worker_;
    std::shared_ptr<std::atomic_bool> cancelled_=std::make_shared<std::atomic_bool>(false);
    std::shared_ptr<const Document> completedPreview_;
    std::shared_ptr<CompositeCache> rawViewportCache_;
    std::optional<RawViewport> runningViewport_,lastViewport_;
    int pendingNavigatorEdge_{};
    std::optional<Document> committed_;
    std::array<std::shared_ptr<const Raster>,4> channels_;
    filters::CameraRawView runningRawView_;
    std::uint64_t version_{},runningVersion_{},retainedPreviewBytes_{};
    bool running_{},runningViewportOnly_{},balanceRunning_{},closed_{},committing_{},progressiveDragging_{},rgbDragging_{},splitLayers_{};
    static std::map<filters::Kind,QPoint>& positions(){static std::map<filters::Kind,QPoint> value;return value;}
    bool autoBalancePending()const{return balanceRunning_&&raw_&&raw_->whiteBalanceIsAuto();}
    void retire(){if(closed_&&!running_&&!balanceRunning_)deleteLater();}
    void beginSplitToLayers(QPushButton* split){
        if(closed_||committing_||splitLayers_||!split)return;
        const auto current=host_.currentDocument?host_.currentDocument():std::nullopt;
        if(!current)return;
        auto target=std::find_if(current->layers.begin(),current->layers.end(),[this](const Layer& layer){return layer.id==original_.id;});
        if(target==current->layers.end()||!target->raster||target->raster!=original_.raster||
           target->deep!=original_.deep||target->transform!=original_.transform){
            status_.setText("The source layer changed. Reopen RGB Split to split its channels.");
            return;
        }
        const auto pixelCount=[](const Raster& raster){return std::uint64_t(raster.width)*raster.height;};
        auto pixels=4*pixelCount(*target->raster);
        for(const auto& layer:current->layers)if(layer.raster)pixels+=pixelCount(*layer.raster);
        if(pixels>maxDocumentImagePixels){
            QMessageBox warning(QMessageBox::Warning,"Cannot Split to Layers",
                QString("The four channel layers would exceed the project's combined %1-megapixel image limit. Reduce the image size or remove other image layers before trying again.")
                    .arg(maxDocumentImagePixels/1'000'000),
                QMessageBox::Ok,&dialog_);
            warning.setProperty("studioChrome",true);warning.exec();
            return;
        }
        splitLayers_=true;
        ++version_;
        rgbDragging_=false;
        cancelled_->store(true);
        debounce_.stop();
        apply_->setEnabled(false);
        split->setEnabled(false);
        for(auto* control:dialog_.findChildren<QDoubleSpinBox*>())control->setEnabled(false);
        for(auto* control:dialog_.findChildren<QCheckBox*>())control->setEnabled(false);
        for(auto* control:dialog_.findChildren<QComboBox*>())control->setEnabled(false);
            for(auto* control:dialog_.findChildren<QSlider*>())control->setEnabled(false);
        completedPreview_.reset();
        clearGuideOverlay();
        status_.setText("Splitting channels…");
        publishPreview();
        start();
    }
    void change(){
        if(closed_||committing_||splitLayers_)return;
        ++version_;
        if(raw_){
            request_.cameraRaw=raw_->rendered();request_.cameraRawView=raw_->previewView();syncGuideOverlay();
            apply_->setEnabled(!autoBalancePending());
            // Let ordinary grades finish and keep only the latest settings.
            // Recomposition and superseded diagnostics can be cancelled.
            if(running_&&(runningViewportOnly_||runningRawView_!=request_.cameraRawView))cancelled_->store(true);
            if(!running_&&!debounce_.isActive()){status_.setText("Updating preview…");debounce_.start();}
            return;
        }
        if(running_)cancelled_->store(true);
        apply_->setEnabled(false);debounce_.start();
    }
    Pixel samplePixel(Point point,bool graded)const{
        const Raster* raster=original_.raster.get();Transform transform=original_.transform;
        if(graded&&completedPreview_)for(const auto& layer:completedPreview_->layers)if(layer.id==original_.id&&layer.raster){raster=layer.raster.get();transform=layer.transform;break;}
        if(!raster)return {};
        const auto unit=transform.toUnit(point);
        if(unit.x<0||unit.y<0||unit.x>=1||unit.y>=1)return {};
        return raster->pixel(int(std::floor(unit.x*raster->width)),int(std::floor(unit.y*raster->height)));
    }
    bool guidePoint(Point point,double& x,double& y)const{
        const auto unit=original_.transform.toUnit(point);
        if(unit.x<0||unit.y<0||unit.x>=1||unit.y>=1)return false;
        x=std::clamp(unit.x,0.,1.);y=std::clamp(1-unit.y,0.,1.);return true;
    }
    // guidePoint stores y as 1-unit.y. Drawing uses that inverse so the segment stays on the sampled pixels.
    Point guideDocument(double x,double y)const{return original_.transform.fromUnit({x,1-y});}
    void syncGuideOverlay(){
        auto* canvas=dynamic_cast<NativeCanvas*>(host_.canvas.data());if(!canvas)return;
        if(request_.kind==filters::Kind::ProgressiveBlur){
            if(request_.settings.progressiveStart&&request_.settings.progressiveEnd)canvas->setCameraRawGuides({{*request_.settings.progressiveStart,*request_.settings.progressiveEnd}});
            return;
        }
        if(request_.kind==filters::Kind::RgbSplit&&request_.settings.rgbSplitRadial){
            const Point c=request_.settings.rgbSplitCenter.value_or(original_.transform.fromUnit({.5,.5}));
            canvas->setCameraRawGuides({{{c.x-12,c.y},{c.x+12,c.y}},{{c.x,c.y-12},{c.x,c.y+12}}});
            return;
        }
        if(request_.kind==filters::Kind::RgbSplit){canvas->setCameraRawGuides({});return;}
        if(!raw_)return;
        const auto guides=raw_->guideOverlay();
        std::vector<std::pair<Point,Point>> lines;lines.reserve(guides.size());
        for(const auto& guide:guides)lines.push_back({guideDocument(guide.startX,guide.startY),guideDocument(guide.endX,guide.endY)});
        canvas->setCameraRawGuides(std::move(lines));
    }
    void clearGuideOverlay(){if(auto* canvas=dynamic_cast<NativeCanvas*>(host_.canvas.data()))canvas->setCameraRawGuides({});}
    double viewY(Point point)const{
        if(auto* canvas=static_cast<NativeCanvas*>(host_.canvas.data()))return canvas->viewMapping().toView(point).y;
        return point.y;
    }
    void publishPreview(){
        if(!host_.preview)return;
        host_.preview(preview_.isChecked()?completedPreview_:nullptr);
    }
    std::optional<RawViewport> currentRawViewport()const{
        auto* canvas=dynamic_cast<NativeCanvas*>(host_.canvas.data());
        if(!canvas||!host_.currentDocument||!preview_.isChecked())return {};
        const auto bounds=canvas->viewportBounds();
        if(bounds.valid)if(auto document=host_.currentDocument())return RawViewport{bounds,std::move(*document),currentCrop_()};
        return {};
    }
    bool deferRawViewport(){
        if(closed_||!preview_.isChecked())return false;
        if(!running_&&!committing_&&!debounce_.isActive()&&completedPreview_&&(!host_.valid||host_.valid())){
            auto viewport=currentRawViewport();
            if(viewport&&(viewport!=lastViewport_||pendingNavigatorEdge_))start(std::move(viewport));
        }
        return true;
    }
    void start(std::optional<RawViewport> viewportOnly={}){
        if(closed_||running_)return;
        if(host_.valid&&!host_.valid()){committing_=false;cancel();return;}
        running_=true;runningVersion_=version_;runningRawView_=request_.cameraRawView;
        cancelled_=std::make_shared<std::atomic_bool>(false);
        if(splitLayers_){
            worker_.setFuture(QtConcurrent::run([source=original_.raster,limits=request_.limits,token=cancelled_]() mutable {
                FilterOutput out;
                try{
                    limits.cancelled=[token]{return token->load();};
                    out.channels=filters::splitChannels(*source,limits);
                }catch(const std::exception& error){out.error=QString::fromUtf8(error.what());}
                return out;
            }));
            return;
        }
        request_.retainedBlurMargin=std::max(request_.retainedBlurMargin,filters::blurMargin(request_.kind,request_.settings));
        auto job=request_;job.preview=!committing_;job.limits.cancelled=[token=cancelled_]{return token->load();};
        const auto existingGrade=viewportOnly?completedPreview_:nullptr;
        runningViewportOnly_=bool(existingGrade);
        runningViewport_.reset();
        if(raw_){
            job.cameraRawView.scopeLimit=job.preview?512:-1;
            if(!job.preview&&completedPreview_){
                completedPreview_.reset();lastViewport_.reset();retainedPreviewBytes_=0;publishPreview();
            }
            if(job.preview)runningViewport_=viewportOnly?std::move(viewportOnly):currentRawViewport();
            job.cameraRawRetainedPreviewBytes=retainedPreviewBytes_;
        }
        if((job.kind==filters::Kind::ProgressiveBlur||job.kind==filters::Kind::RgbSplit||job.kind==filters::Kind::LensEffects)&&job.preview)job.previewMaxEdge=512;
        if(!existingGrade)status_.setText(committing_?"Applying filter…":"Updating preview…");
        const int navigatorEdge=existingGrade?std::exchange(pendingNavigatorEdge_,0):0;
        worker_.setFuture(QtConcurrent::run([job,before=before_,original=original_,selection=rawSelection_,
                                             viewport=runningViewport_,cache=rawViewportCache_,existingGrade,navigatorEdge]() mutable {
            FilterOutput out;out.full=!job.preview;
            try{
                auto compose=[&](const Document& grade){
                    if(!viewport)return;
                    auto& document=viewport->canonical;
                    const auto graded=std::find_if(grade.layers.begin(),grade.layers.end(),[&](const Layer& layer){return layer.id==original.id;});
                    const auto target=std::find_if(document.layers.begin(),document.layers.end(),[&](const Layer& layer){return layer.id==original.id;});
                    if(graded!=grade.layers.end()&&target!=document.layers.end()){
                        target->raster=graded->raster;target->deep=graded->deep;
                    }
                    const auto& bounds=viewport->bounds;
                    try{
                        if(viewport->crop)out.viewport=ui::renderCropViewport(document,*viewport->crop,bounds.x,bounds.y,bounds.width,bounds.height,bounds.units);
                        else out.viewport=cache->renderViewport(document,bounds.x,bounds.y,bounds.width,bounds.height,bounds.units,64,256,{},job.limits.cancelled);
                        if(navigatorEdge){
                            const double units=std::max(1.,std::max(document.width,document.height)/double(navigatorEdge));
                            out.navigator=CompositeCache().renderViewport(document,0,0,document.width,document.height,units,4,16,{},job.limits.cancelled);
                        }
                    }
                    catch(const std::exception& error){cache->reset();out.viewportError=QString::fromUtf8(error.what());}
                };
                if(existingGrade){compose(*existingGrade);return out;}
                if(cache)cache->reset();
                if(selection){
                    if(!*selection)*selection=filterSelection(before,original,job.kind,job.limits.cancelled);
                    job.selection=*selection;
                }
                auto prepared=filteredLayer(original,job);
                auto document=std::move(before);
                if(job.kind==filters::Kind::CameraRaw&&job.preview){
                    if(prepared.layer.raster&&prepared.layer.raster!=original.raster)out.retainedBytes=prepared.layer.raster->retainedBytes();
                    if(prepared.layer.deep&&prepared.layer.deep!=original.deep)out.retainedBytes+=prepared.layer.deep->samples.size()*sizeof(std::uint16_t);
                    out.reduced=prepared.reduced;out.canRefine=prepared.canRefine;
                }
                const auto target=std::find_if(document.layers.begin(),document.layers.end(),[&](const Layer& value){return value.id==original.id;});
                if(target!=document.layers.end()){
                    // The edit view replaces pixels only during Camera Raw preview.
                    if(job.kind==filters::Kind::CameraRaw&&job.preview){
                        target->raster=std::move(prepared.layer.raster);target->deep=std::move(prepared.layer.deep);
                    }else *target=std::move(prepared.layer);
                }
                validateDocument(document);if(job.kind!=filters::Kind::CameraRaw)out.thumbnail=fittedDocumentPreview(document,{640,420});out.document=std::move(document);out.scope=std::move(prepared.scope);
                compose(*out.document);
            }catch(const std::exception& error){if(cache)cache->reset();out.error=QString::fromUtf8(error.what());}
            return out;
        }));
    }
    void finish(int answer){
        if(closed_)return;closed_=true;cancelled_->store(true);debounce_.stop();if(request_.kind!=filters::Kind::CameraRaw)positions()[request_.kind]=dialog_.pos();
        if(balanceCancelled_)balanceCancelled_->store(true);
        try{if(answer==QDialog::Accepted&&splitLayers_&&channels_.front()&&host_.commit){
            constexpr std::array names{"Red","Green","Blue","Alpha"};
            std::vector<Layer> layers;
            layers.reserve(names.size());
            for(std::size_t i=0;i<names.size();++i){
                Layer layer;
                layer.id=newId();
                layer.name=names[i];
                layer.visible=false;
                layer.raster=std::move(channels_[i]);
                layers.push_back(std::move(layer));
            }
            host_.commit({{},original_.id,false,{},std::move(layers)});
        }else if(answer==QDialog::Accepted&&committed_&&host_.commit){
            auto found=std::find_if(committed_->layers.begin(),committed_->layers.end(),[this](const Layer& layer){return layer.id==original_.id;});
            if(found==committed_->layers.end())throw std::runtime_error("Completed filter target is missing");
            auto job=request_;job.preview=false;job.limits.cancelled={};
            auto merge=filterMerge(*found,original_,job);
            host_.commit({std::move(*committed_),original_.id,false,std::move(merge)});
            if(raw_&&rememberRaw_)rememberRaw_(request_.cameraRaw);
            if(rememberLast_)rememberLast_(request_.kind);
        }}
        catch(const std::exception& error){if(host_.error)host_.error(QString::fromUtf8(error.what()));}
        clearRawViewportProviders();
        completedPreview_.reset();rawViewportCache_.reset();clearGuideOverlay();if(host_.preview)host_.preview({});if(host_.closed)host_.closed();host_={};retire();
    }
    void clearRawViewportProviders(){
        if(raw_)if(auto* canvas=dynamic_cast<NativeCanvas*>(host_.canvas.data())){
            canvas->deferViewportComposite={};
            canvas->deferNavigatorComposite={};
        }
    }
public:
    FilterPanel(QWidget* parent,Document before,Layer original,filters::Request request,QString title,ui::EditPanelHost host,std::function<void(const filters::Settings&)> remember,std::function<void(const filters::CameraRawSettings&)> rememberRaw,std::function<void(filters::Kind)> rememberLast,std::function<std::optional<editing::Rect>()> currentCrop)
        :EditPanelSession(parent,Kind::Filter,false,std::move(host)),before_(std::move(before)),original_(std::move(original)),request_(std::move(request)),remember_(std::move(remember)),rememberRaw_(std::move(rememberRaw)),rememberLast_(std::move(rememberLast)),currentCrop_(std::move(currentCrop)),dialog_(parent),layout_(&dialog_){
        const bool cameraRaw=request_.kind==filters::Kind::CameraRaw;
        if(cameraRaw)buttons_.setStandardButtons(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);
        dialog_.mayReject=[this]{return !committing_;};dialog_.setObjectName("filterPanel");dialog_.setWindowTitle(title);dialog_.setWindowFlags(Qt::Tool|Qt::WindowTitleHint|Qt::WindowCloseButtonHint);dialog_.setWindowModality(Qt::NonModal);
        layout_.addLayout(&fields_);thumbnail_.setObjectName("filterPreview");layout_.addWidget(&thumbnail_);thumbnail_.hide();
        preview_.setObjectName("filterPreviewEnabled");preview_.setChecked(true);layout_.addWidget(&preview_);
        status_.setObjectName("filterPreviewStatus");status_.setWordWrap(true);status_.setStyleSheet("color: #989ba3; font-size: 11px;");layout_.addWidget(&status_);
        if(cameraRaw&&before_.selection){auto* note=new QLabel("Limited to the selection");note->setObjectName("cameraRawSelection");layout_.addWidget(note);}
        layout_.addWidget(&buttons_);apply_=buttons_.button(cameraRaw?QDialogButtonBox::Ok:QDialogButtonBox::Apply);apply_->setEnabled(cameraRaw);apply_->setDefault(true);buttons_.button(QDialogButtonBox::Cancel)->setAutoDefault(false);
        layout_.setContentsMargins(18,16,18,16);layout_.setSpacing(12);fields_.setVerticalSpacing(12);
        debounce_.setSingleShot(true);debounce_.setInterval(cameraRaw?16:120);
        const filters::Settings factory{};
        auto number=[this](const QString& label,double& value,double low,double high,int decimals,double fallback){
            auto* spin=new ui::PropertyNumber;spin->setRange(low,high);spin->setDecimals(decimals);const int stepDecimals=label=="Radius"||label=="Amount"||label=="From"||label=="To"||label=="Threshold"||label=="Midpoint"?decimals:0;spin->setSingleStep(std::pow(10.,-stepDecimals));spin->setValue(value);spin->setAccessibleName(label);auto* row=new QWidget;auto* layout=new QHBoxLayout(row);layout->setContentsMargins(0,0,0,0);
            auto* slider=new ui::TrackSlider(Qt::Horizontal);slider->setRange(0,10000);slider->setAccessibleName(label+" slider");
            const bool logarithmic=(label=="Radius"||label=="Distance"||label=="Amount")&&low>0;
            const double first=logarithmic?std::log(low):low,last=logarithmic?std::log(high):high;
            auto position=[first,last,logarithmic](double v){return int(std::lround(((logarithmic?std::log(v):v)-first)/(last-first)*10000));};
            slider->setValue(position(spin->value()));slider->setDefaultValue(position(fallback));layout->addWidget(slider,1);layout->addWidget(spin);fields_.addRow(label,row);
            connect(slider,&QSlider::valueChanged,&dialog_,[spin,first,last,logarithmic,stepDecimals](int v){const double normalized=first+(last-first)*v/10000.;const double factor=std::pow(10.,stepDecimals);spin->setValue(std::round((logarithmic?std::exp(normalized):normalized)*factor)/factor);});
            connect(spin,&QDoubleSpinBox::valueChanged,&dialog_,[slider,position](double v){QSignalBlocker block(slider);slider->setValue(position(v));});
            connect(spin,&QDoubleSpinBox::valueChanged,&dialog_,[this,field=&value](double next){*field=next;change();});
        };
        switch(request_.kind){
        case filters::Kind::GaussianBlur:number("Radius",request_.settings.radius,.1,250,1,factory.radius);break;
        case filters::Kind::MotionBlur:number("Angle",request_.settings.angle,-90,90,1,factory.angle);number("Distance",request_.settings.distance,1,2000,1,factory.distance);break;
        case filters::Kind::AddNoise:{number("Amount",request_.settings.amount,.1,400,1,factory.amount);auto* gaussian=new QCheckBox("Gaussian");auto* mono=new QCheckBox("Monochromatic");gaussian->setChecked(request_.settings.gaussian);mono->setChecked(request_.settings.monochromatic);fields_.addRow(gaussian);fields_.addRow(mono);connect(gaussian,&QCheckBox::toggled,&dialog_,[this](bool value){request_.settings.gaussian=value;change();});connect(mono,&QCheckBox::toggled,&dialog_,[this](bool value){request_.settings.monochromatic=value;change();});break;}
        case filters::Kind::LensCorrection:number("Distortion",request_.settings.distortion,-100,100,1,factory.distortion);break;
        case filters::Kind::LensEffects:{
            auto* mode=new QComboBox;mode->setObjectName("lensEffectMode");mode->addItems({"Anamorphic Flare","Starburst","Mist","Prism","Bokeh"});mode->setCurrentIndex(int(request_.settings.lens.effect));fields_.addRow("Effect",mode);
            auto& v=request_.settings.lens;const auto& d=factory.lens;
            std::array<std::pair<int,int>,5> rows;
            auto begin=[&](int i){rows[i].first=fields_.rowCount();};auto end=[&](int i){rows[i].second=fields_.rowCount();};
            begin(0);number("Threshold",v.threshold,50,100,1,d.threshold);number("Strength",v.flareAmount,0,100,1,d.flareAmount);number("Length",v.flareLength,1,100,1,d.flareLength);number("Blue",v.flareBlue,0,100,1,d.flareBlue);end(0);
            begin(1);number("Star strength",v.starAmount,0,100,1,d.starAmount);number("Star length",v.starLength,2,40,1,d.starLength);number("Star angle",v.starAngle,-180,180,1,d.starAngle);
            auto* points=new QComboBox;points->setObjectName("lensStarPoints");points->addItems({"4","6","8","12","14"});points->setCurrentText(QString::number(v.starPoints));fields_.addRow("Points",points);connect(points,&QComboBox::currentTextChanged,&dialog_,[this](const QString& text){request_.settings.lens.starPoints=text.toInt();change();});end(1);
            begin(2);number("Mist strength",v.mistStrength,0,100,1,d.mistStrength);number("Size",v.mistSize,0,100,1,d.mistSize);end(2);
            begin(3);number("Prism strength",v.prismAmount,0,100,1,d.prismAmount);number("Copies",v.prismCopies,1,8,0,d.prismCopies);number("Offset",v.prismOffset,0,50,1,d.prismOffset);number("Prism angle",v.prismAngle,-180,180,1,d.prismAngle);number("Blur",v.prismBlur,0,100,1,d.prismBlur);
            auto* both=new QCheckBox("Both ways");both->setChecked(v.prismBothWays);fields_.addRow(both);connect(both,&QCheckBox::toggled,&dialog_,[this](bool checked){request_.settings.lens.prismBothWays=checked;change();});end(3);
            begin(4);number("Radius",v.bokehRadius,.5,6,1,d.bokehRadius);number("Brightness",v.bokehBrightness,0,100,1,d.bokehBrightness);number("Ring",v.bokehRing,0,100,1,d.bokehRing);
            auto* shape=new QComboBox;shape->setObjectName("lensBokehShape");shape->addItems({"Round","Hexagon","Oval","Cat's Eye"});shape->setCurrentIndex(int(v.bokehShape));fields_.addRow("Shape",shape);connect(shape,&QComboBox::currentIndexChanged,&dialog_,[this](int index){request_.settings.lens.bokehShape=filters::BokehShape(index);change();});end(4);
            auto show=[this,rows](int index){for(int i=0;i<5;++i)for(int row=rows[i].first;row<rows[i].second;++row)fields_.setRowVisible(row,i==index||(i==0&&row==rows[0].first&&index==1));};show(mode->currentIndex());
            connect(mode,&QComboBox::currentIndexChanged,&dialog_,[this,show](int index){request_.settings.lens.effect=filters::LensEffect(index);show(index);dialog_.adjustSize();change();});break;
        }
        case filters::Kind::ContentAwareFill:break;
        case filters::Kind::Vignette:number("Strength",request_.settings.vignette,0,100,1,factory.vignette);number("Midpoint",request_.settings.vignetteMidpoint,0,1,2,factory.vignetteMidpoint);break;
        case filters::Kind::Bloom:number("Strength",request_.settings.bloom,0,100,1,factory.bloom);number("Threshold",request_.settings.bloomThreshold,0,1,2,factory.bloomThreshold);break;
        case filters::Kind::TonalContrast:number("Strength",request_.settings.tonal,-100,100,1,factory.tonal);break;
        case filters::Kind::Dither:break;
        case filters::Kind::Scanlines:number("Strength",request_.settings.scanline,0,100,1,factory.scanline);number("Glow",request_.settings.scanlineGlow,0,100,1,factory.scanlineGlow);break;
        case filters::Kind::CameraRaw:break;
        case filters::Kind::ProgressiveBlur:{
            number("From",request_.settings.progressiveFrom,0,250,1,factory.progressiveFrom);
            number("To",request_.settings.progressiveTo,0,250,1,factory.progressiveTo);
            auto* radial=new QCheckBox("Radial");radial->setChecked(request_.settings.progressiveRadial);fields_.addRow(radial);
            auto* invert=new QCheckBox("Invert");invert->setChecked(request_.settings.progressiveInverted);fields_.addRow(invert);
            connect(radial,&QCheckBox::toggled,&dialog_,[this](bool value){request_.settings.progressiveRadial=value;change();});
            connect(invert,&QCheckBox::toggled,&dialog_,[this](bool value){request_.settings.progressiveInverted=value;change();});
            break;
        }
        case filters::Kind::RgbSplit:{
            number("Distance",request_.settings.rgbSplitDistance,0,200,1,factory.rgbSplitDistance);
            number("Blur",request_.settings.rgbSplitBlur,0,400,1,factory.rgbSplitBlur);
            number("Angle",request_.settings.rgbSplitAngle,-180,180,1,factory.rgbSplitAngle);
            auto* radial=new QCheckBox("Radial");radial->setObjectName("rgbSplitRadial");radial->setChecked(request_.settings.rgbSplitRadial);fields_.addRow(radial);
            connect(radial,&QCheckBox::toggled,&dialog_,[this](bool value){request_.settings.rgbSplitRadial=value;syncGuideOverlay();change();});
            auto* split=new QPushButton("Split to Layers");split->setObjectName("rgbSplitToLayers");fields_.addRow(split);
            split->setToolTip("Create hidden grayscale layers from the original Red, Green, Blue and Alpha channels.");
            connect(split,&QPushButton::clicked,&dialog_,[this,split]{
                if(closed_||committing_||splitLayers_)return;
                if(original_.deep&&original_.deep->valid()&&!original_.deep->isHdr()){
                    QMessageBox confirmation(QMessageBox::Question,"Create 8-Bit Channels","Create 8-bit channel layers from this 16-bit source? The source keeps its 16-bit pixels.",QMessageBox::Ok|QMessageBox::Cancel,&dialog_);
                    confirmation.setProperty("studioChrome",true);confirmation.setDefaultButton(QMessageBox::Cancel);
                    if(confirmation.exec()!=QMessageBox::Ok||closed_)return;
                    // The OK click that closed the 16-bit prompt can also dismiss a
                    // nested warning. Continue after that dialog has fully left.
                    QTimer::singleShot(0,&dialog_,[this,split]{beginSplitToLayers(split);});
                    return;
                }
                beginSplitToLayers(split);
            });
            break;
        }
        }
        if(cameraRaw){
            request_.cameraRawNativePreview=true;
            request_.cameraRawCache=std::make_shared<filters::CameraRawPreviewCache>();
            rawViewportCache_=std::make_shared<CompositeCache>();
            if(before_.selection)rawSelection_=std::make_shared<std::optional<filters::SourceSelection>>();
            layout_.removeItem(&fields_);raw_=new ui::CameraRawPanel(request_.cameraRaw.normalized(),&dialog_);raw_->edited=[this]{change();};
            raw_->autoRequested=[this]{
                if(!raw_||!original_.raster||closed_||committing_)return;
                raw_->selectAuto();
                if(!balanceRunning_){
                    balanceRunning_=true;
                    balanceCancelled_=std::make_shared<std::atomic_bool>(false);
                    balance_.setFuture(QtConcurrent::run([source=original_.raster,token=balanceCancelled_]{
                        return filters::CameraRawSettings::autoBalance(*source,[token]{return token->load();});
                    }));
                }
                change();
            };
            layout_.insertWidget(0,raw_,1);
            connect(&balance_,&QFutureWatcher<std::optional<std::pair<double,double>>>::finished,&dialog_,[this]{
                balanceRunning_=false;
                if(closed_){retire();return;}
                if(!committing_&&raw_->whiteBalanceIsAuto())raw_->applyAuto(balance_.result());
            });
        }
        connect(&debounce_,&QTimer::timeout,&dialog_,[this]{start();});
        connect(&preview_,&QCheckBox::toggled,&dialog_,[this]{
            if(!raw_||!preview_.isChecked()){
                lastViewport_.reset();pendingNavigatorEdge_=0;publishPreview();
                if(running_&&runningViewportOnly_)cancelled_->store(true);
            }else deferRawViewport();
            if(raw_)if(auto* canvas=dynamic_cast<NativeCanvas*>(host_.canvas.data()))canvas->scheduleNavigator();
        });
        connect(&worker_,&QFutureWatcher<FilterOutput>::finished,&dialog_,[this]{
            running_=false;
            if(closed_){retire();return;}
            if(host_.valid&&!host_.valid()){committing_=false;cancel();return;}
            const bool superseded=runningVersion_!=version_;
            auto viewport=std::exchange(runningViewport_,{});
            if(superseded&&(!raw_||committing_||runningRawView_!=request_.cameraRawView)){
                if(!debounce_.isActive())start();
                return;
            }
            if(runningViewportOnly_&&cancelled_->load()){
                if(superseded)start();else deferRawViewport();
                return;
            }
            auto result=worker_.result();
            if(splitLayers_){
                if(!result.error.isEmpty()){
                    if(host_.error)host_.error(result.error);
                    dialog_.reject();
                }else{
                    channels_=std::move(result.channels);
                    dialog_.accept();
                }
                return;
            }
            if(!result.error.isEmpty()){
                if(superseded){start();return;}
                status_.setText(result.error);apply_->setEnabled(false);
                if(result.full){if(host_.error)host_.error(result.error);committing_=false;dialog_.reject();}
                return;
            }
            if(result.full){committed_=std::move(result.document);dialog_.accept();return;}
            if(!runningViewportOnly_)lastViewport_.reset();
            const auto latestViewport=viewport?currentRawViewport():std::nullopt;
            const bool current=viewport&&viewport==latestViewport;
            auto* canvas=dynamic_cast<NativeCanvas*>(host_.canvas.data());
            const bool navigatorReady=canvas&&result.navigator&&latestViewport&&viewport->canonical==latestViewport->canonical;
            if(navigatorReady)
                canvas->presentNavigator(std::move(*result.navigator));
            if(current){
                lastViewport_=std::move(viewport);
                if(canvas&&result.viewport){
                    auto& frame=*result.viewport;
                    canvas->presentComposite(std::move(frame.raster),frame.documentX,frame.documentY,frame.unitsPerPixel,false);
                }
            }
            if(runningViewportOnly_){
                if(current&&result.viewport)publishPreview();
                if(current&&!superseded&&!result.viewportError.isEmpty())status_.setText(result.viewportError);
                if(superseded)start();else deferRawViewport();
                return;
            }
            completedPreview_=std::make_shared<const Document>(std::move(*result.document));
            retainedPreviewBytes_=result.retainedBytes;
            if(raw_&&result.scope)raw_->setScope(*result.scope);
            if(preview_.isChecked()&&(!raw_||(current&&result.viewport)))publishPreview();
            else if(raw_&&host_.preview)host_.preview({});
            if(raw_&&canvas&&preview_.isChecked()&&!pendingNavigatorEdge_)canvas->scheduleNavigator();
            if(!result.thumbnail.isNull())thumbnail_.setPixmap(QPixmap::fromImage(result.thumbnail));
            if(current&&!result.viewportError.isEmpty())status_.setText(result.viewportError);
            else status_.setText(raw_&&result.reduced?"Preview (reduced to fit memory)":"Preview");
            apply_->setEnabled(!autoBalancePending());
            if(superseded||result.canRefine)start();
            else if(raw_)deferRawViewport();
        });
        connect(apply_,&QPushButton::clicked,&dialog_,[this]{
            if(splitLayers_)return;
            if(raw_){
                if(autoBalancePending())return;
                auto rendered=raw_->rendered();
                if(rendered.isIdentity()){if(rememberRaw_)rememberRaw_(rendered);dialog_.reject();return;}
                if(original_.deep&&original_.deep->valid()&&!original_.deep->isHdr()&&filters::cameraRawNeedsEightBit(rendered)){
                    QMessageBox confirmation(QMessageBox::Question,"Convert to 8-Bit","Mixer, color grading, effects, detail and optics run in 8-bit. Convert this 16-bit layer to 8-bit before applying? Cancel leaves the document unchanged.",QMessageBox::Ok|QMessageBox::Cancel,&dialog_);
                    confirmation.setProperty("studioChrome",true);confirmation.setDefaultButton(QMessageBox::Cancel);
                    if(confirmation.exec()!=QMessageBox::Ok||closed_)return;
                    request_.deepSource.reset();
                }
                request_.cameraRaw=std::move(rendered);request_.cameraRawView={};
            }else if(request_.kind==filters::Kind::LensCorrection&&request_.settings.distortion==0){dialog_.reject();return;}
            else if(request_.kind==filters::Kind::LensEffects&&request_.settings.lens.isIdentity()){dialog_.reject();return;}
            else if(request_.kind==filters::Kind::RgbSplit&&request_.settings.rgbSplitDistance==0&&request_.settings.rgbSplitBlur==0){dialog_.reject();return;}
            if(host_.valid&&!host_.valid()){committing_=false;cancel();return;}
            if(!raw_&&remember_)remember_(request_.settings.normalized());committing_=true;apply_->setEnabled(false);buttons_.button(QDialogButtonBox::Cancel)->setEnabled(false);
            for(auto* control:dialog_.findChildren<QDoubleSpinBox*>())control->setEnabled(false);
            for(auto* control:dialog_.findChildren<QCheckBox*>())control->setEnabled(false);
            for(auto* control:dialog_.findChildren<QComboBox*>())control->setEnabled(false);
            for(auto* control:dialog_.findChildren<QSlider*>())control->setEnabled(false);
            preview_.setEnabled(false);debounce_.stop();
            if(!raw_)publishPreview();
            if(running_){++version_;cancelled_->store(true);}
            start();
        });
        connect(&buttons_,&QDialogButtonBox::rejected,&dialog_,&QDialog::reject);
        connect(&dialog_,&QDialog::finished,this,[this](int answer){finish(answer);});
        syncGuideOverlay();
        if(cameraRaw)if(auto* canvas=dynamic_cast<NativeCanvas*>(host_.canvas.data())){
            canvas->deferViewportComposite=[this]{return deferRawViewport();};
            canvas->deferNavigatorComposite=[this](int edge){
                if(!preview_.isChecked())return false;
                pendingNavigatorEdge_=edge;
                return deferRawViewport();
            };
        }
        if(cameraRaw){auto* dock=new DockFollow(&dialog_,parent->window());parent->window()->installEventFilter(dock);dialog_.setFixedWidth(440);start();dialog_.show();dock->place();}
        else{dialog_.resize(430,220);if(auto found=positions().find(request_.kind);found!=positions().end())dialog_.move(found->second);start();dialog_.show();}
        dialog_.raise();dialog_.activateWindow();
        if(auto* first=dialog_.findChild<QDoubleSpinBox*>())first->setFocus(Qt::ActiveWindowFocusReason);
        else preview_.setFocus(Qt::ActiveWindowFocusReason);
    }
    ~FilterPanel()override{
        if(!closed_)clearRawViewportProviders();
        clearGuideOverlay();cancelled_->store(true);
        if(balanceCancelled_)balanceCancelled_->store(true);
        disconnect(&worker_,nullptr,&dialog_,nullptr);disconnect(&balance_,nullptr,&dialog_,nullptr);
        worker_.waitForFinished();balance_.waitForFinished();
    }
    QDialog* panel()const override{return const_cast<FilterDialog*>(&dialog_);}
    bool committing()const override{return committing_||splitLayers_;}
    void cancel()override{if(!closed_&&!committing_)dialog_.reject();}
    bool samplePress(Point point,double,Qt::KeyboardModifiers)override{
        if(request_.kind==filters::Kind::ProgressiveBlur&&!closed_&&!committing_){
            progressiveDragging_=true;request_.settings.progressiveStart=point;request_.settings.progressiveEnd=point;syncGuideOverlay();change();return true;
        }
        if(request_.kind==filters::Kind::RgbSplit&&request_.settings.rgbSplitRadial&&!closed_&&!committing_){
            rgbDragging_=true;request_.settings.rgbSplitCenter=point;syncGuideOverlay();change();return true;
        }
        if(!raw_||closed_||committing_||raw_->tool()==ui::CameraRawPanel::Tool::None)return false;
        dragKind_=raw_->tool();
        if(dragKind_==ui::CameraRawPanel::Tool::Guide){double x=0,y=0;if(guidePoint(point,x,y))raw_->beginGuide(x,y);syncGuideOverlay();}
        else if(dragKind_==ui::CameraRawPanel::Tool::Curve||dragKind_==ui::CameraRawPanel::Tool::Mixer)raw_->beginTarget(samplePixel(point,true),viewY(point));
        else if(dragKind_==ui::CameraRawPanel::Tool::PointColor)raw_->sampleGraded(samplePixel(point,true));
        else raw_->sampleOriginal(samplePixel(point,false));
        return true;
    }
    bool sampleMove(Point point,double,Qt::KeyboardModifiers,bool finish)override{
        if(progressiveDragging_){request_.settings.progressiveEnd=point;syncGuideOverlay();change();if(finish){progressiveDragging_=false;dialog_.activateWindow();}return true;}
        if(rgbDragging_){request_.settings.rgbSplitCenter=point;syncGuideOverlay();change();if(finish){rgbDragging_=false;dialog_.activateWindow();}return true;}
        if(dragKind_==ui::CameraRawPanel::Tool::None)return false;
        if(dragKind_==ui::CameraRawPanel::Tool::Guide){double x=0,y=0;if(guidePoint(point,x,y))raw_->dragGuide(x,y);if(finish)raw_->endGuide();syncGuideOverlay();}
        else if(dragKind_==ui::CameraRawPanel::Tool::Curve||dragKind_==ui::CameraRawPanel::Tool::Mixer)raw_->dragTarget(viewY(point));
        else if(dragKind_==ui::CameraRawPanel::Tool::PointColor)raw_->sampleGraded(samplePixel(point,true));
        else raw_->sampleOriginal(samplePixel(point,false));
        if(finish){dragKind_=ui::CameraRawPanel::Tool::None;raw_->releaseTool();dialog_.activateWindow();}
        return true;
    }
    void sampleHover(Point point)override{
        if(!raw_)return;const auto pixel=samplePixel(point,preview_.isChecked()&&completedPreview_);
        if(!pixel.a)raw_->setReadout({});else raw_->setReadout(std::array<int,3>{pixel.r,pixel.g,pixel.b});
    }
};
}
QString MainWindow::filterKindDisplayName(filters::Kind kind){const auto names=filterKindNames();const int index=int(kind);return index>=0&&index<names.size()?names[index]:QString{};}
bool MainWindow::canRunFilterKind(EditorProject& project,filters::Kind kind){
    if(editPanel_||project.projectBusy||project.importing||!project.document)return false;
    const auto state=commandState(&project);
    if(state.warpStroke||state.levels||state.hueSaturation||state.filterEdit||state.adjustmentEditing)return false;
    const bool vignette=kind==filters::Kind::Vignette;
    const auto found=std::find_if(project.document->layers.begin(),project.document->layers.end(),[&](const Layer& layer){return layer.id==project.active;});
    if(found==project.document->layers.end()||found->group||!found->adjustmentJson.empty()||(!found->raster&&!vignette))return false;
    if(kind==filters::Kind::ContentAwareFill&&!state.selection)return false;
    if(vignette)return state.singleSelected&&state.effectiveVisible&&!state.maskSelected&&!state.projectBusy&&!state.importing&&!state.brushStroke&&!state.pixelMove;
    return ui::canAdjustColors(state);
}
void MainWindow::repeatLastFilter(){
    auto* p=current();if(!p||!p->toolState.lastFilterKind)return;
    runFilter(int(*p->toolState.lastFilterKind),true);
}
void MainWindow::runFilter(int kindIndex,bool repeating){
    auto* p=current();auto* layer=active();const bool vignette=kindIndex==int(filters::Kind::Vignette);
    if((!repeating&&editPanel_)||!p||!p->document||!layer||layer->group||!layer->adjustmentJson.empty()||(!layer->raster&&!vignette))return;
    const auto names=filterKindNames();
    if(kindIndex<0||kindIndex>=names.size())throw std::runtime_error("Unsupported filter kind");
    const auto kind=filters::Kind(kindIndex);
    if(repeating&&!canRunFilterKind(*p,kind))return;
    if(kind==filters::Kind::ContentAwareFill&&!p->document->selection)return;
    if(layer->deep&&layer->deep->isHdr()&&!filters::supportsHdr(kind))throw std::runtime_error("Use Layer > Convert HDR to 16-Bit before this spatial filter");
    const bool convert=layer->deep&&layer->deep->valid()&&!layer->deep->isHdr()&&(kind==filters::Kind::LensCorrection||kind==filters::Kind::ContentAwareFill);
    if(convert){
        QMessageBox confirmation(QMessageBox::Question,"Convert to 8-Bit","This filter uses an 8-bit kernel. Convert this 16-bit layer to 8-bit first? This conversion can be undone.",QMessageBox::Ok|QMessageBox::Cancel,this);
        confirmation.setProperty("studioChrome",true);confirmation.setDefaultButton(QMessageBox::Cancel);
        if(confirmation.exec()!=QMessageBox::Ok)return;
    }
    const auto before=*p->document;const auto original=*layer;
    auto request=buildFilterRequest(*p,before,original,kind);
    if(convert)request.deepSource.reset();
    if(kind==filters::Kind::ProgressiveBlur&&!repeating){request.settings.progressiveStart=request.transform.fromUnit({.5,0});request.settings.progressiveEnd=request.transform.fromUnit({.5,1});}
    if(kind==filters::Kind::RgbSplit&&!repeating){request.settings.rgbSplitCenter=request.transform.fromUnit({.5,.5});request.settings.rgbSplitReach=std::max(1.,std::hypot(request.transform.width,request.transform.height)/2);}
    if(repeating){
        if(ui::LastFilterJob::find(this,p))return;
        const auto action=names[kindIndex].toStdString();
        auto job=request;job.preview=false;
        if(kind==filters::Kind::CameraRaw)job.cameraRawView.scopeLimit=-1;
        const auto selectionSource=kind==filters::Kind::CameraRaw&&before.selection?std::optional<Document>(before):std::nullopt;
        QPointer<NativeCanvas> token=p->canvas;
        ui::LastFilterJob::Host host;
        host.settled=[this,p,token]{if(!token)return;p->projectBusy=false;refresh(false,false);};
        host.error=[this](const QString& error){QMessageBox::critical(this,"Compositor",error);};
        host.commit=[this,p,token,original,job,action](Layer rendered){
            if(!token||current()!=p||!p->document)return;
            auto target=std::find_if(p->document->layers.begin(),p->document->layers.end(),[&](const Layer& layer){return layer.id==original.id;});
            if(target==p->document->layers.end())return;
            if(target->raster!=original.raster||target->deep!=original.deep||target->transform!=original.transform)return;
            auto merge=filterMerge(rendered,original,job);
            Document prepared=*p->document;
            auto& changed=prepared.layers[size_t(target-p->document->layers.begin())];
            changed=merge(*target);
            validateDocument(prepared);
            p->history.begin(action,p->document,p->active);
            try{p->document=std::move(prepared);p->history.end(p->document,p->active);}
            catch(...){if(auto snapshot=p->history.cancel()){p->document=std::move(snapshot->document);p->active=std::move(snapshot->activeLayer);}throw;}
            if(current()==p)refresh();
        };
        p->projectBusy=true;refresh(false,false);
        try{
            new ui::LastFilterJob([job,original,selectionSource](std::function<bool()> cancelled){
                auto active=job;active.limits.cancelled=std::move(cancelled);
                if(selectionSource)active.selection=filterSelection(*selectionSource,original,active.kind,active.limits.cancelled);
                return filteredLayer(original,active).layer;
            },std::move(host),this,p);
        }catch(...){p->projectBusy=false;refresh(false,false);throw;}
        return;
    }
    auto host=makeEditPanelHost(*p,before,names[kindIndex].toStdString());
    auto rememberLast=[p](filters::Kind applied){if(applied!=filters::Kind::ContentAwareFill)p->toolState.lastFilterKind=applied;};
    editPanel_=new FilterPanel(this,before,original,request,names[kindIndex],std::move(host),[p](const filters::Settings& value){p->toolState.filterSettings.pixels=value;},[p](const filters::CameraRawSettings& value){p->toolState.filterSettings.cameraRaw=value;},std::move(rememberLast),[this]{return tool_==Tool::Crop?cropDraft_:std::nullopt;});refresh(false,false);
}
}
