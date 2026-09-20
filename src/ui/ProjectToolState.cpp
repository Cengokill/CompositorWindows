#include "MainWindow.h"
#include <QSignalBlocker>
#include <QSpinBox>
#include <QJsonDocument>
#include "effects/Adjustments.h"

namespace compositor {
ProjectFilterSettings::ProjectFilterSettings() {
    auto defaults=[](const char* kind,const char* field){return QJsonDocument::fromJson(QByteArray::fromStdString(effects::defaultAdjustmentJson(kind))).object()[field].toObject();};
    curves=defaults("Curves","curves");
    exposure=QJsonObject{{"exposure",0},{"offset",0},{"gamma",1}};
    gradientMap=QJsonObject{{"shadows",QJsonObject{{"red",0},{"green",0},{"blue",0}}},{"highlights",QJsonObject{{"red",1},{"green",1},{"blue",1}}},{"reversed",false}};
    grain=QJsonObject{{"amount",25},{"size",1.5},{"roughness",50},{"seed",0}};
}
std::string ProjectFilterSettings::beginAdjustment(const QString& kind,QColor foreground,QColor backgroundColor) const {
    auto json=QJsonDocument::fromJson(QByteArray::fromStdString(effects::defaultAdjustmentJson(kind.toStdString()))).object();
    if(kind=="Curves")json["curves"]=curves;
    else if(kind=="Exposure")json["exposureSettings"]=exposure;
    else if(kind=="Grain")json["grainSettings"]=grain;
    else if(kind=="Gradient Map"){
        auto color=[](QColor value){return QJsonObject{{"red",value.redF()},{"green",value.greenF()},{"blue",value.blueF()}};};
        // Filters.swift313-315 always starts a new map from today's palette.
        json["gradientMapSettings"]=QJsonObject{{"shadows",color(foreground)},{"highlights",color(backgroundColor)},{"reversed",false}};
    }
    return QJsonDocument(json).toJson(QJsonDocument::Compact).toStdString();
}
void ProjectFilterSettings::rememberAdjustment(const std::string& json) {
    const auto value=QJsonDocument::fromJson(QByteArray::fromStdString(json)).object();const auto kind=value["kind"].toString();
    if(kind=="Curves")curves=value["curves"].toObject();
    else if(kind=="Exposure")exposure=value["exposureSettings"].toObject();
    else if(kind=="Gradient Map")gradientMap=value["gradientMapSettings"].toObject();
    else if(kind=="Grain")grain=value["grainSettings"].toObject();
}
namespace {
template<class T> T* control(QObject* root,const char* objectName,const char* accessibleName="") {
    if(*objectName)if(auto* found=root->findChild<T*>(objectName))return found;
    if(*accessibleName)for(auto* found:root->findChildren<T*>())if(found->accessibleName()==accessibleName)return found;
    return nullptr;
}
void combo(QObject* root,const char* objectName,const char* accessibleName,int value) {
    if(auto* item=control<QComboBox>(root,objectName,accessibleName)){const QSignalBlocker blocker(item);item->setCurrentIndex(value);}
}
void number(QObject* root,const char* objectName,const char* accessibleName,double value) {
    if(auto* item=control<QDoubleSpinBox>(root,objectName,accessibleName)){const QSignalBlocker blocker(item);item->setValue(value);}
}
void check(QObject* root,const char* objectName,const char* toolbar,const char* label,bool value) {
    auto* item=*objectName?root->findChild<QCheckBox*>(objectName):nullptr;
    if(!item){auto* scope=root->findChild<QToolBar*>(toolbar);if(scope)for(auto* candidate:scope->findChildren<QCheckBox*>())if(candidate->text()==label){item=candidate;break;}}
    if(item){const QSignalBlocker blocker(item);item->setChecked(value);}
}
}
void MainWindow::captureToolState(EditorProject& project) const {
    auto& state=project.toolState;
    state.tool=tool_;state.foreground=foreground_;state.background=background_;
    state.brushSettings=brushSettings_;state.cloneSettings=cloneSettings_;state.blurSettings=blurSettings_;state.healingMode=healingMode_;
    state.gradientSettings=gradientSettings_;state.shapeStyle=shapeStyle_;
    state.ellipse=ellipse_;state.selectionAntialias=selectionAntialias_;state.selectionMode=selectionMode_;
    state.cropRatioChoice=cropRatioChoice_;state.lassoKind=lassoKind_;
    state.selectionExpandAmount=selectionExpandAmount_;state.selectionContractAmount=selectionContractAmount_;
    state.wandTolerance=wandTolerance_;state.wandSampleRadius=wandSampleRadius_;state.wandContiguous=wandContiguous_;state.wandAllLayers=wandAllLayers_;
    state.maskPaintWhite=maskPaintWhite_;state.showSampleRing=showSampleRing_;
    if(project.canvas)state.showPixelGrid=project.canvas->showPixelGrid;
    state.lockRatio=lockRatio_;state.autoSelectLayers=autoSelectLayers_;state.transformControls=transformControls_;state.snapping=snapping_;
    state.lastBrushPoint=lastBrushPoint_;state.lastBrushLayer=lastBrushLayer_;state.lastBrushMask=lastBrushMask_;
}
void MainWindow::restoreToolState(const EditorProject& project) {
    const auto& state=project.toolState;
    tool_=state.tool;foreground_=state.foreground;background_=state.background;
    brushSettings_=state.brushSettings;cloneSettings_=state.cloneSettings;blurSettings_=state.blurSettings;healingMode_=state.healingMode;
    gradientSettings_=state.gradientSettings;shapeStyle_=state.shapeStyle;
    ellipse_=state.ellipse;selectionAntialias_=state.selectionAntialias;selectionMode_=state.selectionMode;
    cropRatioChoice_=state.cropRatioChoice;lassoKind_=state.lassoKind;
    selectionExpandAmount_=state.selectionExpandAmount;selectionContractAmount_=state.selectionContractAmount;
    wandTolerance_=state.wandTolerance;wandSampleRadius_=state.wandSampleRadius;wandContiguous_=state.wandContiguous;wandAllLayers_=state.wandAllLayers;
    maskPaintWhite_=state.maskPaintWhite;showSampleRing_=state.showSampleRing;
    if(project.canvas)project.canvas->showPixelGrid=state.showPixelGrid;
    lockRatio_=state.lockRatio;autoSelectLayers_=state.autoSelectLayers;transformControls_=state.transformControls;snapping_=state.snapping;
    lastBrushPoint_=state.lastBrushPoint;lastBrushLayer_=state.lastBrushLayer;lastBrushMask_=state.lastBrushMask;

    combo(this,"gradientShape","",int(gradientSettings_.shape));
    combo(this,"gradientStyle","",int(gradientSettings_.style));
    check(this,"gradientReverse","drawingOptions","Reverse",gradientSettings_.reversed);
    number(this,"","Gradient opacity",gradientSettings_.opacity*100);
    combo(this,"shapeKind","",int(shapeStyle_.kind));number(this,"","Corner radius",shapeStyle_.cornerRadius);
    combo(this,"marqueeShape","Marquee shape",ellipse_?1:0);
    combo(this,"lassoKind","",lassoKind_==editing::LassoKind::Polygonal?1:0);
    if(auto* item=control<QComboBox>(this,"cropRatioChoice")){const QSignalBlocker blocker(item);item->setCurrentText(cropRatioChoice_);}
    for(const auto entry:{std::pair{"selectionExpandAmount",selectionExpandAmount_},std::pair{"selectionContractAmount",selectionContractAmount_}})if(auto* item=control<QSpinBox>(this,entry.first)){const QSignalBlocker blocker(item);item->setValue(entry.second);}
    combo(this,"selectionMode","Selection mode",int(selectionMode_));
    check(this,"selectionAntialias","selectionOptions","Antialias",selectionAntialias_);
    if(auto* item=control<QSpinBox>(this,"wandTolerance","Wand tolerance")){const QSignalBlocker blocker(item);item->setValue(wandTolerance_);}
    combo(this,"wandSampleSize","Wand sample size",wandSampleRadius_);
    check(this,"wandContiguous","wandOptions","Contiguous",wandContiguous_);
    check(this,"wandAllLayers","wandOptions","Sample All Layers",wandAllLayers_);
    check(this,"paintMaskWhite","brushOptions","Paint mask white",maskPaintWhite_);
    check(this,"transformLockRatio","transformOptions","Lock aspect ratio",lockRatio_);
    check(this,"transformAutoSelect","transformOptions","Auto-select layer",autoSelectLayers_);
    check(this,"transformShowControls","transformOptions","Show controls",transformControls_);
    check(this,"transformSnapping","transformOptions","Snap to edges and centers",snapping_);
    for(auto* item:findChildren<QAction*>())if(item->objectName()=="pixelGrid"||item->text()=="Pixel Grid"){const QSignalBlocker blocker(item);item->setChecked(state.showPixelGrid);}
    // These existing refreshers block signals and read the newly selected project
    // for clone source/alignment and layer eligibility.
    refreshBrushControls();refreshRetouchControls();
}
}
