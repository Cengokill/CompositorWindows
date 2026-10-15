#include "MainWindow.h"
#include <algorithm>
#include "ImportActions.h"
#include "WorkspaceDropQueue.h"
#include "ProjectLayerCopyJob.h"
#include "LayerMergeJob.h"
#include "LastFilterJob.h"
#include "LayerPanel.h"
#include "EditorIcons.h"
#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QEvent>
#include <QLabel>
#include <QMoveEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QScopeGuard>
#include <QStatusBar>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace compositor {
namespace {
// Qt shortens the tab bar for the corner widget, so its bottom edge stops at the
// button while the Layers title border sits on a lower pixel behind it. The
// corner matches the tab-bar height and one separator runs from the strip's left
// edge through the clearance before the dock. The Layers title uses that height
// so its border is the same row.
class WorkspaceTabCorner final : public QWidget {
public:
    explicit WorkspaceTabCorner(QTabWidget* tabs):QWidget(tabs),tabs_(tabs){
        setObjectName("newProjectCorner");
        auto* layout=new QHBoxLayout(this);
        layout->setContentsMargins(0,0,6,0);
        layout->setSpacing(0);
        button_=new QToolButton(this);
        button_->setObjectName("newProjectDropTarget");
        button_->setFixedSize(32,30);
        layout->addWidget(button_,0,Qt::AlignVCenter);
        line_=new QWidget(tabs_);
        line_->setObjectName("workspaceSeparator");
        line_->setAttribute(Qt::WA_TransparentForMouseEvents);
        line_->setAttribute(Qt::WA_AlwaysStackOnTop);
        line_->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Ignored);
        line_->setStyleSheet("background:#303237;");
        line_->hide();
        tabs_->installEventFilter(this);
    }
    QToolButton* button() const { return button_; }
    QSize sizeHint() const override {
        const int strip=tabs_?std::max(30,tabs_->tabBar()->sizeHint().height()):30;
        return {38,strip};
    }
protected:
    bool eventFilter(QObject* watched,QEvent* event) override {
        if(watched==tabs_&&(event->type()==QEvent::Resize||event->type()==QEvent::Show||event->type()==QEvent::LayoutRequest))schedule();
        return false;
    }
    void moveEvent(QMoveEvent* event) override { QWidget::moveEvent(event); schedule(); }
    void resizeEvent(QResizeEvent* event) override { QWidget::resizeEvent(event); schedule(); }
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(),QColor(0x20,0x21,0x25));
    }
