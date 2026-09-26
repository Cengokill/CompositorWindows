#include "MainWindow.h"
#include "AdjustmentDialog.h"
#include "SubjectPanel.h"
#include "effects/Adjustments.h"
#include "imaging/SubjectDialog.h"
#include <QApplication>
#include <QMenuBar>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
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
    if(editPanel_)return;
    if(live&&!existing){
        if(p->document->layers.size()>=10000)return;
        Layer added;added.id=newId();added.name=kind.toStdString();added.transform={0,0,double(p->document->width),double(p->document->height)};
        auto settings=QJsonDocument::fromJson(QByteArray::fromStdString(effects::defaultAdjustmentJson(kind.toStdString()))).object();
        if(kind=="Gradient Map"){
            auto color=[](const QColor& value){return QJsonObject{{"red",value.redF()},{"green",value.greenF()},{"blue",value.blueF()}};};
            settings["gradientMapSettings"]=QJsonObject{{"shadows",color(foreground_)},{"highlights",color(background_)},{"reversed",false}};
        }
        if(kind=="Grain")settings["grainSettings"]=QJsonObject{{"amount",25},{"size",1.5},{"roughness",50},{"seed",double(QRandomGenerator::global()->generate())}};
        added.adjustmentJson=QJsonDocument(settings).toJson(QJsonDocument::Compact).toStdString();
        if(l)added.parentId=l->group?l->id:l->parentId;
        const auto at=std::find_if(p->document->layers.begin(),p->document->layers.end(),[&](const Layer& value){return value.id==p->active;});
        const auto index=at==p->document->layers.end()?p->document->layers.size():size_t(at-p->document->layers.begin()+1);
        auto selected=std::vector<std::string>{added.id};auto collapsed=p->collapsedGroups;collapsed.erase(added.parentId);
        const auto name="New "+kind.toStdString()+" Adjustment";
        edit(name.c_str(),[&](Document& document){document.layers.insert(document.layers.begin()+index,added);p->active=added.id;});
        p->selected=std::move(selected);p->collapsedGroups=std::move(collapsed);p->maskSelected=false;existing=true;
    }
    const auto before=*p->document;const auto id=p->active;
    AdjustmentDialogOptions options;
    if(!live){options.initialAdjustmentJson=p->toolState.filterSettings.beginAdjustment(kind,foreground_,background_);options.onApply=[p](const std::string& json){p->toolState.filterSettings.rememberAdjustment(json);};}
    const auto title=live?QJsonDocument::fromJson(QByteArray::fromStdString(active()->adjustmentJson)).object()["kind"].toString():kind;
    auto host=makeEditPanelHost(*p,before,live?"Edit "+title.toStdString()+" Adjustment":title.toStdString());
    try{editPanel_=openAdjustmentPanel(this,before,id,kind,live,existing,options,std::move(host));}
    catch(...){if(live)if(auto snapshot=p->history.cancel()){p->document=std::move(snapshot->document);p->active=std::move(snapshot->activeLayer);}throw;}
    refresh(false,false);
}
void MainWindow::removeBackground(){
    auto*p=current();auto*l=active();if(editPanel_||!p||!p->document||!l||!l->raster||l->group||!l->adjustmentJson.empty())return;
    const auto before=*p->document;const auto original=*l;
    auto model=QDir(QApplication::applicationDirPath()).filePath("models/birefnet-lite.onnx");
    if(!QFileInfo::exists(model))model=QStringLiteral(COMPOSITOR_SOURCE_ROOT)+"/dependencies/imaging/model/birefnet-lite.onnx";
    imaging::SubjectDialogOptions options;options.initial=p->toolState.filterSettings.background;
    options.onApply=[p](const imaging::MatteSettings& settings){p->toolState.filterSettings.background=settings;};
    auto host=makeEditPanelHost(*p,before,"Remove Background");
    editPanel_=openSubjectPanel(this,before,original,std::filesystem::path(model.toStdWString()),options,std::move(host));refresh(false,false);
}
}
