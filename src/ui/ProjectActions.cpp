#include "MainWindow.h"
#include "ImportActions.h"
#include "LayerPanel.h"
#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTabBar>
#include <QVBoxLayout>

namespace compositor {
namespace {
void detach(NativeCanvas*canvas){
    canvas->pointerDown={};canvas->pointerDoubleClick={};canvas->pointerMove={};canvas->pointerUp={};canvas->pointerCancel={};canvas->pointerInterrupted={};
    canvas->pointerHover={};canvas->pointerLeave={};canvas->rightPointerDown={};canvas->rightPointerMove={};canvas->navigationAllowed={};canvas->viewportProvider={};
}
}
MainWindow::~MainWindow(){
    qApp->removeEventFilter(this);stopSelectionAutoscroll();
    // Qt destroys child widgets after derived members. No callback may then use
    // the already-destroyed project vector or a worker's completion host.
    for(auto&p:projects_)detach(p->canvas);
    if(auto* panel=ui::LayerPanelController::find(layers_))panel->finishVisibilitySwipe();
    finishVisibilitySwipe();
    delete ui::LayerPanelController::find(layers_);
    delete commands_;commands_=nullptr;
    delete importQueue_;importQueue_=nullptr;
}
void MainWindow::initializeProject(EditorProject&project){
    project.canvas=new NativeCanvas(warp_);
    project.canvas->showPixelGrid=true;
    project.canvas->pointerDown=[this](QPointF p,Qt::KeyboardModifiers m){try{pointerBegin(p,m);}catch(const std::exception&e){pointerCancel();statusBar()->showMessage(e.what());}};
    project.canvas->pointerDoubleClick=[this](QPointF p,Qt::KeyboardModifiers m){try{pointerBegin(p,m,2);}catch(const std::exception&e){pointerCancel();statusBar()->showMessage(e.what());}};
    project.canvas->pointerMove=[this](QPointF p,Qt::KeyboardModifiers m){try{pointerUpdate(p,m);}catch(const std::exception&e){pointerCancel();statusBar()->showMessage(e.what());}};
    project.canvas->pointerUp=[this](QPointF p,Qt::KeyboardModifiers m){try{pointerEnd(p,m);}catch(const std::exception&e){pointerCancel();statusBar()->showMessage(e.what());}};
    project.canvas->pointerCancel=[this]{pointerCancel();};project.canvas->pointerInterrupted=[this]{interruptPointer();};
    project.canvas->pointerHover=[this](QPointF point,Qt::KeyboardModifiers flags){updateBrushPointer(point,flags);hoverSelection(point,flags);};
    project.canvas->pointerLeave=[this]{clearBrushPointer();if(selectionGesture_.draft()&&selectionGesture_.draft()->kind==editing::LassoKind::Polygonal){selectionGesture_.moveCursor({});refreshSelectionGesture();}};
    project.canvas->rightPointerDown=[this](QPointF point,Qt::KeyboardModifiers flags){return beginBrushTip(point,flags);};
    project.canvas->rightPointerMove=[this](QPointF point,Qt::KeyboardModifiers flags,bool finish){updateBrushTip(point,flags,finish);};
    project.canvas->navigationAllowed=[this]{return canvasNavigationAllowed();};
    auto*owner=&project;project.canvas->viewportProvider=[this,owner](double x,double y,double w,double h,double units){return brushViewport(*owner,x,y,w,h,units);};
}
bool MainWindow::canSwitchProjects(){
    const auto state=commandState(activeProject_);return !state.modalDialog&&ui::canSwitchProject(state);
}
void MainWindow::switchProject(){
    auto*next=current();if(next==activeProject_){refresh(false);return;}
    if(activeProject_&&!canSwitchProjects()){
        for(size_t i=0;i<projects_.size();++i)if(projects_[i].get()==activeProject_){QSignalBlocker block(tabs_);tabs_->setCurrentIndex(int(i));break;}
        return;
    }
    if(auto* panel=ui::LayerPanelController::find(layers_))panel->finishVisibilitySwipe();
    finishVisibilitySwipe();finishOpacityEdit();
    if(activeProject_)captureToolState(*activeProject_);
    if(transformSession_&&transformSession_->persistent)applyTransformSession();
    pointerCancel();activeProject_=next;
    if(next)restoreToolState(*next);
    refresh();
}
EditorProject& MainWindow::addEmptyProject(bool reuseEmpty){
    if(reuseEmpty&&projects_.size()==1&&!projects_.front()->document&&!projects_.front()->importing&&
       (!importQueue_||!importQueue_->contains(projects_.front()->canvas)))return *projects_.front();
    const bool first=projects_.empty();auto project=std::make_unique<EditorProject>();auto*raw=project.get();
    raw->defaultTitle=first&&nextProjectNumber_==2?"Untitled":QString("Untitled %1").arg(nextProjectNumber_++);
    initializeProject(*raw);raw->page=new QStackedWidget;raw->page->setObjectName("projectPage");
    raw->welcome=new QWidget;raw->welcome->setObjectName("newCanvasWelcome");auto*outer=new QVBoxLayout(raw->welcome);outer->addStretch();
    auto*row=new QHBoxLayout;row->addStretch();auto*panel=new QWidget;panel->setMaximumWidth(500);auto*form=new QFormLayout(panel);
    auto*heading=new QLabel("New canvas");QFont font=heading->font();font.setPointSize(20);heading->setFont(font);form->addRow(heading);
    auto*width=new QSpinBox;auto*height=new QSpinBox;for(auto*spin:{width,height})spin->setRange(1,30000);
    width->setObjectName("newCanvasWidth");height->setObjectName("newCanvasHeight");width->setAccessibleName("Width in pixels");height->setAccessibleName("Height in pixels");width->setValue(1920);height->setValue(1080);
    if(!first){const auto image=QApplication::clipboard()->image();if(!image.isNull()&&image.width()<=30000&&image.height()<=30000){width->setValue(image.width());height->setValue(image.height());}}
    form->addRow("Width (pixels)",width);form->addRow("Height (pixels)",height);form->addRow(new QLabel("Transparent canvas · sRGB"));
    auto*buttons=new QDialogButtonBox;auto*open=buttons->addButton("Open project",QDialogButtonBox::ActionRole);auto*import=buttons->addButton("Import image",QDialogButtonBox::ActionRole);auto*create=buttons->addButton("Create canvas",QDialogButtonBox::AcceptRole);create->setObjectName("createCanvas");create->setDefault(true);form->addRow(buttons);
    connect(open,&QPushButton::clicked,this,[this]{openProjectDialog();});connect(import,&QPushButton::clicked,this,[this]{importImage();});
    connect(create,&QPushButton::clicked,this,[this,raw,width,height]{
        if(raw!=current()||raw->document||raw->importing||raw->projectBusy)return;
        Document document;document.id=newId();document.width=width->value();document.height=height->value();Layer layer;layer.id=newId();layer.name="Layer 1";layer.transform={0,0,double(document.width),double(document.height)};document.layers.push_back(layer);
        raw->history.begin("New Canvas",std::nullopt,{});raw->document=std::move(document);raw->active=layer.id;raw->selected={layer.id};raw->history.end(raw->document,raw->active);refresh();raw->canvas->fit();raw->canvas->setFocus();
    });
    row->addWidget(panel);row->addStretch();outer->addLayout(row);outer->addStretch();raw->page->addWidget(raw->welcome);raw->page->addWidget(raw->canvas);
    projects_.push_back(std::move(project));const auto index=tabs_->addTab(raw->page,raw->defaultTitle);tabs_->setCurrentIndex(index);switchProject();width->setFocus();return *raw;
}
EditorProject& MainWindow::addProject(Document document,QString title){
    validateDocument(document);auto&project=addEmptyProject();project.document=std::move(document);project.active=project.document->layers.empty()?"":project.document->layers.back().id;project.selected={project.active};project.defaultTitle=title;
    refresh();project.canvas->fit();project.canvas->setFocus();return project;
}
void MainWindow::newDialog(){if(canSwitchProjects())addEmptyProject(false);}
}
