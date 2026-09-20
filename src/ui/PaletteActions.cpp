#include "MainWindow.h"
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>
namespace compositor {
namespace {
effects_tools::PaletteColor asPalette(QColor c){return {c.redF(),c.greenF(),c.blueF()};}
QColor color(effects_tools::PaletteColor c){return QColor::fromRgbF(c.red,c.green,c.blue);}
Pixel bytes(QColor c){return {uint8_t(c.red()),uint8_t(c.green()),uint8_t(c.blue()),255};}
}
void MainWindow::openPalette(bool background){
    if(stroke_||retouch_)return;
    if(current()&&current()->maskSelected){QMessageBox box(QMessageBox::Question,background?"Mask Background":"Mask Foreground","Choose coverage for this mask color.",QMessageBox::Cancel,this);auto*black=box.addButton("Black · Hide",QMessageBox::AcceptRole);auto*white=box.addButton("White · Reveal",QMessageBox::AcceptRole);box.exec();if(box.clickedButton()==black||box.clickedButton()==white){const bool chooseWhite=box.clickedButton()==white;maskPaintWhite_=background?!chooseWhite:chooseWhite;refreshGradient();refresh(false,false);}return;}
    if(colorPicker_){colorPicker_->raise();colorPicker_->activateWindow();return;}
    auto*dialog=new PaletteDialog(asPalette(background?background_:foreground_),background?"Color Picker (Background Color)":"Color Picker (Foreground Color)",this);colorPicker_=dialog;connect(dialog,&QDialog::finished,this,[this,dialog,background](int result){if(result==QDialog::Accepted&&(!current()||!current()->maskSelected)){if(background)background_=color(dialog->color());else foreground_=color(dialog->color());}palettePosition_=dialog->pos();colorPicker_=nullptr;samplingPalette_=false;if(canvas())canvas()->setSampleRing({});dialog->deleteLater();refreshGradient();refresh(false,false);});if(palettePosition_)dialog->move(*palettePosition_);dialog->show();
}
void MainWindow::swapPalette(){if(stroke_||retouch_)return;if(current()&&current()->maskSelected)maskPaintWhite_=!maskPaintWhite_;else std::swap(foreground_,background_);refreshGradient();refresh(false,false);}
void MainWindow::resetPalette(){if(stroke_||retouch_)return;if(current()&&current()->maskSelected)maskPaintWhite_=false;else{foreground_=Qt::black;background_=Qt::white;}refreshGradient();refresh(false,false);}
bool MainWindow::beginPalette(Point point,Qt::KeyboardModifiers modifiers){
    const bool temporary=modifiers.testFlag(Qt::AltModifier)&&(tool_==Tool::Brush||tool_==Tool::Eraser||tool_==Tool::SpotHealing||tool_==Tool::Gradient);
    if(!colorPicker_&&tool_!=Tool::Eyedropper&&!temporary)return false;
    if(!current()||!current()->document)return true;samplingPalette_=true;sampleOriginal_=colorPicker_?color(colorPicker_->color()):foreground_;updatePalette(point,false);return true;
}
bool MainWindow::updatePalette(Point point,bool finish){
    if(!samplingPalette_)return false;auto*project=current();if(project&&project->document){if(auto sampled=effects_tools::sampleCompositeColor(*project->document,point,SoftwareRenderer())){if(colorPicker_)colorPicker_->sample(*sampled);else{foreground_=color(*sampled);refreshGradient();}}if(canvas()){canvas()->setCursor(Qt::CrossCursor);canvas()->setSampleRing(showSampleRing_?std::optional(point):std::nullopt,bytes(sampleOriginal_),bytes(colorPicker_?color(colorPicker_->color()):foreground_));}}
    if(finish){samplingPalette_=false;if(canvas())canvas()->setSampleRing({});}return true;
}
}
