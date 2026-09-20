#include "SubjectDialog.h"
#include "onnx_subject_provider.h"
#include <QtConcurrent/QtConcurrentRun>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
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
    std::shared_ptr<const GrayMask> base_;
    QComboBox* quality_{};QWidget* advanced_{};QSpinBox* refine_{};QSpinBox* contrast_{};QSpinBox* shift_{};
    Preview* preview_{};QLabel* status_{};QProgressBar* progress_{};QPushButton* apply_{};
    QFutureWatcher<Result> watcher_;QTimer debounce_;Stage stage_{Stage::Idle},pending_{Stage::Idle};
    std::shared_ptr<std::atomic<bool>> cancelled_;bool closing_{};
    std::optional<GrayMask> committed_;
    QSpinBox* control(QGridLayout* layout,int row,const QString& text,int low,int high,int value,const QString& suffix){
        auto* label=new QLabel(text,advanced_);auto* slider=new QSlider(Qt::Horizontal,advanced_);slider->setRange(low,high);slider->setValue(value);slider->setAccessibleName(text);
        auto* spin=new QSpinBox(advanced_);spin->setRange(low,high);spin->setValue(value);spin->setSuffix(suffix);spin->setAccessibleName(text);label->setBuddy(spin);
        layout->addWidget(label,row,0);layout->addWidget(slider,row,1);layout->addWidget(spin,row,2);
        connect(slider,&QSlider::valueChanged,spin,&QSpinBox::setValue);connect(spin,&QSpinBox::valueChanged,slider,&QSlider::setValue);
        connect(spin,&QSpinBox::valueChanged,this,[this]{queuePreview();});return spin;
    }
    MatteSettings settings()const{return {quality_->currentIndex()==1,double(refine_->value()),double(contrast_->value()),double(shift_->value())};}
    void queuePreview(){
        if(closing_||!base_||stage_==Stage::Apply)return;
        if(cancelled_&&stage_==Stage::Preview)cancelled_->store(true);
        pending_=Stage::Preview;debounce_.start(100);
    }
    void launch(Stage next){
        if(closing_)return;
        if(stage_!=Stage::Idle){pending_=next;if(cancelled_&&stage_!=Stage::Infer)cancelled_->store(true);return;}
        if(next!=Stage::Infer&&!base_)return;
        stage_=next;pending_=Stage::Idle;cancelled_=std::make_shared<std::atomic<bool>>(false);
        auto cancellation=cancelled_;auto source=source_;auto existing=existing_;auto base=base_;auto model=model_;auto parameters=settings();
        progress_->show();status_->setText(next==Stage::Infer?tr("Finding foreground…"):next==Stage::Apply?tr("Applying mask…"):tr("Updating preview…"));
        apply_->setEnabled(next==Stage::Preview);quality_->setEnabled(next!=Stage::Infer&&next!=Stage::Apply);advanced_->setEnabled(next!=Stage::Infer&&next!=Stage::Apply);
        watcher_.setFuture(QtConcurrent::run([source,existing,base,model,parameters,cancellation,next]{
            Result out;ImportOptions options;options.cancelled=[cancellation]{return cancellation->load();};
            try{
                if(next==Stage::Infer){OnnxSubjectProvider provider(model);out.mask=provider.infer(*source,options);}
                else{auto mask=refineSubjectMask(*base,*source,parameters,next==Stage::Preview,existing.get(),options);checkCancelled(options);if(next==Stage::Preview)out.preview=applySubjectMask(*source,mask);else out.mask=std::move(mask);}
            }catch(const std::exception& error){out.cancelled=cancellation->load();out.error=QString::fromUtf8(error.what());}
            out.cancelled=out.cancelled||cancellation->load();return out;
        }));
    }
    void finished(){
        auto result=watcher_.result();const auto completed=stage_;stage_=Stage::Idle;progress_->hide();
        if(closing_)return;
        if(!result.cancelled&&!result.error.isEmpty()){
            qWarning("Background removal: %s",qPrintable(result.error));status_->setText(result.error.contains("No foreground")?tr("No foreground subject was detected. Try an image with a more distinct subject."):tr("Background removal could not be completed. Try a smaller image or reopen the app."));
            apply_->setEnabled(bool(base_));quality_->setEnabled(bool(base_));advanced_->setEnabled(bool(base_));
        }else if(!result.cancelled){
            if(completed==Stage::Infer&&result.mask){base_=std::make_shared<const GrayMask>(std::move(*result.mask));pending_=Stage::Preview;}
            else if(completed==Stage::Preview&&result.preview){preview_->setImage(*result.preview);status_->clear();apply_->setEnabled(true);}
            else if(completed==Stage::Apply&&result.mask){committed_=std::move(result.mask);accept();return;}
        }
        if(pending_!=Stage::Idle&&!debounce_.isActive()){const auto next=pending_;pending_=Stage::Idle;launch(next);}
    }