private:
    void schedule(){if(pending_)return;pending_=true;QTimer::singleShot(0,this,[this]{pending_=false;align();});}
    void align(){
        if(!tabs_||aligning_)return;
        const QRect bar=tabs_->tabBar()->geometry();
        if(bar.height()<30||bar.width()<=0)return;
        const QRect goal(bar.right()+1,bar.top(),38,bar.height());
        const QRect separator(bar.left(),bar.bottom(),goal.right()-bar.left()+1,1);
        if(auto* dock=tabs_->window()->findChild<QDockWidget*>("layersDock"))
            if(auto* title=dock->titleBarWidget())if(title->height()!=bar.height())title->setFixedHeight(bar.height());
        if(geometry()==goal&&line_->geometry()==separator)return;
        aligning_=true;
        setGeometry(goal);
        line_->setGeometry(separator);
        line_->show();
        line_->raise();
        aligning_=false;
    }
    QTabWidget* tabs_{};
    QToolButton* button_{};
    QWidget* line_{};
    bool aligning_{};
    bool pending_{};
};
void detach(NativeCanvas*canvas){
    canvas->pointerDown={};canvas->pointerDoubleClick={};canvas->pointerMove={};canvas->pointerUp={};canvas->pointerCancel={};canvas->pointerInterrupted={};
    canvas->pointerHover={};canvas->pointerLeave={};canvas->rightPointerDown={};canvas->rightPointerMove={};canvas->navigationAllowed={};canvas->viewportProvider={};canvas->navigatorProvider={};
    canvas->guideDragUpdated={};canvas->guideDragFinished={};canvas->guideDragCanceled={};
}
}
MainWindow::~MainWindow(){
    if(movePreviewJob_)movePreviewJob_->shutdown();
    if(viewportComposite_)viewportComposite_->shutdown();
    detachOpacityPercent();
    qApp->removeEventFilter(this);stopSelectionAutoscroll();
    cancelEditPanel();
    std::vector<QPointer<ui::EditPanelSession>> panels;
    for(auto* object:findChildren<QObject*>())if(auto* session=dynamic_cast<ui::EditPanelSession*>(object))panels.push_back(session);
    for(const auto& session:panels)if(session)delete session.data();
    // Qt destroys child widgets after derived members. No callback may then use
    // the already-destroyed project vector or a worker's completion host.
    for(auto&p:projects_)detach(p->canvas);
    if(auto* panel=ui::LayerPanelController::find(layers_))panel->finishVisibilitySwipe();
    finishVisibilitySwipe();
    delete ui::LayerPanelController::find(layers_);
    delete workspaceDrops_;workspaceDrops_=nullptr;
    delete ui::ProjectLayerCopyJob::find(this);
    delete ui::LayerMergeJob::find(this);
    while(auto* job=ui::LastFilterJob::find(this))delete job;
    delete commands_;commands_=nullptr;
    delete importQueue_;importQueue_=nullptr;
}
void MainWindow::initializeProject(EditorProject&project){
    if(!tabs_->cornerWidget(Qt::TopRightCorner)){
        auto* corner=new WorkspaceTabCorner(tabs_);auto* button=corner->button();
        for(auto* action:findChildren<QAction*>())if(action->property("commandId").toString()=="file.new"){button->setDefaultAction(action);break;}
        if(!button->defaultAction())connect(button,&QToolButton::clicked,this,[this]{newDialog();});
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);button->setIcon(ui::editorIcon(ui::EditorIcon::Plus));
        if(button->defaultAction())connect(button->defaultAction(),&QAction::changed,button,[button]{button->setIcon(ui::editorIcon(ui::EditorIcon::Plus));});
        button->setText("+");button->setAccessibleName("New canvas; drop into a new project");button->setToolTip("New canvas · Drop images or layers here for new projects");tabs_->setCornerWidget(corner,Qt::TopRightCorner);
    }
    project.canvas=new NativeCanvas(warp_);
    project.canvas->showPixelGrid=true;
    project.canvas->pointerDown=[this](QPointF p,Qt::KeyboardModifiers m){try{pointerBegin(p,m);}catch(const std::exception&e){pointerCancel();statusBar()->showMessage(e.what());}};
    project.canvas->pointerDoubleClick=[this](QPointF p,Qt::KeyboardModifiers m){try{pointerBegin(p,m,2);}catch(const std::exception&e){pointerCancel();statusBar()->showMessage(e.what());}};
    project.canvas->pointerMove=[this](QPointF p,Qt::KeyboardModifiers m){try{pointerUpdate(p,m);}catch(const std::exception&e){pointerCancel();statusBar()->showMessage(e.what());}};
    project.canvas->pointerUp=[this](QPointF p,Qt::KeyboardModifiers m){try{pointerEnd(p,m);}catch(const std::exception&e){pointerCancel();statusBar()->showMessage(e.what());}};
    project.canvas->pointerCancel=[this]{pointerCancel();};project.canvas->pointerInterrupted=[this]{interruptPointer();};
    project.canvas->pointerHover=[this](QPointF point,Qt::KeyboardModifiers flags){if(editPanel_)editPanel_->sampleHover({point.x(),point.y()});updateBrushPointer(point,flags);hoverSelection(point,flags);};
    project.canvas->pointerLeave=[this]{clearBrushPointer();if(selectionGesture_.draft()&&selectionGesture_.draft()->kind==editing::LassoKind::Polygonal){selectionGesture_.moveCursor({});refreshSelectionGesture();}};
    project.canvas->rightPointerDown=[this](QPointF point,Qt::KeyboardModifiers flags){return beginBrushTip(point,flags);};
    project.canvas->rightPointerMove=[this](QPointF point,Qt::KeyboardModifiers flags,bool finish){updateBrushTip(point,flags,finish);};
    project.canvas->navigationAllowed=[this]{return canvasNavigationAllowed();};
    project.canvas->textKey=[this](QKeyEvent* event){return handleTextKey(event);};
    project.canvas->textInput=[this](QInputMethodEvent* event){handleTextInput(event);};
    auto*owner=&project;project.canvas->viewportProvider=[this,owner](double x,double y,double w,double h,double units){return brushViewport(*owner,x,y,w,h,units);};
    project.canvas->navigatorProvider=[this,owner](int edge){return navigatorComposite(*owner,edge);};
    project.canvas->guideDragUpdated=[this](bool horizontal,double position){statusBar()->showMessage(QString("%1 guide %2").arg(horizontal?QStringLiteral("Horizontal"):QStringLiteral("Vertical")).arg(position,0,'f',1));};
    project.canvas->guideDragCanceled=[this]{statusBar()->clearMessage();};
    project.canvas->guideDragFinished=[this](bool horizontal,double position,int index){
        try{
            if(auto* item=findChild<QAction*>("viewGuides"))item->setChecked(true);
            if(auto* surface=canvas())surface->showGuides=true;
            if(index<0)edit("Add Guide",[horizontal,position](Document& document){document.guides.push_back({newId(),horizontal,position});});
            else edit("Move Guide",[index,horizontal,position](Document& document){
                if(index<0||size_t(index)>=document.guides.size()||document.guides[size_t(index)].horizontal!=horizontal)return;
                document.guides[size_t(index)].position=position;
            });
        }catch(const std::exception& error){statusBar()->showMessage(error.what());}
    };
}
bool MainWindow::canSwitchProjects(){
    const auto state=commandState(activeProject_);return !state.modalDialog&&ui::canSwitchProject(state);
}
void MainWindow::switchProject(){
    auto*next=current();if(next==activeProject_){refresh(false);return;}
    if(activeProject_&&!selectingProjectForOpen_&&!canSwitchProjects()){
        for(size_t i=0;i<projects_.size();++i)if(projects_[i].get()==activeProject_){QSignalBlocker block(tabs_);tabs_->setCurrentIndex(int(i));break;}
        return;
    }
    if(auto* panel=ui::LayerPanelController::find(layers_))panel->finishVisibilitySwipe();
    finishVisibilitySwipe();finishOpacityEdit();
    if(textSession_&&textSession_->owner==activeProject_)finishText();
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
    auto*row=new QHBoxLayout;row->addStretch();auto*panel=new QWidget;panel->setObjectName("newCanvasCard");panel->setMaximumWidth(440);
    panel->setStyleSheet("QWidget#newCanvasCard { background: #27292e; border: 1px solid #3b3e46; border-radius: 14px; } QLabel { background: transparent; } QLabel#newCanvasHeading { font-size: 28px; font-weight: 500; margin-bottom: 14px; }");
    auto*form=new QFormLayout(panel);form->setContentsMargins(28,28,28,26);form->setVerticalSpacing(14);form->setHorizontalSpacing(18);
    auto*heading=new QLabel("New canvas");heading->setObjectName("newCanvasHeading");QFont font=heading->font();font.setPointSize(20);heading->setFont(font);form->addRow(heading);
    auto*width=new QSpinBox;auto*height=new QSpinBox;for(auto*spin:{width,height})spin->setRange(1,30000);
    width->setObjectName("newCanvasWidth");height->setObjectName("newCanvasHeight");width->setAccessibleName("Width in pixels");height->setAccessibleName("Height in pixels");width->setValue(1920);height->setValue(1080);
    if(!first){const auto image=QApplication::clipboard()->image();if(!image.isNull()&&image.width()<=30000&&image.height()<=30000){width->setValue(image.width());height->setValue(image.height());}}
    form->addRow("Width (pixels)",width);form->addRow("Height (pixels)",height);form->addRow(new QLabel("Transparent canvas · sRGB"));
    auto*buttons=new QDialogButtonBox;auto*open=buttons->addButton("Open project",QDialogButtonBox::ActionRole);auto*import=buttons->addButton("Import image",QDialogButtonBox::ActionRole);import->setObjectName("welcomeImportImage");auto*create=buttons->addButton("Create canvas",QDialogButtonBox::AcceptRole);create->setObjectName("createCanvas");create->setDefault(true);form->addRow(buttons);
    connect(open,&QPushButton::clicked,this,[this]{openProjectDialog();});connect(import,&QPushButton::clicked,this,[this]{importWelcomeImage();});
    connect(create,&QPushButton::clicked,this,[this,raw,width,height]{
        if(raw!=current()||raw->document||raw->importing||raw->projectBusy)return;
        Document document;document.id=newId();document.width=width->value();document.height=height->value();Layer layer;layer.id=newId();layer.name="Layer 1";layer.transform={0,0,double(document.width),double(document.height)};document.layers.push_back(layer);
        raw->history.begin("New Canvas",std::nullopt,{});raw->document=std::move(document);raw->active=layer.id;raw->selected={layer.id};raw->history.end(raw->document,raw->active);refresh();raw->canvas->fit();raw->canvas->setFocus();
    });
    row->addWidget(panel);row->addStretch();outer->addLayout(row);outer->addStretch();raw->page->addWidget(raw->welcome);raw->page->addWidget(raw->canvas);
    projects_.push_back(std::move(project));const auto index=tabs_->addTab(raw->page,raw->defaultTitle);tabs_->setCurrentIndex(index);switchProject();width->setFocus();return *raw;
}
EditorProject& MainWindow::addProject(Document document,QString title,bool reuseEmpty){
    validateDocument(document);
    const auto restoreNumber=qScopeGuard([this,reuseEmpty,number=nextProjectNumber_]{if(!reuseEmpty)nextProjectNumber_=number;});
    // A drop marks the workspace busy, which otherwise refuses the tab change
    // onto the project that is about to receive the document. The refused
    // change then deletes the still-active project.
    const bool previousSelecting=selectingProjectForOpen_;
    selectingProjectForOpen_=true;
    const auto restoreSelecting=qScopeGuard([this,previousSelecting]{selectingProjectForOpen_=previousSelecting;});
    const bool replaceWelcome=!reuseEmpty&&projects_.size()==1&&!projects_.front()->document&&!projects_.front()->importing&&!projects_.front()->projectBusy&&(!importQueue_||!importQueue_->contains(projects_.front()->canvas));
    auto&project=addEmptyProject(reuseEmpty);project.document=std::move(document);project.active=project.document->layers.empty()?"":project.document->layers.back().id;project.selected={project.active};project.defaultTitle=title;if(!project.document->guides.empty())project.canvas->showGuides=true;
    if(replaceWelcome){
        // ProjectWorkspace.swift:81-86: attach the successfully loaded fresh
        // session before discarding the sole empty welcome session. Tab-change
        // callbacks already selected the new owner in addEmptyProject(false).
        auto* previous=projects_.front().get();if(activeProject_==previous)activeProject_=nullptr;detach(previous->canvas);
        previous->page->setEnabled(false);previous->page->hide();
        {QSignalBlocker blocked(tabs_);tabs_->removeTab(0);}
        previous->page->deleteLater();projects_.erase(projects_.begin());
        if(!activeProject_)activeProject_=&project;
    }
    refresh();project.canvas->fit();project.canvas->setFocus();return project;
}
void MainWindow::newDialog(){if(canSwitchProjects())addEmptyProject(false);}
CompositeViewport MainWindow::navigatorComposite(EditorProject& project,int maxEdge){
    if(!project.document||maxEdge<1)return {};
    const int edge=std::clamp(maxEdge,1,256);
    const double units=std::clamp(std::max(1.,std::max(project.document->width,project.document->height)/double(edge)),1./32.,30000.);
    // A separate cache keeps the interactive viewport tiles. The same units key
    // would discard them on every overview pass.
    auto render=[&](const Document& document,std::shared_ptr<const LayerRenderPreview> layerPreview={}){
        return project.navigatorCache.renderViewport(document,0,0,document.width,document.height,units,4,16,std::move(layerPreview));
    };
    if(auto preview=editPanelPreview(project))return render(*preview);
    if(project.blendPreview&&project.active==project.blendPreview->first){
        auto preview=*project.document;
        for(auto& layer:preview.layers)if(layer.id==project.blendPreview->first){layer.blend=project.blendPreview->second;break;}
        return render(preview);
    }
    if(project.textPreview)return render(*project.document,project.textPreview);
    if(project.gradientPreview)return render(*project.document,project.gradientPreview->renderPreview());
    if(project.retouchPreview)return render(*project.document,project.retouchPreview);
    if(project.brushPreview)return render(*project.document,project.brushPreview->renderPreview());
    return render(*project.document);
}
}
