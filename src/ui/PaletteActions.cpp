#include "MainWindow.h"
#include "PaletteSwatches.h"
#include <QGuiApplication>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QStatusBar>
#include <QVBoxLayout>
#include <algorithm>
namespace compositor {
namespace {
effects_tools::PaletteColor asPalette(QColor c){return {c.redF(),c.greenF(),c.blueF()};}
QColor color(effects_tools::PaletteColor c){return QColor::fromRgbF(c.red,c.green,c.blue);}
Pixel bytes(QColor c){return {uint8_t(c.red()),uint8_t(c.green()),uint8_t(c.blue()),255};}
}
void MainWindow::setupPaletteControls(QToolBar* tools){
    std::array<QAction*,4> actions{};
    const std::array<const char*,4> labels{"Foreground","Background","Swap (X)","Default (D)"};
    for(size_t index=0;index<actions.size();++index){
        auto* item=new QAction(labels[index],this);actions[index]=item;
        bindCommand(item,"Tools",labels[index],[this,index]{if(index<2)openPalette(index==1);else if(index==2)swapPalette();else resetPalette();});addAction(item);
    }
    paletteSwatches_=new ui::PaletteSwatches(actions,tools);tools->addWidget(paletteSwatches_);
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
