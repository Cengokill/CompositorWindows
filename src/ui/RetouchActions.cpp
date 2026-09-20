#include "MainWindow.h"
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
std::shared_ptr<const GrayRaster> expandedMask(const Layer& layer,const Document& document) {
    const auto original=layer.mask->raster;
    if(original->width!=1||original->height!=1)return original;
    auto result=std::make_shared<GrayRaster>();
    result->width=layer.raster?layer.raster->width:document.width;
    result->height=layer.raster?layer.raster->height:document.height;
    result->pixels.assign(size_t(result->width)*result->height,original->pixels.front());
    return result;
}
// The source displays warp's document image before final selection clipping.
// Its owned mask keeps its original document placement during this preview.
Layer previewLayer(const Layer& original,retouch::RetouchSession& session,
                   retouch::Mode mode,bool mask,int width,int height,bool finish) {
    Layer result=original;
    if(mask) {
        auto gray=finish?session.commitMask():session.previewMask();
        const auto previous=original.mask->raster;
        if(previous->width==1&&previous->height==1&&
            std::all_of(gray->pixels.begin(),gray->pixels.end(),[&](uint8_t v){return v==previous->pixels.front();}))gray=previous;
        result.mask->raster=std::move(gray);
    } else if(isWarp(mode)&&!finish) {
        result.raster=session.warpDocumentPreview();
        result.transform={0,0,double(width),double(height)};
        if(result.mask)result.mask->placement=original.mask->placement.value_or(original.transform);
    } else {
        result.raster=finish?session.commit():session.preview();
    }
    if(!mask&&result.raster!=original.raster)result.shapeJson.clear();
    return result;
}
}

