#include "MainWindow.h"
#include "PropertyControls.h"
#include "WrappingToolOptions.h"
#include "CropViewport.h"
#include <QToolBar>
#include <QLabel>
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QKeyEvent>
#include <QStatusBar>
#include <QLineEdit>
#include <QCheckBox>
#include <QSignalBlocker>
#include <QElapsedTimer>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QAbstractSpinBox>
#include <QButtonGroup>
#include <QToolButton>
#include <cmath>

namespace compositor {
void MainWindow::setupBrushControls(){
    auto*bar=addToolBar("Brush Options");bar->setObjectName("brushOptions");
    auto* options=new ui::WrappingToolOptions;options->setObjectName("brushOptionsContents");bar->addWidget(options);
    target_=new QComboBox;target_->addItems({"Image","Mask"});target_->setAccessibleName("Editing target");options->addControl(target_);connect(target_,&QComboBox::currentIndexChanged,this,[this](int i){
        if(refreshing_)return;
        if(auto* project=current();project&&project->document&&active())selectLayerTarget(project->active,i==1);
        // Rejected busy/stroke or stale callbacks must show the actual target.
        if(target_){QSignalBlocker block(target_);target_->setCurrentIndex(current()&&current()->maskSelected?1:0);}
        refreshBrushControls();
    });
    auto*maskWhite=new QCheckBox("Paint mask white");maskWhite->setObjectName("paintMaskWhite");maskWhite->setChecked(maskPaintWhite_);options->addControl(maskWhite);connect(maskWhite,&QCheckBox::toggled,this,[this](bool v){maskPaintWhite_=v;refreshPaletteControls();});
    auto* modes=new QButtonGroup(this);modes->setExclusive(true);
    std::array<QToolButton*,2> modeButtons{};
    for(bool erase:{false,true}){auto* button=new QToolButton;modeButtons[size_t(erase)]=button;button->setText(erase?"Erase":"Paint");button->setAccessibleName(erase?"Erase":"Paint");button->setObjectName(erase?"brushModeErase":"brushModePaint");button->setCheckable(true);modes->addButton(button);connect(button,&QToolButton::clicked,this,[this,erase]{selectTool(erase?Tool::Eraser:Tool::Brush);refreshBrushControls();});}
    options->addGroup({modeButtons[0],modeButtons[1]});
    auto slider=[&](int index){auto* value=new ui::TrackSlider(Qt::Horizontal);value->setObjectName(index?"brushOpacitySlider":"brushHardnessSlider");value->setAccessibleName(index?"Brush opacity slider":"Brush hardness slider");value->setRange(index?100:0,10000);value->setFixedWidth(100);brushSliders_[size_t(index)]=value;return value;};
    auto*size=new ui::PropertyNumber;size->setDecimals(0);size->setRange(1,2000);size->setValue(40);size->setAccessibleName("Brush diameter");options->addGroup({new QLabel("Size"),size});
    auto*hardness=new ui::PropertyNumber;hardness->setDecimals(0);hardness->setRange(0,100);hardness->setValue(100);hardness->setSuffix("%");options->addGroup({new QLabel("Hardness"),slider(0),hardness});
    auto*opacity=new ui::PropertyNumber;opacity->setDecimals(0);opacity->setRange(1,100);opacity->setValue(100);opacity->setSuffix("%");options->addGroup({new QLabel("Opacity"),slider(1),opacity});
    for(auto* number:{size,hardness,opacity})number->releaseFocus=[this]{if(canvas())canvas()->setFocus();};
    hardness->setAccessibleName("Brush hardness");opacity->setAccessibleName("Brush opacity");
    brushTip_={size,hardness,opacity};
    connect(size,&QDoubleSpinBox::valueChanged,this,[this](double v){if(!refreshing_&&!stroke_){brushSettings_.radius=v/2;refreshBrushControls();}});
    connect(hardness,&QDoubleSpinBox::valueChanged,this,[this](double v){if(!refreshing_&&!stroke_){brushSettings_.hardness=v/100;refreshBrushControls();}});
    connect(opacity,&QDoubleSpinBox::valueChanged,this,[this](double v){if(!refreshing_&&!stroke_){brushSettings_.opacity=v/100;refreshBrushControls();}});
    for(size_t i=0;i<brushSliders_.size();++i)connect(brushSliders_[i],&QSlider::valueChanged,this,[this,i](int value){if(refreshing_||stroke_||retouch_)return;(i?brushSettings_.opacity:brushSettings_.hardness)=value/10000.;refreshBrushControls();});
}
void MainWindow::refreshBrushControls(){
    const auto* project=current();const bool enabled=project&&!project->projectBusy&&!project->importing&&!stroke_&&!retouch_;
    if(target_){const auto state=commandState();const auto* layer=active();
        target_->setEnabled(project&&project->document&&layer&&layer->mask&&!state.projectBusy&&!state.importing&&!state.modalDialog);
    }
    const double values[]{brushSettings_.radius*2,brushSettings_.hardness*100,brushSettings_.opacity*100};
    for(size_t i=0;i<brushTip_.size();++i)if(brushTip_[i]){QSignalBlocker block(brushTip_[i]);ui::synchronizeNumber(brushTip_[i],values[i]);brushTip_[i]->setEnabled(enabled);}
    for(size_t i=0;i<brushSliders_.size();++i)if(auto* slider=brushSliders_[i]){const QSignalBlocker block(slider);slider->setValue(int(std::lround(values[i+1]*100)));slider->setEnabled(enabled);}
    const bool modeEnabled=ui::commandEnabled(ui::CommandGate::Tool,commandState());
    for(bool erase:{false,true})if(auto* button=findChild<QToolButton*>(erase?"brushModeErase":"brushModePaint")){const QSignalBlocker block(button);button->setChecked((brushMode_==ProjectBrushMode::Erase)==erase);button->setEnabled(modeEnabled);}
}
bool MainWindow::beginBrush(Point point,Qt::KeyboardModifiers modifiers){
    if(tool_!=Tool::Brush&&tool_!=Tool::Eraser)return false;
    auto*p=current();auto*l=active();if(!p||!p->document||!l||layerSelection().ids.size()!=1)return true;
    strokeMask_=p->maskSelected;if(strokeMask_&&(!l->mask||!l->mask->enabled))return true;if(!strokeMask_&&(l->group||!l->adjustmentJson.empty()))return true;
    auto visible=layers::entries(*p->document);if(std::none_of(visible.begin(),visible.end(),[&](const auto&item){return item.id==l->id&&item.visible;}))return true;
    if(p->document->selection&&(!p->document->selection->coverage||!p->document->selection->coverage->hasCoverage()))return true;
    p->history.begin(tool_==Tool::Eraser?"Erase":"Brush",p->document,p->active);
    try{
        if(!brushGpu_){auto shader=QDir(QApplication::applicationDirPath()).filePath("shaders/BrushCoverage.hlsl");if(!QFileInfo::exists(shader))shader=QStringLiteral(COMPOSITOR_SOURCE_ROOT)+"/shaders/BrushCoverage.hlsl";try{brushGpu_=std::make_shared<graphics::D3D11BrushCoverage>(std::filesystem::path(shader.toStdWString()),warp_);}catch(const std::exception&e){statusBar()->showMessage(QString("Using software brush: ")+e.what());}}
        auto settings=brushSettings_;settings.color={uint8_t(foreground_.red()),uint8_t(foreground_.green()),uint8_t(foreground_.blue())};settings.opacity*=foreground_.alphaF();settings.erase=tool_==Tool::Eraser&&!strokeMask_;if(strokeMask_){auto v=uint8_t(maskPaintWhite_?255:0);settings.color={v,v,v};settings.opacity=brushSettings_.opacity;}
        uint64_t remaining=100000000;
        for(const auto&layer:p->document->layers)if(layer.id!=l->id){
            if(strokeMask_&&layer.mask&&layer.mask->raster)remaining-=uint64_t(layer.mask->raster->width)*layer.mask->raster->height;
            else if(!strokeMask_&&layer.raster)remaining-=uint64_t(layer.raster->width)*layer.raster->height;
        }
        if(!strokeMask_&&l->mask){uint64_t maskBudget=100000000;for(const auto&layer:p->document->layers)if(layer.id!=l->id&&layer.mask&&layer.mask->raster)maskBudget-=uint64_t(layer.mask->raster->width)*layer.mask->raster->height;remaining=std::min(remaining,maskBudget);}
        p->brushLayerId=l->id;
        stroke_=std::make_unique<graphics::GrowingBrushSession>(*l,settings,p->document->width,p->document->height,brushGpu_,p->document->selection?p->document->selection->coverage:nullptr,strokeMask_,remaining);
        if(modifiers.testFlag(Qt::ShiftModifier)&&lastBrushPoint_&&lastBrushLayer_==l->id&&lastBrushMask_==strokeMask_){stroke_->begin(*lastBrushPoint_);stroke_->append(point);}else stroke_->begin(point);publishBrush(stroke_->preview());lastBrushPoint_=point;lastBrushLayer_=l->id;lastBrushMask_=strokeMask_;refresh();
    }catch(const std::exception&e){pointerCancel();statusBar()->showMessage(e.what());}
    return true;
}
void MainWindow::publishBrush(std::shared_ptr<const graphics::GrowingBrushSnapshot> snapshot,bool finish){
    auto*p=current();if(!p||!p->document)return;
    if(!finish){p->brushPreview=std::move(snapshot);return;}
    auto layer=std::find_if(p->document->layers.begin(),p->document->layers.end(),[&](const Layer&l){return l.id==p->brushLayerId;});
    if(layer!=p->document->layers.end()&&snapshot->changed())*layer=snapshot->materializeLayer();
    p->brushPreview.reset();p->brushLayerId.clear();
}
CompositeViewport MainWindow::brushViewport(EditorProject&project,double x,double y,double width,double height,double requestedUnits){
    if(!project.document)return {};
    auto render=[&](const Document& document,std::shared_ptr<const LayerRenderPreview> layerPreview={}){
        if(tool_==Tool::Crop&&current()==&project&&cropDraft_)
            return ui::renderCropViewport(document,*cropDraft_,x,y,width,height,requestedUnits,std::move(layerPreview));
        return project.composite.renderViewport(document,x,y,width,height,requestedUnits,64,256,std::move(layerPreview));
    };
    if(auto preview=editPanelPreview(project))return render(*preview);
    if(project.blendPreview&&project.active==project.blendPreview->first){
        auto preview=*project.document;
        for(auto& layer:preview.layers)if(layer.id==project.blendPreview->first){layer.blend=project.blendPreview->second;break;}
        return render(preview);
    }
    if(project.gradientPreview)return render(*project.document,project.gradientPreview->renderPreview());
    if(project.retouchPreview)return render(*project.document,project.retouchPreview);
    if(!project.brushPreview)return render(*project.document);
    return render(*project.document,project.brushPreview->renderPreview());
}
bool MainWindow::updateBrush(Point point,bool finish){
    if(!stroke_)return false;
    try{stroke_->append(point);if(finish){publishBrush(stroke_->commit(),true);stroke_.reset();current()->history.end(current()->document,current()->active);}else publishBrush(stroke_->preview());lastBrushPoint_=point;refresh();}
    catch(const std::exception&e){pointerCancel();statusBar()->showMessage(e.what());}
    return true;
}
void MainWindow::keyPressEvent(QKeyEvent*e){
    auto*focus=QApplication::focusWidget();if(qobject_cast<QLineEdit*>(focus)||qobject_cast<QAbstractSpinBox*>(focus)||qobject_cast<QTextEdit*>(focus)||qobject_cast<QPlainTextEdit*>(focus)){QMainWindow::keyPressEvent(e);return;}
    if(e->key()==Qt::Key_Escape&&editPanel_){cancelEditPanel();e->accept();return;}
    if(handleEditingKey(e))return;
    const bool brushTool=tool_==Tool::Brush||tool_==Tool::Eraser||tool_==Tool::SpotHealing||tool_==Tool::CloneStamp||tool_==Tool::Blur;
    if(brushTool&&!stroke_&&!retouch_&&(e->modifiers()==Qt::NoModifier||e->modifiers()==Qt::ShiftModifier)){
        double*radius=&brushSettings_.radius,*hardness=&brushSettings_.hardness;if(tool_==Tool::CloneStamp){radius=&cloneSettings_.radius;hardness=&cloneSettings_.hardness;}else if(tool_==Tool::Blur){radius=&blurSettings_.radius;hardness=&blurSettings_.hardness;}
        if(e->key()==Qt::Key_BracketLeft||e->key()==Qt::Key_BracketRight||e->key()==Qt::Key_BraceLeft||e->key()==Qt::Key_BraceRight){bool up=e->key()==Qt::Key_BracketRight||e->key()==Qt::Key_BraceRight;if(e->modifiers()==Qt::ShiftModifier||e->key()==Qt::Key_BraceLeft||e->key()==Qt::Key_BraceRight){double quarter=*hardness*4;*hardness=std::clamp(up?std::floor(quarter+.001)+1:std::ceil(quarter-.001)-1,0.,4.)/4;}else{double diameter=*radius*2;*radius=std::clamp(up?std::max(diameter+1,std::round(diameter*1.2)):std::min(diameter-1,std::round(diameter/1.2)),1.,2000.)/2;}refresh(false,false);return;}
    }
    if(e->modifiers()==Qt::NoModifier&&e->key()>=Qt::Key_0&&e->key()<=Qt::Key_9&&!stroke_&&!retouch_&&(brushTool||tool_==Tool::Gradient||(tool_==Tool::Move&&canEditLayers()))){
        const int digit=e->key()-Qt::Key_0;QElapsedTimer clock;clock.start();const auto now=clock.msecsSinceReference();int percent=digit?digit*10:100;if(opacityDigit_&&now-opacityDigit_->second<600){percent=std::max(1,opacityDigit_->first*10+digit);opacityDigit_.reset();}else opacityDigit_=std::pair{digit,now};double value=percent/100.;
        if(tool_==Tool::CloneStamp)cloneSettings_.opacity=value;else if(tool_==Tool::Blur)blurSettings_.opacity=value;else if(brushTool)brushSettings_.opacity=value;else if(tool_==Tool::Gradient){gradientSettings_.opacity=value;refreshGradient();}else{auto selected=layerSelection().ids;finishOpacityEdit();edit("Layer Opacity",[&](Document&d){for(auto&layer:d.layers)if(!layer.group&&std::find(selected.begin(),selected.end(),layer.id)!=selected.end())layer.opacity=value;});}refresh(false,false);return;
    }
    if(e->modifiers()==Qt::NoModifier){switch(e->key()){
        case Qt::Key_W:selectTool(Tool::Wand);return;case Qt::Key_M:selectTool(Tool::Marquee);return;case Qt::Key_L:selectTool(Tool::Lasso);return;
        case Qt::Key_Return:case Qt::Key_Enter:if(drawingOriginal_)applyGradient();else if(transformSession_)applyTransformSession();else if(tool_==Tool::Crop)applyCrop();else finishPolygon();return;
        case Qt::Key_G:selectTool(Tool::Gradient);refresh(false);return;case Qt::Key_U:selectTool(Tool::Shape);refresh(false);return;case Qt::Key_C:selectTool(Tool::Crop);refresh(false);return;
        case Qt::Key_X:swapPalette();return;case Qt::Key_D:resetPalette();return;case Qt::Key_I:selectTool(Tool::Eyedropper);return;case Qt::Key_Z:selectTool(Tool::Zoom);return;
        case Qt::Key_B:selectTool(Tool::Brush);return;case Qt::Key_E:selectTool(Tool::Eraser);return;
        case Qt::Key_V:selectTool(Tool::Move);return;case Qt::Key_H:selectTool(Tool::Hand);return;
        case Qt::Key_Escape:pointerCancel();return;default:break;
    }}
    QMainWindow::keyPressEvent(e);
}
}
