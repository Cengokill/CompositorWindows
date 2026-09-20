#include "MainWindow.h"
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
#include <atomic>
#include <cmath>

namespace compositor {
namespace {
struct FilterOutput {Layer layer;QImage image;QString error;bool full{};};
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
}
void MainWindow::runFilter(int kindIndex){
    auto*p=current();auto*l=active();if(!p||!p->document||!l||!l->raster||l->group||!l->adjustmentJson.empty())return;
    const auto kind=filters::Kind(kindIndex);if(kind==filters::Kind::ContentAwareFill&&!p->document->selection)return;
    const auto before=*p->document;const auto original=*l;
    filters::Request request;request.kind=kind;request.source=l->raster;request.transform=l->transform;request.seed=QRandomGenerator::global()->generate();request.selection=filterSelection(before,original,kind);
    const QStringList names{"Gaussian Blur","Motion Blur","Add Noise","Lens Correction","Content-Aware Fill"};
    QDialog dialog(this);dialog.setWindowTitle(names[kindIndex]);QVBoxLayout layout(&dialog);QFormLayout fields;layout.addLayout(&fields);
    QLabel preview;preview.setMinimumSize(500,320);preview.setAlignment(Qt::AlignCenter);layout.addWidget(&preview,1);QLabel status;status.setWordWrap(true);layout.addWidget(&status);
    QDialogButtonBox buttons(QDialogButtonBox::Apply|QDialogButtonBox::Cancel);layout.addWidget(&buttons);auto*apply=buttons.button(QDialogButtonBox::Apply);apply->setEnabled(false);
    QTimer debounce;debounce.setSingleShot(true);debounce.setInterval(120);QFutureWatcher<FilterOutput> worker;
    auto cancel=std::make_shared<std::atomic_bool>(false);uint64_t version=0,runningVersion=0;bool pending=false,closing=false,full=false;std::optional<Layer> completed;
    auto change=[&]{++version;cancel->store(true);apply->setEnabled(false);debounce.start();};
    auto number=[&](const QString&label,double&field,double low,double high,int decimals){auto*spin=new QDoubleSpinBox;spin->setRange(low,high);spin->setDecimals(decimals);spin->setValue(field);spin->setAccessibleName(label);fields.addRow(label,spin);auto*value=&field;connect(spin,&QDoubleSpinBox::valueChanged,&dialog,[&,value](double v){*value=v;change();});};
    switch(kind){
        case filters::Kind::GaussianBlur:number("Radius",request.settings.radius,.1,250,1);break;
        case filters::Kind::MotionBlur:number("Angle",request.settings.angle,-90,90,1);number("Distance",request.settings.distance,1,2000,1);break;
        case filters::Kind::AddNoise:{number("Amount",request.settings.amount,.1,400,1);auto*gaussian=new QCheckBox("Gaussian");auto*mono=new QCheckBox("Monochromatic");fields.addRow(gaussian);fields.addRow(mono);connect(gaussian,&QCheckBox::toggled,&dialog,[&](bool v){request.settings.gaussian=v;change();});connect(mono,&QCheckBox::toggled,&dialog,[&](bool v){request.settings.monochromatic=v;change();});break;}
        case filters::Kind::LensCorrection:number("Distortion",request.settings.distortion,-100,100,1);break;
        case filters::Kind::ContentAwareFill:break;
    }
    std::function<void()> start=[&]{
        if(closing)return;if(worker.isRunning()){pending=true;return;}pending=false;runningVersion=version;
        request.retainedBlurMargin=std::max(request.retainedBlurMargin,filters::blurMargin(kind,request.settings));
        cancel=std::make_shared<std::atomic_bool>(false);auto token=cancel;auto job=request;job.preview=!full;job.limits.cancelled=[token]{return token->load();};
        status.setText(full?"Applying filter…":"Updating preview…");const bool fullJob=full;
        worker.setFuture(QtConcurrent::run([job,before,original,fullJob]{FilterOutput out;out.full=fullJob;try{auto result=filters::apply(job);out.layer=original;out.layer.raster=result.raster;out.layer.transform=result.transform;if(result.changed)out.layer.shapeJson.clear();auto doc=before;for(auto&layer:doc.layers)if(layer.id==original.id)layer=out.layer;auto raster=SoftwareRenderer().render(doc,0,0,doc.width,doc.height);auto pixels=raster->rgba();out.image=QImage(pixels.data(),doc.width,doc.height,doc.width*4,QImage::Format_RGBA8888_Premultiplied).scaled(640,420,Qt::KeepAspectRatio,Qt::SmoothTransformation);}catch(const std::exception&e){out.error=e.what();}return out;}));
    };
    connect(&debounce,&QTimer::timeout,&dialog,start);connect(&worker,&QFutureWatcher<FilterOutput>::finished,&dialog,[&]{if(closing)return;auto result=worker.result();if(pending||runningVersion!=version){start();return;}if(!result.error.isEmpty()){status.setText(result.error);apply->setEnabled(false);full=false;return;}preview.setPixmap(QPixmap::fromImage(result.image));if(result.full){completed=result.layer;dialog.accept();return;}status.setText("Preview");apply->setEnabled(true);});
    connect(apply,&QPushButton::clicked,&dialog,[&]{full=true;apply->setEnabled(false);start();});connect(&buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    start();int answer=dialog.exec();closing=true;cancel->store(true);debounce.stop();worker.waitForFinished();
    if(answer==QDialog::Accepted&&completed&&p==current()&&p->document&&*p->document==before)edit(names[kindIndex].toUtf8().constData(),[&](Document&){*active()=std::move(*completed);});
}
}
