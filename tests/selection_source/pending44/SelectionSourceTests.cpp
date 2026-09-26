#include "ui/MainWindow.h"
#include "ui/LayerPanel.h"
#include <QApplication>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTest>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <windows.h>

using namespace compositor;
using namespace compositor::editing;
namespace {
constexpr auto source="CompositorTests/SelectionTests.swift";
QJsonArray checks;
QJsonArray nativeChecks;
QJsonArray unavailableChecks;
int failures{};
int nativeFailures{};
struct UnavailableSourceObservation:std::runtime_error{using std::runtime_error::runtime_error;};
void guard(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void check(int line,bool value,const char* kind){checks.append(QJsonObject{{"source_file",source},{"source_line",line},{"kind",kind},{"passed",value}});if(!value){++failures;std::fprintf(stderr,"FAIL %s line%d\n",kind,line);}}
void expect(int line,bool value){check(line,value,"expect");}
void required(int line,bool value){check(line,value,"require");if(!value)throw std::runtime_error("Original source requirement failed");}
void nativeExpect(const char* name,bool value){nativeChecks.append(QJsonObject{{"name",name},{"passed",value}});if(!value){++nativeFailures;std::fprintf(stderr,"FAIL native %s\n",name);}}
void unavailable(int line,const char* why){unavailableChecks.append(QJsonObject{{"source_file",source},{"source_line",line},{"kind","expect"},{"reason",why}});throw UnavailableSourceObservation(why);}

// Standard explicit-template instantiation grants access to the existing
// controller without replacing its definition or changing the production ABI.
// Only source-session entry points/observations are exposed. All geometry,
// movement, history, command gates and actual event routing remain production.
template<class Tag,typename Tag::type Member>struct Access {friend typename Tag::type member(Tag){return Member;}};
#define SOURCE_FIELD(Tag,Type,Name) struct Tag{using type=Type MainWindow::*;friend type member(Tag);};template struct Access<Tag,&MainWindow::Name>;
#define SOURCE_METHOD(Tag,Type,Name) struct Tag{using type=Type;friend type member(Tag);};template struct Access<Tag,&MainWindow::Name>;
SOURCE_FIELD(ToolField,ProjectTool,tool_)
SOURCE_FIELD(KindField,LassoKind,lassoKind_)
SOURCE_FIELD(EllipseField,bool,ellipse_)
SOURCE_FIELD(AaField,bool,selectionAntialias_)
SOURCE_FIELD(ModeField,SelectionMode,selectionMode_)
SOURCE_FIELD(GestureField,SelectionGesture,selectionGesture_)
SOURCE_FIELD(OwnerField,EditorProject*,selectionOwner_)
SOURCE_FIELD(PointerOwnerField,EditorProject*,pointerOwner_)
SOURCE_FIELD(MovingField,bool,movingSelection_)
SOURCE_FIELD(PressField,Point,press_)
using VoidMethod=void(MainWindow::*)();
using RefreshMethod=void(MainWindow::*)(bool,bool);
using ToolMethod=void(MainWindow::*)(ProjectTool);
using ResizeMethod=void(MainWindow::*)(bool,double);
using BeginMethod=bool(MainWindow::*)(Point,Qt::KeyboardModifiers,int);
using UpdateMethod=bool(MainWindow::*)(Point,Qt::KeyboardModifiers,bool);
using StateMethod=ui::CommandState(MainWindow::*)(EditorProject*);
using KeysMethod=void(MainWindow::*)(Qt::KeyboardModifiers);
using LoadMethod=void(MainWindow::*)(const std::string&,bool,Qt::KeyboardModifiers);
using AddMethod=void(MainWindow::*)(std::shared_ptr<const Raster>,Point,const char*);
SOURCE_METHOD(Refresh,RefreshMethod,refresh)
SOURCE_METHOD(SelectTool,ToolMethod,selectTool)
SOURCE_METHOD(Finish,VoidMethod,finishSelectionGesture)
SOURCE_METHOD(CancelDraft,VoidMethod,cancelSelectionGesture)
SOURCE_METHOD(CancelPointer,VoidMethod,pointerCancel)
SOURCE_METHOD(RefreshDraft,VoidMethod,refreshSelectionGesture)
SOURCE_METHOD(Resize,ResizeMethod,resizeSelection)
SOURCE_METHOD(Begin,BeginMethod,beginSelection)
SOURCE_METHOD(Update,UpdateMethod,updateSelection)
SOURCE_METHOD(State,StateMethod,commandState)
SOURCE_METHOD(Keys,KeysMethod,selectionModifiersChanged)
SOURCE_METHOD(Load,LoadMethod,loadLayerSelection)
SOURCE_METHOD(Add,AddMethod,addPixelLayer)
#undef SOURCE_FIELD
#undef SOURCE_METHOD

Document blank(int width,int height){Document d;d.id=newId();d.width=width;d.height=height;Layer l;l.id=newId();l.name="Layer 1";l.transform={0,0,double(width),double(height)};d.layers.push_back(l);return d;}
QAction* command(MainWindow& window,const char* id){for(auto* action:window.findChildren<QAction*>())if(action->property("commandId").toString()==id)return action;throw std::runtime_error(std::string("Missing command ")+id);}
Qt::KeyboardModifiers flags(bool shift,bool option){Qt::KeyboardModifiers result;if(shift)result|=Qt::ShiftModifier;if(option)result|=Qt::AltModifier;return result;}
struct Session {
    MainWindow window{true};EditorProject& project;
    Session(int width=100,int height=100):project(window.addProject(blank(width,height))){window.show();QApplication::processEvents();selectTool(ProjectTool::Lasso);}
    explicit Session(const Document& d,bool show):project(window.addProject(d)){if(show){window.show();QApplication::processEvents();}selectTool(ProjectTool::Lasso);}
    auto& document(){return project.document;}
    auto& selection(){return document()->selection;}
    auto& history(){return project.history;}
    auto& gesture(){return window.*member(GestureField{});}
    ProjectTool tool()const{return window.*member(ToolField{});}
    bool sourceLasso()const{return tool()==ProjectTool::Lasso||tool()==ProjectTool::Polygon;}
    LassoKind kind()const{return window.*member(KindField{});}
    bool ellipse()const{return window.*member(EllipseField{});}
    SelectionMode choice()const{return window.*member(ModeField{});}
    void refresh(){(window.*member(Refresh{}))(false,false);}
    void selectTool(ProjectTool value){(window.*member(SelectTool{}))(value);}
    void trigger(const char* id){auto* action=command(window,id);guard(action->isEnabled(),"Source command precondition enabled");action->trigger();}
    void setKind(LassoKind value){auto* combo=window.findChild<QComboBox*>("lassoKind");guard(combo,"Lasso control exists");combo->setCurrentIndex(value==LassoKind::Polygonal?1:0);}
    void setEllipse(bool value){for(auto* combo:window.findChildren<QComboBox*>())if(combo->accessibleName()=="Marquee shape"){combo->setCurrentIndex(value?1:0);return;}throw std::runtime_error("Marquee control missing");}
    void setChoice(SelectionMode value){window.findChild<QComboBox*>("selectionMode")->setCurrentIndex(int(value));}
    void setAA(bool value){for(auto* box:window.findChildren<QCheckBox*>())if(box->text()=="Antialias"){box->setChecked(value);return;}throw std::runtime_error("Antialias control missing");}
    std::shared_ptr<const SelectionOutline> outline(){return selection()?selection()->outline:nullptr;}
    Rect bounds(){auto value=outline();guard(bool(value),"Native outline representation exists");return value->bounds();}
    bool boundsEqual(Rect value){return outline()&&bounds()==value;}
    bool empty(){return outline()&&outline()->empty();}
    void begin(Point point,SelectionMode mode=SelectionMode::Replace){
        window.*member(OwnerField{})=&project;
        const auto k=tool()==ProjectTool::Marquee?(ellipse()?LassoKind::Ellipse:LassoKind::Rectangle):kind();
        gesture().begin(point,k,mode);refresh();
    }
    void extend(Point point){gesture().extend(point);(window.*member(RefreshDraft{}))();}
    void finish(){(window.*member(Finish{}))();}
    void cancel(){(window.*member(CancelDraft{}))();}
    void lasso(std::vector<Point> points,SelectionMode mode=SelectionMode::Replace){begin(points.front(),mode);for(size_t i=1;i<points.size();++i)extend(points[i]);finish();}
    void marquee(Point first,Point last,SelectionMode mode=SelectionMode::Replace,bool square=false,bool centered=false){begin(first,mode);gesture().dragMarquee(last,square,centered);finish();}
    SelectionMode mode(bool shift,bool option){return selectionMode(shift,option,choice());}
    SelectionMode cursorMode(bool shift,bool option){return gesture().cursorMode(shift,option,choice());}
    void held(bool shift,bool option){(window.*member(Keys{}))(flags(shift,option));}
    SelectionMode displayed(){return SelectionMode(window.findChild<QComboBox*>("selectionMode")->currentIndex());}
    bool canModify(){return ui::commandEnabled(ui::CommandGate::ModifySelection,(window.*member(State{}))(&project));}
    void resize(bool expand,double amount){(window.*member(Resize{}))(expand,amount);}
    bool canMove(Point point){
        // A separate real host observes the existing production begin-selection
        // predicate without disturbing this source session's history or draft.
        const auto originalDraft=gesture();const auto originalTool=tool();const auto originalChoice=choice();
        Session probe(*document(),false);probe.project.active=project.active;probe.project.selected=project.selected;
        probe.selectTool(originalTool);probe.window.*member(ModeField{})=originalChoice;probe.refresh();
        auto press=[&](Point at,Qt::KeyboardModifiers modifiers){
            auto* canvas=probe.project.canvas;const auto mapped=canvas->viewMapping().toView(at);const QPointF local(mapped.x,mapped.y);
            QMouseEvent event(QEvent::MouseButtonPress,local,canvas->mapToGlobal(local.toPoint()),Qt::LeftButton,Qt::LeftButton,modifiers);
            QApplication::sendEvent(canvas,&event);
        };
        if(originalDraft.active()){
            const auto& draft=*originalDraft.draft();press(draft.points.front(),draft.mode==SelectionMode::Add?Qt::ShiftModifier:draft.mode==SelectionMode::Subtract?Qt::AltModifier:Qt::NoModifier);
            probe.gesture()=originalDraft;probe.refresh();
        }
        press(point,{});
        const bool result=probe.window.*member(MovingField{});
        std::fprintf(stdout,"canMove observer originalDraft=%d probeDraft=%d result=%d\n",int(originalDraft.active()),int(probe.gesture().active()),int(result));
        guard(gesture().active()==originalDraft.active(),"Read-only move observer preserves source session draft");
        QTest::keyClick(probe.project.canvas,Qt::Key_Escape);return result;
    }
    bool beginMove(){
        Point point{};if(auto value=outline();value&&!value->empty()){
            const auto box=value->bounds();point={box.x+box.width/2,box.y+box.height/2};
            guard(value->contains(point),"These original move fixtures have an interior center");
        }
        window.*member(PointerOwnerField{})=&project;(window.*member(Begin{}))(point,{},1);
        const bool result=window.*member(MovingField{});if(!result)(window.*member(CancelPointer{}))();return result;
    }
    void move(Point offset){const auto start=window.*member(PressField{});(window.*member(Update{}))({start.x+offset.x,start.y+offset.y},{},false);}
    void endMove(){(window.*member(Update{}))({}, {},true);window.*member(PointerOwnerField{})=nullptr;}
    void nudge(int dx,int dy){const int key=dx>0?Qt::Key_Right:dx<0?Qt::Key_Left:dy<0?Qt::Key_Up:Qt::Key_Down;QTest::keyClick(project.canvas,Qt::Key(key),std::abs(dx)+std::abs(dy)==10?Qt::ShiftModifier:Qt::NoModifier);}
    void load(const std::string& id,bool mask,SelectionMode mode=SelectionMode::Replace){(window.*member(Load{}))(id,mask,mode==SelectionMode::Add?Qt::ShiftModifier:mode==SelectionMode::Subtract?Qt::AltModifier:Qt::NoModifier);}
    int coverage(int x,int y){
        required(23,document().has_value());required(24,selection().has_value());
        guard(bool(selection()->coverage),"Selection supplies production coverage");const auto& gray=*selection()->coverage;
        QImage context(document()->width,document()->height,QImage::Format_Grayscale8);
        required(25,!context.isNull());
        for(int row=0;row<context.height();++row)for(int column=0;column<context.width();++column)context.scanLine(row)[column]=gray.pixel(column,row);
        required(28,context.constBits()!=nullptr);guard(x>=0&&y>=0&&x<context.width()&&y<context.height(),"Source sample coordinates valid");return context.constScanLine(y)[x];
    }
    void send(QEvent::Type type,Point point,Qt::KeyboardModifiers modifiers,int sourceRequire){
        auto* canvas=project.canvas;const auto mapped=canvas->viewMapping().toView(point);const QPointF at(mapped.x,mapped.y);
        const bool up=type==QEvent::MouseButtonRelease,drag=type==QEvent::MouseMove;
        auto event=std::make_unique<QMouseEvent>(type,at,canvas->mapToGlobal(at.toPoint()),drag?Qt::NoButton:Qt::LeftButton,up?Qt::NoButton:Qt::LeftButton,modifiers);
        if(sourceRequire)required(sourceRequire,bool(event));else guard(bool(event),"Supplemental native event exists");QApplication::sendEvent(canvas,event.get());
    }
    void key(int key,bool repeat,int sourceRequire){auto event=std::make_unique<QKeyEvent>(QEvent::KeyPress,key,Qt::NoModifier,QString(QChar(key).toLower()),repeat,1);required(sourceRequire,bool(event));QApplication::sendEvent(project.canvas,event.get());}
};
std::vector<Point> square(double x,double y,double side){return {{x,y},{x+side,y},{x+side,y+side},{x,y+side}};}

void combine(){Session s;s.lasso(square(10,10,40));expect(35,s.coverage(30,30)==255&&s.coverage(70,70)==0);s.lasso(square(50,50,40),SelectionMode::Add);expect(37,s.coverage(30,30)==255&&s.coverage(70,70)==255);s.lasso(square(20,20,20),SelectionMode::Subtract);expect(39,s.coverage(30,30)==0&&s.coverage(15,15)==255);s.lasso(square(60,10,20));expect(41,s.coverage(70,20)==255&&s.coverage(70,70)==0&&s.coverage(15,15)==0);}
void modifiers_clip(){Session s;expect(46,s.mode(false,false)==SelectionMode::Replace);expect(47,s.mode(true,false)==SelectionMode::Add);expect(48,s.mode(true,true)==SelectionMode::Subtract);expect(49,s.mode(false,true)==SelectionMode::Subtract);s.lasso(square(-50,-50,100));required(51,s.selection().has_value());const auto b=s.bounds();expect(52,b.x>=0&&b.y>=0&&b.x+b.width<=50.001&&b.y+b.height<=50.001);}
void empty_distinct(){Session s;s.lasso(square(0,0,50),SelectionMode::Subtract);expect(58,!s.selection());s.lasso(square(10,10,20));s.lasso(square(0,0,60),SelectionMode::Subtract);required(61,s.selection().has_value());expect(62,s.empty());expect(63,s.coverage(20,20)==0);s.trigger("selection.deselect");expect(65,!s.selection());}
void click_undo(){Session s;const auto count=s.history().undoCount();s.lasso(square(10,10,40));expect(72,s.history().undoCount()==count+1&&s.history().undoName()=="Lasso");s.lasso({{5,5}});expect(74,!s.selection()&&s.history().undoName()=="Deselect");s.trigger("edit.undo");expect(76,s.outline()&&!s.empty());s.trigger("edit.undo");expect(78,!s.selection());s.trigger("edit.redo");expect(80,s.coverage(30,30)==255);}
void polygon(){Session s;s.setKind(LassoKind::Polygonal);s.begin({10,10});s.extend({90,10});s.extend({50,50});s.gesture().removeLast();s.extend({90,90});s.extend({10,90});expect(92,s.gesture().draft()&&s.gesture().draft()->points.size()==4);s.finish();expect(94,s.history().undoName()=="Polygonal Lasso");expect(95,s.coverage(80,80)==255&&s.coverage(5,50)==0);s.begin({1,1});s.cancel();expect(98,!s.gesture().active()&&s.outline()&&!s.empty());}
void antialias(){Session s;const std::vector<Point> triangle{{0,0},{100,0},{0,100}};s.lasso(triangle);std::array<int,100> edges{};for(int x=0;x<100;++x)edges[x]=s.coverage(x,99-x);expect(107,std::any_of(edges.begin(),edges.end(),[](int v){return v>0&&v<255;}));s.setAA(false);s.lasso(triangle);std::array<int,100> hard{};for(int x=0;x<100;++x)hard[x]=s.coverage(x,99-x);expect(111,std::all_of(hard.begin(),hard.end(),[](int v){return v==0||v==255;}));}
void inverse_tool(){Session s;s.trigger("selection.all");expect(117,s.coverage(0,0)==255&&s.coverage(99,99)==255);s.lasso(square(0,0,50));s.trigger("selection.inverse");expect(120,s.coverage(25,25)==0&&s.coverage(75,75)==255);s.begin({5,5});s.selectTool(ProjectTool::Brush);expect(123,!s.gesture().active());}
void cursor(){Session s;expect(128,s.cursorMode(false,false)==SelectionMode::Replace);expect(129,s.cursorMode(true,false)==SelectionMode::Add);expect(130,s.cursorMode(false,true)==SelectionMode::Subtract);s.setChoice(SelectionMode::Add);expect(132,s.cursorMode(false,false)==SelectionMode::Add);s.begin({5,5},SelectionMode::Subtract);expect(134,s.cursorMode(false,false)==SelectionMode::Subtract);expect(135,s.cursorMode(true,false)==SelectionMode::Subtract);s.cancel();s.setChoice(SelectionMode::Replace);s.held(true,false);expect(139,s.displayed()==SelectionMode::Add&&s.choice()==SelectionMode::Replace);s.held(true,true);expect(141,s.displayed()==SelectionMode::Subtract);s.held(false,false);expect(143,s.displayed()==SelectionMode::Replace);unavailable(144,"Pinned test references deleted CanvasView.lassoCursors; replacement selectionCursors has four icon families and custom Replace cursors. No equivalent legacy observer exists.");}
void move_undo(){Session s;s.lasso(square(10,10,20));expect(150,s.canMove({20,20}));expect(151,!s.canMove({60,60}));const auto count=s.history().undoCount();expect(153,s.beginMove());s.move({10.4,29.6});s.move({40.2,40.4});s.endMove();expect(157,s.history().undoCount()==count+1&&s.history().undoName()=="Move Selection");expect(158,s.boundsEqual({50,50,20,20}));expect(159,s.coverage(55,55)==255&&s.coverage(15,15)==0);s.trigger("edit.undo");expect(161,s.boundsEqual({10,10,20,20}));}
void offcanvas(){Session s;s.lasso(square(10,10,20));expect(167,s.beginMove());s.move({-25,0});s.endMove();expect(170,s.coverage(0,20)==255);expect(171,s.beginMove());s.move({25,0});s.endMove();expect(174,s.boundsEqual({10,10,20,20}));}
void nudge(){Session s;expect(179,!s.beginMove());s.lasso(square(10,10,20));const auto count=s.history().undoCount();s.nudge(1,0);s.nudge(0,-10);expect(184,s.boundsEqual({11,0,20,20}));expect(185,s.history().undoCount()==count+2);expect(186,s.mode(true,false)!=SelectionMode::Replace);s.begin({5,5},SelectionMode::Add);expect(188,!s.canMove({20,10}));s.cancel();s.lasso(square(0,0,60),SelectionMode::Subtract);expect(191,s.empty()&&!s.beginMove());}
void resize_outline(){Session s;expect(196,!s.canModify());s.lasso(square(40,40,20));expect(198,s.canModify());s.resize(true,5);expect(200,s.history().undoName()=="Expand Selection");required(201,s.selection().has_value());auto b=s.bounds();expect(202,std::abs(b.x-35)<.01&&std::abs(b.width-30)<.01);expect(203,s.coverage(37,50)==255&&s.coverage(33,50)==0);s.resize(false,8);required(205,s.selection().has_value());b=s.bounds();expect(206,std::abs(b.x-43)<.01&&std::abs(b.width-14)<.01);s.trigger("edit.undo");required(208,s.selection().has_value());expect(208,std::abs(s.bounds().width-30)<.01);}
void resize_empty(){Session s;s.trigger("selection.all");s.resize(true,10);expect(215,s.boundsEqual({0,0,100,100}));s.resize(false,10);expect(217,s.coverage(5,50)==0&&s.coverage(50,50)==255);s.resize(false,45);expect(219,s.empty());expect(220,!s.canModify());}
std::string maskedLayer(Session& s){required(225,!s.project.active.empty());const auto id=s.project.active;auto mask=std::make_shared<GrayRaster>();mask->width=mask->height=100;mask->pixels.assign(10000,255);for(int y=30;y<70;++y)for(int x=20;x<60;++x)mask->pixels[size_t(y)*100+x]=0;for(int y=40;y<50;++y)for(int x=30;x<40;++x)mask->pixels[size_t(y)*100+x]=255;required(233,mask&&mask->pixels.size()==10000);s.trigger("mask.add_white");auto index=std::find_if(s.document()->layers.begin(),s.document()->layers.end(),[&](const Layer& l){return l.id==id;});required(235,index!=s.document()->layers.end());index->mask=Mask{mask};s.refresh();return id;}
void mask_black(){Session s;const auto id=maskedLayer(s);s.load(id,true);expect(244,s.history().undoName()=="Load Mask Selection");expect(245,s.coverage(25,35)==255);expect(246,s.coverage(35,45)==0);expect(247,s.coverage(80,80)==0);expect(248,s.coverage(59,69)==255&&s.coverage(60,70)==0);s.lasso(square(80,80,10));s.load(id,true,SelectionMode::Add);expect(252,s.coverage(85,85)==255&&s.coverage(25,35)==255);s.load(id,true,SelectionMode::Subtract);expect(254,s.coverage(85,85)==255&&s.coverage(25,35)==0);}
void mask_transform(){Session s(200,200);const auto id=maskedLayer(s);auto index=std::find_if(s.document()->layers.begin(),s.document()->layers.end(),[&](const Layer& l){return l.id==id;});required(260,index!=s.document()->layers.end());index->transform={0,0,200,200};s.load(id,true);required(264,s.selection().has_value());expect(265,s.boundsEqual({40,60,80,80}));s.trigger("selection.deselect");s.trigger("layer.new");s.trigger("mask.add_white");required(269,!s.project.active.empty());s.load(s.project.active,true);expect(270,!s.selection());}
void layer_alpha(){Session s(200,200);std::vector<uint8_t> rgba(50*50*4);for(int y=10;y<40;++y)for(int x=10;x<40;++x)if(!(x>=20&&x<30&&y>=20&&y<30)){auto at=size_t(y*50+x)*4;rgba[at]=rgba[at+3]=255;}rgba[0]=rgba[3]=64;auto image=Raster::fromRgba(50,50,rgba.data(),200);required(282,bool(image));(s.window.*member(Add{}))(image,{75,75},"Import Image");required(284,!s.project.active.empty());const auto id=s.project.active;auto index=std::find_if(s.document()->layers.begin(),s.document()->layers.end(),[&](const Layer& l){return l.id==id;});required(285,index!=s.document()->layers.end());index->name="Ring";index->transform={50,50,100,100};s.load(id,false);expect(288,s.history().undoName()=="Load Layer Selection");expect(289,s.boundsEqual({70,70,60,60}));expect(290,s.coverage(75,75)==255);expect(291,s.coverage(100,100)==0);expect(292,s.coverage(50,50)==0);s.lasso(square(0,0,20));s.load(id,false,SelectionMode::Add);expect(295,s.coverage(10,10)==255&&s.coverage(75,75)==255);s.trigger("layer.new");const auto before=s.selection();required(298,!s.project.active.empty());s.load(s.project.active,false);expect(299,s.selection()==before);}
void marquee_rectangle(){Session s;s.selectTool(ProjectTool::Marquee);s.marquee({60.4,70.6},{20.2,30.3});expect(313,s.history().undoName()=="Rectangular Marquee");expect(314,s.boundsEqual({20,30,40,41}));expect(315,s.coverage(20,30)==255&&s.coverage(19,30)==0);s.marquee({80,80},{90,90},SelectionMode::Add);expect(317,s.coverage(85,85)==255&&s.coverage(40,50)==255);s.marquee({30,40},{50,60},SelectionMode::Subtract);expect(319,s.coverage(40,50)==0&&s.coverage(25,35)==255);s.marquee({5,5},{5,5});expect(321,!s.selection());}
void marquee_center(){Session s;s.selectTool(ProjectTool::Marquee);s.marquee({10,10},{40,20},SelectionMode::Replace,true);expect(328,s.boundsEqual({10,10,30,30}));s.marquee({50,50},{60,55},SelectionMode::Replace,false,true);expect(330,s.boundsEqual({40,45,20,10}));s.marquee({50,50},{45,58},SelectionMode::Replace,true,true);expect(332,s.boundsEqual({42,42,16,16}));s.selectTool(ProjectTool::Marquee);const bool marquee=(s.window.*member(Begin{}))({1,1},{},1);s.cancel();s.selectTool(ProjectTool::Brush);const bool brush=(s.window.*member(Begin{}))({1,1},{},1);expect(333,marquee&&!brush);}
void marquee_ellipse(){Session s;s.selectTool(ProjectTool::Marquee);s.setEllipse(true);s.marquee({10,20},{70,60});required(343,s.outline()!=nullptr);auto b=s.bounds();expect(344,std::abs(b.x-10)<.5&&std::abs(b.x+b.width-70)<.5&&std::abs(b.y-20)<.5&&std::abs(b.y+b.height-60)<.5);expect(345,s.coverage(40,40)==255);expect(346,s.coverage(11,21)==0);s.marquee({5,5},{45,25},SelectionMode::Replace,true);required(350,s.outline()!=nullptr);b=s.bounds();expect(351,std::abs(b.width-b.height)<.5);s.setEllipse(!s.ellipse());expect(353,!s.ellipse());}
void option_event(){Session s;s.selectTool(ProjectTool::Marquee);s.trigger("selection.all");s.send(QEvent::MouseButtonPress,{40,40},Qt::AltModifier,369);s.send(QEvent::MouseMove,{60,60},Qt::AltModifier,369);s.send(QEvent::MouseButtonRelease,{60,60},Qt::AltModifier,369);expect(376,s.coverage(50,50)==0);expect(377,s.coverage(30,30)==255);}
void m_key(){Session s;expect(388,s.sourceLasso()&&!s.ellipse());s.key(Qt::Key_M,false,385);expect(390,s.tool()==ProjectTool::Marquee&&!s.ellipse());s.key(Qt::Key_M,false,385);expect(392,s.ellipse());s.key(Qt::Key_M,true,385);expect(394,s.ellipse());s.key(Qt::Key_M,false,385);expect(396,!s.ellipse());s.key(Qt::Key_M,false,385);s.selectTool(ProjectTool::Brush);s.key(Qt::Key_M,false,385);expect(400,s.tool()==ProjectTool::Marquee&&s.ellipse());}
void fresh_shift(){Session s;s.selectTool(ProjectTool::Marquee);s.history().begin("Select",s.document(),s.project.active);s.selection()=rasterSelection(applySelection({},SelectionOutline::rectangle({5,5,10,10}),SelectionMode::Replace,100,100,true),100,100);s.history().end(s.document(),s.project.active);s.refresh();s.send(QEvent::MouseButtonPress,{40,40},Qt::ShiftModifier,417);s.send(QEvent::MouseMove,{70,50},Qt::ShiftModifier,417);s.send(QEvent::MouseButtonRelease,{70,50},Qt::ShiftModifier,417);expect(430,s.coverage(10,10)==255);expect(431,s.coverage(65,45)==255);expect(432,s.coverage(65,60)==0);s.send(QEvent::MouseButtonPress,{20,60},Qt::ShiftModifier,417);s.send(QEvent::MouseMove,{30,65},Qt::ShiftModifier,417);s.send(QEvent::MouseMove,{35,68},{},417);s.send(QEvent::MouseMove,{40,70},Qt::ShiftModifier,417);s.send(QEvent::MouseButtonRelease,{40,70},Qt::ShiftModifier,417);expect(440,s.coverage(30,75)==255);expect(441,s.coverage(65,45)==255&&s.coverage(10,10)==255);}
void l_key(){Session s;s.selectTool(ProjectTool::Marquee);expect(453,s.kind()==LassoKind::Freehand);s.key(Qt::Key_L,false,450);expect(455,s.sourceLasso()&&s.kind()==LassoKind::Freehand);s.key(Qt::Key_L,false,450);expect(457,s.kind()==LassoKind::Polygonal);s.key(Qt::Key_L,true,450);expect(459,s.kind()==LassoKind::Polygonal);s.key(Qt::Key_L,false,450);expect(461,s.kind()==LassoKind::Freehand);s.key(Qt::Key_L,false,450);s.selectTool(ProjectTool::Brush);s.key(Qt::Key_L,false,450);expect(465,s.sourceLasso()&&s.kind()==LassoKind::Polygonal);}
void draft_press_guard(){
    for(const auto tool:{ProjectTool::Lasso,ProjectTool::Marquee}){
        Session s;s.lasso(square(10,10,20));s.selectTool(tool);const auto before=s.document();const auto count=s.history().undoCount();
        s.send(QEvent::MouseButtonPress,{5,5},Qt::ShiftModifier,0);s.send(QEvent::MouseMove,{6,7},Qt::ShiftModifier,0);
        guard(s.gesture().active()&&!(s.window.*member(MovingField{})),"Supplemental original active draft precondition");
        s.send(QEvent::MouseButtonPress,{20,20},{},0);
        nativeExpect("repeated_press_does_not_begin_move",!(s.window.*member(MovingField{})));
        nativeExpect("repeated_press_does_not_open_history",s.history().canUndo()&&s.history().undoCount()==count);
        nativeExpect("repeated_press_preserves_canonical_selection",s.document()==before);
        QTest::keyClick(s.project.canvas,Qt::Key_Escape);
        nativeExpect("cancel_retains_original_selection_and_history",s.document()==before&&s.history().canUndo()&&s.history().undoCount()==count);
        guard(s.beginMove(),"Control ordinary move begins");s.move({2,3});s.endMove();
        nativeExpect("ordinary_move_still_commits_once",s.history().undoCount()==count+1&&s.history().undoName()=="Move Selection"&&s.boundsEqual({12,13,20,20}));
    }
}
}
int main(int argc,char** argv){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    QApplication app(argc,argv);
    const std::map<std::string,void(*)()> cases{{"combine",combine},{"modifiers_clip",modifiers_clip},{"empty_distinct",empty_distinct},{"click_undo",click_undo},{"polygon",polygon},{"antialias",antialias},{"inverse_tool",inverse_tool},{"cursor",cursor},{"move_undo",move_undo},{"offcanvas",offcanvas},{"nudge",nudge},{"resize_outline",resize_outline},{"resize_empty",resize_empty},{"mask_black",mask_black},{"mask_transform",mask_transform},{"layer_alpha",layer_alpha},{"marquee_rectangle",marquee_rectangle},{"marquee_center",marquee_center},{"marquee_ellipse",marquee_ellipse},{"option_event",option_event},{"m_key",m_key},{"fresh_shift",fresh_shift},{"l_key",l_key}};
    std::string error;bool complete=false;bool unmapped=false;const std::string key=argc>1?argv[1]:"";
    try{guard(argc==3&&(cases.contains(key)||key=="draft_press_guard"),"Provide CASE ABS_NEW_REPORT.json");guard(!QFile::exists(QString::fromLocal8Bit(argv[2])),"Fresh report required");if(key=="draft_press_guard")draft_press_guard();else cases.at(key)();complete=true;}catch(const UnavailableSourceObservation& e){error=e.what();unmapped=true;}catch(const std::exception& e){error=e.what();}
    const bool passed=complete&&failures==0&&nativeFailures==0;
    QJsonObject result{{"schema","SELECTION_SOURCE_RESULT_V1"},{"case",QString::fromStdString(key)},{"status",passed?"passed":unmapped?"not_run":complete?"failed":"error"},{"complete",complete},{"source_expect_failures",failures},{"native_failures",nativeFailures},{"native_checks",nativeChecks},{"unavailable_checks",unavailableChecks},{"error",QString::fromStdString(error)},{"checks",checks}};
    if(argc==3){QFile out(QString::fromLocal8Bit(argv[2]));if(out.open(QIODevice::WriteOnly|QIODevice::NewOnly))out.write(QJsonDocument(result).toJson());else return 2;}
    std::fprintf(stdout,"%s %s checks=%lld failures=%d\n",passed?"PASS":unmapped?"NOT_RUN":"FAIL",key.c_str(),static_cast<long long>(checks.size()),failures);if(!error.empty())std::fprintf(stderr,"%s\n",error.c_str());return passed?0:unmapped?3:complete?1:2;
}
