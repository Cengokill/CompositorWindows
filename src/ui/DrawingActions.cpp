#include "MainWindow.h"
#include "PropertyControls.h"
#include <QMenuBar>
#include <QToolBar>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QColorDialog>
#include <QSignalBlocker>
#include <QLabel>
#include <QPushButton>
#include <cmath>
#include <numbers>

namespace compositor {
void MainWindow::documentSizeDialog(bool imageSize){
    auto*p=current();if(!p||!p->document)return;const auto original=*p->document;
    QDialog dialog(this);dialog.setWindowTitle(imageSize?"Image Size":"Canvas Size");QFormLayout form(&dialog);
    editing::CanvasSizeDraft draft(original.width,original.height,original.resolution);
    QComboBox units;units.addItems({"Pixels","Percent","Inches","Centimeters"});form.addRow("Units",&units);
    QDoubleSpinBox width,height,resolution;for(auto*spin:{&width,&height}){spin->setDecimals(3);spin->setRange(-30000,30000);}width.setValue(original.width);height.setValue(original.height);width.setAccessibleName("Width");height.setAccessibleName("Height");form.addRow("Width",&width);form.addRow("Height",&height);
    QCheckBox relative("Relative"),locked("Lock aspect ratio");locked.setChecked(imageSize);draft.locked=imageSize;if(!imageSize)form.addRow(&relative);form.addRow(&locked);
    resolution.setRange(1,9600);resolution.setDecimals(2);resolution.setValue(original.resolution);if(imageSize)form.addRow("Resolution (pixels/inch)",&resolution);
    QComboBox anchor;anchor.addItems({"Top left","Top","Top right","Left","Center","Right","Bottom left","Bottom","Bottom right"});anchor.setCurrentIndex(4);if(!imageSize)form.addRow("Anchor",&anchor);
    QComboBox sampling;sampling.addItems({"Nearest","Smooth","High quality"});sampling.setCurrentIndex(2);if(imageSize)form.addRow("Resampling",&sampling);
    QCheckBox extension("Fill canvas extension");QPushButton fillColor("Choose Color…");QColor fill=Qt::white;if(!imageSize){form.addRow(&extension);form.addRow(&fillColor);}
    connect(&fillColor,&QPushButton::clicked,&dialog,[&]{auto color=QColorDialog::getColor(fill,&dialog,"Canvas extension color");if(color.isValid()){fill=color;extension.setChecked(true);}});
    QDialogButtonBox buttons(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);form.addRow(&buttons);
    auto sync=[&]{QSignalBlocker a(width),b(height);width.setValue(draft.displayed(true));height.setValue(draft.displayed(false));buttons.button(QDialogButtonBox::Ok)->setEnabled(draft.valid());};
    connect(&width,&QDoubleSpinBox::valueChanged,&dialog,[&](double v){draft.set(v,true);sync();});connect(&height,&QDoubleSpinBox::valueChanged,&dialog,[&](double v){draft.set(v,false);sync();});
    connect(&units,&QComboBox::currentIndexChanged,&dialog,[&](int i){draft.unit=editing::CanvasUnit(i);sync();});connect(&relative,&QCheckBox::toggled,&dialog,[&](bool v){draft.relative=v;sync();});connect(&locked,&QCheckBox::toggled,&dialog,[&](bool v){draft.locked=v;});connect(&resolution,&QDoubleSpinBox::valueChanged,&dialog,[&](double v){draft.resolution=v;sync();});
    connect(&buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(&buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if(dialog.exec()!=QDialog::Accepted)return;
    edit(imageSize?"Image Size":"Canvas Size",[&](Document&d){if(imageSize)d=editing::imageResize(d,{int(std::round(draft.width)),int(std::round(draft.height)),resolution.value(),Transform::Sampling(sampling.currentIndex())});else{editing::CanvasSizeOptions options{int(std::round(draft.width)),int(std::round(draft.height)),anchor.currentIndex()};if(extension.isChecked())options.fill=Pixel{uint8_t(fill.red()),uint8_t(fill.green()),uint8_t(fill.blue()),255};d=editing::canvasResize(d,options);}});canvas()->fit();
}
void MainWindow::setupDrawingActions(){
    auto*menu=menuBar()->addMenu("&Canvas");action(menu,"Canvas Size…",{},[this]{documentSizeDialog(false);});action(menu,"Image Size…",{},[this]{documentSizeDialog(true);});
    action(menu,"Flip Canvas Horizontally",{},[this]{edit("Flip Canvas Horizontal",[](Document&d){d=editing::flipCanvas(d,true);});});action(menu,"Flip Canvas Vertically",{},[this]{edit("Flip Canvas Vertical",[](Document&d){d=editing::flipCanvas(d,false);});});
    action(menu,"Crop Tool",{},[this]{selectTool(Tool::Crop);refresh(false);});auto*cropApply=action(menu,"Apply Crop",{},[this]{applyCrop();});auto*cropCancel=action(menu,"Cancel Crop",{},[this]{cancelCrop();});
    auto*bar=addToolBar("Drawing Options");bar->setObjectName("drawingOptions");auto*gradient=bar->addAction("Gradient (G)");bindCommand(gradient,"Tools","Gradient (G)",[this]{selectTool(Tool::Gradient);refresh(false);});gradient->setShortcut({});
    auto*gradientShape=new QComboBox;gradientShape->setObjectName("gradientShape");gradientShape->addItems({"Linear","Radial"});bar->addWidget(gradientShape);connect(gradientShape,&QComboBox::currentIndexChanged,this,[this](int i){gradientSettings_.shape=editing::GradientShape(i);refreshGradient();});
    auto*style=new QComboBox;style->setObjectName("gradientStyle");style->addItems({"Foreground to Background","Foreground to Transparent"});style->setCurrentIndex(1);bar->addWidget(style);connect(style,&QComboBox::currentIndexChanged,this,[this](int i){gradientSettings_.style=editing::GradientStyle(i);refreshGradient();});
    auto*reverse=new QCheckBox("Reverse");reverse->setObjectName("gradientReverse");bar->addWidget(reverse);connect(reverse,&QCheckBox::toggled,this,[this](bool v){gradientSettings_.reversed=v;refreshGradient();});auto*opacity=new ui::PropertyNumber;opacity->releaseFocus=[this]{if(canvas())canvas()->setFocus();};opacity->setRange(1,100);opacity->setValue(100);opacity->setSuffix("%");opacity->setAccessibleName("Gradient opacity");bar->addWidget(opacity);connect(opacity,&QDoubleSpinBox::valueChanged,this,[this](double v){gradientSettings_.opacity=v/100;refreshGradient();});
    auto*cancel=bar->addAction("Cancel Gradient");cancel->setObjectName("cancelGradient");bindCommand(cancel,"Drawing Options","Cancel Gradient",[this]{cancelGradient();});cancel->setShortcut({});auto*apply=bar->addAction("Apply Gradient");apply->setObjectName("applyGradient");bindCommand(apply,"Drawing Options","Apply Gradient",[this]{applyGradient();});apply->setShortcut({});
    auto*shape=bar->addAction("Shape (U)");bindCommand(shape,"Tools","Shape (U)",[this]{selectTool(Tool::Shape);refresh(false);});shape->setShortcut({});auto*kind=new QComboBox;kind->setObjectName("shapeKind");kind->addItems({"Rectangle","Ellipse","Line"});bar->addWidget(kind);connect(kind,&QComboBox::currentIndexChanged,this,[this](int i){if(refreshing_)return;cancelShape();shapeStyle_.kind=editing::ShapeKind(i);refresh(false,false);});
    auto*radius=new ui::PropertyNumber;radius->releaseFocus=[this]{if(canvas())canvas()->setFocus();};radius->setObjectName("shapeCornerRadius");radius->setRange(0,5000);radius->setDecimals(0);radius->setAccessibleName("Corner radius");radius->setSuffix(" px radius");bar->addWidget(radius);connect(radius,&QDoubleSpinBox::valueChanged,this,[this](double v){shapeStyle_.cornerRadius=v;});
    auto*restyle=bar->addAction("Update Shape Style");bindCommand(restyle,"Drawing Options","Update Shape Style",[this]{if(!active()||active()->shapeJson.empty())return;edit("Shape Style",[&](Document&){auto style=shapeStyle_;style.red=foreground_.redF();style.green=foreground_.greenF();style.blue=foreground_.blueF();*active()=editing::restyleShape(*active(),style);});});
    auto*cropRatioControl=new QComboBox;cropRatioControl->setObjectName("cropRatioChoice");cropRatioControl->setAccessibleName("Crop ratio");cropRatioControl->addItems({"Free","Original","1:1","4:3","3:4","16:9","9:16"});bar->addWidget(cropRatioControl);
    connect(cropRatioControl,&QComboBox::currentTextChanged,this,[this](const QString& value){if(refreshing_)return;cropRatioChoice_=value;changeCropRatio();});
    auto* dimensions=new QLabel;dimensions->setObjectName("cropDimensions");bar->addWidget(dimensions);bar->addAction(cropCancel);bar->addAction(cropApply);
    auto*background=bar->addAction("Background Color");bindCommand(background,"Tools","Background",[this]{openPalette(true);});background->setShortcut({});
}
bool MainWindow::beginDrawing(Point point,Qt::KeyboardModifiers){
    if(tool_!=Tool::Gradient&&tool_!=Tool::Shape&&tool_!=Tool::Crop)return false;auto*p=current();if(!p||!p->document)return true;press_=point;
    if(tool_==Tool::Crop){
        if(!std::isfinite(point.x)||!std::isfinite(point.y))return true;
        const editing::Rect full{0,0,double(p->document->width),double(p->document->height)};
        editing::CropDrag drag;drag.start=point;drag.original=cropDraft_.value_or(full);
        const auto hit=NativeCanvas::cropResizeHandle(drag.original,canvas()->viewMapping().toView(point),canvas()->viewMapping());
        if(hit){drag.mode=editing::CropDrag::Mode::Resize;drag.handle=*hit;}
        else if(cropDraft_&&*cropDraft_!=full&&point.x>=cropDraft_->x&&point.x<cropDraft_->x+cropDraft_->width&&point.y>=cropDraft_->y&&point.y<cropDraft_->y+cropDraft_->height)drag.mode=editing::CropDrag::Mode::Move;
        if(drag.mode==editing::CropDrag::Mode::Create)cropDraft_.reset();
        cropDrag_=drag;cropSnap_=editing::cropSnapTargets(*p->document,8/std::max(canvas()->pointsPerPixel(),.0001));refresh(false,false);return true;
    }
    if(tool_==Tool::Gradient){beginGradient(point);return true;}
    if(!canEditLayers()||!std::isfinite(point.x)||!std::isfinite(point.y))return true;
    press_={std::round(point.x),std::round(point.y)};shapeDraftId_=newId();shapeDraftStyle_=shapeStyle_;
    if(shapeDraftStyle_.kind==editing::ShapeKind::Ellipse)shapeDraftStyle_.cornerRadius=0;
    shapeDraftRect_=editing::Rect{press_.x,press_.y,0,0};refresh(false,false);return true;
}
bool MainWindow::updateDrawing(Point point,Qt::KeyboardModifiers modifiers,bool finish){
    if(tool_!=Tool::Gradient&&tool_!=Tool::Shape&&tool_!=Tool::Crop)return false;auto*p=current();if(!p||!p->document)return true;
    if(tool_==Tool::Crop){
        if(!cropDrag_)return true;
        if(std::isfinite(point.x)&&std::isfinite(point.y)){
            const auto ratio=cropRatio();const bool symmetric=modifiers.testFlag(Qt::AltModifier);
            auto next=cropDrag_->updated(point,ratio,symmetric);
            if(cropSnap_&&!modifiers.testFlag(Qt::ControlModifier))next=cropSnap_->apply(next,*cropDrag_,point,ratio,symmetric);
            if(editing::validCrop(next))cropDraft_=next;
        }
        if(finish){cropDrag_.reset();cropSnap_.reset();}refresh(false,false);return true;
    }
    if(tool_==Tool::Gradient){updateGradient(point,modifiers,finish);return true;}
    if(shapeDraftId_.empty()||!std::isfinite(point.x)||!std::isfinite(point.y))return true;
    shapeDraftRect_=editing::dragBox(press_,point,modifiers.testFlag(Qt::ShiftModifier),modifiers.testFlag(Qt::AltModifier));
    if(!finish){refresh(false,false);return true;}
    const auto rect=*shapeDraftRect_;auto style=shapeDraftStyle_;cancelShape();
    if(!canEditLayers()||rect.width<1||rect.height<1){refresh(false,false);return true;}
    style.red=foreground_.redF();style.green=foreground_.greenF();style.blue=foreground_.blueF();
    auto layer=editing::createShapeLayer(rect,style,editing::nextShapeName(*p->document,style.kind));
    if(!layer){refresh(false,false);return true;}
    // SelectionClipboard.addPixelLayer inserts immediately above the active
    // record, including a folder's record; it keeps the existing selection.
    auto next=*p->document;const auto at=std::find_if(next.layers.begin(),next.layers.end(),[&](const Layer& value){return value.id==p->active;});
    const auto insertion=at==next.layers.end()?next.layers.size():size_t(at-next.layers.begin())+1;
    if(at!=next.layers.end())layer->parentId=at->group?at->id:at->parentId;
    auto selected=std::vector<std::string>{layer->id};auto activeId=layer->id;
    next.layers.insert(next.layers.begin()+insertion,std::move(*layer));validateDocument(next);
    auto previousSelected=p->selected;const bool previousMask=p->maskSelected;finishOpacityEdit();
    try{edit(style.kind==editing::ShapeKind::Rectangle?"Rectangle":"Ellipse",[&](Document& document){document=std::move(next);p->active=std::move(activeId);p->selected=std::move(selected);p->maskSelected=false;});}
    catch(...){p->selected=std::move(previousSelected);p->maskSelected=previousMask;throw;}
    return true;
}
void MainWindow::cancelShape(){shapeDraftId_.clear();shapeDraftRect_.reset();if(canvas())canvas()->setShapeDraft({});}
void MainWindow::refreshShapeControls(){
    if(auto* kind=findChild<QComboBox*>("shapeKind")){const QSignalBlocker block(kind);kind->setCurrentIndex(int(shapeStyle_.kind));kind->setVisible(tool_==Tool::Shape);}
    if(auto* radius=findChild<QDoubleSpinBox*>("shapeCornerRadius")){const QSignalBlocker block(radius);ui::synchronizeNumber(radius,shapeStyle_.cornerRadius);radius->setVisible(tool_==Tool::Shape&&shapeStyle_.kind==editing::ShapeKind::Rectangle);}
    if(!canvas())return;std::optional<NativeCanvas::ShapeDraftOverlay> overlay;
    if(tool_==Tool::Shape&&!shapeDraftId_.empty()&&shapeDraftRect_&&shapeDraftRect_->width>0&&shapeDraftRect_->height>0)
        overlay=NativeCanvas::ShapeDraftOverlay{*shapeDraftRect_,shapeDraftStyle_.kind,shapeDraftStyle_.cornerRadius,{uint8_t(foreground_.red()),uint8_t(foreground_.green()),uint8_t(foreground_.blue()),255},shapeDraftStyle_.lineWidth};
    canvas()->setShapeDraft(std::move(overlay));
}
std::optional<double> MainWindow::cropRatio() {
    if(cropRatioChoice_=="Original"){auto* p=current();if(p&&p->document)return double(p->document->width)/p->document->height;}
    else if(cropRatioChoice_=="1:1")return 1.;else if(cropRatioChoice_=="4:3")return 4./3.;else if(cropRatioChoice_=="3:4")return 3./4.;else if(cropRatioChoice_=="16:9")return 16./9.;else if(cropRatioChoice_=="9:16")return 9./16.;return {};
}
void MainWindow::changeCropRatio(){
    auto* p=current();if(tool_!=Tool::Crop||!p||!p->document)return;auto ratio=cropRatio();if(!ratio)return;
    const auto rect=cropDraft_.value_or(editing::Rect{0,0,double(p->document->width),double(p->document->height)});const double height=rect.width / *ratio;
    auto next=editing::snappedCrop({rect.x,rect.y+rect.height/2-height/2,rect.width,height});if(editing::validCrop(next))cropDraft_=next;refresh(false,false);
}
void MainWindow::cancelCrop(){cropDraft_.reset();cropDrag_.reset();cropSnap_.reset();refresh(false,false);}
void MainWindow::refreshCropControls(){
    auto* project=current();const bool visible=tool_==Tool::Crop;const bool enabled=project&&project->document&&!project->projectBusy&&!project->importing;
    if(auto* ratio=findChild<QComboBox*>("cropRatioChoice")){QSignalBlocker block(ratio);ratio->setCurrentText(cropRatioChoice_);ratio->setVisible(visible);ratio->setEnabled(enabled);}
    if(auto* label=findChild<QLabel*>("cropDimensions")){label->setVisible(visible&&cropDraft_.has_value());label->setText(cropDraft_?QString("%1 × %2 px").arg(int(cropDraft_->width)).arg(int(cropDraft_->height)):QString{});}
    for(auto* item:findChildren<QAction*>()){const auto id=item->property("commandId").toString();if(id=="crop.apply"||id=="crop.cancel")item->setVisible(visible);}
    if(canvas()){std::optional<editing::Rect> rect;if(visible&&project&&project->document)rect=cropDraft_.value_or(editing::Rect{0,0,double(project->document->width),double(project->document->height)});canvas()->setCropOverlay(rect);}
}
void MainWindow::applyCrop(){if(!current()||!current()->document||!cropDraft_||!editing::validCrop(*cropDraft_))return;const auto rect=*cropDraft_;edit("Crop",[&](Document&d){d=editing::cropDocument(d,rect);});cancelCrop();canvas()->fit();}
}
