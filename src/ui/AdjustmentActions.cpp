#include "MainWindow.h"
#include "AdjustmentDialog.h"
#include "imaging/SubjectDialog.h"
#include <QApplication>
#include <QMenuBar>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <cmath>

namespace compositor {
void MainWindow::setupAdjustmentActions(){
    auto*menu=menuBar()->addMenu("&Adjustments");
    auto*live=menu->addMenu("New Adjustment Layer");
    for(const QString kind:{"Hue/Saturation","Levels","Curves","Exposure","Gradient Map","Grain"}){
        action(menu,kind+"…",{},[this,kind]{adjust(kind,false);});
        action(live,kind+"…",{},[this,kind]{adjust(kind,true);});
    }
    action(menu,"Edit Adjustment Layer…",{},[this]{if(active()&&!active()->adjustmentJson.empty())adjust({},true,true);});
    auto*filters=menuBar()->addMenu("&Filters");
    int filterIndex=0;
    for(const QString name:{"Gaussian Blur","Motion Blur","Add Noise","Lens Correction","Content-Aware Fill"}){int index=filterIndex++;action(filters,name+"…",{},[this,index]{runFilter(index);});}
    action(filters,"Remove Background…",{},[this]{removeBackground();});
}
void MainWindow::adjust(const QString&kind,bool live,bool existing){
    auto*p=current();auto*l=active();
    if(!p||!p->document||(!live&&(!l||!l->raster||l->group||!l->adjustmentJson.empty())))return;
    const auto before=*p->document;const auto id=p->active;
    AdjustmentDialogOptions options;
    if(!live){options.initialAdjustmentJson=p->toolState.filterSettings.beginAdjustment(kind,foreground_,background_);options.onApply=[p](const std::string& json){p->toolState.filterSettings.rememberAdjustment(json);};}
    auto result=showAdjustmentDialog(this,before,id,kind,live,existing,options);
    if(!result)return;
    if(p!=current()||!p->document||*p->document!=before){QMessageBox::information(this,"Adjustment","The project changed while the adjustment was open. Reopen the adjustment to apply it.");return;}
    edit(existing?"Edit Adjustment":live?"Add Adjustment Layer":"Adjust Image",[&](Document&d){d=std::move(result->document);p->active=result->active;});
}
void MainWindow::removeBackground(){
    auto*p=current();auto*l=active();if(!p||!p->document||!l||!l->raster||l->group||!l->adjustmentJson.empty())return;
    auto original=*l;auto selection=p->document->selection;
    imaging::RgbaImage image{uint32_t(l->raster->width),uint32_t(l->raster->height),size_t(l->raster->width)*4,l->raster->rgba()};
    std::optional<imaging::GrayMask> old;
    if(l->mask&&!l->mask->placement&&l->mask->raster&&l->mask->raster->width==int(image.width)&&l->mask->raster->height==int(image.height))old=imaging::GrayMask{image.width,image.height,image.width,l->mask->raster->pixels};
    auto model=QDir(QApplication::applicationDirPath()).filePath("models/birefnet-lite.onnx");
    if(!QFileInfo::exists(model))model=QStringLiteral(COMPOSITOR_SOURCE_ROOT)+"/dependencies/imaging/model/birefnet-lite.onnx";
    imaging::SubjectDialogOptions options;options.initial=p->toolState.filterSettings.background;
    options.onApply=[p](const imaging::MatteSettings& settings){p->toolState.filterSettings.background=settings;};
    auto result=imaging::showSubjectDialog(this,image,old?&*old:nullptr,std::filesystem::path(model.toStdWString()),options);
    if(!result)return;
    if(p!=current()||!active()||active()->id!=original.id||active()->raster!=original.raster||active()->transform!=original.transform)return;
    auto mask=std::make_shared<GrayRaster>();mask->width=int(result->width);mask->height=int(result->height);mask->pixels=std::move(result->pixels);
    if(selection)for(int y=0;y<mask->height;++y)for(int x=0;x<mask->width;++x){
        auto point=original.transform.fromUnit({(x+.5)/mask->width,(y+.5)/mask->height});
        unsigned c=selection->coverage?selection->coverage->pixel(int(std::floor(point.x)),int(std::floor(point.y))):0;
        auto i=size_t(y)*mask->width+x;unsigned base=old?old->pixels[size_t(y)*old->stride+x]:255;
        mask->pixels[i]=uint8_t((mask->pixels[i]*c+base*(255-c)+127)/255);
    }
    edit("Remove Background",[&](Document&){auto&target=*active();if(target.mask){target.mask->raster=mask;target.mask->enabled=true;}else target.mask=Mask{mask};});
}
}
