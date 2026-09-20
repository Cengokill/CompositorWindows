#include "MainWindow.h"
#include <algorithm>
#include <cmath>

namespace compositor {
void MainWindow::updateBrushPointer(QPointF logical,Qt::KeyboardModifiers modifiers){
    brushPointer_=brushTipDrag_?brushTipDrag_->start:logical;brushPointerModifiers_=modifiers;
    refreshBrushPointer();
}
void MainWindow::clearBrushPointer(){brushPointer_.reset();refreshBrushPointer();}
void MainWindow::refreshBrushPointer(){
    auto* view=canvas();if(!view)return;
    const bool brush=tool_==Tool::Brush||tool_==Tool::Eraser||tool_==Tool::SpotHealing||tool_==Tool::CloneStamp||tool_==Tool::Blur;
    const bool option=brushPointerModifiers_.testFlag(Qt::AltModifier);
    const bool picking=bool(colorPicker_)||samplingPalette_||(option&&(tool_==Tool::Brush||tool_==Tool::Eraser||tool_==Tool::SpotHealing));
    if(!brush||spaceHeld_||spaceDragging_||picking||!brushPointer_){view->setBrushCursor({},1);if(view->cursor().shape()==Qt::BlankCursor)view->unsetCursor();return;}
    const double radius=tool_==Tool::CloneStamp?cloneSettings_.radius:tool_==Tool::Blur?blurSettings_.radius:brushSettings_.radius;
    const double hardness=tool_==Tool::CloneStamp?cloneSettings_.hardness:tool_==Tool::Blur?blurSettings_.hardness:brushSettings_.hardness;
    std::optional<Point> marker;
    if(tool_==Tool::CloneStamp&&current()&&current()->document){const auto document=view->documentPoint(*brushPointer_);if(const auto source=current()->cloneAlignment.samplePoint({document.x(),document.y()},bool(retouch_)))marker=view->viewMapping().toView(*source);}
    const auto shownHardness=brushTipDrag_&&brushTipDrag_->hardnessShown?std::optional(hardness):std::nullopt;
    view->setBrushCursor(Point{brushPointer_->x(),brushPointer_->y()},std::max(1.,2*radius*view->pointsPerPixel()),shownHardness,marker);
    view->setCursor(Qt::BlankCursor);
}
bool MainWindow::beginBrushTip(QPointF logical,Qt::KeyboardModifiers modifiers){
    const bool brush=tool_==Tool::Brush||tool_==Tool::Eraser||tool_==Tool::SpotHealing||tool_==Tool::CloneStamp||tool_==Tool::Blur;
    if(!brush||stroke_||retouch_||spaceHeld_||spaceDragging_)return false;
    const double radius=tool_==Tool::CloneStamp?cloneSettings_.radius:tool_==Tool::Blur?blurSettings_.radius:brushSettings_.radius;
    const double hardness=tool_==Tool::CloneStamp?cloneSettings_.hardness:tool_==Tool::Blur?blurSettings_.hardness:brushSettings_.hardness;
    brushTipDrag_=BrushTipDrag{logical,radius,hardness,modifiers.testFlag(Qt::ShiftModifier)};
    updateBrushPointer(logical,modifiers);return true;
}
bool MainWindow::updateBrushTip(QPointF logical,Qt::KeyboardModifiers modifiers,bool finish){
    if(!brushTipDrag_)return false;
    if(finish){brushTipDrag_.reset();updateBrushPointer(logical,modifiers);return true;}
    if(!canvas())return true;
    auto& drag=*brushTipDrag_;drag.hardnessShown=modifiers.testFlag(Qt::ShiftModifier);
    const double delta=logical.x()-drag.start.x();
    double* radius=tool_==Tool::CloneStamp?&cloneSettings_.radius:tool_==Tool::Blur?&blurSettings_.radius:&brushSettings_.radius;
    double* hardness=tool_==Tool::CloneStamp?&cloneSettings_.hardness:tool_==Tool::Blur?&blurSettings_.hardness:&brushSettings_.hardness;
    if(drag.hardnessShown){*hardness=std::clamp(drag.hardness+delta/200,0.,1.);*radius=drag.radius;}
    else{*radius=std::clamp(std::round(2*drag.radius+2*delta/std::max(.0001,canvas()->pointsPerPixel())),1.,2000.)/2;*hardness=drag.hardness;}
    updateBrushPointer(drag.start,modifiers);refreshBrushControls();refreshRetouchControls();return true;
}
void MainWindow::cancelBrushTip(){brushTipDrag_.reset();brushPointer_.reset();if(canvas())canvas()->setBrushCursor({},1);}
bool MainWindow::canvasNavigationAllowed(){return !transformDrag_&&!cropDrag_&&!stroke_&&!retouch_;}
}
