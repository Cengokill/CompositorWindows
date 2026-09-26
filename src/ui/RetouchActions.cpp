#include "MainWindow.h"
#include "PropertyControls.h"
#include "WrappingToolOptions.h"
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QRandomGenerator>
#include <QSignalBlocker>
#include <QShortcut>
#include <QStatusBar>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <stdexcept>

namespace compositor {
namespace {
bool isWarp(retouch::Mode mode) { return mode==retouch::Mode::Smudge||mode==retouch::Mode::Liquify; }
const char* editName(retouch::Mode mode,bool mask) {
    if(mask)return "Blur Mask";
    switch(mode) {
    case retouch::Mode::Clone:return "Clone Stamp";
    case retouch::Mode::Blur:return "Blur";
    case retouch::Mode::Smudge:return "Smudge";
    case retouch::Mode::Liquify:return "Liquify";
    default:return "Spot Healing";
    }
}
Layer* findLayer(EditorProject* project,const std::string& id) {
    if(!project||!project->document)return nullptr;
    for(auto& layer:project->document->layers)if(layer.id==id)return &layer;
    return nullptr;
}
uint64_t retouchPixelBudget(const Document& document,const Layer& target,bool mask) {
    uint64_t imageRemaining=100000000,maskRemaining=100000000;
    auto consume=[](uint64_t& remaining,int width,int height){
        if(width<=0||height<=0||width>30000||height>30000)throw std::runtime_error("Invalid existing raster extent");
        const auto pixels=uint64_t(width)*uint64_t(height);
        if(pixels>remaining)throw std::runtime_error("Existing layers exceed the retouch pixel budget");
        remaining-=pixels;
    };
    for(const auto& layer:document.layers)if(layer.id!=target.id){
        if(!mask&&layer.raster)consume(imageRemaining,layer.raster->width,layer.raster->height);
        if(layer.mask&&layer.mask->raster)consume(maskRemaining,layer.mask->raster->width,layer.mask->raster->height);
    }
    return mask?maskRemaining:target.mask?std::min(imageRemaining,maskRemaining):imageRemaining;
}
}

void MainWindow::setupRetouchActions() {
    retouchBar_=addToolBar("Retouch Options");
    retouchBar_->setObjectName("retouchOptions");
    auto addTool=[&](const QString& label,const char* objectName,const QKeySequence& shortcut,Tool tool) {
        auto* item=retouchBar_->addAction(label);
        item->setObjectName(objectName);item->setProperty("retouchShortcut",shortcut.toString());item->setCheckable(true);
        bindCommand(item,"Tools",tool==Tool::CloneStamp?"Clone (S)":tool==Tool::SpotHealing?"Heal (J)":"Retouch (R)",[this,tool]{selectTool(tool);});item->setShortcut({});item->setVisible(false);
    };
    addTool("Clone Stamp (S)","retouchClone",QKeySequence(Qt::Key_S),Tool::CloneStamp);
    addTool("Spot Healing (J)","retouchHeal",QKeySequence(Qt::Key_J),Tool::SpotHealing);
    addTool("Smear (R)","retouchSmear",QKeySequence(Qt::Key_R),Tool::Blur);
    auto* options=new ui::WrappingToolOptions;options->setObjectName("retouchOptionsContents");retouchBar_->addWidget(options);
    auto* family=new QLabel;family->setObjectName("retouchFamily");options->addControl(family);
    healingModes_=new QComboBox;
    healingModes_->setObjectName("retouchHealingMode");healingModes_->setAccessibleName("Healing mode");
    healingModes_->addItems({"Content-Aware","Create Texture","Proximity Match"});options->addControl(healingModes_);
    blurModes_=new QComboBox;
    blurModes_->setObjectName("retouchSmearMode");blurModes_->setAccessibleName("Smear mode");
    blurModes_->addItems({"Liquify","Blur","Smudge"});options->addControl(blurModes_);
    cloneAligned_=new QCheckBox("Aligned");cloneAligned_->setObjectName("retouchAligned");cloneAligned_->setChecked(true);options->addControl(cloneAligned_);
    cloneAllLayers_=new QCheckBox("Sample All Layers");cloneAllLayers_->setObjectName("retouchAllLayers");options->addControl(cloneAllLayers_);
    const char* labels[]{" Size "," Hardness "," Opacity "};
    const char* names[]{"Retouch diameter","Retouch hardness","Retouch opacity"};
    const char* objects[]{"retouchDiameter","retouchHardness","retouchOpacity"};
    for(size_t i=0;i<retouchTip_.size();++i) {
        auto* label=new QLabel(QString::fromUtf8(labels[i]).trimmed());if(i==2)label->setObjectName("retouchOpacityLabel");
        if(i){
            auto* slider=new ui::TrackSlider(Qt::Horizontal);slider->setObjectName(i==1?"retouchHardnessSlider":"retouchOpacitySlider");slider->setAccessibleName(i==1?"Retouch hardness slider":"Retouch opacity slider");slider->setRange(i==1?0:100,10000);slider->setFixedWidth(100);retouchSliders_[i-1]=slider;
            connect(slider,&QSlider::valueChanged,this,[this,i](int value){
                if(refreshing_||retouch_||stroke_)return;
                double* field=nullptr;
                if(tool_==Tool::CloneStamp)field=i==1?&cloneSettings_.hardness:&cloneSettings_.opacity;
                else if(tool_==Tool::Blur)field=i==1?&blurSettings_.hardness:&blurSettings_.opacity;
                else if(tool_==Tool::SpotHealing)field=i==1?&brushSettings_.hardness:&brushSettings_.opacity;
                if(field){*field=value/10000.;refreshRetouchControls();}
            });
        }
        auto* spin=new ui::PropertyNumber;spin->releaseFocus=[this]{if(canvas())canvas()->setFocus();};retouchTip_[i]=spin;
        spin->setObjectName(objects[i]);spin->setAccessibleName(names[i]);spin->setDecimals(i==0?1:0);
        spin->setRange(i==1?0:1,i==0?2000:100);if(i!=0)spin->setSuffix("%");
        if(i)options->addGroup({label,retouchSliders_[i-1],spin});else options->addGroup({label,spin});
        connect(spin,&QDoubleSpinBox::valueChanged,this,[this,i](double value) {
            if(refreshing_||retouch_||stroke_)return;
            double* field=nullptr;
            if(tool_==Tool::CloneStamp){double* fields[]{&cloneSettings_.radius,&cloneSettings_.hardness,&cloneSettings_.opacity};field=fields[i];}
            else if(tool_==Tool::Blur){double* fields[]{&blurSettings_.radius,&blurSettings_.hardness,&blurSettings_.opacity};field=fields[i];}
            else if(tool_==Tool::SpotHealing){double* fields[]{&brushSettings_.radius,&brushSettings_.hardness,&brushSettings_.opacity};field=fields[i];}
            if(field){*field=value/(i==0?2:100);refreshRetouchControls();}
        });
    }
    connect(healingModes_,&QComboBox::currentIndexChanged,this,[this](int index) {
        if(refreshing_||retouch_)return;
        constexpr retouch::Mode modes[]{retouch::Mode::HealContentAware,retouch::Mode::HealCreateTexture,retouch::Mode::HealProximity};
        if(index>=0&&index<3)healingMode_=modes[index];
    });
    connect(blurModes_,&QComboBox::currentIndexChanged,this,[this](int index) {
        if(refreshing_||retouch_)return;
        constexpr retouch::Mode modes[]{retouch::Mode::Liquify,retouch::Mode::Blur,retouch::Mode::Smudge};
        if(index>=0&&index<3)blurSettings_.mode=modes[index];
    });
    connect(cloneAligned_,&QCheckBox::toggled,this,[this](bool value){if(!refreshing_&&!retouch_&&current())current()->cloneAlignment.aligned=value;});
    connect(cloneAllLayers_,&QCheckBox::toggled,this,[this](bool value){if(!refreshing_&&!retouch_&&current())current()->cloneSampleAllLayers=value;});
    refreshRetouchControls();
}

void MainWindow::refreshRetouchControls() {
    if(!retouchBar_)return;
    const bool clone=tool_==Tool::CloneStamp,heal=tool_==Tool::SpotHealing,smear=tool_==Tool::Blur;
    retouchBar_->setVisible(clone||heal||smear);
    const bool enabled=!retouch_&&!stroke_&&current()&&current()->document&&!current()->projectBusy&&!current()->importing;
    for(auto* item:retouchBar_->actions()) {
        if(item->objectName()=="retouchClone")item->setChecked(clone);
        if(item->objectName()=="retouchHeal")item->setChecked(heal);
        if(item->objectName()=="retouchSmear")item->setChecked(smear);
    }
    // Family controls are children of the wrapping container. The toolbar
    // owns only that container, so it cannot reshow an inactive family option.
    const auto showOption=[](QWidget* widget,bool visible){widget->setVisible(visible);};
    showOption(healingModes_,heal);healingModes_->setEnabled(enabled);
    showOption(blurModes_,smear);blurModes_->setEnabled(enabled);
    showOption(cloneAligned_,clone);cloneAligned_->setEnabled(enabled);
    showOption(cloneAllLayers_,clone);cloneAllLayers_->setEnabled(enabled);
    const QSignalBlocker healingBlock(healingModes_),blurBlock(blurModes_),alignedBlock(cloneAligned_),allBlock(cloneAllLayers_);
    healingModes_->setCurrentIndex(healingMode_==retouch::Mode::HealCreateTexture?1:healingMode_==retouch::Mode::HealProximity?2:0);
    blurModes_->setCurrentIndex(blurSettings_.mode==retouch::Mode::Blur?1:blurSettings_.mode==retouch::Mode::Smudge?2:0);
    cloneAligned_->setChecked(current()?current()->cloneAlignment.aligned:true);
    cloneAllLayers_->setChecked(current()&&current()->cloneSampleAllLayers);
    const double radius=clone?cloneSettings_.radius:smear?blurSettings_.radius:brushSettings_.radius;
    const double hardness=clone?cloneSettings_.hardness:smear?blurSettings_.hardness:brushSettings_.hardness;
    const double opacity=clone?cloneSettings_.opacity:smear?blurSettings_.opacity:brushSettings_.opacity;
    const double values[]{radius*2,hardness*100,opacity*100};
    for(size_t i=0;i<retouchTip_.size();++i) {
        const QSignalBlocker block(retouchTip_[i]);ui::synchronizeNumber(retouchTip_[i],values[i]);retouchTip_[i]->setEnabled(enabled&&(clone||heal||smear));
    }
    for(size_t i=0;i<retouchSliders_.size();++i)if(auto* slider=retouchSliders_[i]){const QSignalBlocker block(slider);slider->setValue(int(std::lround(values[i+1]*100)));slider->setEnabled(enabled&&(clone||heal||smear));}
    if(auto* label=retouchBar_->findChild<QLabel*>("retouchOpacityLabel"))label->setText(smear?"Strength":"Opacity");
    if(auto* label=retouchBar_->findChild<QLabel*>("retouchFamily"))label->setText(clone?"Clone Stamp":heal?"Spot Healing":"Smear");
}

bool MainWindow::beginRetouch(Point point,Qt::KeyboardModifiers modifiers) {
    if(tool_!=Tool::CloneStamp&&tool_!=Tool::SpotHealing&&tool_!=Tool::Blur)return false;
    auto* project=current();if(!project||!project->document)return true;
    if(retouch_)cancelRetouch();
    // Source sampling is a pointer action, even on an ineligible paint target.
    if(tool_==Tool::CloneStamp&&modifiers.testFlag(Qt::AltModifier)) {
        try{project->cloneAlignment.setSource(point);statusBar()->showMessage(QString("Clone source: %1, %2").arg(point.x,0,'f',1).arg(point.y,0,'f',1));}
        catch(const std::exception& error){statusBar()->showMessage(error.what());}
        return true;
    }
    auto* layer=active();
    if(!layer||layerSelection().ids.size()!=1)return true;
    const auto entries=layers::entries(*project->document);
    if(std::none_of(entries.begin(),entries.end(),[&](const layers::Entry& entry){return entry.id==layer->id&&entry.visible;}))return true;
    const auto& selected=project->document->selection;
    if(selected&&(!selected->coverage||!selected->coverage->hasCoverage()))return true;
    retouch::Settings settings;
    if(tool_==Tool::CloneStamp){settings=cloneSettings_;settings.mode=retouch::Mode::Clone;settings.sampleAllLayers=project->cloneSampleAllLayers;}
    else if(tool_==Tool::Blur)settings=blurSettings_;
    else {settings.mode=healingMode_;settings.radius=brushSettings_.radius;settings.hardness=brushSettings_.hardness;settings.opacity=brushSettings_.opacity;settings.healingSeed=QRandomGenerator::global()->generate();}
    const bool mask=project->maskSelected;
    if(mask) {
        if(settings.mode!=retouch::Mode::Blur){statusBar()->showMessage("Use Blur to retouch a mask. Clone, Healing, Smudge and Liquify edit image pixels.");return true;}
        if(!layer->mask||!layer->mask->enabled||!layer->mask->raster)return true;
    } else if(layer->group||!layer->adjustmentJson.empty()||(!layer->raster&&(settings.mode==retouch::Mode::Blur||isWarp(settings.mode))))return true;
    const Point start=modifiers.testFlag(Qt::ShiftModifier)&&lastBrushPoint_&&lastBrushLayer_==layer->id&&lastBrushMask_==mask?*lastBrushPoint_:point;
    retouch::Sources sources;
    if(settings.mode==retouch::Mode::Clone) {
        sources.cloneOffset=project->cloneAlignment.strokeOffset(start);
        if(!sources.cloneOffset){statusBar()->showMessage("Alt-click where Clone Stamp should copy from first.");return true;}
    }
    QString fallback;
    try {
        const Layer original=*layer;
        const auto& document=*project->document;
        retouch::validateCanvasExtent(document.width,document.height);
        const auto remainingPixels=retouchPixelBudget(document,original,mask);
        if(!brushGpu_) {
            auto shader=QDir(QApplication::applicationDirPath()).filePath("shaders/BrushCoverage.hlsl");
            if(!QFileInfo::exists(shader))shader=QStringLiteral(COMPOSITOR_SOURCE_ROOT)+"/shaders/BrushCoverage.hlsl";
            try{brushGpu_=std::make_shared<graphics::D3D11BrushCoverage>(std::filesystem::path(shader.toStdWString()),warp_);}
            catch(const std::exception& error){fallback=QString("Using software retouch coverage: ")+error.what();}
        }
        if(settings.mode==retouch::Mode::Clone&&settings.sampleAllLayers)sources.allLayersSource=retouch::compositeSource(document);
        const auto selection=selected?selected->coverage:nullptr;
        auto session=std::make_unique<retouch::RetouchSession>(original,document.width,document.height,settings,sources,selection,brushGpu_,mask,remainingPixels);
        if(!session->begin(start))return true;
        if(start!=point)session->append(point);
        finishOpacityEdit();project->history.begin(editName(settings.mode,mask),project->document,project->active);
        retouch_=std::move(session);retouchOwner_=project;retouchOriginal_=original;retouchMode_=settings.mode;retouchMask_=mask;
        if(settings.mode==retouch::Mode::Clone)project->cloneAlignment.beginStroke(start);
        retouchAxisAnchor_=modifiers.testFlag(Qt::ShiftModifier)?std::optional(point):std::nullopt;retouchAxisHorizontal_.reset();
        lastBrushPoint_=point;lastBrushLayer_=original.id;lastBrushMask_=mask;
        publishRetouch(false);refresh(true,false);
        if(!fallback.isEmpty())statusBar()->showMessage(fallback);
    } catch(const std::exception& error) {cancelRetouch();statusBar()->showMessage(error.what());}
    return true;
}

Point MainWindow::constrainRetouch(Point point,Qt::KeyboardModifiers modifiers) {
    if(modifiers.testFlag(Qt::ShiftModifier)) {
        if(!retouchAxisAnchor_){retouchAxisAnchor_=lastBrushPoint_.value_or(point);retouchAxisHorizontal_.reset();}
        const auto anchor=*retouchAxisAnchor_;
        if(!retouchAxisHorizontal_&&std::hypot(point.x-anchor.x,point.y-anchor.y)>=3)retouchAxisHorizontal_=std::abs(point.x-anchor.x)>=std::abs(point.y-anchor.y);
        if(retouchAxisHorizontal_)point=*retouchAxisHorizontal_?Point{point.x,anchor.y}:Point{anchor.x,point.y};
        else point=anchor;
    } else {retouchAxisAnchor_.reset();retouchAxisHorizontal_.reset();}
    return point;
}

void MainWindow::publishRetouch(bool finish) {
    if(!retouch_||!retouchOwner_||!retouchOwner_->document||!retouchOriginal_)throw std::logic_error("No active retouch target");
    auto* layer=findLayer(retouchOwner_,retouchOriginal_->id);
    if(!layer||*layer!=*retouchOriginal_)throw std::runtime_error("The retouch target changed during the stroke");
    if(!finish){retouchOwner_->retouchPreview=retouch_->livePreview();return;}
    const auto snapshot=retouch_->commitSnapshot();
    if(!snapshot)throw std::runtime_error("Retouch did not produce a commit snapshot");
    if(snapshot->changed())*layer=snapshot->materializeLayer();
    retouchOwner_->retouchPreview.reset();
}

bool MainWindow::updateRetouch(Point point,Qt::KeyboardModifiers modifiers) {
    if(!retouch_)return tool_==Tool::CloneStamp||tool_==Tool::SpotHealing||tool_==Tool::Blur;
    try {
        if(current()!=retouchOwner_||!retouchOwner_->document||retouchOwner_->active!=retouchOriginal_->id||retouchOwner_->maskSelected!=retouchMask_){cancelRetouch();return true;}
        auto* layer=findLayer(retouchOwner_,retouchOriginal_->id);
        if(!layer||*layer!=*retouchOriginal_)throw std::runtime_error("The retouch target changed during the stroke");
        point=constrainRetouch(point,modifiers);
        retouch_->append(point);lastBrushPoint_=point;
        publishRetouch(false);refresh(true,false);
    } catch(const std::exception& error) {cancelRetouch();statusBar()->showMessage(error.what());}
    return true;
}

bool MainWindow::endRetouch(Point point,Qt::KeyboardModifiers modifiers) {
    if(!retouch_)return tool_==Tool::CloneStamp||tool_==Tool::SpotHealing||tool_==Tool::Blur;
    updateRetouch(point,modifiers);
    if(!retouch_)return true;
    try {
        publishRetouch(true);validateDocument(*retouchOwner_->document);
        retouchOwner_->history.end(retouchOwner_->document,retouchOwner_->active);
        retouch_.reset();retouchOriginal_.reset();retouchOwner_=nullptr;
        retouchAxisAnchor_.reset();retouchAxisHorizontal_.reset();refresh();
    } catch(const std::exception& error) {cancelRetouch();statusBar()->showMessage(error.what());}
    return true;
}

void MainWindow::cancelRetouch() {
    if(!retouch_)return;
    auto* owner=retouchOwner_;
    retouch_->cancelSnapshot();retouch_.reset();retouchOriginal_.reset();retouchOwner_=nullptr;
    if(owner)owner->retouchPreview.reset();
    retouchAxisAnchor_.reset();retouchAxisHorizontal_.reset();
    if(owner)if(auto snapshot=owner->history.cancel()){owner->document=snapshot->document;owner->active=snapshot->activeLayer;}
    if(owner==current())refresh();
}
}