public:
    SubjectDialog(QWidget* parent,const RgbaImage& source,const GrayMask* existing,const std::filesystem::path& model):QDialog(parent),source_(std::make_shared<const RgbaImage>(source)),existing_(existing?std::make_shared<const GrayMask>(*existing):nullptr),model_(model){
        setWindowTitle(tr("Remove Background"));setModal(true);resize(760,690);auto* layout=new QVBoxLayout(this);
        preview_=new Preview(this);preview_->setImage(source);layout->addWidget(preview_,1);
        auto* qualityLayout=new QHBoxLayout;auto* qualityLabel=new QLabel(tr("Quality"),this);quality_=new QComboBox(this);quality_->setObjectName("backgroundQuality");quality_->addItems({tr("Basic"),tr("Advanced")});quality_->setAccessibleName(tr("Quality"));qualityLabel->setBuddy(quality_);qualityLayout->addWidget(qualityLabel);qualityLayout->addWidget(quality_);qualityLayout->addStretch();layout->addLayout(qualityLayout);
        advanced_=new QWidget(this);auto* grid=new QGridLayout(advanced_);grid->setContentsMargins(0,0,0,0);refine_=control(grid,0,tr("Refine"),0,40,12,tr(" px"));contrast_=control(grid,1,tr("Contrast"),0,100,25,tr(" %"));shift_=control(grid,2,tr("Shift Edge"),-10,10,0,tr(" px"));advanced_->hide();layout->addWidget(advanced_);
        status_=new QLabel(this);status_->setWordWrap(true);layout->addWidget(status_);progress_=new QProgressBar(this);progress_->setRange(0,0);progress_->setTextVisible(false);layout->addWidget(progress_);
        auto* buttons=new QDialogButtonBox(QDialogButtonBox::Apply|QDialogButtonBox::Cancel,this);apply_=buttons->button(QDialogButtonBox::Apply);apply_->setObjectName("applySubjectMask");apply_->setEnabled(false);layout->addWidget(buttons);
        connect(buttons,&QDialogButtonBox::rejected,this,&SubjectDialog::reject);connect(apply_,&QPushButton::clicked,this,[this]{debounce_.stop();apply_->setEnabled(false);quality_->setEnabled(false);advanced_->setEnabled(false);pending_=Stage::Apply;launch(Stage::Apply);});
        connect(quality_,&QComboBox::currentIndexChanged,this,[this](int index){advanced_->setVisible(index==1);queuePreview();});
        debounce_.setSingleShot(true);connect(&debounce_,&QTimer::timeout,this,[this]{if(pending_!=Stage::Idle)launch(pending_);});
        connect(&watcher_,&QFutureWatcher<Result>::finished,this,[this]{finished();});QTimer::singleShot(0,this,[this]{launch(Stage::Infer);});
    }
    void reject()override{closing_=true;debounce_.stop();if(cancelled_)cancelled_->store(true);QDialog::reject();}
    ~SubjectDialog()override{closing_=true;if(cancelled_)cancelled_->store(true);watcher_.waitForFinished();}
    std::optional<GrayMask> takeCommitted(){return std::move(committed_);}
};
}
std::optional<GrayMask> showSubjectDialog(QWidget* parent,const RgbaImage& source,const GrayMask* existing,const std::filesystem::path& path){
    validate(source);if(existing){validate(*existing);if(existing->width!=source.width||existing->height!=source.height)throw std::runtime_error("Existing mask dimensions differ");}SubjectDialog dialog(parent,source,existing,path);if(dialog.exec()!=QDialog::Accepted)return std::nullopt;return dialog.takeCommitted();
}
}
