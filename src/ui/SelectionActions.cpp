#include "MainWindow.h"
#include "editing/PixelEdits.h"
#include <QMenuBar>
#include <QToolBar>
#include <QCheckBox>
#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QBuffer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QColorDialog>
#include <QMessageBox>
#include <QStatusBar>
#include <QKeyEvent>
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
    selectionScrollTimer_=new QChronoTimer(this);selectionScrollTimer_->setTimerType(Qt::PreciseTimer);selectionScrollTimer_->setInterval(std::chrono::nanoseconds(16666667));
    connect(selectionScrollTimer_,&QChronoTimer::timeout,this,[this]{try{stepSelectionAutoscroll();}catch(const std::exception& error){pointerCancel();statusBar()->showMessage(error.what());}});
    auto*menu=menuBar()->addMenu("&Select");
    for(bool expand:{true,false})action(menu,expand?"Expand…":"Contract…",{},[this,expand]{auto*p=current();if(!p||!p->document||!p->document->selection)return;bool ok=false;auto amount=QInputDialog::getDouble(this,expand?"Expand Selection":"Contract Selection","Pixels",1,1,500,0,&ok);if(ok)edit(expand?"Expand Selection":"Contract Selection",[&](Document&d){auto outline=outlineOf(d.selection);if(outline)d.selection=editing::rasterSelection(outline->resized(expand?amount:-amount,d.width,d.height),d.width,d.height);});});
    action(menu,"All",QKeySequence::SelectAll,[this]{edit("Select All",[&](Document&d){d.selection=editing::rasterSelection(editing::SelectionOutline::rectangle({0,0,double(d.width),double(d.height)}),d.width,d.height);});});
    action(menu,"Deselect",QKeySequence("Ctrl+D"),[this]{edit("Deselect",[](Document&d){d.selection.reset();});});
    action(menu,"Inverse",QKeySequence("Ctrl+Shift+I"),[this]{if(current()&&current()->document&&current()->document->selection)edit("Inverse",[](Document&d){if(d.selection->outline)d.selection=editing::rasterSelection(editing::inverseSelection(*d.selection->outline,d.width,d.height),d.width,d.height);else{auto gray=std::make_shared<GrayRaster>();gray->width=d.width;gray->height=d.height;gray->pixels.resize(size_t(d.width)*d.height);for(size_t i=0;i<gray->pixels.size();++i)gray->pixels[i]=d.selection->coverage?255-d.selection->coverage->pixels[i]:255;d.selection=Selection{gray};}});});
    auto*clipboard=menuBar()->addMenu("&Clipboard");
    action(clipboard,"Cut",QKeySequence::Cut,[this]{copySelection(false,true);});
    action(clipboard,"Copy",QKeySequence::Copy,[this]{copySelection(false);});
    action(clipboard,"Copy Merged",QKeySequence("Ctrl+Shift+C"),[this]{copySelection(true);});
    action(clipboard,"Paste",QKeySequence::Paste,[this]{pasteSelection();});
    action(clipboard,"Layer via Copy",QKeySequence("Ctrl+J"),[this]{copySelection(false,false,true);});
    action(clipboard,"Clear",{},[this]{pixelEdit(int(editing::PixelEdit::Clear));});
    auto*bar=addToolBar("Selection Options");bar->setObjectName("selectionOptions");
    auto*marquee=bar->addAction("Marquee (M)");bindCommand(marquee,"Tools","Marquee (M)",[this]{selectTool(Tool::Marquee);});marquee->setShortcut({});
    auto*shape=new QComboBox;shape->addItems({"Rectangle","Ellipse"});shape->setAccessibleName("Marquee shape");bar->addWidget(shape);connect(shape,&QComboBox::currentIndexChanged,this,[this](int v){if(selectionGesture_.active())pointerCancel();ellipse_=v==1;selectTool(Tool::Marquee);});
    auto*lasso=bar->addAction("Lasso (L)");bindCommand(lasso,"Tools","Lasso (L)",[this]{selectTool(Tool::Lasso);});lasso->setShortcut({});
    auto*polygon=bar->addAction("Polygonal Lasso");bindCommand(polygon,"Tools","Polygon",[this]{selectTool(Tool::Polygon);});polygon->setShortcut({});
    auto*mode=new QComboBox;mode->setObjectName("selectionMode");mode->addItems({"New selection","Add","Subtract"});mode->setAccessibleName("Selection mode");bar->addWidget(mode);connect(mode,&QComboBox::currentIndexChanged,this,[this](int i){selectionMode_=editing::SelectionMode(i);});
    auto*aa=new QCheckBox("Antialias");aa->setChecked(true);bar->addWidget(aa);connect(aa,&QCheckBox::toggled,this,[this](bool v){selectionAntialias_=v;});
    auto*wandBar=addToolBar("Magic Wand Options");wandBar->setObjectName("wandOptions");auto*tolerance=new QSpinBox;tolerance->setRange(0,255);tolerance->setValue(32);tolerance->setPrefix("Tolerance ");tolerance->setAccessibleName("Wand tolerance");wandBar->addWidget(tolerance);connect(tolerance,&QSpinBox::valueChanged,this,[this](int v){wandTolerance_=v;});
    auto*sample=new QComboBox;sample->addItems({"Point Sample","3 by 3 Average","5 by 5 Average"});sample->setAccessibleName("Wand sample size");wandBar->addWidget(sample);connect(sample,&QComboBox::currentIndexChanged,this,[this](int v){wandSampleRadius_=v;});auto*contiguous=new QCheckBox("Contiguous");contiguous->setChecked(true);wandBar->addWidget(contiguous);connect(contiguous,&QCheckBox::toggled,this,[this](bool v){wandContiguous_=v;});auto*all=new QCheckBox("Sample All Layers");wandBar->addWidget(all);connect(all,&QCheckBox::toggled,this,[this](bool v){wandAllLayers_=v;});
}
bool MainWindow::beginSelection(Point point,Qt::KeyboardModifiers modifiers,int clickCount){
    const bool selectionTool=tool_==Tool::Marquee||tool_==Tool::Lasso||tool_==Tool::Polygon||tool_==Tool::Wand;
    if(!selectionTool)return false;
    auto* p=current();if(!p||!p->document||!canEditLayers())return true;
    selectionModifiers_=modifiers;
    if(selectionGesture_.draft()&&selectionGesture_.draft()->kind==editing::LassoKind::Polygonal){
        if(selectionOwner_!=p){cancelSelectionGesture();return true;}
        const auto first=p->canvas->viewMapping().toView(selectionGesture_.draft()->points.front());
        const auto here=p->canvas->viewMapping().toView(point);
        if(selectionGesture_.polygonPress(point,std::hypot(here.x-first.x,here.y-first.y),clickCount)==editing::PolygonPressResult::FinishRequested)finishSelectionGesture();
        else refreshSelectionGesture();
        return true;
    }
    const auto mode=editing::selectionMode(modifiers.testFlag(Qt::ShiftModifier),modifiers.testFlag(Qt::AltModifier),selectionMode_);
    const auto before=outlineOf(p->document->selection);
    const bool inside=before&&!before->empty()&&before->contains(point);
    if(mode==editing::SelectionMode::Replace&&inside){
        finishOpacityEdit();selectionOwner_=p;selectionBefore_=p->document->selection;press_=point;movingSelection_=true;
        p->history.begin("Move Selection",p->document,p->active);refreshSelectionGesture();return true;
    }
    if(tool_==Tool::Wand){runWand(point,modifiers);return true;}
    finishOpacityEdit();selectionOwner_=p;selectionBefore_.reset();press_=point;
    const auto kind=tool_==Tool::Marquee?(ellipse_?editing::LassoKind::Ellipse:editing::LassoKind::Rectangle):tool_==Tool::Polygon?editing::LassoKind::Polygonal:editing::LassoKind::Freehand;
    selectionGesture_.begin(point,kind,mode,modifiers.testFlag(Qt::ShiftModifier));refresh(false,false);return true;
}
void MainWindow::finishSelectionGesture(){
    stopSelectionAutoscroll();auto* p=selectionOwner_;
    if(!p||p!=current()||!p->document){cancelSelectionGesture();return;}
    const auto before=outlineOf(p->document->selection);
    auto result=selectionGesture_.finish(before,p->document->width,p->document->height,selectionAntialias_);
    selectionOwner_=nullptr;pointerOwner_=nullptr;p->canvas->setSelectionDraft({});
    if(result&&result->requestsEdit){
        const bool same=(!before&&!result->selection)||(before&&result->selection&&before->geometricallyEquals(*result->selection));
        if(!same){
            const auto selection=editing::rasterSelection(result->selection,p->document->width,p->document->height);
            p->history.begin(result->historyName,p->document,p->active);p->document->selection=selection;p->history.end(p->document,p->active);
        }
    }
    refresh(false,false);
}
bool MainWindow::updateSelection(Point point,Qt::KeyboardModifiers modifiers,bool finish){
    if(!movingSelection_&&!selectionGesture_.active())return tool_==Tool::Marquee||tool_==Tool::Lasso||tool_==Tool::Polygon||tool_==Tool::Wand;
    auto* p=selectionOwner_;if(!p||p!=current()||!p->document){cancelSelectionGesture();return true;}
    selectionModifiers_=modifiers;
    if(movingSelection_){
        if(!finish){
            const auto offset=editing::selectionMoveOffset(press_,point,modifiers.testFlag(Qt::ShiftModifier));
            if(std::round(offset.x)==0&&std::round(offset.y)==0)p->document->selection=selectionBefore_;
            else p->document->selection=editing::moveSelectionCoverage(selectionBefore_,offset);
            updateSelectionAutoscroll(p->canvas->viewMapping().toView(point));refresh(false,false);return true;
        }
        stopSelectionAutoscroll();const bool moved=p->document->selection!=selectionBefore_;
        p->history.end(p->document,p->active);movingSelection_=false;selectionBefore_.reset();selectionOwner_=nullptr;
        if(!moved){if(tool_==Tool::Wand)runWand(press_,modifiers,editing::SelectionMode::Replace);else{p->history.begin("Deselect",p->document,p->active);p->document->selection.reset();p->history.end(p->document,p->active);}}
        refresh(false,false);return true;
    }
    if(finish){stopSelectionAutoscroll();if(selectionGesture_.draft()->kind!=editing::LassoKind::Polygonal)finishSelectionGesture();return true;}
    selectionGesture_.move(point,modifiers.testFlag(Qt::ShiftModifier));
    if(selectionGesture_.marquee())updateSelectionAutoscroll(p->canvas->viewMapping().toView(point));
    refreshSelectionGesture();return true;
}
void MainWindow::finishPolygon(){if(selectionGesture_.draft()&&selectionGesture_.draft()->kind==editing::LassoKind::Polygonal)finishSelectionGesture();}
void MainWindow::cancelSelectionGesture(){
    stopSelectionAutoscroll();if(selectionOwner_)selectionOwner_->canvas->setSelectionDraft({});
    selectionGesture_.cancel();selectionOwner_=nullptr;selectionBefore_.reset();movingSelection_=false;refreshSelectionGesture();if(commands_)commands_->refresh();
}
void MainWindow::refreshSelectionGesture(){
    auto* p=current();if(p&&p->canvas)p->canvas->setSelectionDraft(selectionOwner_==p?selectionGesture_.draft():std::nullopt);
    const auto mode=selectionGesture_.cursorMode(selectionModifiers_.testFlag(Qt::ShiftModifier),selectionModifiers_.testFlag(Qt::AltModifier),selectionMode_);
    if(auto* box=findChild<QComboBox*>("selectionMode")){QSignalBlocker block(box);box->setCurrentIndex(int(mode));}
    if(p&&p->canvas){p->canvas->setProperty("selectionCursorMode",int(mode));if(tool_==Tool::Marquee||tool_==Tool::Lasso||tool_==Tool::Polygon||tool_==Tool::Wand){if(!spaceHeld_&&!spaceDragging_)p->canvas->setCursor(movingSelection_?Qt::SizeAllCursor:Qt::CrossCursor);}}
}
void MainWindow::hoverSelection(QPointF view,Qt::KeyboardModifiers flags){
    selectionModifiers_=flags;
    if(selectionGesture_.draft()&&selectionGesture_.draft()->kind==editing::LassoKind::Polygonal&&selectionOwner_==current()){
        const auto point=selectionOwner_->canvas->documentPoint(view);selectionGesture_.moveCursor(Point{point.x(),point.y()});
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
    if(event->type()==QEvent::KeyPress||event->type()==QEvent::KeyRelease){
        auto* widget=qobject_cast<QWidget*>(watched);if(widget&&widget->window()==this){auto* key=static_cast<QKeyEvent*>(event);
            if(key->key()==Qt::Key_Shift||key->key()==Qt::Key_Alt||key->key()==Qt::Key_Control){
                auto flags=key->modifiers();const auto flag=key->key()==Qt::Key_Shift?Qt::ShiftModifier:key->key()==Qt::Key_Alt?Qt::AltModifier:Qt::ControlModifier;
                flags.setFlag(flag,event->type()==QEvent::KeyPress);
                try{selectionModifiersChanged(flags);}catch(const std::exception& error){pointerCancel();statusBar()->showMessage(error.what());}
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
