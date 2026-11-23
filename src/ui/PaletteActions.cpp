#include "MainWindow.h"
#include "PaletteSwatches.h"
#include "ToolbarControls.h"
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QSignalBlocker>
#include <QGuiApplication>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QStatusBar>
#include <QTimer>
#include <QHBoxLayout>
#include <QToolBar>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
namespace compositor {
namespace {
effects_tools::PaletteColor asPalette(QColor c){return {c.redF(),c.greenF(),c.blueF()};}
QColor color(effects_tools::PaletteColor c){return QColor::fromRgbF(c.red,c.green,c.blue);}
Pixel bytes(QColor c){return {uint8_t(c.red()),uint8_t(c.green()),uint8_t(c.blue()),255};}
}
void MainWindow::setupPaletteControls(QWidget* tools){
    std::array<QAction*,4> actions{};
    const std::array<const char*,4> labels{"Foreground","Background","Swap (X)","Default (D)"};
    for(size_t index=0;index<actions.size();++index){
        auto* item=new QAction(labels[index],this);actions[index]=item;
        bindCommand(item,"Tools",labels[index],[this,index]{if(index<2)openPalette(index==1);else if(index==2)swapPalette();else resetPalette();});addAction(item);
    }
    paletteSwatches_=new ui::PaletteSwatches(actions,tools);
    // The Eyedropper has one option: whether the sampling ring follows the pointer.
    auto* eyedropperBar=addToolBar("Eyedropper Options");eyedropperBar->setObjectName("eyedropperOptions");
    ui::addToolbarTitle(eyedropperBar,"Eyedropper","eyedropperTitle");
    auto* sampleRing=ui::addOptionCheck(eyedropperBar,"Sample Ring","eyedropperSampleRing",showSampleRing_);
    connect(sampleRing,&QCheckBox::toggled,this,[this](bool on){showSampleRing_=on;});
    // The Zoom bar shows the view as a percentage of actual pixels (100 is one display pixel per document pixel) and edits it around the view center.
    auto* zoomBar=addToolBar("Zoom Options");zoomBar->setObjectName("zoomOptions");
    ui::addToolbarTitle(zoomBar,"Zoom","zoomTitle");
    auto* zoomField=ui::makeNumberField(1,3200,100,"Zoom percentage",0);zoomField->setObjectName("zoomPercent");zoomField->releaseFocus=[this]{if(canvas())canvas()->setFocus();};zoomField->setKeyboardTracking(false);
    ui::addToolbarGroup(zoomBar,{zoomField,ui::makeCaption("%")},"zoomPercentGroup");
    connect(zoomField,&QDoubleSpinBox::valueChanged,this,[this,zoomField]{if(canvas()&&!refreshing_)canvas()->zoomAt(zoomField->value()/100,canvas()->contentRect().center());});
}
void MainWindow::refreshZoomControl(){
    auto* field=findChild<QDoubleSpinBox*>("zoomPercent");
    if(!field||!canvas()||field->hasFocus())return;
    const QSignalBlocker block(field);
    field->setValue(std::round(canvas()->zoom*100));
}
// The status bar reads like the Mac one: zoom, canvas size and color mode on the left, and a hint for the active tool on the right.
void MainWindow::setupStatusInfo(){
    auto* bar=statusBar();
    auto* host=new QWidget;host->setObjectName("statusInfo");
    auto* row=new QHBoxLayout(host);row->setContentsMargins(18,0,18,0);row->setSpacing(16);
    const auto add=[&](const char* name){auto* label=new QLabel;label->setObjectName(name);row->addWidget(label);return label;};
    add("statusZoom")->setFixedWidth(62);add("statusDimensions");add("statusProfile");
    row->addStretch();add("statusHint");
    host->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred);
    bar->addPermanentWidget(host,1);
    // A transient message takes the place of the hint until it clears.
    // Qt keeps a message without a timeout until another one replaces it, so the hint returns by itself after a while.
    auto* expire=new QTimer(bar);expire->setSingleShot(true);expire->setInterval(8000);
    connect(expire,&QTimer::timeout,bar,&QStatusBar::clearMessage);
    connect(bar,&QStatusBar::messageChanged,this,[this,expire](const QString& message){if(message.isEmpty())expire->stop();else expire->start();refreshStatusInfo();});
    refreshStatusInfo();
}
void MainWindow::refreshStatusInfo(){
    const auto text=[this](const char* name)->QLabel*{return findChild<QLabel*>(name);};
    auto* zoom=text("statusZoom");auto* dimensions=text("statusDimensions");auto* profile=text("statusProfile");auto* hint=text("statusHint");
    if(!zoom||!dimensions||!profile||!hint)return;
    auto* p=current();const bool document=p&&p->document&&canvas();
    zoom->setVisible(document);dimensions->setVisible(document);
    zoom->setText(document?QString("%1 %").arg(std::lround(canvas()->zoom*100)):QString());
    dimensions->setText(document?QString::fromUtf8("%1 × %2 px").arg(p->document->width).arg(p->document->height):QString());
    profile->setText(document?"sRGB · Transparent":"Ready when you are");
    const auto message=statusBar()->currentMessage();
    hint->setText(!message.isEmpty()?message:document?toolHint():QString());
}
QString MainWindow::toolHint() const {
    const QString select=QString::fromUtf8("Shift add · Alt subtract · Drag inside to move · Delete clears · Ctrl+D deselect");
    switch(tool_){
    case Tool::Marquee:return (ellipse_?"Drag an ellipse · ":"Drag a rectangle · ")+select;
    case Tool::Lasso:return QString::fromUtf8("Drag to select · ")+select;
    case Tool::Polygon:return QString::fromUtf8("Click corners · Click the start point or press Enter to close · Backspace removes a corner");
    case Tool::Wand:return (wandObjectMode_?"Click an object to select its outline · Tab for Wand · ":"Click to select similar colors · Tab for Object · ")+select;
    case Tool::Brush:return QString::fromUtf8("Drag to paint · Space to pan");
    case Tool::Eraser:return QString::fromUtf8("Drag to erase · Space to pan");
    case Tool::SpotHealing:return QString::fromUtf8("Drag over blemishes to heal · Space to pan");
    case Tool::CloneStamp:return QString::fromUtf8("Alt-click to set the source · Drag to clone · Space to pan");
    case Tool::Blur:return QString::fromUtf8(retouchMode_==retouch::Mode::Blur?"Drag to soften · Space to pan":retouchMode_==retouch::Mode::Smudge?"Drag to smudge · Space to pan":"Drag to push pixels · Space to pan");
    case Tool::Text:return QString::fromUtf8("Drag a text box · Click text to edit");
    case Tool::Shape:return QString::fromUtf8("Drag to draw a shape on a new layer · Escape cancel · Space to pan");
    case Tool::Gradient:return QString::fromUtf8("Drag to draw · Drag ends to adjust · Escape cancel");
    case Tool::Crop:return QString::fromUtf8("Drag to crop · Space to pan");
    case Tool::Move:return QString::fromUtf8("Drag to move · Handles to resize · Space to pan");
    case Tool::Hand:return QString::fromUtf8("Drag to pan");
    case Tool::Eyedropper:return QString::fromUtf8("Click to sample a color");
    case Tool::Zoom:return QString::fromUtf8("Click to zoom in · Space to pan");
    }
    return {};
}
void MainWindow::refreshPaletteControls(){
    if(!paletteSwatches_)return;
    auto* owner=current();const bool masked=owner&&owner->maskSelected;
    const bool targetChanged=owner!=paletteTarget_||masked!=paletteWasMaskSelected_;
    // Publish the observed target before reject emits the existing finished
    // callback, which may refresh the complete window synchronously.
    paletteTarget_=owner;paletteWasMaskSelected_=masked;
    if(targetChanged&&maskPalettePopup_)maskPalettePopup_->close();
    if(masked&&colorPicker_)colorPicker_->reject();
    paletteSwatches_->setColors(masked?QColor(maskPaintWhite_?Qt::white:Qt::black):foreground_,masked?QColor(maskPaintWhite_?Qt::black:Qt::white):background_);
    // The Brush bar shows the foreground color; a mask paints only black or white, which the swatches already show.
    if(auto* well=dynamic_cast<ui::ColorWell*>(findChild<QAbstractButton*>("brushColor"))){well->setColor(foreground_);well->setEnabled(!masked&&!stroke_&&!retouch_);}
}
void MainWindow::openPalette(bool background){
    if(stroke_||retouch_)return;
    refreshPaletteControls();if(maskPalettePopup_)maskPalettePopup_->close();
    if(auto* owner=current();owner&&owner->maskSelected){
        auto* popup=new QWidget(this,Qt::Popup);popup->setObjectName("maskPalettePopover");popup->setAttribute(Qt::WA_DeleteOnClose);maskPalettePopup_=popup;
        auto* layout=new QVBoxLayout(popup);layout->setContentsMargins(16,16,16,16);layout->setSpacing(12);
        auto* heading=new QLabel(background?"Mask background":"Mask foreground",popup);auto font=heading->font();font.setBold(true);heading->setFont(font);layout->addWidget(heading);
        auto* choices=new QHBoxLayout;layout->addLayout(choices);
        for(bool white:{false,true}){
            auto* choice=new QPushButton(white?"White · Reveal":"Black · Hide",popup);choices->addWidget(choice);
            connect(choice,&QPushButton::clicked,this,[this,owner,popup,background,white]{
                if(current()==owner&&owner->maskSelected&&ui::commandEnabled(ui::CommandGate::Palette,commandState())&&!stroke_&&!retouch_){maskPaintWhite_=background?!white:white;refreshGradient();refresh(false,false);}
                popup->close();
            });
        }
        popup->adjustSize();auto* anchor=paletteSwatches_->swatch(background);QPoint position=anchor->mapToGlobal(QPoint(0,anchor->height()+4));
        if(auto* screen=QGuiApplication::screenAt(position)){
            const auto area=screen->availableGeometry();position.setX(std::clamp(position.x(),area.left(),std::max(area.left(),area.right()+1-popup->width())));
            if(position.y()+popup->height()>area.bottom()+1)position.setY(anchor->mapToGlobal(QPoint(0,0)).y()-popup->height()-4);
            position.setY(std::clamp(position.y(),area.top(),std::max(area.top(),area.bottom()+1-popup->height())));
        }
        popup->move(position);popup->show();return;
    }
    if(colorPicker_){if(colorPicker_->property("editPanelColor").toBool())colorPicker_->accept();else colorPicker_->reject();}
    auto*dialog=new PaletteDialog(asPalette(background?background_:foreground_),background?"Color Picker (Background Color)":"Color Picker (Foreground Color)",this);colorPicker_=dialog;connect(dialog,&QDialog::finished,this,[this,dialog,background](int result){if(result==QDialog::Accepted&&(!current()||!current()->maskSelected)){if(background)background_=color(dialog->color());else foreground_=color(dialog->color());}palettePosition_=dialog->pos();colorPicker_=nullptr;samplingPalette_=false;if(canvas())canvas()->setSampleRing({});dialog->deleteLater();refreshGradient();refresh(false,false);});if(palettePosition_)dialog->move(*palettePosition_);dialog->show();
}
void MainWindow::openEditPanelColor(effects_tools::PaletteColor original,const QString& title,std::function<void(effects_tools::PaletteColor)> changed){
    if(colorPicker_||stroke_||retouch_||!editPanel_||editPanel_->committing())return;
    auto* dialog=new PaletteDialog(original,title,this);dialog->setProperty("editPanelColor",true);colorPicker_=dialog;
    dialog->onPreview=changed;
    connect(dialog,&QDialog::finished,this,[this,dialog,original,changed=std::move(changed)](int result){
        if(changed)changed(result==QDialog::Accepted?dialog->color():original);
        palettePosition_=dialog->pos();if(colorPicker_==dialog)colorPicker_=nullptr;
        samplingPalette_=false;if(canvas())canvas()->setSampleRing({});dialog->deleteLater();refresh(false,false);
    });
    if(palettePosition_)dialog->move(*palettePosition_);dialog->show();dialog->raise();dialog->activateWindow();
}
void MainWindow::closeEditPanelColor(bool commit){
    if(!colorPicker_||!colorPicker_->property("editPanelColor").toBool())return;
    if(commit)colorPicker_->accept();else colorPicker_->reject();
}
void MainWindow::swapPalette(){if(stroke_||retouch_)return;if(current()&&current()->maskSelected)maskPaintWhite_=!maskPaintWhite_;else std::swap(foreground_,background_);refreshGradient();refresh(false,false);}
void MainWindow::resetPalette(){if(stroke_||retouch_)return;if(current()&&current()->maskSelected)maskPaintWhite_=false;else{foreground_=Qt::black;background_=Qt::white;}refreshGradient();refresh(false,false);}
bool MainWindow::beginPalette(Point point,Qt::KeyboardModifiers modifiers){
    const bool temporary=modifiers.testFlag(Qt::AltModifier)&&(tool_==Tool::Brush||tool_==Tool::Eraser||tool_==Tool::SpotHealing||tool_==Tool::Gradient);
    if(!colorPicker_&&tool_!=Tool::Eyedropper&&!temporary)return false;
    if(!current()||!current()->document)return true;samplingPalette_=true;sampleOriginal_=colorPicker_?color(colorPicker_->color()):foreground_;updatePalette(point,false);return true;
}
bool MainWindow::updatePalette(Point point,bool finish){
    if(!samplingPalette_)return false;auto*project=current();if(project&&project->document){auto preview=editPanelPreview(*project);if(auto sampled=effects_tools::sampleCompositeColor(preview?*preview:*project->document,point,SoftwareRenderer())){if(colorPicker_)colorPicker_->sample(*sampled);else{foreground_=color(*sampled);refreshGradient();refreshPaletteControls();}}if(canvas()){canvas()->setCursor(Qt::CrossCursor);canvas()->setSampleRing(showSampleRing_?std::optional(point):std::nullopt,bytes(sampleOriginal_),bytes(colorPicker_?color(colorPicker_->color()):foreground_));}}
    if(finish){samplingPalette_=false;if(canvas())canvas()->setSampleRing({});}return true;
}
}
