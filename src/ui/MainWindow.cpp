#include "MainWindow.h"
#include "LayerPanel.h"
#include "PreviewComboBox.h"
#include "PropertyControls.h"
#include "ProjectChrome.h"
#include "EditorIcons.h"
#include "VisualStyle.h"
#include <QToolButton>
#include <QApplication>
#include <QMenuBar>
#include <QStatusBar>
#include <QDockWidget>
#include <QToolBar>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QPlainTextEdit>
#include <QColorDialog>
#include <QClipboard>
#include <QImageReader>
#include <QImageWriter>
#include <QSaveFile>
#include <QCloseEvent>
#include <QSignalBlocker>
#include <QScreen>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace compositor {
static QImage imageOf(const Raster&r){auto bytes=r.rgba();return QImage(bytes.data(),r.width,r.height,r.width*4,QImage::Format_RGBA8888_Premultiplied).copy();}
EditorProject* MainWindow::current(){int i=tabs_->currentIndex();return i<0||i>=int(projects_.size())?nullptr:projects_[i].get();}
Layer* MainWindow::active(){auto*p=current();if(!p||!p->document)return nullptr;for(auto&l:p->document->layers)if(l.id==p->active)return &l;return nullptr;}
bool MainWindow::canEditLayers(){const auto state=commandState();return !state.modalDialog&&ui::canEditLayers(state)&&shapeDraftId_.empty();}
bool MainWindow::canEditAppearance(){return canEditLayers()&&active()&&!active()->group&&current()->selected.size()==1;}
QAction* MainWindow::action(QMenu* menu,const QString& name,const QKeySequence& key,std::function<void()> invoke){
    auto* item=menu->addAction(name);if(!key.isEmpty())item->setShortcut(key);
    bindCommand(item,menu->title(),name,std::move(invoke));return item;
}
MainWindow::MainWindow(bool warp):warp_(warp){ui::installVisualStyle();setWindowTitle("Compositor");resize(1280,820);setAcceptDrops(true);tabs_=new QTabWidget;tabs_->setTabsClosable(true);tabs_->setDocumentMode(true);setCentralWidget(tabs_);tabs_->setMovable(true);connect(tabs_,&QTabWidget::currentChanged,this,[this]{switchProject();});connect(tabs_,&QTabWidget::tabCloseRequested,this,[this](int i){closeProject(i);});
initializeCommands();
auto*file=menuBar()->addMenu("&File");action(file,"&New Canvas…",QKeySequence::New,[this]{newDialog();});action(file,"&Import Image…",{},[this]{importImage();});action(file,"&Export Image…",QKeySequence("Ctrl+Shift+E"),[this]{exportImage();});file->addSeparator();action(file,"Close Project",QKeySequence::Close,[this]{closeProject(tabs_->currentIndex());});action(file,"Exit",QKeySequence("Alt+F4"),[this]{close();});
action(file,"&Open Project…",QKeySequence::Open,[this]{openProjectDialog();});action(file,"&Save Project",QKeySequence::Save,[this]{saveProject();});action(file,"Save Project &As…",QKeySequence::SaveAs,[this]{saveProject(true);});
auto*editMenu=menuBar()->addMenu("&Edit");undo_=action(editMenu,"Undo",QKeySequence::Undo,[this]{if(textSession_){undoTextTyping(false);return;}if(drawingOriginal_){cancelGradient();return;}if(auto*p=current())if(auto s=p->history.undo()){restoreHistorySnapshot(std::move(*s));}});redo_=action(editMenu,"Redo",QKeySequence::Redo,[this]{if(textSession_){undoTextTyping(true);return;}if(auto*p=current())if(auto s=p->history.redo()){restoreHistorySnapshot(std::move(*s));}});
auto*layerMenu=menuBar()->addMenu("&Layer");action(layerMenu,"New Layer",QKeySequence("Ctrl+Shift+N"),[this]{newBlankLayer();});action(layerMenu,"Duplicate Layer",{},[this]{layerCommand(3);});action(layerMenu,"Rename Layer…",{},[this]{auto*l=active();if(!l)return;bool ok;auto name=QInputDialog::getText(this,"Rename Layer","Name",QLineEdit::Normal,QString::fromStdString(l->name),&ok).trimmed();if(ok&&!name.isEmpty())edit("Rename Layer",[&](Document&){active()->name=name.toStdString();});});action(layerMenu,"Delete Layer",{},[this]{layerCommand(6);});action(layerMenu,"Raise Layer",QKeySequence("Ctrl+]"),[this]{layerCommand(4);});action(layerMenu,"Lower Layer",QKeySequence("Ctrl+["),[this]{layerCommand(5);});
auto*imageMenu=menuBar()->addMenu("&Image");action(imageMenu,"Fill…",{},[this]{pixelEdit(0);});action(imageMenu,"Invert",QKeySequence("Ctrl+I"),[this]{pixelEdit(2);});action(imageMenu,"Trim",{},[this]{edit("Trim",[](Document&d){d=editing::trimDocument(d);});});
auto*view=menuBar()->addMenu("&View");action(view,"Fit Canvas",QKeySequence("Ctrl+0"),[this]{if(canvas())canvas()->fit();});action(view,"Actual Pixels",QKeySequence("Ctrl+1"),[this]{if(canvas())canvas()->zoomAt(1,canvas()->rect().center());});auto*grid=action(view,"Pixel Grid",{},[this]{if(canvas()){canvas()->showPixelGrid=!canvas()->showPixelGrid;canvas()->update();}});grid->setCheckable(true);auto*guidesAction=action(view,"Guides",{},[this]{auto*p=current();if(!p||!p->document||!canvas())return;if(p->document->guides.empty())edit("Add Guide",[p](Document&d){d.guides.push_back({newId(),true,d.height/2.});});canvas()->showGuides=!canvas()->showGuides;canvas()->guides.clear();if(canvas()->showGuides&&p->document)for(const auto&guide:p->document->guides)canvas()->guides.push_back({guide.horizontal,guide.position});canvas()->update();});guidesAction->setCheckable(true);auto*snap=action(view,"Snap",{},[this]{snapToGuides_=!snapToGuides_;});snap->setCheckable(true);snap->setChecked(true);auto*help=menuBar()->addMenu("&Help");action(help,"Keyboard Shortcuts…",{},[this]{QDialog dialog(this);dialog.setWindowTitle("Keyboard Shortcuts");QString text;for(const auto&spec:ui::commandCatalog())if(!spec.windowsShortcut.isEmpty())text+=spec.label+"    "+spec.windowsShortcut+"\n";auto*viewText=new QPlainTextEdit(text,&dialog);viewText->setReadOnly(true);auto*layout=new QVBoxLayout(&dialog);layout->addWidget(viewText);dialog.resize(520,640);dialog.exec();});action(help,"Check for Updates…",{},[this]{checkForUpdates();});action(help,"About Compositor",{},[this]{QMessageBox::about(this,"Compositor","<h3>Compositor Windows Preview</h3><p>An independent Windows port of Compositor by Robbie Tilton. Ported behavior follows upstream 1.0.4 through 1.4.5. The dependency pin remains the 1.0.4 commit a19db90. Mac equivalence is unverified.</p><p><a href=\"https://github.com/robbietilton/Compositor\">Original Compositor project</a><br>Copyright Wonder Assembly LLC. MIT source.</p><p>This independent port is not endorsed by the upstream author.</p><p>Qt and other dependency notices accompany this package.</p>");});
auto* tools = addToolBar("Tools");
tools->setObjectName("tools");
tools->setMovable(false);
tools->setFloatable(false);
tools->setFixedWidth(56);
tools->setIconSize({22,22});
tools->setToolButtonStyle(Qt::ToolButtonIconOnly);
tools->setStyleSheet(R"(
    QToolBar#tools { border: 0; border-right: 1px solid palette(mid); padding: 8px 7px; spacing: 2px; }
    QToolBar#tools QToolButton[editorToolButton="true"] {
        background: transparent; border: 1px solid transparent; border-radius: 7px; padding: 0;
    }
    QToolBar#tools QToolButton[editorToolButton="true"]:hover { background: rgba(128,128,128,32); }
    QToolBar#tools QToolButton[editorToolButton="true"]:checked {
        background: rgba(128,128,128,55); border: 1px solid rgba(128,128,128,80);
    }
    QToolBar#tools QToolButton[editorToolButton="true"]:focus { border: 1px solid palette(highlight); }
)");
addToolBar(Qt::LeftToolBarArea,tools);
struct ToolEntry { const char* label; Tool tool; ui::EditorIcon icon; };
const ToolEntry entries[]{
    {"Move (V)",Tool::Move,ui::EditorIcon::Move},
    {"Marquee (M)",Tool::Marquee,ui::EditorIcon::Marquee},
    {"Lasso (L)",Tool::Lasso,ui::EditorIcon::Lasso},
    {"Polygon",Tool::Polygon,ui::EditorIcon::Polygon},
    {"Wand (W)",Tool::Wand,ui::EditorIcon::Wand},
    {"Crop (C)",Tool::Crop,ui::EditorIcon::Crop},
    {"Brush (B)",Tool::Brush,ui::EditorIcon::Brush},
    {"Eraser (E)",Tool::Eraser,ui::EditorIcon::Eraser},
    {"Heal (J)",Tool::SpotHealing,ui::EditorIcon::Heal},
    {"Clone (S)",Tool::CloneStamp,ui::EditorIcon::Clone},
    {"Retouch (R)",Tool::Blur,ui::EditorIcon::Retouch},
    {"Gradient (G)",Tool::Gradient,ui::EditorIcon::Gradient},
    {"Shape (U)",Tool::Shape,ui::EditorIcon::Shape},
    {"Type (T)",Tool::Text,ui::EditorIcon::Type},
    {"Eyedropper (I)",Tool::Eyedropper,ui::EditorIcon::Eyedropper},
    {"Hand (H)",Tool::Hand,ui::EditorIcon::Hand},
    {"Zoom (Z)",Tool::Zoom,ui::EditorIcon::Zoom}
};
for (const auto& entry : entries) {
    const bool alternate = entry.tool==Tool::Polygon || entry.tool==Tool::Eraser;
    auto* a = alternate ? new QAction(ui::editorIcon(entry.icon),entry.label,tools)
                        : tools->addAction(ui::editorIcon(entry.icon),entry.label);
    if(alternate)addAction(a);
    a->setCheckable(true);
    a->setToolTip(entry.label);
    a->setProperty("editorTool",int(entry.tool));
    bindCommand(a,"Tools",entry.label,[this,t=entry.tool]{selectTool(t==Tool::Brush&&brushMode_==ProjectBrushMode::Erase?Tool::Eraser:t);});
    if(entry.tool==Tool::Brush)a->setShortcut({});
    if(auto* button=qobject_cast<QToolButton*>(tools->widgetForAction(a))) {
        button->setProperty("editorToolButton",true);
        button->setAccessibleName(entry.label);
        button->setFixedSize(36,36);
    }
}
auto*brushKey=new QAction("Brush (B)",this);bindCommand(brushKey,"Tools","Brush (B)",[this]{selectTool(Tool::Brush);});addAction(brushKey);
setupPaletteControls(tools);
auto*dock=new QDockWidget("Layers",this);dock->setObjectName("layersDock");auto*panel=new QWidget;auto*layout=new QVBoxLayout(panel);auto*blendPicker=new ui::PreviewComboBox;blend_=blendPicker;blendPicker->preview=[this](int index){if(index>=0&&index<int(blendNames.size()))previewBlendMode(Blend(index));};blendPicker->dismissed=[this]{previewBlendMode({});};blend_->setObjectName("layerBlend");blend_->setAccessibleName("Layer blend mode");for(auto n:blendNames)blend_->addItem(n);layout->addWidget(blend_);opacity_=new ui::TrackSlider(Qt::Horizontal);opacity_->setObjectName("layerOpacity");opacity_->setRange(0,100);opacity_->setAccessibleName("Layer opacity");layout->addWidget(createLayerOpacityControl());layers_=new QTreeWidget;layers_->setHeaderHidden(true);layers_->setAccessibleName("Layer list");layout->addWidget(layers_);dock->setWidget(panel);addDockWidget(Qt::RightDockWidgetArea,dock);connect(layers_,&QTreeWidget::itemChanged,this,[this](QTreeWidgetItem*i){if(refreshing_||!i)return;auto id=i->data(0,Qt::UserRole).toString().toStdString();bool visible=i->checkState(0)==Qt::Checked;edit("Layer Visibility",[&](Document&d){for(auto&l:d.layers)if(l.id==id){l.visible=visible;if(!i->text(0).trimmed().isEmpty())l.name=i->text(0).trimmed().toStdString();}});});connect(blend_,&QComboBox::currentIndexChanged,this,[this](int i){if(!refreshing_&&i>=0&&i<int(blendNames.size()))setLayerBlendMode(Blend(i));});connect(opacity_,&QSlider::sliderPressed,this,[this]{if(!canEditAppearance())return;finishOpacityEdit();opacityOwner_=current();if(opacityOwner_)opacityOwner_->history.begin("Layer Opacity",opacityOwner_->document,opacityOwner_->active);});connect(opacity_,&QSlider::sliderReleased,this,[this]{finishOpacityEdit();refresh(false);});connect(opacity_,&QSlider::valueChanged,this,[this](int v){if(!refreshing_&&canEditAppearance())edit("Layer Opacity",[&](Document&){active()->opacity=v/100.;});});
auto*inspector=new QDockWidget("Transform",this);inspector->setObjectName("transformDock");auto*formWidget=new QWidget;auto*form=new QFormLayout(formWidget);const char*names[]{"X","Y","Width","Height","Angle"};for(int i=0;i<5;++i){auto*spin=new ui::PropertyNumber;spin->releaseFocus=[this]{if(canvas())canvas()->setFocus();};spin->setDecimals(3);spin->setRange(i==2||i==3?1:-1000000,i==2||i==3?300000:1000000);spin->setAccessibleName(names[i]);geometry_[i]=spin;form->addRow(names[i],spin);connect(spin,&QDoubleSpinBox::valueChanged,this,[this,i](double value){if(!refreshing_)editTransformGeometry(i,value);});}inspector->setWidget(formWidget);addDockWidget(Qt::RightDockWidgetArea,inspector);setupBrushControls();setupAdjustmentActions();setupSelectionActions();setupLayerActions();setupTransformActions();setupDrawingActions();setupTypeControls();setupRetouchActions();statusBar()->showMessage("Create a canvas or import an image");ui::installProjectChrome(this,tabs_,dock,layers_);ui::styleWorkspace(this);refresh(false);addEmptyProject(false);}
void MainWindow::restoreHistorySnapshot(Snapshot snapshot){
    auto* project=current();if(!project)return;
    const bool keepDraft=selectionGesture_.active()&&selectionOwner_==project;
    auto draft=keepDraft?std::optional(std::move(selectionGesture_)):std::nullopt;
    auto* pointer=pointerOwner_;const auto scrollPoint=selectionScrollPoint_;
    const bool scrolling=selectionScrollTimer_&&selectionScrollTimer_->isActive();
    pointerCancel();project->document=std::move(snapshot.document);project->active=std::move(snapshot.activeLayer);
    if(draft){selectionGesture_=std::move(*draft);selectionOwner_=project;if(pointer==project)pointerOwner_=project;
        selectionScrollPoint_=scrollPoint;if(scrolling&&project->document)selectionScrollTimer_->start();}
    refresh();
}
void MainWindow::finishOpacityEdit(){auto*owner=opacityOwner_;opacityOwner_=nullptr;if(!owner)return;try{owner->history.end(owner->document,owner->active);}catch(...){if(auto snapshot=owner->history.cancel()){owner->document=std::move(snapshot->document);owner->active=std::move(snapshot->activeLayer);}throw;}}
void MainWindow::selectTool(Tool value){
    if(!ui::commandEnabled(ui::CommandGate::Tool,commandState()))return;
    if(textSession_&&value!=Tool::Text&&!finishText())return;
    if(value==Tool::Brush||value==Tool::Eraser)brushMode_=value==Tool::Eraser?ProjectBrushMode::Erase:ProjectBrushMode::Paint;
    if(value==Tool::Lasso&&lassoKind_==editing::LassoKind::Polygonal)value=Tool::Polygon;
    if(value==Tool::Polygon)lassoKind_=editing::LassoKind::Polygonal;
    if(value!=tool_){applyGradient();if(transformSession_&&transformSession_->persistent)applyTransformSession();pointerCancel();tool_=value;}
    if(value==Tool::Crop&&!cropDraft_)if(auto* p=current();p&&p->document){cropRatioChoice_="Free";cropDraft_=editing::Rect{0,0,double(p->document->width),double(p->document->height)};if(p->document->selection&&p->document->selection->coverage){auto bounds=editing::coverageBounds(*p->document->selection->coverage);if(!bounds.empty())cropDraft_=bounds;}}
    if(auto* box=findChild<QComboBox*>("lassoKind")){const QSignalBlocker block(box);box->setCurrentIndex(lassoKind_==editing::LassoKind::Polygonal?1:0);}
    if(auto* box=findChild<QComboBox*>("cropRatioChoice")){const QSignalBlocker block(box);box->setCurrentText(cropRatioChoice_);}
    refresh(false,false);
}
void MainWindow::edit(const char*name,const std::function<void(Document&)>&fn){auto*p=current();if(!p||!p->document||p->importing||p->projectBusy)return;p->history.begin(name,p->document,p->active);try{fn(*p->document);validateDocument(*p->document);p->history.end(p->document,p->active);}catch(...){if(auto s=p->history.cancel()){p->document=std::move(s->document);p->active=std::move(s->activeLayer);}throw;}refresh();}
void MainWindow::refresh(bool render,bool rebuildLayers){if(refreshing_)return;refreshing_=true;auto*p=current();auto*l=active();const auto layerState=commandState(p);const bool layerEditing=canEditLayers();layers_->setEnabled(p&&p->document&&!layerState.modalDialog&&!layerState.projectBusy&&!layerState.importing);layers_->setSelectionMode(layerEditing?QAbstractItemView::ExtendedSelection:QAbstractItemView::NoSelection);layers_->setEditTriggers(layerEditing?QAbstractItemView::EditKeyPressed:QAbstractItemView::NoEditTriggers);if(!rebuildLayers)if(auto* controller=ui::LayerPanelController::find(layers_))controller->updateSelection();if(p&&p->page){p->page->setCurrentWidget(p->document?static_cast<QWidget*>(p->canvas):p->welcome);p->welcome->setEnabled(!p->importing&&!p->projectBusy);}if(p&&!p->document){p->canvas->setDocumentSize(0,0);p->selected.clear();tabs_->setTabText(tabs_->currentIndex(),p->defaultTitle+(p->history.modified()?" *":""));}if((!p||!p->document)&&rebuildLayers)refreshLayerPanel();if(p&&p->document){if(p->selected.empty()||std::find(p->selected.begin(),p->selected.end(),p->active)==p->selected.end())p->selected={p->active};if(rebuildLayers)refreshLayerPanel();
p->canvas->setTransformOverlay(tool_==Tool::Move&&(transformControls_||transformSession_)?selectedTransform():std::nullopt);p->canvas->setGradientLine(drawingOriginal_&&gradientOwner_==p?std::optional(std::pair{gradientStart_,gradientEnd_}):std::nullopt);p->canvas->setDistortionOverlay(tool_==Tool::Move&&transformSession_?transformSession_->corners:std::nullopt);p->canvas->guides.clear();if(p->canvas->showGuides)for(const auto&guide:p->document->guides)p->canvas->guides.push_back({guide.horizontal,guide.position});p->canvas->setSelection(p->document->selection?p->document->selection->coverage:nullptr,p->document->selection?p->document->selection->outline:nullptr);if(render){try{p->canvas->setDocumentSize(p->document->width,p->document->height);}catch(const std::exception&e){statusBar()->showMessage(e.what());}}QString title=p->path.isEmpty()?p->defaultTitle:QFileInfo(p->path).fileName();tabs_->setTabText(tabs_->currentIndex(),title+(p->history.modified()?" *":""));statusBar()->showMessage(QString("%1 × %2 px  ·  %3 ppi").arg(p->document->width).arg(p->document->height).arg(p->document->resolution));}if(target_){QSignalBlocker block(target_);target_->setEnabled(l&&l->mask.has_value());if(p&&(!l||!l->mask))p->maskSelected=false;target_->setCurrentIndex(p&&p->maskSelected?1:0);}blend_->setEnabled(canEditAppearance());opacity_->setEnabled(canEditAppearance());refreshOpacityPercent();if(l){blend_->setCurrentIndex(int(l->blend));opacity_->setValue(int(std::lround(l->opacity*100)));auto t=selectedTransform().value_or(l->transform);double values[]{t.x,t.y,t.width,t.height,t.rotation};for(int i=0;i<5;++i)ui::synchronizeNumber(geometry_[i],values[i]);}for(auto*g:geometry_)g->setEnabled(ui::commandEnabled(ui::CommandGate::TransformDraft,commandState())&&l!=nullptr&&p&&!p->importing&&!p->projectBusy&&(!transformSession_||!transformSession_->corners));undo_->setEnabled(p&&!p->importing&&!p->projectBusy&&p->history.canUndo());redo_->setEnabled(p&&!p->importing&&!p->projectBusy&&p->history.canRedo());undo_->setText(p&&p->history.canUndo()?"Undo "+QString::fromStdString(p->history.undoName()):"Undo");redo_->setText(p&&p->history.canRedo()?"Redo "+QString::fromStdString(p->history.redoName()):"Redo");for(const auto*name:{"applyGradient","cancelGradient"})if(auto*a=findChild<QAction*>(name)){a->setVisible(drawingOriginal_.has_value());a->setEnabled(drawingOriginal_.has_value());}updateTransformActions();refreshBrushControls();refreshRetouchControls();refreshBrushPointer();for(auto*bar:findChildren<QToolBar*>()){auto name=bar->objectName();if(name=="wandOptions")bar->setVisible(tool_==Tool::Wand);else if(name=="brushOptions")bar->setVisible(tool_==Tool::Brush||tool_==Tool::Eraser);else if(name=="selectionOptions")bar->setVisible(tool_==Tool::Marquee||tool_==Tool::Lasso||tool_==Tool::Polygon||tool_==Tool::Wand);else if(name=="transformOptions")bar->setVisible(tool_==Tool::Move);else if(name=="drawingOptions")bar->setVisible(tool_==Tool::Gradient||tool_==Tool::Shape||tool_==Tool::Crop);else if(name=="typeOptions")bar->setVisible(tool_==Tool::Text||bool(textSession_));if(name=="tools")for(auto*a:bar->findChildren<QAction*>())if(a->property("editorTool").isValid()){
    const auto actionTool=Tool(a->property("editorTool").toInt());
    a->setChecked(actionTool==tool_||(actionTool==Tool::Brush&&tool_==Tool::Eraser)||(actionTool==Tool::Lasso&&tool_==Tool::Polygon));
    if(actionTool==Tool::Marquee)a->setIcon(ui::editorIcon(ellipse_?ui::EditorIcon::EllipseMarquee:ui::EditorIcon::Marquee));
    if(actionTool==Tool::Lasso)a->setIcon(ui::editorIcon(lassoKind_==editing::LassoKind::Polygonal?ui::EditorIcon::Polygon:ui::EditorIcon::Lasso));
    if(actionTool==Tool::Brush)a->setIcon(ui::editorIcon(brushMode_==ProjectBrushMode::Erase?ui::EditorIcon::Eraser:ui::EditorIcon::Brush));
}}refreshing_=false;refreshSelectionGesture();if(commands_)commands_->refresh();refreshPaletteControls();
    for(const char* name:{"selectionExpandAmount","selectionContractAmount"})if(auto* control=findChild<QSpinBox*>(name))control->setEnabled(ui::commandEnabled(ui::CommandGate::ModifySelection,commandState()));
    if(auto* control=findChild<QComboBox*>("cropRatioChoice")){control->setVisible(tool_==Tool::Crop);control->setEnabled(p&&p->document&&!p->projectBusy&&!p->importing);}
    if(auto* control=findChild<QComboBox*>("lassoKind"))control->setVisible(tool_==Tool::Lasso||tool_==Tool::Polygon);
    QStringList titles;for(const auto& project:projects_)titles.push_back(project->path.isEmpty()?project->defaultTitle:QFileInfo(project->path).fileName());ui::refreshProjectTabs(tabs_,canSwitchProjects(),titles);
    refreshShapeControls();refreshCropControls();refreshTypeControls();
}
bool MainWindow::closeProject(int i){
    if(i<0||i>=int(projects_.size()))return true;if(!canSwitchProjects()||projects_[i]->importing||projects_[i]->projectBusy)return false;
    if(auto* panel=ui::LayerPanelController::find(layers_))panel->finishVisibilitySwipe();finishVisibilitySwipe();
    finishOpacityEdit();if(textSession_&&textSession_->owner==projects_[i].get())finishText();applyGradient();if(transformSession_&&transformSession_->persistent)applyTransformSession();pointerCancel();
    auto&p=*projects_[i];
    if(p.history.modified()){
        tabs_->setCurrentIndex(i);
        auto answer=QMessageBox::warning(this,"Unsaved Changes","Save changes before closing this project?",QMessageBox::Save|QMessageBox::Discard|QMessageBox::Cancel,QMessageBox::Save);
        if(answer==QMessageBox::Cancel)return false;
        if(answer==QMessageBox::Save){try{if(!saveProject())return false;}catch(const std::exception&e){QMessageBox::critical(this,"Save Project",e.what());return false;}}
    }
    auto*widget=p.page;p.canvas->viewportProvider={};p.canvas->pointerDown={};p.canvas->pointerDoubleClick={};p.canvas->pointerMove={};p.canvas->pointerUp={};p.canvas->pointerCancel={};p.canvas->pointerInterrupted={};p.canvas->pointerHover={};p.canvas->pointerLeave={};p.canvas->rightPointerDown={};p.canvas->rightPointerMove={};p.canvas->navigationAllowed={};if(activeProject_==&p)activeProject_=nullptr;projects_.erase(projects_.begin()+i);tabs_->removeTab(i);widget->deleteLater();if(projects_.empty()&&!closingWindow_)addEmptyProject(false);else switchProject();refresh(false);return true;
}
void MainWindow::closeEvent(QCloseEvent*e){closingWindow_=true;for(int i=int(projects_.size())-1;i>=0;--i)if(!closeProject(i)){closingWindow_=false;e->ignore();return;}e->accept();}
void MainWindow::pointerBegin(QPointF p,Qt::KeyboardModifiers m,int clickCount){if(current()&&(current()->importing||current()->projectBusy))return;pointerOwner_=current();if(beginTemporaryHand({p.x(),p.y()}))return;if(beginEditPanelPointer({p.x(),p.y()},m))return;if(beginPalette({p.x(),p.y()},m))return;if(tool_==Tool::Text){beginText({p.x(),p.y()},false,m.testFlag(Qt::ShiftModifier));return;}if(tool_==Tool::Zoom&&canvas()){zoomDragging_=true;zoomMoved_=false;zoomStart_=canvas()->zoom;auto at=canvas()->viewMapping().toView({p.x(),p.y()});zoomAnchor_={at.x,at.y};return;}if(beginRetouch({p.x(),p.y()},m))return;if(beginDrawing({p.x(),p.y()},m))return;if(beginTransform({p.x(),p.y()},m))return;if(beginSelection({p.x(),p.y()},m,clickCount))return;if(beginBrush({p.x(),p.y()},m))return;if(!current())return;press_={p.x(),p.y()};panOrigin_=canvas()->pan;if(tool_==Tool::Move&&active()){moving_=active()->transform;current()->history.begin("Move Layer",current()->document,current()->active);}}
void MainWindow::pointerUpdate(QPointF p,Qt::KeyboardModifiers m){if(pointerOwner_!=current()){pointerCancel();return;}if(updateTemporaryHand({p.x(),p.y()},false))return;if(tool_==Tool::Text)return;if(updateEditPanelPointer({p.x(),p.y()},m,false))return;if(updatePalette({p.x(),p.y()},false))return;if(zoomDragging_&&canvas()){auto at=canvas()->viewMapping().toView({p.x(),p.y()});auto dx=at.x-zoomAnchor_.x();zoomMoved_|=std::abs(dx)>3;if(zoomMoved_)canvas()->zoomAt(zoomStart_*std::pow(2,dx/100),zoomAnchor_);return;}if(updateRetouch({p.x(),p.y()},m))return;if(updateDrawing({p.x(),p.y()},m,false))return;if(transformDrag_){updateTransform({p.x(),p.y()},m,false);return;}if(updateSelection({p.x(),p.y()},m,false))return;if(updateBrush({p.x(),p.y()},false))return;if(transformDrag_){updateTransform({p.x(),p.y()},m,false);return;}if(tool_==Tool::Hand){canvas()->panBy(QPointF(p.x()-press_.x,p.y()-press_.y)*canvas()->pointsPerPixel());return;}if(!moving_||!active())return;double dx=p.x()-press_.x,dy=p.y()-press_.y;if(m&Qt::ShiftModifier){if(std::abs(dx)>std::abs(dy))dy=0;else dx=0;}active()->transform.x=std::round(moving_->x+dx);active()->transform.y=std::round(moving_->y+dy);refresh();}
void MainWindow::pointerEnd(QPointF p,Qt::KeyboardModifiers m){if(pointerOwner_!=current()){pointerCancel();return;}if(updateTemporaryHand({p.x(),p.y()},true)){pointerOwner_=nullptr;return;}if(tool_==Tool::Text){pointerOwner_=nullptr;return;}if(updateEditPanelPointer({p.x(),p.y()},m,true)){pointerOwner_=nullptr;return;}if(updatePalette({p.x(),p.y()},true)){pointerOwner_=nullptr;return;}if(zoomDragging_&&canvas()){if(!zoomMoved_)canvas()->zoomAt(zoomStart_*(m.testFlag(Qt::AltModifier)?.5:2),zoomAnchor_);zoomDragging_=false;pointerOwner_=nullptr;return;}if(endRetouch({p.x(),p.y()},m)){pointerOwner_=nullptr;return;}if(updateDrawing({p.x(),p.y()},m,true)){pointerOwner_=nullptr;return;}if(transformDrag_){updateTransform({p.x(),p.y()},m,true);pointerOwner_=nullptr;return;}if(updateSelection({p.x(),p.y()},m,true)){if(tool_!=Tool::Polygon)pointerOwner_=nullptr;return;}if(updateBrush({p.x(),p.y()},true)){pointerOwner_=nullptr;return;}if(transformDrag_){updateTransform({p.x(),p.y()},m,true);pointerOwner_=nullptr;return;}if(moving_){pointerUpdate(p,m);moving_.reset();current()->history.end(current()->document,current()->active);refresh(false);}pointerOwner_=nullptr;}
void MainWindow::pointerCancel(){cancelShape();cancelBrushTip();cancelTemporaryHand();cancelGradient();samplingPalette_=false;zoomDragging_=false;if(canvas())canvas()->setSampleRing({});cancelTransformSession();cancelRetouch();drawingOriginal_.reset();shapeDraftId_.clear();cropDraft_.reset();cropDrag_.reset();cropSnap_.reset();refreshCropControls();transformDrag_.reset();transformBefore_.reset();transformIds_.clear();cancelSelectionGesture();selectionBefore_.reset();movingSelection_=false;if(stroke_){stroke_->cancel();stroke_.reset();}moving_.reset();auto*owner=pointerOwner_;pointerOwner_=nullptr;if(owner){owner->brushPreview.reset();owner->brushLayerId.clear();}if(owner)if(auto s=owner->history.cancel()){owner->document=std::move(s->document);owner->active=std::move(s->activeLayer);if(owner==current())refresh();}}
void MainWindow::addFeasibilityDocument(){Document d;d.id=newId();d.width=640;d.height=420;Layer bg;bg.id=newId();bg.name="Background";bg.transform={0,0,640,420};bg.raster=Raster::filled(640,420,{35,65,90,255});Layer l;l.id=newId();l.name="Translucent masked layer";l.transform={145.25,90.5,320,220,17};l.raster=Raster::filled(320,220,{180,75,30,200});auto mask=std::make_shared<GrayRaster>();mask->width=320;mask->height=220;mask->pixels.resize(320*220);for(int y=0;y<220;++y)for(int x=0;x<320;++x)mask->pixels[size_t(y)*320+x]=uint8_t(x*255/319);l.mask=Mask{mask};d.layers={bg,l};addProject(d,"Feasibility");}
}

