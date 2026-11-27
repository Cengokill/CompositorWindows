#include "MainWindow.h"
#include "SelectionCursors.h"
#include "SegmentedControl.h"
#include "ToolbarControls.h"
#include "OpacityDigit.h"
#include "editing/PixelEdits.h"
#include "editing/SelectionExtras.h"
#include <QMenuBar>
#include <QToolBar>
#include <QCheckBox>
#include <QLabel>
#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QBuffer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QColorDialog>
#include <QMessageBox>
#include <QStatusBar>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QTabBar>
#include <QComboBox>
#include <QTimer>
#include <QSignalBlocker>
#include <chrono>
#include <QInputDialog>
#include <cmath>

namespace compositor {
namespace {
std::optional<editing::SelectionOutline> outlineOf(const std::optional<Selection>&selection){if(!selection)return {};if(selection->outline)return *selection->outline;if(selection->coverage)return editing::SelectionOutline::fromCoverage(*selection->coverage);return editing::SelectionOutline();}
}
void MainWindow::setupSelectionActions(){
    qApp->installEventFilter(this);
    qApp->installNativeEventFilter(this);
    selectionScrollTimer_=new QChronoTimer(this);selectionScrollTimer_->setTimerType(Qt::PreciseTimer);selectionScrollTimer_->setInterval(std::chrono::nanoseconds(16666667));
    connect(selectionScrollTimer_,&QChronoTimer::timeout,this,[this]{try{stepSelectionAutoscroll();}catch(const std::exception& error){pointerCancel();statusBar()->showMessage(error.what());}});
    auto*menu=menuBar()->addMenu("&Select");
    for(bool expand:{true,false})action(menu,expand?"Expand…":"Contract…",{},[this,expand]{
        auto* p=current();if(!p||!p->document||!p->document->selection)return;
        auto& remembered=expand?selectionExpandAmount_:selectionContractAmount_;bool ok=false;
        const auto amount=QInputDialog::getDouble(this,expand?"Expand Selection":"Contract Selection","Pixels",remembered,1,500,0,&ok);
        if(ok){remembered=int(amount);if(auto* spin=findChild<QSpinBox*>(expand?"selectionExpandAmount":"selectionContractAmount")){const QSignalBlocker block(spin);spin->setValue(remembered);}resizeSelection(expand,amount);}
    });
    action(menu,"All",QKeySequence::SelectAll,[this]{edit("Select All",[&](Document&d){d.selection=editing::rasterSelection(editing::SelectionOutline::rectangle({0,0,double(d.width),double(d.height)}),d.width,d.height);});});
    action(menu,"Deselect",QKeySequence("Ctrl+D"),[this]{edit("Deselect",[](Document&d){d.selection.reset();});});
    action(menu,"Feather…",{},[this]{
        auto*p=current();if(!p||!p->document||!p->document->selection)return;
        bool ok=false;const auto radius=QInputDialog::getDouble(this,"Feather Selection","Radius",selectionFeatherAmount_,0,250,0,&ok);
        if(!ok)return;
        selectionFeatherAmount_=int(radius);if(auto* spin=findChild<QSpinBox*>("selectionFeatherAmount")){const QSignalBlocker block(spin);spin->setValue(selectionFeatherAmount_);}
        featherSelectionBy(radius);
    });
    action(menu,"Color Range…",{},[this]{auto*p=current();auto*l=active();if(!p||!p->document||!l||!l->raster)return;bool ok=false;auto tolerance=QInputDialog::getInt(this,"Color Range","Tolerance",32,0,255,1,&ok);if(!ok)return;edit("Color Range",[l,tolerance](Document&d){auto mask=editing::colorRangeMask(*l->raster,l->raster->width/2,l->raster->height/2,tolerance);if(mask->width!=d.width||mask->height!=d.height)throw std::runtime_error("Color range needs a canvas-sized layer");d.selection=Selection{mask};});});
    action(menu,"Inverse",QKeySequence("Ctrl+Shift+I"),[this]{if(current()&&current()->document&&current()->document->selection)edit("Inverse",[](Document&d){
        if(d.selection->outline)d.selection=editing::rasterSelection(editing::inverseSelection(*d.selection->outline,d.width,d.height),d.width,d.height);
        else if(d.selection->coverage&&d.selection->coverage->source){
            auto input=d.selection->coverage;std::shared_ptr<const editing::SelectionOutline> outline;
            if(auto path=input->source->vectorOutline())outline=std::make_shared<editing::SelectionOutline>(*editing::inverseSelection(*path,d.width,d.height));
            d.selection=Selection{GrayRaster::sampled(d.width,d.height,{0,0,d.width,d.height},[input](int x,int y){return uint8_t(255-input->pixel(x,y));},input->retainedBytes(),std::move(outline))};
        }else{auto gray=std::make_shared<GrayRaster>();gray->width=d.width;gray->height=d.height;gray->pixels.resize(size_t(d.width)*d.height);for(size_t i=0;i<gray->pixels.size();++i)gray->pixels[i]=d.selection->coverage?255-d.selection->coverage->pixels[i]:255;d.selection=Selection{gray};}
    });});
    auto*clipboard=menuBar()->addMenu("&Clipboard");
    action(clipboard,"Cut",QKeySequence::Cut,[this]{copySelection(false,true);});
    action(clipboard,"Copy",QKeySequence::Copy,[this]{copySelection(false);});
    action(clipboard,"Copy Merged",QKeySequence("Ctrl+Shift+C"),[this]{copySelection(true);});
    action(clipboard,"Paste",QKeySequence::Paste,[this]{pasteSelection();});
    action(clipboard,"Layer via Copy",QKeySequence("Ctrl+J"),[this]{copySelection(false,false,true);});
    action(clipboard,"Clear",{},[this]{pixelEdit(int(editing::PixelEdit::Clear));});
    auto*bar=addToolBar("Selection Options");bar->setObjectName("selectionOptions");
    // The tool rail shows these three tools; the actions stay here as command targets so the bar shows only the active tool's options.
    auto*marquee=bar->addAction("Marquee (M)");bindCommand(marquee,"Tools","Marquee (M)",[this]{selectTool(Tool::Marquee);});commands_->releaseShortcut(marquee);marquee->setVisible(false);
    auto*lasso=bar->addAction("Lasso (L)");bindCommand(lasso,"Tools","Lasso (L)",[this]{selectTool(Tool::Lasso);});commands_->releaseShortcut(lasso);lasso->setVisible(false);
    auto*polygon=bar->addAction("Polygonal Lasso");bindCommand(polygon,"Tools","Polygon",[this]{selectTool(Tool::Polygon);});commands_->releaseShortcut(polygon);polygon->setVisible(false);
    ui::addToolbarTitle(bar,"Lasso","selectionToolTitle");
    auto*shape=new ui::SegmentedControl({"Rectangle","Ellipse"});shape->setObjectName("marqueeShape");shape->setAccessibleName("Marquee shape");bar->addWidget(shape);shape->changed=[this](int v){if(selectionGesture_.active())pointerCancel();ellipse_=v==1;selectTool(Tool::Marquee);};
    auto*lassoKind=new ui::SegmentedControl({"Freehand","Polygonal"});lassoKind->setObjectName("lassoKind");lassoKind->setAccessibleName("Lasso kind");bar->addWidget(lassoKind);
    lassoKind->changed=[this](int index){if(selectionGesture_.active())pointerCancel();lassoKind_=index?editing::LassoKind::Polygonal:editing::LassoKind::Freehand;selectTool(Tool::Lasso);};
    auto*wandKind=new ui::SegmentedControl({"Wand","Object"});wandKind->setObjectName("wandKind");wandKind->setAccessibleName("Wand mode");bar->addWidget(wandKind);wandKind->changed=[this](int index){wandObjectMode_=index==1;refresh(false,false);};
    auto*mode=new ui::SegmentedControl({"New","Add","Subtract"});mode->setObjectName("selectionMode");mode->setAccessibleName("Selection mode");bar->addWidget(mode);mode->changed=[this](int i){selectionMode_=editing::SelectionMode(i);};
    // The Wand options sit in this bar too; refresh() shows them only for the Wand tool.
    ui::addToolbarCaption(bar,"Tolerance","wandToleranceCaption");
    auto*tolerance=ui::addNumberField(bar,"wandTolerance","Wand tolerance",0,255,wandTolerance_);connect(tolerance,&QSpinBox::valueChanged,this,[this](int v){wandTolerance_=v;});
    auto*sample=ui::addDropdown(bar,"wandSampleSize","Wand sample size",{"Point Sample","3 by 3 Average","5 by 5 Average"});sample->useGlassPopup();connect(sample,&QComboBox::currentIndexChanged,this,[this](int v){wandSampleRadius_=v;});
    auto*scope=new ui::SegmentedControl({"This Layer","All Layers"});scope->setObjectName("wandScope");scope->setAccessibleName("Wand layers");bar->addWidget(scope);scope->changed=[this](int index){wandAllLayers_=index==1;};
    auto*contiguous=ui::addOptionCheck(bar,"Contiguous","wandContiguous",wandContiguous_);connect(contiguous,&QCheckBox::toggled,this,[this](bool v){wandContiguous_=v;});
    auto*aa=ui::addOptionCheck(bar,"Anti-alias","selectionAntialias",true);connect(aa,&QCheckBox::toggled,this,[this](bool v){selectionAntialias_=v;});
    bar->addSeparator();
    for(bool expand:{true,false}){
        auto* button=bar->addAction(expand?"Expand":"Contract");bindCommand(button,"Select",expand?"Expand…":"Contract…",[this,expand]{resizeSelection(expand,expand?selectionExpandAmount_:selectionContractAmount_);});ui::markPillButton(bar,button);
        auto* amount=ui::addNumberField(bar,expand?"selectionExpandAmount":"selectionContractAmount",expand?"Expand pixels":"Contract pixels",1,500,1,"px");
        connect(amount,&QSpinBox::valueChanged,this,[this,expand](int value){(expand?selectionExpandAmount_:selectionContractAmount_)=value;});
    }
    auto* feather=bar->addAction("Feather");bindCommand(feather,"Select","Feather…",[this]{featherSelectionBy(selectionFeatherAmount_);});ui::markPillButton(bar,feather);
    auto* featherAmount=ui::addNumberField(bar,"selectionFeatherAmount","Feather pixels",0,250,selectionFeatherAmount_,"px");
    connect(featherAmount,&QSpinBox::valueChanged,this,[this](int value){selectionFeatherAmount_=value;});
}
void MainWindow::featherSelectionBy(double radius){
    if(!ui::commandEnabled(ui::CommandGate::ModifySelection,commandState()))return;
    edit("Feather",[radius](Document& d){if(d.selection)d.selection=editing::featherSelection(*d.selection,radius);});
}
void MainWindow::resizeSelection(bool expand,double amount){
    if(!ui::commandEnabled(ui::CommandGate::ModifySelection,commandState()))return;
    edit(expand?"Expand Selection":"Contract Selection",[&](Document& document){auto outline=outlineOf(document.selection);if(outline)document.selection=editing::rasterSelection(outline->resized(expand?amount:-amount,document.width,document.height),document.width,document.height);});
}
bool MainWindow::beginSelection(Point point,Qt::KeyboardModifiers modifiers,int){
    const bool selectionTool=tool_==Tool::Marquee||tool_==Tool::Lasso||tool_==Tool::Polygon||tool_==Tool::Wand;
    if(!selectionTool)return false;
    auto* p=current();if(!p||!p->document||!canEditLayers())return true;
    selectionModifiers_=modifiers;
    if(selectionGesture_.draft()&&selectionGesture_.draft()->kind==editing::LassoKind::Polygonal){
        if(selectionOwner_!=p){cancelSelectionGesture();return true;}
        point=snapGesturePoint(point,modifiers);
        const auto first=p->canvas->viewMapping().toView(selectionGesture_.draft()->points.front());
        const auto here=p->canvas->viewMapping().toView(point);
        if(selectionGesture_.polygonPress(point,std::hypot(here.x-first.x,here.y-first.y))==editing::PolygonPressResult::FinishRequested)finishSelectionGesture();
        else refreshSelectionGesture();
        return true;
    }
    const auto mode=editing::selectionMode(modifiers.testFlag(Qt::ShiftModifier),modifiers.testFlag(Qt::AltModifier),selectionMode_);
    const auto before=outlineOf(p->document->selection);
    const bool inside=!selectionGesture_.active()&&before&&!before->empty()&&before->contains(point);
    if(mode==editing::SelectionMode::Replace&&inside){
        finishOpacityEdit();selectionOwner_=p;selectionBefore_=p->document->selection;press_=point;movingSelection_=true;
        selectionMoveBox_=before->bounds();
        p->history.begin("Move Selection",p->document,p->active);refreshSelectionGesture();return true;
    }
    if(tool_==Tool::Wand){runWand(point,modifiers);return true;}
    selectionPointer_=point;
    if(tool_==Tool::Marquee||tool_==Tool::Polygon)point=snapGesturePoint(point,modifiers);
    finishOpacityEdit();selectionOwner_=p;selectionBefore_.reset();press_=point;
    const auto kind=tool_==Tool::Marquee?(ellipse_?editing::LassoKind::Ellipse:editing::LassoKind::Rectangle):tool_==Tool::Polygon?editing::LassoKind::Polygonal:editing::LassoKind::Freehand;
    selectionGesture_.begin(point,kind,mode,modifiers.testFlag(Qt::ShiftModifier));refresh(false,false);return true;
}
void MainWindow::finishSelectionGesture(){
    stopSelectionAutoscroll();auto* p=selectionOwner_;
    if(!p||p!=current()||!p->document){cancelSelectionGesture();return;}
    const auto before=outlineOf(p->document->selection);
    const bool marquee=selectionGesture_.marquee();
    auto result=selectionGesture_.finish(before,p->document->width,p->document->height,selectionAntialias_);
    selectionOwner_=nullptr;pointerOwner_=nullptr;p->canvas->setSelectionDraft({});p->canvas->setSnapGuides();
    if(result&&result->requestsEdit){
        const bool same=(!before&&!result->selection)||(before&&result->selection&&before->geometricallyEquals(*result->selection));
        if(!same){
            const auto selection=editing::rasterSelection(result->selection,p->document->width,p->document->height);
            p->history.begin(result->historyName,p->document,p->active);p->document->selection=selection;p->history.end(p->document,p->active);
        }
    }
    refresh(false,false);
    // Space outlasting the Marquee it was moving goes back to panning.
    if(marquee&&spaceHeld_)p->canvas->setCursor(Qt::OpenHandCursor);
}
bool MainWindow::updateSelection(Point point,Qt::KeyboardModifiers modifiers,bool finish){
    if(!movingSelection_&&!selectionGesture_.active())return tool_==Tool::Marquee||tool_==Tool::Lasso||tool_==Tool::Polygon||tool_==Tool::Wand;
    auto* p=selectionOwner_;if(!p||p!=current()||!p->document){cancelSelectionGesture();return true;}
    selectionModifiers_=modifiers;
    if(movingSelection_){
        if(!finish){
            auto offset=editing::selectionMoveOffset(press_,point,modifiers.testFlag(Qt::ShiftModifier));
            // Snapping follows the moved selection bounds. Shift's locked axis never snaps.
            if(selectionMoveBox_&&!modifiers.testFlag(Qt::ControlModifier))if(auto lines=snapLines(*p->document)){
                const auto& box=*selectionMoveBox_;
                auto snap=editing_transform::snapOffset({box.x+offset.x,box.y+offset.y,box.width,box.height},*lines,8/std::max(p->canvas->pointsPerPixel(),.0001));
                if(modifiers.testFlag(Qt::ShiftModifier)){if(offset.y==0){snap.offset.y=0;snap.y.reset();}else{snap.offset.x=0;snap.x.reset();}}
                // A grid-only snap moves without a guide.
                if(snap.x||snap.y||snap.offset.x!=0||snap.offset.y!=0){offset.x=std::round(offset.x+snap.offset.x);offset.y=std::round(offset.y+snap.offset.y);}
                p->canvas->setSnapGuides(snap.x,snap.y);
            }
            if(std::round(offset.x)==0&&std::round(offset.y)==0)p->document->selection=selectionBefore_;
            else p->document->selection=editing::moveSelectionCoverage(selectionBefore_,offset);
            updateSelectionAutoscroll(p->canvas->viewMapping().toView(point));refresh(false,false);return true;
        }
        stopSelectionAutoscroll();p->canvas->setSnapGuides();const bool moved=p->document->selection!=selectionBefore_;
        p->history.end(p->document,p->active);movingSelection_=false;selectionBefore_.reset();selectionMoveBox_.reset();selectionOwner_=nullptr;
        if(!moved){if(tool_==Tool::Wand)runWand(press_,modifiers,editing::SelectionMode::Replace);else{p->history.begin("Deselect",p->document,p->active);p->document->selection.reset();p->history.end(p->document,p->active);}}
        refresh(false,false);return true;
    }
    if(finish){stopSelectionAutoscroll();if(selectionGesture_.draft()->kind!=editing::LassoKind::Polygonal)finishSelectionGesture();return true;}
    // A Space move and autoscroll follow the real pointer; only resizing snaps.
    selectionPointer_=point;
    if(selectionGesture_.movingMarquee())p->canvas->setSnapGuides();
    else if(selectionGesture_.marquee())point=snapGesturePoint(point,modifiers);
    selectionGesture_.move(point,modifiers.testFlag(Qt::ShiftModifier));
    if(selectionGesture_.marquee())updateSelectionAutoscroll(p->canvas->viewMapping().toView(selectionPointer_));
    refreshSelectionGesture();return true;
}
void MainWindow::finishPolygon(){if(selectionGesture_.draft()&&selectionGesture_.draft()->kind==editing::LassoKind::Polygonal)finishSelectionGesture();}
void MainWindow::cancelSelectionGesture(){
    stopSelectionAutoscroll();if(selectionOwner_){selectionOwner_->canvas->setSelectionDraft({});selectionOwner_->canvas->setSnapGuides();}
    selectionGesture_.cancel();selectionOwner_=nullptr;selectionBefore_.reset();selectionMoveBox_.reset();movingSelection_=false;refreshSelectionGesture();if(commands_)commands_->refresh();
}
void MainWindow::refreshSelectionGesture(){
    auto* p=current();if(p&&p->canvas)p->canvas->setSelectionDraft(selectionOwner_==p?selectionGesture_.draft():std::nullopt);
    const auto mode=selectionGesture_.cursorMode(selectionModifiers_.testFlag(Qt::ShiftModifier),selectionModifiers_.testFlag(Qt::AltModifier),selectionMode_);
    if(auto* box=ui::findSegmentedControl(this,"selectionMode"))box->setCurrentIndex(int(mode));
    if(p&&p->canvas){
        p->canvas->setProperty("selectionCursorMode",int(mode));
        if((tool_==Tool::Marquee||tool_==Tool::Lasso||tool_==Tool::Polygon||tool_==Tool::Wand)&&!spaceHeld_&&!spaceDragging_){
            if(movingSelection_)p->canvas->setCursor(Qt::SizeAllCursor);
            else if(tool_==Tool::Wand)p->canvas->setCursor(Qt::CrossCursor);
            else{
                const auto kind=tool_==Tool::Marquee?(ellipse_?editing::LassoKind::Ellipse:editing::LassoKind::Rectangle):tool_==Tool::Polygon?editing::LassoKind::Polygonal:editing::LassoKind::Freehand;
                p->canvas->setCursor(ui::selectionToolCursor(kind,mode,p->canvas->devicePixelRatioF()));
            }
        }
    }
}
void MainWindow::hoverSelection(QPointF view,Qt::KeyboardModifiers flags){
    selectionModifiers_=flags;
    if(selectionGesture_.draft()&&selectionGesture_.draft()->kind==editing::LassoKind::Polygonal&&selectionOwner_==current()){
        const auto point=selectionOwner_->canvas->documentPoint(view);selectionGesture_.moveCursor(snapGesturePoint(Point{point.x(),point.y()},flags));
    }
    refreshSelectionGesture();
}
void MainWindow::selectionModifiersChanged(Qt::KeyboardModifiers flags){
    selectionModifiers_=flags;selectionGesture_.modifiersChanged(flags.testFlag(Qt::ShiftModifier));
    if(movingSelection_&&selectionScrollPoint_&&selectionOwner_==current()){
        const auto pixel=selectionOwner_->canvas->documentPoint({selectionScrollPoint_->x,selectionScrollPoint_->y});updateSelection({pixel.x(),pixel.y()},flags,false);
    }
    refreshSelectionGesture();
}
void MainWindow::updateSelectionAutoscroll(Point point){
    selectionScrollPoint_=point;if(!selectionOwner_||(!movingSelection_&&!selectionGesture_.marquee()))return;
    auto* view=selectionOwner_->canvas;const auto visible=view->visibleRegion().boundingRect();
    const auto delta=editing::selectionAutoscrollDelta({double(visible.x()),double(visible.y()),double(visible.width()),double(visible.height())},point);
    if(delta==Point{})selectionScrollTimer_->stop();else if(!selectionScrollTimer_->isActive())selectionScrollTimer_->start();
}
void MainWindow::stopSelectionAutoscroll(){if(selectionScrollTimer_)selectionScrollTimer_->stop();selectionScrollPoint_.reset();}
void MainWindow::stepSelectionAutoscroll(){
    if(!selectionScrollPoint_||!selectionOwner_||selectionOwner_!=current()||(!movingSelection_&&!selectionGesture_.marquee())){stopSelectionAutoscroll();return;}
    auto* view=selectionOwner_->canvas;const auto visible=view->visibleRegion().boundingRect();const auto point=*selectionScrollPoint_;
    const auto delta=editing::selectionAutoscrollDelta({double(visible.x()),double(visible.y()),double(visible.width()),double(visible.height())},point);
    if(delta==Point{}){stopSelectionAutoscroll();return;}
    view->panBy({delta.x,delta.y});const auto pixel=view->documentPoint({point.x,point.y});
    updateSelection({pixel.x(),pixel.y()},selectionModifiers_,false);
}
bool MainWindow::eventFilter(QObject* watched,QEvent* event){
    if(tabs_&&(event->type()==QEvent::DragEnter||event->type()==QEvent::DragMove||event->type()==QEvent::Drop)){
        auto* widget=qobject_cast<QWidget*>(watched);
        auto* bar=tabs_->tabBar();
        auto* corner=tabs_->cornerWidget(Qt::TopRightCorner);
        const bool tabStrip=widget&&widget->window()==this&&((bar&&(widget==bar||bar->isAncestorOf(widget)))||(corner&&(widget==corner||corner->isAncestorOf(widget))));
        if(tabStrip){
            auto* drag=static_cast<QDropEvent*>(event);
            const QPoint where=widget->mapTo(this,drag->position().toPoint());
            if(event->type()==QEvent::Drop){
                QDropEvent forwarded(where,drag->possibleActions(),drag->mimeData(),drag->buttons(),drag->modifiers());
                dropEvent(&forwarded);
                drag->setDropAction(forwarded.dropAction());
                if(forwarded.isAccepted())drag->accept();else drag->ignore();
            }else{
                acceptWorkspaceDrag(drag,where);
                // A rejected DragEnter leaves no Qt target, so later DragMove events never arrive.
                // Accept every move with a one-pixel answer and use IgnoreAction until the tab is legal.
                if(auto* move=dynamic_cast<QDragMoveEvent*>(drag)){
                    const QRect here(move->position().toPoint(),QSize(1,1));
                    const bool allowed=move->isAccepted()&&move->dropAction()!=Qt::IgnoreAction;
                    if(!allowed)move->setDropAction(Qt::IgnoreAction);
                    move->accept(here);
                }
            }
            return true;
        }
    }
    // Escape leaves canvas-only mode before the canvas or text handlers can cancel a crop, gradient, transform, or text draft.
    // A held canvas gesture keeps Escape, which cancels that gesture as usual.
    if(canvasOnly_&&event->type()==QEvent::KeyPress&&static_cast<QKeyEvent*>(event)->key()==Qt::Key_Escape){
        auto* widget=qobject_cast<QWidget*>(watched);
        if(widget&&widget->window()==this&&!(canvas()&&canvas()->pointerHeld())){setCanvasOnly(false);return true;}
    }
    if(event->type()==QEvent::ShortcutOverride){
        auto* focus=QApplication::focusWidget();
        const bool searchingFonts=focus&&textFontPopup_&&textFontPopup_->isVisible()&&textFontPopup_->isAncestorOf(focus);
        if(searchingFonts||(textSession_&&!qobject_cast<QLineEdit*>(focus)&&!qobject_cast<QAbstractSpinBox*>(focus)&&!qobject_cast<QComboBox*>(focus))){
            auto* key=static_cast<QKeyEvent*>(event);
            if(key->key()==Qt::Key_Escape||ui::reservesTextShortcut(*key)){event->accept();return true;}
        }
    }
    if(event->type()==QEvent::Hide&&textStyleBox_&&textStyleBox_->view()&&watched==textStyleBox_->view()->window()){
        if(auto* timer=textStyleBox_->findChild<QTimer*>("textStylePreviewTimer"))timer->stop();
        endTextFontPreview();
        QTimer::singleShot(0,this,[this]{
            if((textFontPopup_&&textFontPopup_->isVisible())||(textStyleBox_&&textStyleBox_->view()&&textStyleBox_->view()->isVisible()))return;
            auto* focus=QApplication::focusWidget();
            if(textStyleBox_&&(focus==textStyleBox_||focus==textStyleBox_->lineEdit()))return;
            focusTextCanvas();
        });
    }
    if(event->type()==QEvent::KeyPress||event->type()==QEvent::KeyRelease){
        auto* widget=qobject_cast<QWidget*>(watched);if(widget&&widget->window()==this){auto* key=static_cast<QKeyEvent*>(event);
            if(key->key()==Qt::Key_Shift||key->key()==Qt::Key_Alt||key->key()==Qt::Key_Control){
                auto flags=key->modifiers();const auto flag=key->key()==Qt::Key_Shift?Qt::ShiftModifier:key->key()==Qt::Key_Alt?Qt::AltModifier:Qt::ControlModifier;
                flags.setFlag(flag,event->type()==QEvent::KeyPress);
                try{selectionModifiersChanged(flags);}catch(const std::exception& error){pointerCancel();statusBar()->showMessage(error.what());}
            }else if(event->type()==QEvent::KeyPress&&!textSession_&&widget->objectName()=="layersTree"&&ui::plainDigit(key)>=0){
                // The Layers list would take a digit as type-ahead search; let the window use it for the opacity shortcut first.
                // Not while text is being edited: the window would type the digit into the text.
                const std::unique_ptr<QKeyEvent> forwarded(key->clone());keyPressEvent(forwarded.get());
                if(forwarded->isAccepted())return true;
            }
        }
    }
    return QMainWindow::eventFilter(watched,event);
}
void MainWindow::pixelEdit(int operation){
    if(!current()||!current()->document||!active())return;
    QColor color=foreground_;auto kind=editing::PixelEdit(operation);
    if(kind==editing::PixelEdit::Fill){color=QColorDialog::getColor(foreground_,this,"Fill");if(!color.isValid())return;foreground_=color;}
    edit(kind==editing::PixelEdit::Fill?"Fill":kind==editing::PixelEdit::Invert?"Invert":"Clear",[&](Document&d){*active()=editing::editLayer(*active(),d,kind,{uint8_t(color.red()),uint8_t(color.green()),uint8_t(color.blue()),255},current()->maskSelected);});
}
void MainWindow::addPixelLayer(std::shared_ptr<const Raster> raster,Point origin,const char* operation){
    if(!raster||!canEditLayers())return;
    edit(operation,[&](Document& document){
        Layer layer;layer.id=newId();layer.raster=std::move(raster);layer.transform={origin.x,origin.y,double(layer.raster->width),double(layer.raster->height)};
        int number=1;while(std::any_of(document.layers.begin(),document.layers.end(),[&](const Layer& item){return item.name=="Layer "+std::to_string(number);}))++number;layer.name="Layer "+std::to_string(number);
        auto selected=std::find_if(document.layers.begin(),document.layers.end(),[&](const Layer& item){return item.id==current()->active;});size_t insertion=document.layers.size();
        if(selected!=document.layers.end()){insertion=size_t(selected-document.layers.begin())+1;layer.parentId=selected->group?selected->id:selected->parentId;}
        current()->active=layer.id;current()->selected={layer.id};current()->maskSelected=false;
        document.layers.insert(document.layers.begin()+insertion,std::move(layer));document.selection.reset();
    });
}
void MainWindow::copySelection(bool merged,bool cut,bool viaLayer){
    auto*p=current();if(!p||!p->document||(!merged&&!active()))return;
    if(viaLayer&&!p->document->selection){layerCommand(3);return;}
    auto result=editing::copyPixels(*p->document,merged?std::nullopt:std::optional<std::string_view>(p->active),SoftwareRenderer(),nullptr,p->maskSelected);if(!result)return;
    if(viaLayer){addPixelLayer(result->raster,result->origin,"Layer via Copy");return;}
    auto bytes=result->raster->rgba();QImage image(bytes.data(),result->raster->width,result->raster->height,result->raster->width*4,QImage::Format_RGBA8888_Premultiplied);auto*mime=new QMimeData;mime->setImageData(image.copy());QByteArray png;QBuffer buffer(&png);buffer.open(QIODevice::WriteOnly);image.save(&buffer,"PNG");mime->setData("image/png",png);mime->setData("application/x-compositor-origin",QJsonDocument(QJsonObject{{"x",result->origin.x},{"y",result->origin.y}}).toJson());QApplication::clipboard()->setMimeData(mime);
    if(cut)pixelEdit(int(editing::PixelEdit::Clear));
}
void MainWindow::pasteSelection(){
    const auto*mime=QApplication::clipboard()->mimeData();auto image=QApplication::clipboard()->image();if(image.isNull()&&mime->hasFormat("image/png"))image=QImage::fromData(mime->data("image/png"));if(image.isNull())return;
    image=image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);auto raster=Raster::fromRgba(image.width(),image.height(),image.constBits(),size_t(image.bytesPerLine()));
    auto* project=current();if(!project||!project->document)return;
    const auto& document=*project->document;Point origin{std::floor((document.width-raster->width)/2.),std::floor((document.height-raster->height)/2.)};
    if(mime->hasFormat("application/x-compositor-origin")){const auto data=QJsonDocument::fromJson(mime->data("application/x-compositor-origin")).object();origin={data["x"].toDouble(),data["y"].toDouble()};}
    addPixelLayer(std::move(raster),origin,"Paste");
}
}
