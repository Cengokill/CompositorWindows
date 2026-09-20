#include "MainWindow.h"
#include "graphics/PixelAlgorithms.h"
#include <QFutureWatcher>
#include <QtConcurrent>
#include <QProgressDialog>
#include <cmath>

namespace compositor {
void MainWindow::runWand(Point point,Qt::KeyboardModifiers modifiers,std::optional<editing::SelectionMode> modeOverride){
    auto*p=current();if(!p||!p->document||point.x<0||point.y<0||point.x>=p->document->width||point.y>=p->document->height)return;
    const auto before=*p->document;auto sample=before;sample.selection.reset();
    if(!wandAllLayers_){sample.layers.clear();if(const auto*l=active();l&&!l->group&&l->raster){auto layer=*l;layer.mask.reset();layer.maskSourceId.clear();layer.parentId.clear();layer.adjustmentJson.clear();layer.visible=true;layer.opacity=1;layer.blend=Blend::Normal;sample.layers.push_back(std::move(layer));}}
    const auto mode=modeOverride.value_or(editing::selectionMode(modifiers.testFlag(Qt::ShiftModifier),modifiers.testFlag(Qt::AltModifier),selectionMode_));
    const auto tolerance=wandTolerance_,radius=wandSampleRadius_;const auto contiguous=wandContiguous_,antialiased=selectionAntialias_;
    struct Result{std::optional<editing::SelectionOutline> outline;std::string error;};
    QProgressDialog progress("Selecting similar pixels…","Cancel",0,0,this);progress.setWindowModality(Qt::WindowModal);progress.setMinimumDuration(100);progress.setAutoClose(false);
    QFutureWatcher<Result> watcher;connect(&watcher,&QFutureWatcher<Result>::finished,&progress,&QProgressDialog::accept);
    auto future=QtConcurrent::run([sample,point,tolerance,radius,contiguous,antialiased]{Result result;try{
        auto rendered=SoftwareRenderer().render(sample,0,0,sample.width,sample.height);auto bytes=rendered->rgba();
        GrayRaster mask{sample.width,sample.height,std::vector<uint8_t>(size_t(sample.width)*sample.height)};
        auto count=graphics::wandMask({bytes,uint32_t(sample.width),uint32_t(sample.height),size_t(sample.width)*4},size_t(std::floor(point.x)),size_t(std::floor(point.y)),size_t(radius),tolerance,contiguous,{mask.pixels,uint32_t(mask.width),uint32_t(mask.height),size_t(mask.width)});
        if(count)result.outline=editing::SelectionOutline::fromCoverage(mask,antialiased);
    }catch(const std::exception&e){result.error=e.what();}return result;});watcher.setFuture(future);progress.exec();bool cancelled=progress.wasCanceled();future.waitForFinished();if(cancelled||current()!=p||!p->document||*p->document!=before)return;auto result=future.result();if(!result.error.empty())throw std::runtime_error(result.error);
    edit("Magic Wand",[&](Document&d){if(!result.outline){if(mode==editing::SelectionMode::Replace)d.selection.reset();return;}std::optional<editing::SelectionOutline> original;if(d.selection){if(d.selection->outline)original=*d.selection->outline;else if(d.selection->coverage)original=editing::SelectionOutline::fromCoverage(*d.selection->coverage);}auto combined=mode==editing::SelectionMode::Replace?result.outline:editing::applySelection(original,*result.outline,mode,d.width,d.height,antialiased);d.selection=editing::rasterSelection(combined,d.width,d.height);});
}
}
