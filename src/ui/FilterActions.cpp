#include "MainWindow.h"
#include "DocumentPreview.h"
#include "EditPanelSession.h"
#include "PropertyControls.h"
#include "graphics/MaskSampling.h"
#include "graphics/Downsample.h"
#include <map>
#include "filters/PixelFilters.h"
#include "editing/Selection.h"
#include <QDialog>
#include <QDialogButtonBox>
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
#include <atomic>
#include <cmath>

namespace compositor {
namespace {
std::optional<filters::SourceSelection> filterSelection(const Document&doc,const Layer&layer,filters::Kind kind){
    if(!doc.selection)return {};
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
Layer filteredLayer(const Layer& original,const filters::Request& job){
    auto result=filters::apply(job);auto layer=original;
    layer.raster=result.raster;layer.transform=result.transform;if(result.changed)layer.shapeJson.clear();
    return carryFilterMask(original,std::move(layer),job,result.changed);
}
struct FilterOutput {std::optional<Document> document;QImage thumbnail;QString error;bool full{};};
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
    bool pending_{},closed_{},committing_{};
    static std::map<filters::Kind,QPoint>& positions(){static std::map<filters::Kind,QPoint> value;return value;}
    void retire(){if(closed_&&!worker_.isRunning())deleteLater();}
    void change(){
        if(closed_||committing_)return;++version_;apply_->setEnabled(false);debounce_.start();
    }
    void publishPreview(){if(host_.preview)host_.preview(preview_.isChecked()?completedPreview_:nullptr);}
    void start(){
        if(closed_)return;if(worker_.isRunning()){pending_=true;return;}
        if(host_.valid&&!host_.valid()){committing_=false;cancel();return;}
        pending_=false;runningVersion_=version_;
        request_.retainedBlurMargin=std::max(request_.retainedBlurMargin,filters::blurMargin(request_.kind,request_.settings));
        cancelled_=std::make_shared<std::atomic_bool>(false);
        auto job=request_;job.preview=!committing_;job.limits.cancelled=[token=cancelled_]{return token->load();};
        status_.setText(committing_?"Applying filter…":"Updating preview…");
        worker_.setFuture(QtConcurrent::run([job,before=before_,original=original_]{
            FilterOutput out;out.full=!job.preview;
            try{auto layer=filteredLayer(original,job);auto document=before;
                for(auto& value:document.layers)if(value.id==original.id){value=std::move(layer);break;}
                validateDocument(document);out.thumbnail=fittedDocumentPreview(document,{640,420});out.document=std::move(document);
            }catch(const std::exception& error){out.error=QString::fromUtf8(error.what());}
            return out;
        }));
    }
    void finish(int answer){
        if(closed_)return;closed_=true;cancelled_->store(true);debounce_.stop();positions()[request_.kind]=dialog_.pos();
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
    FilterPanel(QWidget* parent,Document before,Layer original,filters::Request request,QString title,ui::EditPanelHost host,std::function<void(const filters::Settings&)> remember)
        :EditPanelSession(parent,Kind::Filter,false,std::move(host)),before_(std::move(before)),original_(std::move(original)),request_(std::move(request)),remember_(std::move(remember)),dialog_(parent),layout_(&dialog_){
        dialog_.mayReject=[this]{return !committing_;};dialog_.setObjectName("filterPanel");dialog_.setWindowTitle(title);dialog_.setWindowFlags(Qt::Tool|Qt::WindowTitleHint|Qt::WindowCloseButtonHint);dialog_.setWindowModality(Qt::NonModal);
        layout_.addLayout(&fields_);thumbnail_.setObjectName("filterPreview");layout_.addWidget(&thumbnail_);thumbnail_.hide();
        preview_.setObjectName("filterPreviewEnabled");preview_.setChecked(true);layout_.addWidget(&preview_);
        status_.setWordWrap(true);layout_.addWidget(&status_);layout_.addWidget(&buttons_);apply_=buttons_.button(QDialogButtonBox::Apply);apply_->setEnabled(false);
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
            completedPreview_=std::make_shared<const Document>(std::move(*result.document));publishPreview();thumbnail_.setPixmap(QPixmap::fromImage(result.thumbnail));status_.setText("Preview");apply_->setEnabled(true);
        });
        connect(apply_,&QPushButton::clicked,&dialog_,[this]{
            if(request_.kind==filters::Kind::LensCorrection&&request_.settings.distortion==0){dialog_.reject();return;}
            if(host_.valid&&!host_.valid()){committing_=false;cancel();return;}
            if(remember_)remember_(request_.settings.normalized());committing_=true;apply_->setEnabled(false);buttons_.button(QDialogButtonBox::Cancel)->setEnabled(false);
            for(auto* control:dialog_.findChildren<QDoubleSpinBox*>())control->setEnabled(false);
            for(auto* control:dialog_.findChildren<QCheckBox*>())control->setEnabled(false);
            for(auto* control:dialog_.findChildren<QSlider*>())control->setEnabled(false);
            preview_.setEnabled(false);debounce_.stop();publishPreview();start();
        });
        connect(&buttons_,&QDialogButtonBox::rejected,&dialog_,&QDialog::reject);
        connect(&dialog_,&QDialog::finished,this,[this](int answer){finish(answer);});
        dialog_.resize(430,220);if(auto found=positions().find(request_.kind);found!=positions().end())dialog_.move(found->second);start();dialog_.show();dialog_.raise();dialog_.activateWindow();
        if(auto* first=dialog_.findChild<QDoubleSpinBox*>())first->setFocus(Qt::ActiveWindowFocusReason);
        else preview_.setFocus(Qt::ActiveWindowFocusReason);
    }
    ~FilterPanel()override{cancelled_->store(true);disconnect(&worker_,nullptr,&dialog_,nullptr);worker_.waitForFinished();}
    QDialog* panel()const override{return const_cast<FilterDialog*>(&dialog_);}
    bool committing()const override{return committing_;}
    void cancel()override{if(!closed_&&!committing_)dialog_.reject();}
};
}
void MainWindow::runFilter(int kindIndex){
    auto* p=current();auto* layer=active();if(editPanel_||!p||!p->document||!layer||!layer->raster||layer->group||!layer->adjustmentJson.empty())return;
    const QStringList names{"Gaussian Blur","Motion Blur","Add Noise","Lens Correction","Content-Aware Fill"};
    if(kindIndex<0||kindIndex>=names.size())throw std::runtime_error("Unsupported filter kind");
    const auto kind=filters::Kind(kindIndex);if(kind==filters::Kind::ContentAwareFill&&!p->document->selection)return;
    const auto before=*p->document;const auto original=*layer;
    filters::Request request;request.settings=p->toolState.filterSettings.pixels;request.kind=kind;request.source=layer->raster;request.transform=layer->transform;request.seed=QRandomGenerator::global()->generate();request.selection=filterSelection(before,original,kind);
    auto host=makeEditPanelHost(*p,before,names[kindIndex].toStdString());
    editPanel_=new FilterPanel(this,before,original,request,names[kindIndex],std::move(host),[p](const filters::Settings& value){p->toolState.filterSettings.pixels=value;});refresh(false,false);
}
}
