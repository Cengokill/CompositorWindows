#include "SubjectDialog.h"
#include "onnx_subject_provider.h"
#include "../ui/PropertyControls.h"
#include <QtConcurrent/QtConcurrentRun>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QAbstractSpinBox>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QLabel>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <atomic>
#include <algorithm>
#include <cmath>

namespace compositor::imaging {
namespace {
class Preview final:public QWidget {
    QImage image_;
public:
    explicit Preview(QWidget* parent=nullptr):QWidget(parent){setMinimumSize(480,320);setAccessibleName("Background removal preview");}
    void setImage(const RgbaImage& rgba){
        QImage wrapped(rgba.pixels.data(),int(rgba.width),int(rgba.height),qsizetype(rgba.stride),QImage::Format_RGBA8888_Premultiplied);
        image_=wrapped.scaled(1200,900,Qt::KeepAspectRatio,Qt::SmoothTransformation);update();
    }
    void paintEvent(QPaintEvent*)override{
        QPainter p(this);for(int y=0;y<height();y+=12)for(int x=0;x<width();x+=12)p.fillRect(x,y,12,12,(x/12+y/12)%2?QColor(205,205,205):QColor(240,240,240));
        if(!image_.isNull()){auto size=image_.size().scaled(this->size(),Qt::KeepAspectRatio);QRect target(QPoint((width()-size.width())/2,(height()-size.height())/2),size);p.drawImage(target,image_);}
    }
};
enum class Stage{Idle,Infer,Preview,Apply};
struct Result {std::optional<GrayMask> mask;std::optional<RgbaImage> preview;QString error;bool cancelled{};};
class SubjectDialog final:public QDialog {
    const std::shared_ptr<const RgbaImage> source_;
    const std::shared_ptr<const GrayMask> existing_;
    const std::filesystem::path model_;
    const std::function<void(const MatteSettings&)> onApply_;
    SubjectDialogCallbacks callbacks_;
    bool nonmodal_{};
    std::shared_ptr<const GrayMask> base_;
    QComboBox* quality_{};QWidget* advanced_{};QAbstractSpinBox* refine_{};QAbstractSpinBox* contrast_{};QAbstractSpinBox* shift_{};
    Preview* preview_{};QLabel* status_{};QProgressBar* progress_{};QPushButton* apply_{};
    QPushButton* cancel_{};QCheckBox* previewEnabled_{};
    std::shared_ptr<const RgbaImage> completedPreview_;
    QFutureWatcher<Result> watcher_;QTimer debounce_;Stage stage_{Stage::Idle},pending_{Stage::Idle};
    std::shared_ptr<std::atomic<bool>> cancelled_;bool closing_{};
    std::optional<GrayMask> committed_;
    bool previewReady_{};
    QAbstractSpinBox* control(QGridLayout* layout,int row,const QString& text,int low,int high,int value,const QString& suffix){
        auto* label=new QLabel(text,advanced_);auto* slider=new ui::TrackSlider(Qt::Horizontal,advanced_);slider->setRange(low,high);slider->setValue(value);slider->setAccessibleName(text);
        QAbstractSpinBox* spin;
        if(nonmodal_){auto* field=new ui::PropertyNumber(advanced_);field->setDecimals(0);field->setRange(low,high);field->setSingleStep(1);field->setValue(value);field->setSuffix(suffix);spin=field;
            connect(slider,&QSlider::valueChanged,field,&QDoubleSpinBox::setValue);connect(field,&QDoubleSpinBox::valueChanged,this,[this,slider](double v){slider->setValue(int(v));queuePreview();});
        }else{auto* field=new QSpinBox(advanced_);field->setRange(low,high);field->setValue(value);field->setSuffix(suffix);spin=field;
            connect(slider,&QSlider::valueChanged,field,&QSpinBox::setValue);connect(field,&QSpinBox::valueChanged,this,[this,slider](int v){slider->setValue(v);queuePreview();});
        }
        spin->setAccessibleName(text);label->setBuddy(spin);
        layout->addWidget(label,row,0);layout->addWidget(slider,row,1);layout->addWidget(spin,row,2);
        return spin;
    }
    MatteSettings settings()const{return {quality_->currentIndex()==1,refine_->property("value").toDouble(),contrast_->property("value").toDouble(),shift_->property("value").toDouble()};}
    void publish(){if(callbacks_.preview)callbacks_.preview(previewEnabled_&&previewEnabled_->isChecked()?completedPreview_:nullptr);}
    void retire(){if(nonmodal_&&closing_&&!watcher_.isRunning())deleteLater();}
    void queuePreview(){
        if(closing_||!base_||stage_==Stage::Apply)return;
        if(nonmodal_){previewReady_=false;apply_->setEnabled(false);}
        if(cancelled_&&stage_==Stage::Preview)cancelled_->store(true);
        pending_=Stage::Preview;debounce_.start(100);
    }
    void launch(Stage next){
        if(closing_)return;
        if(stage_!=Stage::Idle){pending_=next;if(cancelled_&&stage_!=Stage::Infer)cancelled_->store(true);return;}
        if(next!=Stage::Infer&&!base_)return;
        if(next==Stage::Apply&&!previewReady_)return;
        auto existing=existing_;
        try{if(next==Stage::Apply){if(callbacks_.existingAtApply)existing=callbacks_.existingAtApply();if(onApply_)onApply_(settings());}}
        catch(const std::exception& error){status_->setText(QString::fromUtf8(error.what()));apply_->setEnabled(previewReady_);quality_->setEnabled(true);advanced_->setEnabled(true);cancel_->setEnabled(true);return;}
        stage_=next;pending_=Stage::Idle;cancelled_=std::make_shared<std::atomic<bool>>(false);
        if(next==Stage::Apply){cancel_->setEnabled(false);previewEnabled_->setEnabled(false);if(callbacks_.committingChanged)callbacks_.committingChanged(true);}
        auto cancellation=cancelled_;auto source=source_;auto base=base_;auto model=model_;auto parameters=settings();const bool standalone=!nonmodal_;
        auto processPreview=callbacks_.processPreview;auto processMask=callbacks_.processMask;
        progress_->show();status_->setText(next==Stage::Infer?tr("Finding foreground…"):next==Stage::Apply?tr("Applying mask…"):tr("Updating preview…"));
        apply_->setEnabled(false);quality_->setEnabled(next!=Stage::Infer&&next!=Stage::Apply);advanced_->setEnabled(next!=Stage::Infer&&next!=Stage::Apply);
        watcher_.setFuture(QtConcurrent::run([source,existing,base,model,parameters,cancellation,next,standalone,processPreview,processMask]{
            Result out;ImportOptions options;options.cancelled=[cancellation]{return cancellation->load();};
            try{
                if(next==Stage::Infer){OnnxSubjectProvider provider(model);out.mask=provider.infer(*source,options);}
                else{auto mask=refineSubjectMask(*base,*source,parameters,next==Stage::Preview,(next==Stage::Apply||standalone)?existing.get():nullptr,options);checkCancelled(options);if(next==Stage::Preview){out.preview=applySubjectMask(*source,mask);if(processPreview)processPreview(*out.preview,*source,options);}else{if(processMask)processMask(mask,existing.get(),options);out.mask=std::move(mask);}}
            }catch(const std::exception& error){out.cancelled=cancellation->load();out.error=QString::fromUtf8(error.what());}
            out.cancelled=out.cancelled||cancellation->load();return out;
        }));
    }
    void finished(){
        auto result=watcher_.result();const auto completed=stage_;stage_=Stage::Idle;progress_->hide();
        if(closing_){retire();return;}
        if(!result.cancelled&&!result.error.isEmpty()){
            if(completed==Stage::Preview)previewReady_=false;
            qWarning("Background removal: %s",qPrintable(result.error));status_->setText(result.error.contains("ONNX Runtime")?tr("Background removal could not load its runtime. Repair or reinstall Compositor."):result.error.contains("No foreground")?tr("No foreground subject was detected. Try an image with a more distinct subject."):tr("Background removal could not be completed. Try a smaller image or reopen the app."));
            apply_->setEnabled(previewReady_);quality_->setEnabled(bool(base_));advanced_->setEnabled(bool(base_));
            if(completed==Stage::Apply){cancel_->setEnabled(true);previewEnabled_->setEnabled(true);if(callbacks_.committingChanged)callbacks_.committingChanged(false);}
        }else if(!result.cancelled){
            if(completed==Stage::Infer&&result.mask){base_=std::make_shared<const GrayMask>(std::move(*result.mask));pending_=Stage::Preview;}
            else if(completed==Stage::Preview&&result.preview){completedPreview_=std::make_shared<const RgbaImage>(std::move(*result.preview));preview_->setImage(*completedPreview_);publish();previewReady_=pending_==Stage::Idle;status_->clear();apply_->setEnabled(previewReady_);}
            else if(completed==Stage::Apply&&result.mask){committed_=std::move(result.mask);accept();return;}
        }
        if(pending_!=Stage::Idle&&!debounce_.isActive()){const auto next=pending_;pending_=Stage::Idle;launch(next);}
    }
public:
    SubjectDialog(QWidget* parent,const RgbaImage& source,const GrayMask* existing,const std::filesystem::path& model,const SubjectDialogOptions& options,bool nonmodal=false,SubjectDialogCallbacks callbacks={}):QDialog(parent),source_(std::make_shared<const RgbaImage>(source)),existing_(existing?std::make_shared<const GrayMask>(*existing):nullptr),model_(model),onApply_(options.onApply),callbacks_(std::move(callbacks)),nonmodal_(nonmodal){
        const auto setting=[](double value,int low,int high,int fallback){return std::isfinite(value)?int(std::lround(std::clamp(value,double(low),double(high)))):fallback;};
        setWindowTitle(tr("Remove Background"));setObjectName("subjectPanel");setModal(!nonmodal_);resize(nonmodal_?440:760,nonmodal_?380:690);if(nonmodal_)setWindowFlags(Qt::Tool|Qt::WindowTitleHint|Qt::WindowCloseButtonHint);auto* layout=new QVBoxLayout(this);
        preview_=new Preview(this);preview_->setImage(source);layout->addWidget(preview_,1);
        if(nonmodal_)preview_->hide();
        auto* qualityLayout=new QHBoxLayout;auto* qualityLabel=new QLabel(tr("Quality"),this);quality_=new QComboBox(this);quality_->setObjectName("backgroundQuality");quality_->addItems({tr("Basic"),tr("Advanced")});quality_->setCurrentIndex(options.initial.advanced?1:0);quality_->setAccessibleName(tr("Quality"));qualityLabel->setBuddy(quality_);qualityLayout->addWidget(qualityLabel);qualityLayout->addWidget(quality_);qualityLayout->addStretch();layout->addLayout(qualityLayout);
        advanced_=new QWidget(this);auto* grid=new QGridLayout(advanced_);grid->setContentsMargins(0,0,0,0);refine_=control(grid,0,tr("Refine"),0,40,setting(options.initial.refineEdges,0,40,12),tr(" px"));contrast_=control(grid,1,tr("Contrast"),0,100,setting(options.initial.contrast,0,100,25),tr(" %"));shift_=control(grid,2,tr("Shift Edge"),-10,10,setting(options.initial.shiftEdge,-10,10,0),tr(" px"));advanced_->setVisible(options.initial.advanced);layout->addWidget(advanced_);
        auto* guidance=new QLabel(tr("Works best with a distinct, opaque subject. Transparent objects, fine hair and fur may need manual mask cleanup."),this);guidance->setWordWrap(true);layout->addWidget(guidance);
        status_=new QLabel(this);status_->setWordWrap(true);layout->addWidget(status_);progress_=new QProgressBar(this);progress_->setRange(0,0);progress_->setTextVisible(false);layout->addWidget(progress_);
        previewEnabled_=new QCheckBox(tr("Preview"),this);previewEnabled_->setChecked(true);previewEnabled_->setObjectName("subjectPreviewEnabled");layout->addWidget(previewEnabled_);connect(previewEnabled_,&QCheckBox::toggled,this,[this]{publish();if(!nonmodal_)preview_->setImage(previewEnabled_->isChecked()&&completedPreview_?*completedPreview_:*source_);});
        auto* buttons=new QDialogButtonBox(QDialogButtonBox::Apply|QDialogButtonBox::Cancel,this);apply_=buttons->button(QDialogButtonBox::Apply);cancel_=buttons->button(QDialogButtonBox::Cancel);apply_->setObjectName("applySubjectMask");apply_->setEnabled(false);layout->addWidget(buttons);
        connect(buttons,&QDialogButtonBox::rejected,this,&SubjectDialog::reject);connect(apply_,&QPushButton::clicked,this,[this]{debounce_.stop();apply_->setEnabled(false);quality_->setEnabled(false);advanced_->setEnabled(false);pending_=Stage::Apply;launch(Stage::Apply);});
        connect(quality_,&QComboBox::currentIndexChanged,this,[this](int index){advanced_->setVisible(index==1);queuePreview();});
        debounce_.setSingleShot(true);connect(&debounce_,&QTimer::timeout,this,[this]{if(pending_!=Stage::Idle)launch(pending_);});
        connect(&watcher_,&QFutureWatcher<Result>::finished,this,[this]{finished();});QTimer::singleShot(0,this,[this]{launch(Stage::Infer);});
        connect(this,&QDialog::finished,this,[this](int answer){closing_=true;debounce_.stop();if(cancelled_)cancelled_->store(true);if(callbacks_.finished)callbacks_.finished(answer==QDialog::Accepted?std::move(committed_):std::nullopt);if(callbacks_.preview)callbacks_.preview({});callbacks_={};retire();});
    }
    void reject()override{if(nonmodal_&&stage_==Stage::Apply)return;closing_=true;debounce_.stop();if(cancelled_)cancelled_->store(true);QDialog::reject();}
    ~SubjectDialog()override{closing_=true;callbacks_={};if(cancelled_)cancelled_->store(true);disconnect(&watcher_,nullptr,this,nullptr);watcher_.waitForFinished();}
    std::optional<GrayMask> takeCommitted(){return std::move(committed_);}
};
}
QDialog* openSubjectDialog(QWidget* parent,const RgbaImage& source,const std::filesystem::path& path,const SubjectDialogOptions& options,SubjectDialogCallbacks callbacks){
    validate(source);auto* dialog=new SubjectDialog(parent,source,nullptr,path,options,true,std::move(callbacks));dialog->show();dialog->raise();dialog->activateWindow();return dialog;
}
std::optional<GrayMask> showSubjectDialog(QWidget* parent,const RgbaImage& source,const GrayMask* existing,const std::filesystem::path& path,const SubjectDialogOptions& options){
    validate(source);if(existing){validate(*existing);if(existing->width!=source.width||existing->height!=source.height)throw std::runtime_error("Existing mask dimensions differ");}SubjectDialog dialog(parent,source,existing,path,options);if(dialog.exec()!=QDialog::Accepted)return std::nullopt;return dialog.takeCommitted();
}
}