void MainWindow::setupRetouchActions() {
    retouchBar_=addToolBar("Retouch Options");
    retouchBar_->setObjectName("retouchOptions");
    auto addTool=[&](const QString& label,const char* objectName,const QKeySequence& shortcut,Tool tool) {
        auto* item=retouchBar_->addAction(label);
        item->setObjectName(objectName);item->setProperty("retouchShortcut",shortcut.toString());item->setCheckable(true);
        bindCommand(item,"Tools",tool==Tool::CloneStamp?"Clone (S)":tool==Tool::SpotHealing?"Heal (J)":"Retouch (R)",[this,tool]{selectTool(tool);});item->setShortcut({});
    };
    addTool("Clone Stamp (S)","retouchClone",QKeySequence(Qt::Key_S),Tool::CloneStamp);
    addTool("Spot Healing (J)","retouchHeal",QKeySequence(Qt::Key_J),Tool::SpotHealing);
    addTool("Smear (R)","retouchSmear",QKeySequence(Qt::Key_R),Tool::Blur);
    retouchBar_->addSeparator();
    healingModes_=new QComboBox;
    healingModes_->setObjectName("retouchHealingMode");healingModes_->setAccessibleName("Healing mode");
    healingModes_->addItems({"Content-Aware","Create Texture","Proximity Match"});retouchBar_->addWidget(healingModes_);
    blurModes_=new QComboBox;
    blurModes_->setObjectName("retouchSmearMode");blurModes_->setAccessibleName("Smear mode");
    blurModes_->addItems({"Liquify","Blur","Smudge"});retouchBar_->addWidget(blurModes_);
    cloneAligned_=new QCheckBox("Aligned");cloneAligned_->setObjectName("retouchAligned");cloneAligned_->setChecked(true);retouchBar_->addWidget(cloneAligned_);
    cloneAllLayers_=new QCheckBox("Sample All Layers");cloneAllLayers_->setObjectName("retouchAllLayers");retouchBar_->addWidget(cloneAllLayers_);
    const char* labels[]{" Size "," Hardness "," Opacity "};
    const char* names[]{"Retouch diameter","Retouch hardness","Retouch opacity"};
    const char* objects[]{"retouchDiameter","retouchHardness","retouchOpacity"};
    for(size_t i=0;i<retouchTip_.size();++i) {
        retouchBar_->addWidget(new QLabel(labels[i]));
        auto* spin=new QDoubleSpinBox;retouchTip_[i]=spin;
        spin->setObjectName(objects[i]);spin->setAccessibleName(names[i]);spin->setDecimals(i==0?1:0);
        spin->setRange(i==1?0:1,i==0?2000:100);if(i!=0)spin->setSuffix("%");
        retouchBar_->addWidget(spin);
        connect(spin,&QDoubleSpinBox::valueChanged,this,[this,i](double value) {
            if(refreshing_||retouch_||stroke_)return;
            double* field=nullptr;
            if(tool_==Tool::CloneStamp){double* fields[]{&cloneSettings_.radius,&cloneSettings_.hardness,&cloneSettings_.opacity};field=fields[i];}
            else if(tool_==Tool::Blur){double* fields[]{&blurSettings_.radius,&blurSettings_.hardness,&blurSettings_.opacity};field=fields[i];}
            else if(tool_==Tool::SpotHealing){double* fields[]{&brushSettings_.radius,&brushSettings_.hardness,&brushSettings_.opacity};field=fields[i];}
            if(field)*field=value/(i==0?2:100);
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
    const bool enabled=!retouch_&&!stroke_&&current()&&current()->document;
    for(auto* item:retouchBar_->actions()) {
        if(item->objectName()=="retouchClone")item->setChecked(clone);
        if(item->objectName()=="retouchHeal")item->setChecked(heal);
        if(item->objectName()=="retouchSmear")item->setChecked(smear);
    }
    healingModes_->setVisible(heal);healingModes_->setEnabled(enabled);
    blurModes_->setVisible(smear);blurModes_->setEnabled(enabled);
    cloneAligned_->setVisible(clone);cloneAligned_->setEnabled(enabled);
    cloneAllLayers_->setVisible(clone);cloneAllLayers_->setEnabled(enabled);
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
        const QSignalBlocker block(retouchTip_[i]);retouchTip_[i]->setValue(values[i]);retouchTip_[i]->setEnabled(enabled&&(clone||heal||smear));
    }
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
    if(selected&&(!selected->coverage||std::none_of(selected->coverage->pixels.begin(),selected->coverage->pixels.end(),[](uint8_t value){return value!=0;})))return true;
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
        if(!brushGpu_) {
            auto shader=QDir(QApplication::applicationDirPath()).filePath("shaders/BrushCoverage.hlsl");
            if(!QFileInfo::exists(shader))shader=QStringLiteral(COMPOSITOR_SOURCE_ROOT)+"/shaders/BrushCoverage.hlsl";
            try{brushGpu_=std::make_shared<graphics::D3D11BrushCoverage>(std::filesystem::path(shader.toStdWString()),warp_);}
            catch(const std::exception& error){fallback=QString("Using software retouch coverage: ")+error.what();}
        }
        if(settings.mode==retouch::Mode::Clone&&settings.sampleAllLayers)sources.allLayers=SoftwareRenderer().render(document,0,0,document.width,document.height);
        const auto selection=selected?selected->coverage:nullptr;
        std::unique_ptr<retouch::RetouchSession> session;
        if(mask)session=std::make_unique<retouch::RetouchSession>(expandedMask(original,document),original.mask->placement.value_or(original.transform),document.width,document.height,settings,selection,brushGpu_);
        else {
            auto pixels=original.raster;
            if(!pixels)pixels=Raster::filled(std::max(1,int(std::round(original.transform.width))),std::max(1,int(std::round(original.transform.height))));
            session=std::make_unique<retouch::RetouchSession>(pixels,original.transform,document.width,document.height,settings,sources,selection,brushGpu_);
        }
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
    if(!layer)throw std::runtime_error("The retouch target was removed");
    const auto& document=*retouchOwner_->document;
    *layer=previewLayer(*retouchOriginal_,*retouch_,retouchMode_,retouchMask_,document.width,document.height,finish);
}

bool MainWindow::updateRetouch(Point point,Qt::KeyboardModifiers modifiers) {
    if(!retouch_)return tool_==Tool::CloneStamp||tool_==Tool::SpotHealing||tool_==Tool::Blur;
    try {
        if(current()!=retouchOwner_||!retouchOwner_->document||retouchOwner_->active!=retouchOriginal_->id||retouchOwner_->maskSelected!=retouchMask_){cancelRetouch();return true;}
        auto* layer=findLayer(retouchOwner_,retouchOriginal_->id);
        const auto& document=*retouchOwner_->document;
        if(!layer||*layer!=previewLayer(*retouchOriginal_,*retouch_,retouchMode_,retouchMask_,document.width,document.height,false))throw std::runtime_error("The retouch target changed during the stroke");
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
    retouch_->cancel();retouch_.reset();retouchOriginal_.reset();retouchOwner_=nullptr;
    retouchAxisAnchor_.reset();retouchAxisHorizontal_.reset();
    if(owner)if(auto snapshot=owner->history.cancel()){owner->document=snapshot->document;owner->active=snapshot->activeLayer;}
    if(owner==current())refresh();
}
}
