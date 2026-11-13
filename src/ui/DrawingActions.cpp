#include "MainWindow.h"
#include "CropViewport.h"
#include "PropertyControls.h"
#include <QComboBox>
#include <QDialog>
#include <QMenuBar>
#include <QToolBar>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QColorDialog>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QLabel>
#include <QPushButton>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace {
constexpr double maxCropSide=30000.;
QString cropDimensionsText(compositor::editing::Rect crop){return QString("%1 × %2 px").arg(int(crop.width)).arg(int(crop.height));}
std::optional<double> parseCropRatio(const QString& value,QString* normalized=nullptr){
    const auto parts=value.split(QLatin1Char(':'),Qt::KeepEmptyParts);if(parts.size()!=2)return {};
    auto widthText=parts[0].trimmed().replace(QLatin1Char(','),QLatin1Char('.'));
    auto heightText=parts[1].trimmed().replace(QLatin1Char(','),QLatin1Char('.'));
    bool widthOk=false,heightOk=false;const double width=widthText.toDouble(&widthOk),height=heightText.toDouble(&heightOk);
    if(!widthOk||!heightOk||!std::isfinite(width)||!std::isfinite(height)||width<=0||height<=0)return {};
    const double ratio=width/height;if(!std::isfinite(ratio)||ratio<1./maxCropSide||ratio>maxCropSide)return {};
    if(normalized)*normalized=widthText+QLatin1Char(':')+heightText;
    return ratio;
}
bool customCropRatioChoice(const QString& value){
    static const QStringList presets{"Free","Canvas","1:1","4:3","3:4","16:9","9:16","2.39:1"};
    return !presets.contains(value)&&parseCropRatio(value).has_value();
}
std::optional<QString> customCropRatioDialog(QWidget* parent,const QString& initial){
    QDialog dialog(parent);dialog.setWindowTitle("Custom ratio");QFormLayout form(&dialog);
    QLineEdit input(initial);input.setObjectName("customCropRatioInput");input.setAccessibleName("Width:height");input.setPlaceholderText("4:5");form.addRow("Width:height",&input);
    QLabel example("Width:height, such as 4:5 or 3:2");example.setWordWrap(true);form.addRow(&example);
    QLabel invalid("Enter a positive ratio within the document size limits.");invalid.setObjectName("customCropRatioValidation");invalid.setWordWrap(true);form.addRow(&invalid);
    QDialogButtonBox buttons(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);buttons.button(QDialogButtonBox::Ok)->setText("Apply");form.addRow(&buttons);
    auto update=[&]{QString normalized;const bool valid=parseCropRatio(input.text(),&normalized).has_value();buttons.button(QDialogButtonBox::Ok)->setEnabled(valid);invalid.setVisible(!valid);};
    QObject::connect(&input,&QLineEdit::textChanged,&dialog,update);
    QObject::connect(&buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);QObject::connect(&buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    update();input.selectAll();if(dialog.exec()!=QDialog::Accepted)return {};
    QString normalized;if(!parseCropRatio(input.text(),&normalized))return {};return normalized;
}
}

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
    auto*bar=addToolBar("Drawing Options");bar->setObjectName("drawingOptions");auto*gradient=bar->addAction("Gradient (G)");bindCommand(gradient,"Tools","Gradient (G)",[this]{selectTool(Tool::Gradient);refresh(false);});commands_->releaseShortcut(gradient);
    auto*gradientShape=new QComboBox;gradientShape->setObjectName("gradientShape");gradientShape->addItems({"Linear","Radial"});bar->addWidget(gradientShape);connect(gradientShape,&QComboBox::currentIndexChanged,this,[this](int i){gradientSettings_.shape=editing::GradientShape(i);refreshGradient();});
    auto*style=new QComboBox;style->setObjectName("gradientStyle");style->addItems({"Foreground to Background","Foreground to Transparent"});style->setCurrentIndex(1);bar->addWidget(style);connect(style,&QComboBox::currentIndexChanged,this,[this](int i){gradientSettings_.style=editing::GradientStyle(i);refreshGradient();});
    auto*reverse=new QCheckBox("Reverse");reverse->setObjectName("gradientReverse");bar->addWidget(reverse);connect(reverse,&QCheckBox::toggled,this,[this](bool v){gradientSettings_.reversed=v;refreshGradient();});auto*opacity=new ui::PropertyNumber;opacity->releaseFocus=[this]{if(canvas())canvas()->setFocus();};opacity->setRange(1,100);opacity->setValue(100);opacity->setSuffix("%");opacity->setAccessibleName("Gradient opacity");bar->addWidget(opacity);connect(opacity,&QDoubleSpinBox::valueChanged,this,[this](double v){gradientSettings_.opacity=v/100;refreshGradient();});
    auto*cancel=bar->addAction("Cancel Gradient");cancel->setObjectName("cancelGradient");bindCommand(cancel,"Drawing Options","Cancel Gradient",[this]{cancelGradient();});commands_->releaseShortcut(cancel);auto*apply=bar->addAction("Apply Gradient");apply->setObjectName("applyGradient");bindCommand(apply,"Drawing Options","Apply Gradient",[this]{applyGradient();});commands_->releaseShortcut(apply);
    auto*shape=bar->addAction("Shape (U)");bindCommand(shape,"Tools","Shape (U)",[this]{selectTool(Tool::Shape);refresh(false);});commands_->releaseShortcut(shape);auto*kind=new QComboBox;kind->setObjectName("shapeKind");kind->addItems({"Rectangle","Ellipse","Line"});bar->addWidget(kind);connect(kind,&QComboBox::currentIndexChanged,this,[this](int i){if(refreshing_)return;cancelShape();shapeStyle_.kind=editing::ShapeKind(i);applyShapeStyleToSelection();finishShapeStyleEdit();refresh(false,false);});
    auto*radius=new ui::PropertyNumber;radius->releaseFocus=[this]{if(canvas())canvas()->setFocus();};radius->setObjectName("shapeCornerRadius");radius->setRange(0,5000);radius->setDecimals(0);radius->setAccessibleName("Corner radius");radius->setSuffix(" px radius");bar->addWidget(radius);connect(radius,&QDoubleSpinBox::valueChanged,this,[this](double v){shapeStyle_.cornerRadius=v;applyShapeStyleToSelection();});connect(radius,&QDoubleSpinBox::editingFinished,this,[this]{finishShapeStyleEdit();});
    auto* lineWidth=new ui::PropertyNumber;lineWidth->releaseFocus=[this]{if(canvas())canvas()->setFocus();};lineWidth->setObjectName("shapeLineWidth");lineWidth->setRange(.1,500);lineWidth->setDecimals(1);lineWidth->setAccessibleName("Line width");lineWidth->setSuffix(" px");bar->addWidget(lineWidth);connect(lineWidth,&QDoubleSpinBox::valueChanged,this,[this](double v){shapeStyle_.lineWidth=v;applyShapeStyleToSelection();});connect(lineWidth,&QDoubleSpinBox::editingFinished,this,[this]{finishShapeStyleEdit();});
    auto* lineStyle=new QComboBox;lineStyle->setObjectName("shapeLineStyle");lineStyle->setAccessibleName("Line style");lineStyle->addItems({"Solid","Dashed","Long Dash","Dash Dot","Long Dash Dot","Dotted"});bar->addWidget(lineStyle);connect(lineStyle,&QComboBox::currentIndexChanged,this,[this](int i){if(refreshing_)return;finishShapeStyleEdit();shapeStyle_.lineStyle=editing::ShapeLineStyle(i);applyShapeStyleToSelection();finishShapeStyleEdit();refresh(false,false);});
    auto* lineCap=new QComboBox;lineCap->setObjectName("shapeLineCap");lineCap->setAccessibleName("Line cap");lineCap->addItems({"Round","Square"});bar->addWidget(lineCap);connect(lineCap,&QComboBox::currentIndexChanged,this,[this](int i){if(refreshing_)return;finishShapeStyleEdit();shapeStyle_.lineCap=editing::ShapeLineCap(i);applyShapeStyleToSelection();finishShapeStyleEdit();refresh(false,false);});
    auto* smoothing=new QCheckBox("Smooth Corners");
    smoothing->setObjectName("shapeSmoothing");
    smoothing->setToolTip("Smooth the transitions into rounded corners. Requires a corner radius above zero.");
    bar->addWidget(smoothing);
    connect(smoothing,&QCheckBox::toggled,this,[this](bool enabled){
        if(refreshing_)return;
        finishShapeStyleEdit();
        shapeStyle_.smoothing=enabled;
        applyShapeStyleToSelection(enabled);
        finishShapeStyleEdit();
    });
    auto*stroke=new QCheckBox("Stroke");stroke->setObjectName("shapeStroke");bar->addWidget(stroke);connect(stroke,&QCheckBox::toggled,this,[this](bool v){shapeStyle_.strokeEnabled=v;applyShapeStyleToSelection();finishShapeStyleEdit();refresh(false,false);});
    auto*strokeWidth=new ui::PropertyNumber;strokeWidth->releaseFocus=[this]{if(canvas())canvas()->setFocus();};strokeWidth->setObjectName("shapeStrokeWidth");strokeWidth->setRange(.1,500);strokeWidth->setDecimals(1);strokeWidth->setAccessibleName("Stroke width");strokeWidth->setSuffix(" px");bar->addWidget(strokeWidth);connect(strokeWidth,&QDoubleSpinBox::valueChanged,this,[this](double v){shapeStyle_.strokeWidth=v;applyShapeStyleToSelection();});connect(strokeWidth,&QDoubleSpinBox::editingFinished,this,[this]{finishShapeStyleEdit();});
    auto*strokeColor=new QPushButton("Stroke Color…");strokeColor->setObjectName("shapeStrokeColor");strokeColor->setAccessibleName("Stroke color");bar->addWidget(strokeColor);connect(strokeColor,&QPushButton::clicked,this,[this]{const auto color=QColorDialog::getColor(QColor::fromRgbF(shapeStyle_.strokeRed,shapeStyle_.strokeGreen,shapeStyle_.strokeBlue),this,"Stroke color");if(color.isValid()){shapeStyle_.strokeRed=color.redF();shapeStyle_.strokeGreen=color.greenF();shapeStyle_.strokeBlue=color.blueF();applyShapeStyleToSelection();finishShapeStyleEdit();}});
    auto*restyle=bar->addAction("Update Shape Style");bindCommand(restyle,"Drawing Options","Update Shape Style",[this]{
        finishShapeStyleEdit();
        if(!active()||active()->shapeJson.empty())return;
        edit("Shape Style",[&](Document&){
            auto style=shapeStyle_;
            const auto stored=editing::decodeShapeStyle(active()->shapeJson);
            if(style.kind==editing::ShapeKind::Line&&stored.kind==editing::ShapeKind::Line){style.startX=stored.startX;style.startY=stored.startY;style.endX=stored.endX;style.endY=stored.endY;}
            if(style.kind!=editing::ShapeKind::Rectangle)style.smoothing=false;
            else if(stored.kind==editing::ShapeKind::Rectangle)style.smoothing=stored.smoothing;
            if(style.kind==editing::ShapeKind::Line)style.strokeEnabled=false;
            style.red=foreground_.redF();style.green=foreground_.greenF();style.blue=foreground_.blueF();
            *active()=editing::restyleShape(*active(),style);
        });
    });
    for(auto* item:bar->actions())item->setProperty("hideDuringCrop",true);
    auto*cropRatioControl=new QComboBox;cropRatioControl->setObjectName("cropRatioChoice");cropRatioControl->setAccessibleName("Crop ratio");
    cropRatioControl->addItems({"Free","Canvas"});cropRatioControl->insertSeparator(cropRatioControl->count());cropRatioControl->addItem("1:1");cropRatioControl->insertSeparator(cropRatioControl->count());cropRatioControl->addItems({"4:3","16:9","2.39:1"});cropRatioControl->insertSeparator(cropRatioControl->count());cropRatioControl->addItems({"3:4","9:16"});cropRatioControl->insertSeparator(cropRatioControl->count());
    const int customActionIndex=cropRatioControl->count();cropRatioControl->addItem("Custom…");cropRatioControl->setItemData(customActionIndex,"crop-custom-action",Qt::UserRole);bar->addWidget(cropRatioControl);
    connect(cropRatioControl,&QComboBox::currentTextChanged,this,[this](const QString& value){
        if(refreshing_)return;
        if(value=="Custom…"){
            const auto initial=customCropRatioChoice(cropRatioChoice_)?cropRatioChoice_:QString("4:5");
            if(auto custom=customCropRatioDialog(this,initial)){cropRatioChoice_=*custom;changeCropRatio();}else refreshCropControls();
            return;
        }
        cropRatioChoice_=value;changeCropRatio();
    });
    auto* dimensions=new QLabel;dimensions->setObjectName("cropDimensions");bar->addWidget(dimensions);bar->addAction(cropCancel);bar->addAction(cropApply);
    auto*background=bar->addAction("Background Color");background->setProperty("hideDuringCrop",true);bindCommand(background,"Tools","Background",[this]{openPalette(true);});commands_->releaseShortcut(background);
}
bool MainWindow::beginDrawing(Point point,Qt::KeyboardModifiers modifiers){
    if(tool_!=Tool::Gradient&&tool_!=Tool::Shape&&tool_!=Tool::Crop)return false;auto*p=current();if(!p||!p->document)return true;press_=point;
    if(tool_==Tool::Crop){
        if(!std::isfinite(point.x)||!std::isfinite(point.y))return true;
        const editing::Rect full{0,0,double(p->document->width),double(p->document->height)};
        editing::CropDrag drag;drag.start=point;drag.original=cropDraft_.value_or(full);
        const auto hit=NativeCanvas::cropResizeHandle(drag.original,canvas()->viewMapping().toView(point),canvas()->viewMapping());
        if(hit){drag.mode=editing::CropDrag::Mode::Resize;drag.handle=*hit;}
        else if(cropDraft_&&*cropDraft_!=full&&point.x>=cropDraft_->x&&point.x<cropDraft_->x+cropDraft_->width&&point.y>=cropDraft_->y&&point.y<cropDraft_->y+cropDraft_->height)drag.mode=editing::CropDrag::Mode::Move;
        const bool hadDraft=cropDraft_.has_value();
        if(drag.mode==editing::CropDrag::Mode::Create)cropDraft_.reset();
        cropDrag_=drag;
        if(snapPolicy_.enabled)cropSnap_=editing::cropSnapTargets(*p->document,snapPolicy_,canvas()->layoutGrid(),8/std::max(canvas()->pointsPerPixel(),.0001));
        if(hadDraft&&!cropDraft_)refresh(false,false);
        return true;
    }
    if(tool_==Tool::Gradient){beginGradient(point);return true;}
    if(!canEditLayers()||!std::isfinite(point.x)||!std::isfinite(point.y))return true;
    const auto anchor=snapGesturePoint(point,modifiers);
    press_={std::round(anchor.x),std::round(anchor.y)};shapeDraftId_=newId();shapeDraftStyle_=shapeStyle_;
    if(shapeDraftStyle_.kind!=editing::ShapeKind::Rectangle)shapeDraftStyle_.smoothing=false;
    if(shapeDraftStyle_.kind==editing::ShapeKind::Ellipse)shapeDraftStyle_.cornerRadius=0;
    if(shapeDraftStyle_.kind==editing::ShapeKind::Line)shapeDraftStyle_.strokeEnabled=false;
    shapeDraftRect_=editing::Rect{press_.x,press_.y,0,0};refresh(false,false);return true;
}
bool MainWindow::updateDrawing(Point point,Qt::KeyboardModifiers modifiers,bool finish){
    if(tool_!=Tool::Gradient&&tool_!=Tool::Shape&&tool_!=Tool::Crop)return false;auto*p=current();if(!p||!p->document)return true;
    if(tool_==Tool::Crop){
        if(!cropDrag_)return true;
        const auto previous=cropDraft_;
        if(std::isfinite(point.x)&&std::isfinite(point.y)){
            const auto ratio=cropRatio();const bool symmetric=modifiers.testFlag(Qt::AltModifier);
            auto next=cropDrag_->updated(point,ratio,symmetric);
            if(cropSnap_&&!modifiers.testFlag(Qt::ControlModifier))next=cropSnap_->apply(next,*cropDrag_,point,ratio,symmetric);
            if(editing::validCrop(next))cropDraft_=next;
        }
        if(finish){cropDrag_.reset();cropSnap_.reset();}
        if(previous.has_value()!=cropDraft_.has_value())refresh(false,false);
        else if(previous!=cropDraft_){
            const auto& next=*cropDraft_;
            auto* view=p->canvas;
            const auto bounds=view->viewportBounds();
            if(bounds.valid){
                const auto samples=[&](editing::Rect crop){
                    return ui::cropViewportSamples(p->document->width,p->document->height,crop,bounds.x,bounds.y,bounds.width,bounds.height,bounds.units);
                };
                if(samples(*previous)!=samples(next))view->invalidateComposite();
            }
            if(int(previous->width)!=int(next.width)||int(previous->height)!=int(next.height))
                if(auto* label=findChild<QLabel*>("cropDimensions"))label->setText(cropDimensionsText(next));
            view->setCropOverlay(next);
        }
        return true;
    }
    if(tool_==Tool::Gradient){updateGradient(point,modifiers,finish);return true;}
    if(shapeDraftId_.empty()||!std::isfinite(point.x)||!std::isfinite(point.y))return true;
    // Snapping runs before dragBox so Shift's square and Alt's centered constraints still hold on the final geometry.
    const auto corner=snapGesturePoint(point,modifiers);
    shapeDraftRect_=editing::dragBox(press_,corner,modifiers.testFlag(Qt::ShiftModifier),modifiers.testFlag(Qt::AltModifier));
    if(shapeDraftStyle_.kind==editing::ShapeKind::Line){shapeDraftStyle_.startX=corner.x<press_.x?1:0;shapeDraftStyle_.endX=1-shapeDraftStyle_.startX;shapeDraftStyle_.startY=corner.y<press_.y?1:0;shapeDraftStyle_.endY=1-shapeDraftStyle_.startY;}
    if(!finish){refresh(false,false);return true;}
    auto rect=*shapeDraftRect_;auto style=shapeDraftStyle_;cancelShape();
    if(!canEditLayers()||(style.kind==editing::ShapeKind::Line?(rect.width<1&&rect.height<1):(rect.width<1||rect.height<1))){refresh(false,false);return true;}
    if(style.kind==editing::ShapeKind::Line){
        const double padding=std::ceil(style.lineWidth/std::sqrt(2.)+1);
        const double sx=rect.x+style.startX*rect.width,sy=rect.y+style.startY*rect.height,ex=rect.x+style.endX*rect.width,ey=rect.y+style.endY*rect.height;
        rect={rect.x-padding,rect.y-padding,std::ceil(rect.width+2*padding)+1,std::ceil(rect.height+2*padding)+1};
        style.startX=(sx-rect.x)/(rect.width-1);style.startY=(sy-rect.y)/(rect.height-1);style.endX=(ex-rect.x)/(rect.width-1);style.endY=(ey-rect.y)/(rect.height-1);
    }
    style.red=foreground_.redF();style.green=foreground_.greenF();style.blue=foreground_.blueF();
    auto layer=editing::createShapeLayer(rect,style,editing::nextShapeName(*p->document,style.kind));
    if(!layer){refresh(false,false);return true;}
    // SelectionClipboard.addPixelLayer inserts immediately above the active
    // record, including a folder's record; it keeps the existing selection.
    auto next=*p->document;const auto at=std::find_if(next.layers.begin(),next.layers.end(),[&](const Layer& value){return value.id==p->active;});
    const auto insertion=at==next.layers.end()?next.layers.size():size_t(at-next.layers.begin())+1;
    if(at!=next.layers.end())layer->parentId=at->group?at->id:at->parentId;
    auto selected=std::vector<std::string>{layer->id};auto activeId=layer->id;
    const auto committedStyle=editing::decodeShapeStyle(layer->shapeJson);
    next.layers.insert(next.layers.begin()+insertion,std::move(*layer));validateDocument(next);
    auto previousSelected=p->selected;const bool previousMask=p->maskSelected;finishOpacityEdit();finishShapeStyleEdit();
    // Keep the tool preferences and remember the actual committed style separately.
    shapeStyleLayer_=activeId;shapeStyleSynced_=committedStyle;
    try{edit(style.kind==editing::ShapeKind::Rectangle?"Rectangle":style.kind==editing::ShapeKind::Ellipse?"Ellipse":"Line",[&](Document& document){document=std::move(next);p->active=std::move(activeId);p->selected=std::move(selected);p->maskSelected=false;});}
    catch(...){p->selected=std::move(previousSelected);p->maskSelected=previousMask;throw;}
    return true;
}
void MainWindow::cancelShape(){
    const bool hadDraft=!shapeDraftId_.empty();
    shapeDraftId_.clear();shapeDraftRect_.reset();
    if(hadDraft)refreshShapeControls();
    if(canvas()){
        if(!hadDraft)canvas()->setShapeDraft({});
        canvas()->setSnapGuides();
    }
}
void MainWindow::refreshShapeControls(){
    const auto* layer=active();
    const bool shapeLayer=layer&&!layer->shapeJson.empty();
    if(shapeLayer){const auto stored=editing::decodeShapeStyle(layer->shapeJson);if(layer->id!=shapeStyleLayer_||stored!=shapeStyleSynced_){shapeStyle_=stored;shapeStyleLayer_=layer->id;shapeStyleSynced_=stored;}}
    else shapeStyleLayer_.clear();
    const bool controls=tool_==Tool::Shape||shapeLayer;
    if(auto* kind=findChild<QComboBox*>("shapeKind")){const QSignalBlocker block(kind);kind->setCurrentIndex(int(shapeStyle_.kind));kind->setVisible(controls);}
    const bool rectangleControls=controls&&shapeStyle_.kind==editing::ShapeKind::Rectangle;
    const bool lineControls=controls&&shapeStyle_.kind==editing::ShapeKind::Line;
    if(auto* width=findChild<QDoubleSpinBox*>("shapeLineWidth")){const QSignalBlocker block(width);ui::synchronizeNumber(width,shapeStyle_.lineWidth);width->setVisible(lineControls);}
    if(auto* style=findChild<QComboBox*>("shapeLineStyle")){const QSignalBlocker block(style);style->setCurrentIndex(int(shapeStyle_.lineStyle));style->setVisible(lineControls);}
    if(auto* cap=findChild<QComboBox*>("shapeLineCap")){const QSignalBlocker block(cap);cap->setCurrentIndex(int(shapeStyle_.lineCap));cap->setVisible(lineControls);}
    if(auto* radius=findChild<QDoubleSpinBox*>("shapeCornerRadius")){const QSignalBlocker block(radius);ui::synchronizeNumber(radius,shapeStyle_.cornerRadius);radius->setVisible(rectangleControls);}
    if(auto* smoothing=findChild<QCheckBox*>("shapeSmoothing")){
        const QSignalBlocker block(smoothing);
        const bool selectedRectangle=shapeLayer&&shapeStyleSynced_.kind==editing::ShapeKind::Rectangle&&shapeDraftId_.empty();
        smoothing->setChecked(selectedRectangle?shapeStyleSynced_.smoothing:shapeStyle_.smoothing);
        smoothing->setVisible(rectangleControls);
    }
    const bool strokable=controls&&shapeStyle_.kind!=editing::ShapeKind::Line;
    if(auto* stroke=findChild<QCheckBox*>("shapeStroke")){const QSignalBlocker block(stroke);stroke->setChecked(shapeStyle_.strokeEnabled);stroke->setVisible(strokable);}
    if(auto* width=findChild<QDoubleSpinBox*>("shapeStrokeWidth")){const QSignalBlocker block(width);ui::synchronizeNumber(width,shapeStyle_.strokeWidth);width->setVisible(strokable&&shapeStyle_.strokeEnabled);}
    if(auto* color=findChild<QPushButton*>("shapeStrokeColor"))color->setVisible(strokable&&shapeStyle_.strokeEnabled);
    if(!canvas())return;std::optional<NativeCanvas::ShapeDraftOverlay> overlay;
    if(tool_==Tool::Shape&&!shapeDraftId_.empty()&&shapeDraftRect_&&(shapeDraftStyle_.kind==editing::ShapeKind::Line?(shapeDraftRect_->width>0||shapeDraftRect_->height>0):(shapeDraftRect_->width>0&&shapeDraftRect_->height>0)))
        overlay=NativeCanvas::ShapeDraftOverlay{*shapeDraftRect_,shapeDraftStyle_,{uint8_t(foreground_.red()),uint8_t(foreground_.green()),uint8_t(foreground_.blue()),255}};
    canvas()->setShapeDraft(std::move(overlay));
}
// Bar controls edit a selected shape directly and keep its fill, unlike Update Shape Style.
// fill is only for a confirmed background colour while the Shape tool is active.
void MainWindow::applyShapeStyleToSelection(std::optional<bool> smoothing,std::optional<QColor> fill){
    if(!shapeDraftId_.empty()||!active()||active()->shapeJson.empty()||!canEditLayers())return;
    const auto stored=editing::decodeShapeStyle(active()->shapeJson);
    const bool smoothingOnly=!fill&&smoothing.has_value()&&shapeStyle_.kind==stored.kind;
    auto style=fill||smoothingOnly?stored:shapeStyle_;
    if(fill){style.red=fill->redF();style.green=fill->greenF();style.blue=fill->blueF();}
    else{style.red=stored.red;style.green=stored.green;style.blue=stored.blue;}
    if(style.kind==editing::ShapeKind::Line&&stored.kind==editing::ShapeKind::Line){style.startX=stored.startX;style.startY=stored.startY;style.endX=stored.endX;style.endY=stored.endY;}
    if(style.kind!=editing::ShapeKind::Rectangle)style.smoothing=false;
    else if(stored.kind==editing::ShapeKind::Rectangle)style.smoothing=smoothing.value_or(stored.smoothing);
    if(style.kind==editing::ShapeKind::Line)style.strokeEnabled=false;
    if(style==stored)return;
    auto* project=current();
    if(shapeStyleOwner_!=project)finishShapeStyleEdit();
    if(!shapeStyleOwner_){project->history.begin("Shape Style",project->document,project->active);shapeStyleOwner_=project;}
    shapeStyleSynced_=style;// Set before the refresh: it must not reload the bar over the style the user just chose.
    try{*active()=editing::restyleShape(*active(),style);validateDocument(*project->document);}
    catch(...){shapeStyleOwner_=nullptr;if(auto snapshot=project->history.cancel()){project->document=std::move(snapshot->document);project->active=std::move(snapshot->activeLayer);}throw;}
    refresh();
}
// Ends the open Shape Style transaction so consecutive spin-box steps become one undo step.
void MainWindow::finishShapeStyleEdit(){
    auto* owner=shapeStyleOwner_;shapeStyleOwner_=nullptr;if(!owner)return;
    try{owner->history.end(owner->document,owner->active);}
    catch(...){if(auto snapshot=owner->history.cancel()){owner->document=std::move(snapshot->document);owner->active=std::move(snapshot->activeLayer);}throw;}
    refresh(false,false);// Undo is disabled while the transaction is open; refresh recomputes its state.
}
std::optional<double> MainWindow::cropRatio() {
    if(cropRatioChoice_=="Canvas"){auto* p=current();if(p&&p->document)return double(p->document->width)/p->document->height;}
    else if(cropRatioChoice_=="1:1")return 1.;else if(cropRatioChoice_=="4:3")return 4./3.;else if(cropRatioChoice_=="3:4")return 3./4.;else if(cropRatioChoice_=="16:9")return 16./9.;else if(cropRatioChoice_=="9:16")return 9./16.;else if(cropRatioChoice_=="2.39:1")return 2.39;
    return parseCropRatio(cropRatioChoice_);
}
void MainWindow::changeCropRatio(){
    auto* p=current();if(tool_!=Tool::Crop||!p||!p->document)return;auto ratio=cropRatio();if(!ratio)return;
    const auto rect=cropDraft_.value_or(editing::Rect{0,0,double(p->document->width),double(p->document->height)});const double height=std::clamp(rect.width / *ratio,1.,maxCropSide),width=height * *ratio;
    auto next=editing::snappedCrop({rect.x,rect.y+rect.height/2-height/2,width,height});if(editing::validCrop(next))cropDraft_=next;refresh(false,false);
}
void MainWindow::cancelCrop(){cropDraft_.reset();cropDrag_.reset();cropSnap_.reset();refresh(false,false);}
void MainWindow::refreshCropControls(){
    auto* project=current();const bool visible=tool_==Tool::Crop;const bool enabled=project&&project->document&&!project->projectBusy&&!project->importing;
    bool restored=false;
    if(auto* bar=findChild<QToolBar*>("drawingOptions"))for(auto* item:bar->actions())if(item->property("hideDuringCrop").toBool()){
        if(visible){item->setProperty("hiddenForCrop",true);item->setVisible(false);}
        else if(item->property("hiddenForCrop").toBool()){const bool gradientDraft=item->objectName()=="cancelGradient"||item->objectName()=="applyGradient";item->setVisible(!gradientDraft||drawingOriginal_.has_value());item->setProperty("hiddenForCrop",false);restored=true;}
    }
    if(restored)refreshShapeControls();
    if(auto* ratio=findChild<QComboBox*>("cropRatioChoice")){
        QSignalBlocker block(ratio);const int marker=ratio->findData("crop-custom-choice",Qt::UserRole);const auto custom=customCropRatioChoice(cropRatioChoice_)?cropRatioChoice_:QString{};
        if(marker>=0&&ratio->itemText(marker)!=custom)ratio->removeItem(marker);
        if(!custom.isEmpty()&&ratio->findData("crop-custom-choice",Qt::UserRole)<0){const int action=ratio->findData("crop-custom-action",Qt::UserRole);const int insertAt=action>0&&ratio->itemText(action-1).isEmpty()?action-1:action;ratio->insertItem(insertAt,custom);ratio->setItemData(insertAt,"crop-custom-choice",Qt::UserRole);}
        ratio->setCurrentText(cropRatioChoice_);ratio->setVisible(visible);ratio->setEnabled(enabled);
    }
    if(auto* label=findChild<QLabel*>("cropDimensions")){label->setVisible(visible&&cropDraft_.has_value());label->setText(cropDraft_?cropDimensionsText(*cropDraft_):QString{});}
    for(auto* item:findChildren<QAction*>()){const auto id=item->property("commandId").toString();if(id=="crop.apply"||id=="crop.cancel")item->setVisible(visible);}
    if(canvas()){std::optional<editing::Rect> rect;if(visible&&project&&project->document)rect=cropDraft_.value_or(editing::Rect{0,0,double(project->document->width),double(project->document->height)});canvas()->setCropOverlay(rect);}
}
void MainWindow::applyCrop(){if(!current()||!current()->document||!cropDraft_||!editing::validCrop(*cropDraft_))return;const auto rect=*cropDraft_;edit("Crop",[&](Document&d){d=editing::cropDocument(d,rect);});cancelCrop();canvas()->fit();}
}
