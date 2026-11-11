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
    for(const QString kind:{"Hue/Saturation","Levels","Curves","Exposure","Gradient Map","Grain","Invert","Black & White","Color Balance","Gaussian Blur","Motion Blur","Add Noise","Progressive Blur","Bloom / Glow","RGB Split"}){
        const QString label=kind=="Black & White"?"Black and White":kind;
        action(menu,label+"…",{},[this,kind]{adjust(kind,false);});
        action(live,label+"…",{},[this,kind]{adjust(kind,true);});
    }
    action(live,"Vignette…",{},[this]{adjust("Vignette",true);});
    action(live,"Tonal Contrast…",{},[this]{adjust("Tonal Contrast",true);});
    action(live,"Lens Effects…",{},[this]{adjust("Lens Effects",true);});
    action(menu,"Edit Adjustment Layer…",{},[this]{if(active()&&!active()->adjustmentJson.empty())adjust({},true,true);});
    auto*filters=menuBar()->addMenu("&Filters");
    lastFilterAction_=action(filters,"Last Filter",{},[this]{repeatLastFilter();});
    connect(filters,&QMenu::aboutToShow,this,[this]{
        auto* project=current();
        if(!project||!project->toolState.lastFilterKind)lastFilterAction_->setText("Last Filter");
        else lastFilterAction_->setText("Last Filter: "+filterKindDisplayName(*project->toolState.lastFilterKind));
    });
    filters->addSeparator();
    const struct {const char* name;int kind;} filterItems[]{{"Gaussian Blur",0},{"Progressive Blur",11},{"Motion Blur",1},{"Add Noise",2},{"Lens Correction",3},{"Camera Raw Filter",10},{"Content-Aware Fill",4},{"Vignette",5},{"Bloom",6},{"Tonal Contrast",7},{"Dither",8},{"Scanlines",9},{"RGB Split",12},{"Lens Effects",13}};
    for(const auto& item:filterItems)action(filters,QString(item.name)+"…",{},[this,kind=item.kind]{runFilter(kind);});
    action(filters,"Remove Background…",{},[this]{removeBackground();});
}
void MainWindow::adjust(const QString&kind,bool live,bool existing){
    auto*p=current();auto*l=active();
    if(!p||!p->document||(!live&&(!l||!l->raster||l->group||!l->adjustmentJson.empty())))return;
    if(!live&&l->deep&&l->deep->isHdr()&&(kind=="Bloom / Glow"||kind=="RGB Split"))throw std::runtime_error("Use Layer > Convert HDR to 16-Bit before this spatial filter");
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
        if(kind=="Progressive Blur"||kind=="Bloom / Glow"||kind=="RGB Split"){
            QJsonObject layer{{"progressiveFrom",0},{"progressiveTo",20},{"progressiveRadial",false},{"progressiveInverted",false},{"bloomAmount",40},{"bloomRadius",24},{"rgbSplitDistance",10},{"rgbSplitBlur",0},{"rgbSplitAngle",0},{"rgbSplitRadial",false},{"rgbSplitReach",std::max(1.,std::hypot(p->document->width,p->document->height)/2)}};
            if(kind=="Progressive Blur"){layer["progressiveStart"]=QJsonArray{p->document->width/2.,p->document->height*.35};layer["progressiveEnd"]=QJsonArray{p->document->width/2.,double(p->document->height)};}
            if(kind=="RGB Split")layer["rgbSplitCenter"]=QJsonArray{p->document->width/2.,p->document->height/2.};
            settings["filterLayerSettings"]=layer;
        }
        added.adjustmentJson=QJsonDocument(settings).toJson(QJsonDocument::Compact).toStdString();
        if(l)added.parentId=l->group?l->id:l->parentId;
        const auto at=std::find_if(p->document->layers.begin(),p->document->layers.end(),[&](const Layer& value){return value.id==p->active;});
        const auto index=at==p->document->layers.end()?p->document->layers.size():size_t(at-p->document->layers.begin()+1);
        auto selected=std::vector<std::string>{added.id};auto collapsed=p->collapsedGroups;collapsed.erase(added.parentId);
        const auto name="New "+kind.toStdString()+" Adjustment";
        if(kind=="Lens Effects"){p->composite.setLensPreviewMaxEdge(256);p->navigatorCache.setLensPreviewMaxEdge(256);}
        edit(name.c_str(),[&](Document& document){document.layers.insert(document.layers.begin()+index,added);p->active=added.id;});
        p->selected=std::move(selected);p->collapsedGroups=std::move(collapsed);p->maskSelected=false;existing=true;
    }
    const auto before=*p->document;const auto id=p->active;
    AdjustmentDialogOptions options;
    if(!live){options.initialAdjustmentJson=p->toolState.filterSettings.beginAdjustment(kind,foreground_,background_);options.onApply=[p](const std::string& json){p->toolState.filterSettings.rememberAdjustment(json);};}
    const auto title=live?QJsonDocument::fromJson(QByteArray::fromStdString(active()->adjustmentJson)).object()["kind"].toString():kind;
    auto host=makeEditPanelHost(*p,before,live?"Edit "+title.toStdString()+" Adjustment":title.toStdString());
    if(live&&title=="Lens Effects"){
        p->composite.setLensPreviewMaxEdge(256);
        p->navigatorCache.setLensPreviewMaxEdge(256);
        auto closed=std::move(host.closed);
        host.closed=[this,owner=p,closed=std::move(closed)]{
            owner->composite.setLensPreviewMaxEdge(0);
            owner->navigatorCache.setLensPreviewMaxEdge(0);
            if(closed)closed();
        };
    }
    try{editPanel_=openAdjustmentPanel(this,before,id,kind,live,existing,options,std::move(host));}
    catch(...){if(live){p->composite.setLensPreviewMaxEdge(0);p->navigatorCache.setLensPreviewMaxEdge(0);if(auto snapshot=p->history.cancel()){p->document=std::move(snapshot->document);p->active=std::move(snapshot->activeLayer);}}throw;}
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
