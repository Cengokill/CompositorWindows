#define main layer_action_contracts_main
#include "LayerActionAccessibilityBase.cpp"
#undef main
#include "ui/EditPanelSession.h"
#include "ui/CommandRegistry.h"
#include "effects/Adjustments.h"
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QPushButton>
#include <QTemporaryDir>
#include <QSettings>
#include <QDir>
#include <QFileInfo>
#include <QStyleOptionViewItem>
#include <QLineEdit>
#include <QLabel>
#include <QAccessible>
namespace {
Document pendingFixture(){Document d;d.id=newId();d.width=100;d.height=80;for(int n=0;n<2;++n){Layer l;l.id=n?"target":"source";l.name=n?"Target":"Source";l.transform={0,0,100,80};std::vector<uint8_t> bytes(100*80*4);for(int y=0;y<80;++y)for(int x=0;x<100;++x){auto i=size_t((y*100+x)*4);bytes[i]=uint8_t(n?200:x*2);bytes[i+1]=uint8_t(n?40:y*3);bytes[i+2]=uint8_t(n?70:140);bytes[i+3]=255;}l.raster=Raster::fromRgba(100,80,bytes.data(),400);l.mask=Mask{std::make_shared<GrayRaster>(GrayRaster{100,80,std::vector<uint8_t>(8000,255)})};d.layers.push_back(l);}Layer live;live.id="live";live.name="Live exposure";live.transform={0,0,100,80};live.adjustmentJson=effects::defaultAdjustmentJson("Exposure");effects::validateAdjustmentJson(live.adjustmentJson);d.layers.push_back(live);return d;}
QAction* pendingCommand(MainWindow& window,const char* id){for(auto* a:window.findChildren<QAction*>())if(a->property("commandId")==id&&a->isVisible())return a;throw std::runtime_error(std::string("Visible command missing: ")+id);}
void pendingTrigger(MainWindow& window,const char* id){auto* a=pendingCommand(window,id);require(a->isEnabled(),"Setup command enabled");a->trigger();}
void pendingSend(EditorProject& project,QEvent::Type type,Point point){auto* canvas=project.canvas;auto mapped=canvas->viewMapping().toView(point);QPointF at(mapped.x,mapped.y);const bool up=type==QEvent::MouseButtonRelease,move=type==QEvent::MouseMove;QMouseEvent e(type,at,canvas->mapToGlobal(at.toPoint()),move?Qt::NoButton:Qt::LeftButton,up?Qt::NoButton:Qt::LeftButton,{});QApplication::sendEvent(canvas,&e);}
void pendingClick(EditorProject& project,Point point){pendingSend(project,QEvent::MouseButtonPress,point);pendingSend(project,QEvent::MouseButtonRelease,point);}
void pendingDrag(EditorProject& project,Point first,Point last,bool release=true){pendingSend(project,QEvent::MouseButtonPress,first);pendingSend(project,QEvent::MouseMove,last);if(release)pendingSend(project,QEvent::MouseButtonRelease,last);}
QDoubleSpinBox* pendingNumber(QObject& owner,const QString& name){for(auto* n:owner.findChildren<QDoubleSpinBox*>())if(n->accessibleName()==name)return n;throw std::runtime_error("Named number missing: "+name.toStdString());}
ui::EditPanelSession* pendingPanel(MainWindow& window){for(auto* object:window.findChildren<QObject*>())if(auto* panel=dynamic_cast<ui::EditPanelSession*>(object))if(panel->panel()&&panel->panel()->isVisible())return panel;return nullptr;}
QPushButton* pendingApply(QDialog* panel){require(panel,"Owned panel");auto* box=panel->findChild<QDialogButtonBox*>();require(box,"Panel button box");auto* button=box->button(QDialogButtonBox::Apply);require(button,"Panel Apply");return button;}
void pendingReady(ui::EditPanelSession* panel){QPointer<ui::EditPanelSession> guard=panel;QElapsedTimer t;t.start();while(guard&&!pendingApply(guard->panel())->isEnabled()){if(t.elapsed()>=8000){for(auto* label:guard->panel()->findChildren<QLabel*>())std::cerr<<"PANEL_LABEL "<<label->text().toStdString()<<"\n";throw std::runtime_error("Panel preview completes");}QTest::qWait(5);}require(guard&&guard->panel()->isVisible(),"Owned panel remains visible");}
struct PendingCanvasEvents:QObject{EditorProject& project;explicit PendingCanvasEvents(EditorProject& p):project(p){p.canvas->installEventFilter(this);}bool eventFilter(QObject*,QEvent* event)override{if(event->type()==QEvent::FocusOut||event->type()==QEvent::WindowDeactivate||event->type()==QEvent::UngrabMouse||event->type()==QEvent::MouseButtonRelease)std::cerr<<"CANVAS_EVENT "<<int(event->type())<<" brush="<<bool(project.brushPreview)<<" retouch="<<bool(project.retouchPreview)<<"\n";return false;}};
struct PendingObservation {Document before;Layer source,target,live;std::vector<Point> polygon;std::optional<editing::Rect> crop;QPointer<ui::EditPanelSession> panel;};
PendingObservation preparePending(const std::string& scenario,MainWindow& window,EditorProject& project){
    PendingObservation o;o.before=*project.document;o.source=layer(project,"source");o.target=layer(project,"target");o.live=layer(project,"live");
    if(scenario.starts_with("gradient")||scenario=="pending_link"){pendingTrigger(window,"tool.gradient");pendingDrag(project,{10,30},{80,30});require(bool(project.gradientPreview)&&pendingCommand(window,"gradient.apply")->isEnabled(),"Pending gradient created");}
    else if(scenario=="crop_other_mask"||scenario=="non_target_guards"){pendingTrigger(window,"tool.crop");pendingDrag(project,{10,10},{70,50});o.crop=project.canvas->cropOverlay();require(o.crop==editing::Rect{10,10,60,40}&&pendingCommand(window,"crop.apply")->isEnabled(),"Custom crop draft created");}
    else if(scenario=="polygon_other_image"){pendingTrigger(window,"tool.polygon");for(Point point:std::array<Point,3>{{{10,10},{80,10},{80,60}}})pendingClick(project,point);require(project.canvas->selectionDraft()&&project.canvas->selectionDraft()->points.size()==3,"Three-point polygon created");o.polygon=project.canvas->selectionDraft()->points;}
    else if(scenario.starts_with("transform")){pendingTrigger(window,"tool.move");pendingTrigger(window,"transform.free");pendingNumber(window,"X")->setValue(17);project.canvas->setFocus();require(pendingCommand(window,"transform.apply")->isEnabled()&&layer(project,"source").transform.x==17&&project.history.undoCount()==0,"Persistent transform draft created");}
    else if(scenario=="brush_guard"||scenario=="warp_guard"){pendingTrigger(window,scenario=="brush_guard"?"tool.brush":"tool.retouch");window.raise();window.activateWindow();project.canvas->setFocus();QTest::qWait(30);require(project.canvas->isVisible()&&project.canvas->hasFocus(),"Visible canvas owns focus before stroke");pendingDrag(project,{20,20},{45,20},false);require(scenario=="brush_guard"?bool(project.brushPreview):bool(project.retouchPreview),"Running source stroke created");}
    else{
        const char* id=scenario=="hue_other_image"?"adjust.hue_saturation":scenario=="filter_other_mask"?"filter.gaussian":scenario=="levels_other_image"?"adjust.levels":"adjust.edit";
        if(scenario=="live_other_image")mouse(window,"live",1);
        pendingTrigger(window,id);o.panel=pendingPanel(window);require(o.panel,"Nonmodal captured edit opened");pendingReady(o.panel);
        if(scenario=="hue_other_image")pendingNumber(*o.panel->panel(),"Hue")->setValue(35);
        else if(scenario=="filter_other_mask")pendingNumber(*o.panel->panel(),"Radius")->setValue(3);
        else if(scenario=="live_other_image")pendingNumber(*o.panel->panel(),"Exposure")->setValue(1);
        else pendingNumber(*o.panel->panel(),"Gamma")->setValue(1.5);
        pendingReady(o.panel);require(!project.projectBusy&&!QApplication::activeModalWidget(),"Panel is ready and nonmodal");
    }
    require(project.history.undoCount()==0,"Draft setup adds no committed history");return o;
}
void checkPending(const std::string& scenario,MainWindow& window,EditorProject& project,const PendingObservation& o){
    if(scenario=="pending_link"||scenario=="non_target_guards"){
        assertTarget(project,"source",false);require(*project.document==o.before&&project.history.undoCount()==0,"Guarded non-target operation preserves document, target and history");
        if(scenario=="pending_link")require(project.gradientPreview&&layer(project,"source").mask->linked,"Pending link leaves gradient and linked mask unchanged");
        else require(project.canvas->cropOverlay()==o.crop&&pendingCommand(window,"crop.apply")->isEnabled(),"Blocked row operations retain crop draft");return;
    }
    const bool same=scenario=="gradient_same_mask"||scenario=="transform_same_mask";
    const bool masked=same||scenario=="crop_other_mask"||scenario=="filter_other_mask";
    if(scenario=="levels_other_image"||scenario=="brush_guard"||scenario=="warp_guard"){
        assertTarget(project,"source",false);require(*project.document==o.before&&project.history.undoCount()==0,"Guarded target invocation preserves canonical state and history");
        if(scenario=="levels_other_image")require(o.panel&&o.panel->panel()->isVisible(),"Levels guard retains editor");
        else require(scenario=="brush_guard"?bool(project.brushPreview):bool(project.retouchPreview),"UIA target call retains running stroke");return;
    }
    assertTarget(project,same?"source":"target",masked);
    require(layer(project,"target")==o.target,"Target selection does not alter destination pixels or mask");
    if(scenario.starts_with("gradient")){
        require(!project.gradientPreview&&project.history.undoCount()==1&&project.history.undoName()=="Gradient","Thumbnail resolves gradient in one source transaction");require(layer(project,"source").raster!=o.source.raster&&layer(project,"source").mask==o.source.mask,"Gradient commits only captured image");
    }else if(scenario=="crop_other_mask"){
        require(project.canvas->cropOverlay()==o.crop&&pendingCommand(window,"crop.apply")->isEnabled(),"Thumbnail keeps exact pending crop");require(*project.document==o.before&&project.history.undoCount()==0,"Crop target switch leaves canonical/history unchanged");
    }else if(scenario=="polygon_other_image"){
        require(project.canvas->selectionDraft()&&project.canvas->selectionDraft()->points==o.polygon,"Thumbnail keeps exact polygon draft");require(*project.document==o.before&&project.history.undoCount()==0,"Polygon target switch leaves canonical/history unchanged");
    }else if(scenario=="transform_other_image"){
        require(!pendingCommand(window,"transform.apply")->isEnabled()&&project.history.undoCount()==1&&layer(project,"source").transform.x==17,"Different layer target commits transform once");
    }else if(scenario=="transform_same_mask"){
        require(pendingCommand(window,"transform.apply")->isEnabled()&&project.history.undoCount()==0&&layer(project,"source").transform.x==17,"Same layer target retains uncommitted transform");
    }else{
        require(o.panel&&o.panel->panel()->isVisible()&&pendingPanel(window)==o.panel,"Thumbnail preserves captured nonmodal editor");
        require(*project.document==o.before&&project.history.undoCount()==0,"Target switch preserves unapplied pixels/metadata/history");
        pendingApply(o.panel->panel())->click();QElapsedTimer deadline;deadline.start();while(o.panel&&o.panel->panel()&&o.panel->panel()->isVisible()){require(deadline.elapsed()<8000,"Captured editor Apply completes");QTest::qWait(5);}
        require(project.history.undoCount()==1&&layer(project,"target")==o.target,"Apply creates one history step and leaves newly selected target unchanged");
        if(scenario=="live_other_image")require(layer(project,"live").adjustmentJson!=o.live.adjustmentJson&&layer(project,"source")==o.source,"Live Apply changes only captured adjustment metadata");
        else require(layer(project,"source").raster!=o.source.raster&&layer(project,"live")==o.live,"Ordinary Apply changes captured source image");
        assertTarget(project,"target",masked);
    }
}
void runPending(const std::string& key,MainWindow& window,EditorProject& project,Client& client,QJsonObject& report){
    const bool uia=key.ends_with("_uia"),qt=key.ends_with("_qt");const auto scenario=key.substr(0,key.size()-(uia?4:qt?3:6));
    const bool same=scenario=="gradient_same_mask"||scenario=="transform_same_mask";const bool mask=same||scenario=="crop_other_mask"||scenario=="filter_other_mask";
    PendingObservation observation;
    if(qt){gui(window,[&]{
        observation=preparePending(scenario,window,project);
        auto* root=QAccessible::queryAccessibleInterface(tree(window)->parentWidget());require(root,"Qt accessible layer container");QAccessibleInterface* target=nullptr;
        for(int i=0;i<root->childCount();++i){auto* child=root->child(i);if(child&&child->role()==QAccessible::Button&&child->text(QAccessible::Name)=="Select image: Target")target=child;}
        require(target&&!target->state().disabled,"Synchronous Qt target button exists and is enabled");auto* action=target->actionInterface();require(action&&action->actionNames().contains(QAccessibleActionInterface::pressAction()),"Qt target press action");action->doAction(QAccessibleActionInterface::pressAction());
        checkPending(scenario,window,project,observation);
    });return;}
    gui(window,[&]{observation=preparePending(scenario,window,project);});
    if(scenario=="non_target_guards"){
        gui(window,[&]{
            auto* list=tree(window);auto* target=item(window,"target");const auto row=list->visualItemRect(target);
            QStyleOptionViewItem option;option.initFrom(list);option.rect=list->visualRect(list->indexFromItem(target,0));option.features=QStyleOptionViewItem::HasCheckIndicator;option.checkState=target->checkState(0);
            const auto eye=list->style()->subElementRect(QStyle::SE_ItemViewItemCheckIndicator,&option,list);
            QTest::mouseClick(list->viewport(),Qt::LeftButton,Qt::NoModifier,eye.center());
            QTest::mouseClick(list->viewport(),Qt::LeftButton,Qt::NoModifier,QPoint(list->columnWidth(0)-15,row.center().y()));
            list->setFocus();QTest::keyClick(list,Qt::Key_F2);
            for(auto* text:list->findChildren<QLineEdit*>())require(!text->isVisible(),"Pending edit cannot start row rename");
            auto* controller=ui::LayerPanelController::find(list);std::unique_ptr<QMimeData> mime(controller->dragMime());
            require(!controller->accepts(mime.get(),QPoint(5,list->viewport()->height()-5),Qt::NoModifier),"Pending edit rejects internal layer drop before preparation");
        });
        gui(window,[&]{checkPending(scenario,window,project,observation);});return;
    }
    const QString target= same||scenario=="pending_link"?"Source":"Target";
    const QString name=scenario=="pending_link"?"Unlink mask: Source":(mask?"Select mask: ":"Select image: ")+target;
    if(uia){auto element=button(client,name,report);BOOL enabled=FALSE;checked(element->get_CurrentIsEnabled(&enabled),"Pending target enabled state");require(enabled,"Source thumbnail remains enabled while draft/editor exists");if(scenario=="brush_guard"||scenario=="warp_guard")gui(window,[&]{std::cout<<"PRE_INVOKE brush="<<bool(project.brushPreview)<<" retouch="<<bool(project.retouchPreview)<<" canvas_focus="<<project.canvas->hasFocus()<<" active_window="<<(QApplication::activeWindow()==&window)<<"\n";require(scenario=="brush_guard"?bool(project.brushPreview):bool(project.retouchPreview),"Stroke remains active immediately before UIA Invoke");});checked(invokePattern(element.Get())->Invoke(),"UIA pending target Invoke");drain(window);}
    else{gui(window,[&]{require(tree(window)->isEnabled(),"Mouse target tree is enabled before event dispatch");window.activateWindow();tree(window)->setFocus();mouse(window,same||scenario=="pending_link"?"source":"target",scenario=="pending_link"?2:mask?3:1);});}
    gui(window,[&]{checkPending(scenario,window,project,observation);});
}
}
int retained_pending_target_main(int argc,char** argv){
    QApplication app(argc,argv);std::cout<<std::unitbuf;if(argc!=3){std::cerr<<"Usage: pending_layer_selection_tests CASE REPORT.json\n";return 2;}
    QDir().mkpath(QFileInfo(QString::fromUtf8(__FILE__)).dir().filePath("fixtures"));QTemporaryDir settings(QFileInfo(QString::fromUtf8(__FILE__)).dir().filePath("fixtures/settings-XXXXXX"));require(settings.isValid(),"Owned settings fixture");settings.setAutoRemove(false);QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    MainWindow window(true);auto& project=window.addProject(pendingFixture(),"Pending thumbnail fixture");window.resize(1200,850);window.show();window.activateWindow();QTest::qWait(20);mouse(window,"source",1);project.canvas->setFocus();
    PendingCanvasEvents observedCanvas(project);const auto hwnd=reinterpret_cast<HWND>(window.winId());std::atomic<int> result{2};std::thread worker;
    QTimer::singleShot(300,&window,[&]{worker=std::thread([&]{QJsonObject report{{"schema","PENDING_LAYER_SELECTION_V1"},{"case",argv[1]},{"human_acceptance",false},{"settings_fixture",settings.path()}};bool initialized=false;try{checked(CoInitializeEx(nullptr,COINIT_MULTITHREADED),"UIA MTA");initialized=true;Client client(hwnd);runPending(argv[1],window,project,client,report);report["status"]="passed";result=0;std::cout<<"PASS "<<argv[1]<<'\n';}catch(const std::exception& error){report["status"]="failed";report["error"]=error.what();result=1;std::cerr<<"FAIL "<<argv[1]<<": "<<error.what()<<'\n';}QFile output(QString::fromLocal8Bit(argv[2]));if(!output.open(QIODevice::WriteOnly)||output.write(QJsonDocument(report).toJson())<0)result=2;if(initialized)CoUninitialize();QMetaObject::invokeMethod(&window,[&]{app.exit(result.load());},Qt::QueuedConnection);});});app.exec();if(worker.joinable())worker.join();return result;
}
