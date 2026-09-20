#include "MainWindow.h"
#include "LayerPanel.h"
#include "filters/PixelFilters.h"
#include <QApplication>
#include <QKeyEvent>
#include <QSignalBlocker>
#include <QStatusBar>
#include <algorithm>

namespace compositor {
namespace {
bool hasSelection(const Document& document){if(!document.selection)return false;if(document.selection->outline)return !document.selection->outline->empty();const auto& coverage=document.selection->coverage;return coverage&&std::any_of(coverage->pixels.begin(),coverage->pixels.end(),[](uint8_t value){return value!=0;});}
bool visible(const Document& document,const std::string& id){const auto entries=layers::entries(document);return std::any_of(entries.begin(),entries.end(),[&](const layers::Entry& item){return item.id==id&&item.visible;});}
}

bool MainWindow::handleEditingKey(QKeyEvent* event){
    const auto key=event->key();const auto modifiers=event->modifiers();
    const bool shift=modifiers.testFlag(Qt::ShiftModifier),control=modifiers.testFlag(Qt::ControlModifier);
    const bool plain=(modifiers&~Qt::ShiftModifier)==Qt::NoModifier;
    const bool arrow=key==Qt::Key_Left||key==Qt::Key_Right||key==Qt::Key_Up||key==Qt::Key_Down;
    const bool remove=key==Qt::Key_Delete||key==Qt::Key_Backspace;
    auto* project=current();auto* layer=active();
    // EditorCanvas swallows keys during a raster stroke, except Escape cancellation.
    if(stroke_||retouch_)return key!=Qt::Key_Escape;
    const bool canPaint=canEditLayers()&&project&&project->document&&layer&&layerSelection().ids.size()==1&&
        (!project->document->selection||hasSelection(*project->document))&&visible(*project->document,layer->id)&&
        (project->maskSelected?(layer->mask&&layer->mask->enabled):(!layer->group&&layer->adjustmentJson.empty()));
    try{
        if(remove&&(modifiers==Qt::AltModifier||modifiers==Qt::ControlModifier)){
            if(canPaint){const bool background=control;const auto color=background?background_:foreground_;const uint8_t gray=(background?!maskPaintWhite_:maskPaintWhite_)?255:0;
                const Pixel value=project->maskSelected?Pixel{gray,gray,gray,255}:Pixel{uint8_t(color.red()),uint8_t(color.green()),uint8_t(color.blue()),255};
                finishOpacityEdit();edit(project->maskSelected?"Fill Mask":"Fill",[&](Document& document){*active()=editing::editLayer(*active(),document,editing::PixelEdit::Fill,value,project->maskSelected);});
            }return true;
        }
        if(remove&&modifiers==Qt::ShiftModifier){
            if(canPaint&&!project->maskSelected&&layer->raster&&hasSelection(*project->document))runFilter(int(filters::Kind::ContentAwareFill));
            return true;
        }
        if(arrow&&project&&project->document){
            const double step=shift?10:1;const Point delta{key==Qt::Key_Left?-step:key==Qt::Key_Right?step:0,key==Qt::Key_Up?-step:key==Qt::Key_Down?step:0};
            if(control&&(modifiers&~(Qt::ControlModifier|Qt::ShiftModifier))==Qt::NoModifier&&hasSelection(*project->document)&&!selectionGesture_.active()){
                if(canPaint&&!project->maskSelected&&layer->raster){
                    editing_transform::PixelTransformSession session(*layer,project->document->selection->coverage);
                    if(session.begin()){session.move(delta);auto result=session.apply();const auto selection=editing::moveSelectionCoverage(project->document->selection,delta);finishOpacityEdit();
                        edit("Move Pixels",[&](Document& document){auto& target=*active();const auto source=target.raster;target=std::move(result.layer);if(target.raster!=source)target.shapeJson.clear();document.selection=selection;});}
                }return true;
            }
            const bool selectionTool=tool_==Tool::Marquee||tool_==Tool::Lasso||tool_==Tool::Polygon||tool_==Tool::Wand;
            if(plain&&selectionTool&&!selectionGesture_.active()&&hasSelection(*project->document)){
                if(canEditLayers()){finishOpacityEdit();edit("Move Selection",[&](Document& document){document.selection=editing::moveSelectionCoverage(document.selection,delta);});}return true;
            }
            if(plain&&tool_==Tool::Move){
                const bool alreadyEditing=bool(transformSession_);
                if((alreadyEditing||canEditLayers())&&startTransformSession(false)){
                    auto& state=*transformSession_;state.draft.x+=delta.x;state.draft.y+=delta.y;
                    if(state.corners)for(auto& point:*state.corners){point.x+=delta.x;point.y+=delta.y;}
                    publishTransformSession(false);if(!alreadyEditing)applyTransformSession();else refresh();
                }return true;
            }
        }
        if(remove&&modifiers==Qt::NoModifier){
            if(tool_==Tool::Polygon&&selectionGesture_.active()){
                selectionGesture_.removeLast();if(!selectionGesture_.active()){pointerCancel();}else refreshSelectionGesture();return true;
            }
            if(!project||!project->document)return true;
            if(project->document->selection){
                if(canPaint&&(project->maskSelected||layer->raster)){
                    const uint8_t maskBackground=maskPaintWhite_?0:255;finishOpacityEdit();
                    edit(project->maskSelected?"Fill Mask":"Clear",[&](Document& document){*active()=editing::editLayer(*active(),document,editing::PixelEdit::Clear,{},project->maskSelected,maskBackground);});
                }
            }else if(canEditLayers()){
                if(project->maskSelected&&layer&&layer->mask&&layerSelection().ids.size()<=1)maskCommand(int(ui::MaskCommand::Delete));else layerCommand(6);
            }return true;
        }
        if(key==Qt::Key_Space){spaceHeld_=true;refreshBrushPointer();if(canvas())canvas()->setCursor(spaceDragging_?Qt::ClosedHandCursor:Qt::OpenHandCursor);return true;}
        if(plain&&shift&&(key==Qt::Key_Plus||key==Qt::Key_Equal||key==Qt::Key_Minus||key==Qt::Key_Underscore)){
            if(canEditAppearance()){const bool forward=key==Qt::Key_Plus||key==Qt::Key_Equal;const int count=int(blendNames.size()),index=int(active()->blend);finishOpacityEdit();edit("Layer Blend Mode",[&](Document&){active()->blend=Blend((index+(forward?1:count-1))%count);});}return true;
        }
        if(plain&&shift&&key==Qt::Key_U){
            if(tool_==Tool::Shape){if(!shapeDraftId_.empty())pointerCancel();shapeStyle_.kind=shapeStyle_.kind==editing::ShapeKind::Rectangle?editing::ShapeKind::Ellipse:editing::ShapeKind::Rectangle;if(auto* combo=findChild<QComboBox*>("shapeKind")){QSignalBlocker block(combo);combo->setCurrentIndex(int(shapeStyle_.kind));}refresh(false,false);}else selectTool(Tool::Shape);return true;
        }
    }catch(const std::exception& error){if(transformSession_&&!transformSession_->persistent)cancelTransformSession();statusBar()->showMessage(error.what());return true;}
    return false;
}

bool MainWindow::beginTemporaryHand(Point point){
    if(!spaceHeld_)return false;if(!canvas())return true;
    spaceDragging_=true;spacePress_={point.x,point.y};canvas()->setCursor(Qt::ClosedHandCursor);return true;
}
bool MainWindow::updateTemporaryHand(Point point,bool finish){
    if(!spaceDragging_)return false;if(canvas())canvas()->panBy((QPointF(point.x,point.y)-spacePress_)*canvas()->pointsPerPixel());
    if(finish){spaceDragging_=false;if(canvas()){if(spaceHeld_)canvas()->setCursor(Qt::OpenHandCursor);else canvas()->unsetCursor();}refreshBrushPointer();}return true;
}
void MainWindow::cancelTemporaryHand(){spaceHeld_=spaceDragging_=false;if(canvas())canvas()->unsetCursor();}
void MainWindow::keyReleaseEvent(QKeyEvent* event){if(event->key()==Qt::Key_Space){if(!event->isAutoRepeat()){spaceHeld_=false;if(!spaceDragging_&&canvas())canvas()->unsetCursor();refreshBrushPointer();}event->accept();return;}QMainWindow::keyReleaseEvent(event);}
void MainWindow::changeEvent(QEvent* event){if(event->type()==QEvent::ActivationChange&&!isActiveWindow())cancelTemporaryHand();QMainWindow::changeEvent(event);}

void MainWindow::interruptPointer(){
    stopSelectionAutoscroll();
    cancelTemporaryHand();cancelBrushTip();samplingPalette_=false;zoomDragging_=false;if(canvas())canvas()->setSampleRing({});
    // Focus changes stop dragging without applying/cancelling a pending gradient.
    if(drawingOriginal_){gradientHandle_=-1;pointerOwner_=nullptr;refresh(false,false);return;}
    if(transformSession_&&transformSession_->persistent){
        if(transformDrag_){transformSession_->draft=transformDrag_->original;transformSession_->duplicateOnDrag=false;transformDrag_.reset();
            // The pinned source restores the affine draft but retains current corners.
            try{publishTransformSession(false);}catch(const std::exception& error){cancelTransformSession();statusBar()->showMessage(error.what());}}
        pointerOwner_=nullptr;refresh();return;
    }
    if(cropDraft_){cropDrag_.reset();pointerOwner_=nullptr;refresh(false,false);return;}
    if(movingSelection_&&current()){auto* project=current();project->history.end(project->document,project->active);movingSelection_=false;selectionBefore_.reset();cancelSelectionGesture();pointerOwner_=nullptr;refresh(false);return;}
    if(tool_==Tool::Polygon&&selectionGesture_.active()){pointerOwner_=nullptr;return;}
    pointerCancel();
}
}
