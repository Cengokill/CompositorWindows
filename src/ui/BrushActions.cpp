#include "MainWindow.h"
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
#include <cmath>

namespace compositor {
void MainWindow::setupBrushControls(){
    auto*bar=addToolBar("Brush Options");bar->setObjectName("brushOptions");
    target_=new QComboBox;target_->addItems({"Image","Mask"});target_->setAccessibleName("Editing target");bar->addWidget(target_);connect(target_,&QComboBox::currentIndexChanged,this,[this](int i){if(refreshing_||!current())return;applyGradient();pointerCancel();current()->maskSelected=i==1;refresh();});
    auto*maskWhite=new QCheckBox("Paint mask white");maskWhite->setChecked(maskPaintWhite_);bar->addWidget(maskWhite);connect(maskWhite,&QCheckBox::toggled,this,[this](bool v){maskPaintWhite_=v;});
    auto*brush=bar->addAction("Brush (B)");connect(brush,&QAction::triggered,this,[this]{selectTool(Tool::Brush);});
    auto*eraser=bar->addAction("Eraser (E)");connect(eraser,&QAction::triggered,this,[this]{selectTool(Tool::Eraser);});
    bar->addWidget(new QLabel(" Size "));auto*size=new QDoubleSpinBox;size->setRange(1,2000);size->setValue(40);size->setAccessibleName("Brush diameter");bar->addWidget(size);
    bar->addWidget(new QLabel(" Hardness "));auto*hardness=new QDoubleSpinBox;hardness->setRange(0,100);hardness->setValue(100);hardness->setSuffix("%");bar->addWidget(hardness);
    bar->addWidget(new QLabel(" Opacity "));auto*opacity=new QDoubleSpinBox;opacity->setRange(0,100);opacity->setValue(100);opacity->setSuffix("%");bar->addWidget(opacity);
    brushTip_={size,hardness,opacity};
    connect(size,&QDoubleSpinBox::valueChanged,this,[this](double v){if(!stroke_)brushSettings_.radius=v/2;});
    connect(hardness,&QDoubleSpinBox::valueChanged,this,[this](double v){if(!stroke_)brushSettings_.hardness=v/100;});
    connect(opacity,&QDoubleSpinBox::valueChanged,this,[this](double v){if(!stroke_)brushSettings_.opacity=v/100;});
}
void MainWindow::refreshBrushControls(){
    const double values[]{brushSettings_.radius*2,brushSettings_.hardness*100,brushSettings_.opacity*100};
    for(size_t i=0;i<brushTip_.size();++i)if(brushTip_[i]){QSignalBlocker block(brushTip_[i]);brushTip_[i]->setValue(values[i]);}
}
bool MainWindow::beginBrush(Point point,Qt::KeyboardModifiers modifiers){
    if(tool_!=Tool::Brush&&tool_!=Tool::Eraser)return false;
    auto*p=current();auto*l=active();if(!p||!p->document||!l||layerSelection().ids.size()!=1)return true;
    strokeMask_=p->maskSelected;if(strokeMask_&&(!l->mask||!l->mask->enabled))return true;if(!strokeMask_&&(l->group||!l->adjustmentJson.empty()))return true;
    auto visible=layers::entries(*p->document);if(std::none_of(visible.begin(),visible.end(),[&](const auto&item){return item.id==l->id&&item.visible;}))return true;
    if(p->document->selection&&(!p->document->selection->coverage||std::none_of(p->document->selection->coverage->pixels.begin(),p->document->selection->coverage->pixels.end(),[](uint8_t v){return v!=0;})))return true;
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
    if(project.gradientPreview){
        auto preview=*project.document;
        for(auto& layer:preview.layers)if(layer.id==project.gradientPreview->id){layer=*project.gradientPreview;break;}
        return project.composite.renderViewport(preview,x,y,width,height,requestedUnits);
    }
    if(project.retouchPreview)return project.composite.renderViewport(*project.document,x,y,width,height,requestedUnits,64,256,project.retouchPreview);
    if(!project.brushPreview)return project.composite.renderViewport(*project.document,x,y,width,height,requestedUnits);
    return project.composite.renderViewport(*project.document,x,y,width,height,requestedUnits,64,256,project.brushPreview->renderPreview());
}
bool MainWindow::updateBrush(Point point,bool finish){
    if(!stroke_)return false;
    try{stroke_->append(point);if(finish){publishBrush(stroke_->commit(),true);stroke_.reset();current()->history.end(current()->document,current()->active);}else publishBrush(stroke_->preview());lastBrushPoint_=point;refresh();}
    catch(const std::exception&e){pointerCancel();statusBar()->showMessage(e.what());}
    return true;
}
void MainWindow::keyPressEvent(QKeyEvent*e){
    auto*focus=QApplication::focusWidget();if(qobject_cast<QLineEdit*>(focus)||qobject_cast<QAbstractSpinBox*>(focus)||qobject_cast<QTextEdit*>(focus)||qobject_cast<QPlainTextEdit*>(focus)){QMainWindow::keyPressEvent(e);return;}
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
