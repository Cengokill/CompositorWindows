#include "MainWindow.h"
#include "CameraRawPanel.h"
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
#include <QPushButton>
#include <QTimer>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <QRandomGenerator>
#include <QImage>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <algorithm>
#include <atomic>
#include <cmath>

namespace compositor {
namespace {
std::optional<filters::SourceSelection> filterSelection(const Document&doc,const Layer&layer,filters::Kind kind){
    if(!doc.selection||!layer.raster)return {};
    filters::PixelRect rect{0,0,layer.raster->width,layer.raster->height};
    if(kind==filters::Kind::ContentAwareFill&&doc.selection->coverage){
        auto box=editing::coverageBounds(*doc.selection->coverage);double x0=1e100,y0=1e100,x1=-1e100,y1=-1e100;
        for(Point p:std::array<Point,4>{{{box.x,box.y},{box.x+box.width,box.y},{box.x+box.width,box.y+box.height},{box.x,box.y+box.height}}}){auto u=layer.transform.toUnit(p);x0=std::min(x0,u.x*rect.width);x1=std::max(x1,u.x*rect.width);y0=std::min(y0,u.y*rect.height);y1=std::max(y1,u.y*rect.height);}
        if(!box.empty()){rect={int(std::floor(x0)),int(std::floor(y0)),int(std::ceil(x1)-std::floor(x0)),int(std::ceil(y1)-std::floor(y0))};}
    }
    if(rect.width<1||rect.height<1||rect.width>30000||rect.height>30000||uint64_t(rect.width)*rect.height>100000000)throw std::runtime_error("Selection exceeds source allocation limits");
    auto mapping=filters::placedGrid(layer.transform,layer.raster->width,layer.raster->height,rect);
    return filters::SourceSelection{editing::mappedCoverage(doc,mapping,rect.width,rect.height),rect.x,rect.y};
}
// Filters.swift414-424 resamples owned masks over the padded transform before
// assigning the final trimmed layer transform. Keep those two grids distinct.
std::optional<Transform> paddedPlacement(const filters::Request& job){
    const int width=job.source->width,height=job.source->height;
    filters::PixelRect bounds{0,0,width,height};
    if(job.kind==filters::Kind::GaussianBlur||job.kind==filters::Kind::MotionBlur){
        const int margin=int(std::ceil(std::max(job.retainedBlurMargin,filters::blurMargin(job.kind,job.settings))));
        bounds={-margin,-margin,width+2*margin,height+2*margin};
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
    const auto padded=paddedPlacement(job);
    if(!changed||!padded||!layer.mask)return layer;
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
struct PreparedFilter {Layer layer;std::optional<filters::CameraRawScope> scope;};
PreparedFilter filteredLayer(const Layer& original,const filters::Request& job){
    auto result=filters::apply(job);auto layer=original;
    layer.raster=result.raster;layer.transform=result.transform;if(result.changed){layer.shapeJson.clear();layer.text.reset();}
    return {carryFilterMask(original,std::move(layer),job,result.changed),result.cameraRawScope};
}
struct FilterOutput {std::optional<Document> document;QImage thumbnail;QString error;bool full{};std::optional<filters::CameraRawScope> scope;};
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
    Document before_;
    Layer original_;
    filters::Request request_;
    std::function<void(const filters::Settings&)> remember_;
    std::function<void(const filters::CameraRawSettings&)> rememberRaw_;
    ui::CameraRawPanel* raw_{};
    QFutureWatcher<std::optional<std::pair<double,double>>> balance_;
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
    std::optional<Document> committed_;
    uint64_t version_{},runningVersion_{};
    bool pending_{},closed_{},committing_{},refine_{};
    static std::map<filters::Kind,QPoint>& positions(){static std::map<filters::Kind,QPoint> value;return value;}
    void retire(){if(closed_&&!worker_.isRunning())deleteLater();}
    void change(){
        if(closed_||committing_)return;
        refine_=false;
        if(raw_){request_.cameraRaw=raw_->rendered();request_.cameraRawView=raw_->previewView();}
        ++version_;if(worker_.isRunning())cancelled_->store(true);apply_->setEnabled(false);debounce_.start();
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
    double viewY(Point point)const{
        if(auto* canvas=static_cast<NativeCanvas*>(host_.canvas.data()))return canvas->viewMapping().toView(point).y;
        return point.y;
    }
    void publishPreview(){if(host_.preview)host_.preview(preview_.isChecked()?completedPreview_:nullptr);}
    void start(){
        if(closed_)return;if(worker_.isRunning()){if(committing_||runningVersion_!=version_){cancelled_->store(true);pending_=true;}return;}
        if(host_.valid&&!host_.valid()){committing_=false;cancel();return;}
        pending_=false;runningVersion_=version_;
        request_.retainedBlurMargin=std::max(request_.retainedBlurMargin,filters::blurMargin(request_.kind,request_.settings));
        cancelled_=std::make_shared<std::atomic_bool>(false);
        auto job=request_;job.preview=!committing_;job.limits.cancelled=[token=cancelled_]{return token->load();};
        if(job.kind==filters::Kind::CameraRaw&&job.preview){job.previewMaxEdge=refine_?2048:1024;job.cameraRawView.scopeLimit=refine_?0:512;}
        status_.setText(committing_?"Applying filter…":"Updating preview…");
        worker_.setFuture(QtConcurrent::run([job,before=before_,original=original_]{
            FilterOutput out;out.full=!job.preview;
            try{auto prepared=filteredLayer(original,job);auto document=before;
                for(auto& value:document.layers)if(value.id==original.id){value=std::move(prepared.layer);break;}
                validateDocument(document);if(job.kind!=filters::Kind::CameraRaw)out.thumbnail=fittedDocumentPreview(document,{640,420});out.document=std::move(document);out.scope=std::move(prepared.scope);
            }catch(const std::exception& error){out.error=QString::fromUtf8(error.what());}
            return out;
        }));
    }
    void finish(int answer){
        if(closed_)return;closed_=true;cancelled_->store(true);debounce_.stop();if(request_.kind!=filters::Kind::CameraRaw)positions()[request_.kind]=dialog_.pos();
        try{if(answer==QDialog::Accepted&&committed_&&host_.commit){
            auto found=std::find_if(committed_->layers.begin(),committed_->layers.end(),[this](const Layer& layer){return layer.id==original_.id;});
            if(found==committed_->layers.end())throw std::runtime_error("Completed filter target is missing");
            auto job=request_;job.preview=false;job.limits.cancelled={};
            auto merge=[rendered=*found,original=original_,job](const Layer& current){
                auto layer=current;layer.raster=rendered.raster;layer.transform=rendered.transform;layer.shapeJson=rendered.shapeJson;
                if(current.mask==original.mask&&current.transform==original.transform){layer.mask=rendered.mask;return layer;}
                return carryFilterMask(current,std::move(layer),job,rendered.raster!=original.raster||rendered.transform!=original.transform);
            };
            host_.commit({std::move(*committed_),original_.id,false,std::move(merge)});
        }}
        catch(const std::exception& error){if(host_.error)host_.error(QString::fromUtf8(error.what()));}
        if(host_.preview)host_.preview({});if(host_.closed)host_.closed();host_={};retire();
    }
public:
    FilterPanel(QWidget* parent,Document before,Layer original,filters::Request request,QString title,ui::EditPanelHost host,std::function<void(const filters::Settings&)> remember,std::function<void(const filters::CameraRawSettings&)> rememberRaw={})
        :EditPanelSession(parent,Kind::Filter,false,std::move(host)),before_(std::move(before)),original_(std::move(original)),request_(std::move(request)),remember_(std::move(remember)),rememberRaw_(std::move(rememberRaw)),dialog_(parent),layout_(&dialog_){
        const bool cameraRaw=request_.kind==filters::Kind::CameraRaw;
        if(cameraRaw)buttons_.setStandardButtons(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);
        dialog_.mayReject=[this]{return !committing_;};dialog_.setObjectName("filterPanel");dialog_.setWindowTitle(title);dialog_.setWindowFlags(Qt::Tool|Qt::WindowTitleHint|Qt::WindowCloseButtonHint);dialog_.setWindowModality(Qt::NonModal);
        layout_.addLayout(&fields_);thumbnail_.setObjectName("filterPreview");layout_.addWidget(&thumbnail_);thumbnail_.hide();
        preview_.setObjectName("filterPreviewEnabled");preview_.setChecked(true);layout_.addWidget(&preview_);
        status_.setWordWrap(true);status_.setStyleSheet("color: #989ba3; font-size: 11px;");layout_.addWidget(&status_);
        if(cameraRaw&&request_.selection){auto* note=new QLabel("Limited to the selection");note->setObjectName("cameraRawSelection");layout_.addWidget(note);}
        layout_.addWidget(&buttons_);apply_=buttons_.button(cameraRaw?QDialogButtonBox::Ok:QDialogButtonBox::Apply);apply_->setEnabled(false);apply_->setDefault(true);buttons_.button(QDialogButtonBox::Cancel)->setAutoDefault(false);
        if(cameraRaw){
            dialog_.setAttribute(Qt::WA_StyledBackground,true);
            dialog_.setStyleSheet("QDialog#filterPanel { background: #16181c; }");
            const QString glass="QPushButton { background: rgba(255,255,255,0.16); color: white; border: 1px solid rgba(255,255,255,0.46); border-top-color: rgba(255,255,255,0.78); border-radius: 14px; padding: 6px 16px; min-height: 22px; min-width: 72px; } QPushButton:hover { background: rgba(255,255,255,0.28); } QPushButton:default { background: rgba(255,255,255,0.30); }";
            apply_->setStyleSheet(glass);buttons_.button(QDialogButtonBox::Cancel)->setStyleSheet(glass);
        }
        layout_.setContentsMargins(18,16,18,16);layout_.setSpacing(12);fields_.setVerticalSpacing(12);
        debounce_.setSingleShot(true);debounce_.setInterval(120);
        auto number=[this](const QString& label,double& value,double low,double high,int decimals){
            auto* spin=new ui::PropertyNumber;spin->setRange(low,high);spin->setDecimals(decimals);const int stepDecimals=label=="Radius"||label=="Amount"?1:0;spin->setSingleStep(std::pow(10.,-stepDecimals));spin->setValue(value);spin->setAccessibleName(label);auto* row=new QWidget;auto* layout=new QHBoxLayout(row);layout->setContentsMargins(0,0,0,0);
            auto* slider=new ui::TrackSlider(Qt::Horizontal);slider->setRange(0,10000);slider->setAccessibleName(label+" slider");
            const bool logarithmic=label=="Radius"||label=="Distance"||label=="Amount";
            const double first=logarithmic?std::log(low):low,last=logarithmic?std::log(high):high;
            auto position=[first,last,logarithmic](double v){return int(std::lround(((logarithmic?std::log(v):v)-first)/(last-first)*10000));};
            slider->setValue(position(spin->value()));layout->addWidget(slider,1);layout->addWidget(spin);fields_.addRow(label,row);
            connect(slider,&QSlider::valueChanged,&dialog_,[spin,first,last,logarithmic,stepDecimals](int v){const double normalized=first+(last-first)*v/10000.;const double factor=std::pow(10.,stepDecimals);spin->setValue(std::round((logarithmic?std::exp(normalized):normalized)*factor)/factor);});
            connect(spin,&QDoubleSpinBox::valueChanged,&dialog_,[slider,position](double v){QSignalBlocker block(slider);slider->setValue(position(v));});
            connect(spin,&QDoubleSpinBox::valueChanged,&dialog_,[this,field=&value](double next){*field=next;change();});
        };
        switch(request_.kind){
        case filters::Kind::GaussianBlur:number("Radius",request_.settings.radius,.1,250,1);break;
        case filters::Kind::MotionBlur:number("Angle",request_.settings.angle,-90,90,1);number("Distance",request_.settings.distance,1,2000,1);break;
        case filters::Kind::AddNoise:{number("Amount",request_.settings.amount,.1,400,1);auto* gaussian=new QCheckBox("Gaussian");auto* mono=new QCheckBox("Monochromatic");gaussian->setChecked(request_.settings.gaussian);mono->setChecked(request_.settings.monochromatic);fields_.addRow(gaussian);fields_.addRow(mono);connect(gaussian,&QCheckBox::toggled,&dialog_,[this](bool value){request_.settings.gaussian=value;change();});connect(mono,&QCheckBox::toggled,&dialog_,[this](bool value){request_.settings.monochromatic=value;change();});break;}
        case filters::Kind::LensCorrection:number("Distortion",request_.settings.distortion,-100,100,1);break;
        case filters::Kind::ContentAwareFill:break;
        case filters::Kind::Vignette:number("Strength",request_.settings.vignette,0,100,1);number("Midpoint",request_.settings.vignetteMidpoint,0,1,2);break;
        case filters::Kind::Bloom:number("Strength",request_.settings.bloom,0,100,1);number("Threshold",request_.settings.bloomThreshold,0,1,2);break;
        case filters::Kind::TonalContrast:number("Strength",request_.settings.tonal,-100,100,1);break;
        case filters::Kind::Dither:break;
        case filters::Kind::Scanlines:number("Strength",request_.settings.scanline,0,100,1);number("Glow",request_.settings.scanlineGlow,0,100,1);break;
        case filters::Kind::CameraRaw:break;
        }
        if(cameraRaw){
            layout_.removeItem(&fields_);raw_=new ui::CameraRawPanel(request_.cameraRaw.normalized(),&dialog_);raw_->edited=[this]{change();};
            raw_->autoRequested=[this]{if(!raw_||!original_.raster||committing_)return;raw_->selectAuto();change();auto bytes=original_.raster->rgba();const int width=original_.raster->width,height=original_.raster->height;balance_.setFuture(QtConcurrent::run([bytes=std::move(bytes),width,height]{return filters::CameraRawSettings::autoBalance(bytes.data(),width,height,width*4);}));};
            layout_.insertWidget(0,raw_,1);
            connect(&balance_,&QFutureWatcher<std::optional<std::pair<double,double>>>::finished,&dialog_,[this]{if(closed_||committing_||!raw_||!raw_->whiteBalanceIsAuto())return;raw_->applyAuto(balance_.result());});
        }
        connect(&debounce_,&QTimer::timeout,&dialog_,[this]{start();});
        connect(&preview_,&QCheckBox::toggled,&dialog_,[this]{publishPreview();});
        connect(&worker_,&QFutureWatcher<FilterOutput>::finished,&dialog_,[this]{
            if(closed_){retire();return;}auto result=worker_.result();
            if(pending_||runningVersion_!=version_){start();return;}
            if(host_.valid&&!host_.valid()){committing_=false;cancel();return;}
            if(!result.error.isEmpty()){
                status_.setText(result.error);apply_->setEnabled(false);
                if(result.full){if(host_.error)host_.error(result.error);committing_=false;dialog_.reject();}
                return;
            }
            if(result.full){committed_=std::move(result.document);dialog_.accept();return;}
            completedPreview_=std::make_shared<const Document>(std::move(*result.document));if(raw_&&result.scope)raw_->setScope(*result.scope);publishPreview();if(!result.thumbnail.isNull())thumbnail_.setPixmap(QPixmap::fromImage(result.thumbnail));status_.setText("Preview");apply_->setEnabled(true);
            if(request_.kind==filters::Kind::CameraRaw&&!refine_){refine_=true;start();}
        });
        connect(apply_,&QPushButton::clicked,&dialog_,[this]{
            if(raw_){
                auto rendered=raw_->rendered();if(rememberRaw_)rememberRaw_(rendered);if(rendered.isIdentity()){dialog_.reject();return;}
                request_.cameraRaw=rendered;request_.cameraRawView={};
            }else if(request_.kind==filters::Kind::LensCorrection&&request_.settings.distortion==0){dialog_.reject();return;}
            if(host_.valid&&!host_.valid()){committing_=false;cancel();return;}
            if(!raw_&&remember_)remember_(request_.settings.normalized());committing_=true;apply_->setEnabled(false);buttons_.button(QDialogButtonBox::Cancel)->setEnabled(false);
            for(auto* control:dialog_.findChildren<QDoubleSpinBox*>())control->setEnabled(false);
            for(auto* control:dialog_.findChildren<QCheckBox*>())control->setEnabled(false);
            for(auto* control:dialog_.findChildren<QSlider*>())control->setEnabled(false);
            preview_.setEnabled(false);debounce_.stop();publishPreview();start();
        });
        connect(&buttons_,&QDialogButtonBox::rejected,&dialog_,&QDialog::reject);
        connect(&dialog_,&QDialog::finished,this,[this](int answer){finish(answer);});
        if(cameraRaw){auto* dock=new DockFollow(&dialog_,parent->window());parent->window()->installEventFilter(dock);dialog_.setFixedWidth(440);start();dialog_.show();dock->place();}
        else{dialog_.resize(430,220);if(auto found=positions().find(request_.kind);found!=positions().end())dialog_.move(found->second);start();dialog_.show();}
        dialog_.raise();dialog_.activateWindow();
        if(auto* first=dialog_.findChild<QDoubleSpinBox*>())first->setFocus(Qt::ActiveWindowFocusReason);
        else preview_.setFocus(Qt::ActiveWindowFocusReason);
    }
    ~FilterPanel()override{cancelled_->store(true);disconnect(&worker_,nullptr,&dialog_,nullptr);disconnect(&balance_,nullptr,&dialog_,nullptr);worker_.waitForFinished();balance_.waitForFinished();}
    QDialog* panel()const override{return const_cast<FilterDialog*>(&dialog_);}
    bool committing()const override{return committing_;}
    void cancel()override{if(!closed_&&!committing_)dialog_.reject();}
    bool samplePress(Point point,double,Qt::KeyboardModifiers)override{
        if(!raw_||closed_||committing_||raw_->tool()==ui::CameraRawPanel::Tool::None)return false;
        dragKind_=raw_->tool();
        if(dragKind_==ui::CameraRawPanel::Tool::Guide){double x=0,y=0;if(guidePoint(point,x,y))raw_->beginGuide(x,y);}
        else if(dragKind_==ui::CameraRawPanel::Tool::Curve||dragKind_==ui::CameraRawPanel::Tool::Mixer)raw_->beginTarget(samplePixel(point,true),viewY(point));
        else if(dragKind_==ui::CameraRawPanel::Tool::PointColor)raw_->sampleGraded(samplePixel(point,true));
        else raw_->sampleOriginal(samplePixel(point,false));
        return true;
    }
    bool sampleMove(Point point,double,Qt::KeyboardModifiers,bool finish)override{
        if(dragKind_==ui::CameraRawPanel::Tool::None)return false;
        if(dragKind_==ui::CameraRawPanel::Tool::Guide){double x=0,y=0;if(guidePoint(point,x,y))raw_->dragGuide(x,y);if(finish)raw_->endGuide();}
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
void MainWindow::runFilter(int kindIndex){
    auto* p=current();auto* layer=active();const bool vignette=kindIndex==int(filters::Kind::Vignette);
    if(editPanel_||!p||!p->document||!layer||layer->group||!layer->adjustmentJson.empty()||(!layer->raster&&!vignette))return;
    const QStringList names{"Gaussian Blur","Motion Blur","Add Noise","Lens Correction","Content-Aware Fill","Vignette","Bloom","Tonal Contrast","Dither","Scanlines","Camera Raw Filter"};
    if(kindIndex<0||kindIndex>=names.size())throw std::runtime_error("Unsupported filter kind");
    const auto kind=filters::Kind(kindIndex);if(kind==filters::Kind::ContentAwareFill&&!p->document->selection)return;
    const auto before=*p->document;const auto original=*layer;
    filters::Request request;request.settings=p->toolState.filterSettings.pixels;request.cameraRaw=p->toolState.filterSettings.cameraRaw.normalized();request.kind=kind;request.source=layer->raster?layer->raster:Raster::filled(p->document->width,p->document->height);request.transform=layer->raster?layer->transform:Transform{0,0,double(p->document->width),double(p->document->height)};request.seed=QRandomGenerator::global()->generate();request.selection=filterSelection(before,original,kind);
    if(kind==filters::Kind::Dither&&request.settings.ditherLevels<=0)request.settings.ditherLevels=4;if(kind==filters::Kind::Vignette&&request.settings.vignette==0)request.settings.vignette=40;if(kind==filters::Kind::Bloom&&request.settings.bloom==0)request.settings.bloom=30;if(kind==filters::Kind::Scanlines&&request.settings.scanline==0)request.settings.scanline=35;
    auto host=makeEditPanelHost(*p,before,names[kindIndex].toStdString());
    editPanel_=new FilterPanel(this,before,original,request,names[kindIndex],std::move(host),[p](const filters::Settings& value){p->toolState.filterSettings.pixels=value;},[p](const filters::CameraRawSettings& value){p->toolState.filterSettings.cameraRaw=value;});refresh(false,false);
}
}
