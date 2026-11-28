#include "ui/MainWindow.h"
#include "ui/SegmentedControl.h"
#include "ui/ToolFlyoutButton.h"
#include <QMenu>
#include <QApplication>
#include <QDialog>
#include <QInputDialog>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QMouseEvent>
#include <QSpinBox>
#include <QToolBar>
#include <QToolButton>
#include <QTest>
#include <QTimer>
#include <iostream>
#include <stdexcept>
using namespace compositor;
namespace {
void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
QAction* command(MainWindow& w,const char* id){for(auto* a:w.findChildren<QAction*>())if(a->property("commandId").toString()==id)return a;throw std::runtime_error("missing command");}
Document document(){Document d;d.id=newId();d.width=d.height=100;Layer l;l.id=newId();l.name="Layer 1";l.transform={0,0,100,100};d.layers.push_back(l);return d;}
struct Fixture {
 MainWindow window{true};EditorProject& project;
 Fixture():project(window.addProject(document())){window.show();QApplication::processEvents();}
 void send(QEvent::Type type,Point point,Qt::KeyboardModifiers flags={}){auto* c=project.canvas;const auto v=c->viewMapping().toView(point);const QPointF at(v.x,v.y);QMouseEvent e(type,at,c->mapToGlobal(at.toPoint()),Qt::LeftButton,type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton,flags);QApplication::sendEvent(c,&e);}
 void click(Point p){send(QEvent::MouseButtonPress,p);send(QEvent::MouseButtonRelease,p);}
 int coverage(int x,int y)const{return project.document->selection?project.document->selection->coverage->pixel(x,y):-1;}
};
void undo_redo_draft(){Fixture f;command(f.window,"layer.new")->trigger();command(f.window,"tool.polygon")->trigger();f.click({10,10});f.click({90,10});require(f.project.canvas->selectionDraft().has_value(),"polygon fixture exists");const auto points=f.project.canvas->selectionDraft()->points;command(f.window,"edit.undo")->trigger();const bool afterUndo=f.project.canvas->selectionDraft()&&f.project.canvas->selectionDraft()->points==points;require(f.project.history.undoCount()==0&&f.project.document->layers.size()==1,"source Undo restores previous document");command(f.window,"edit.redo")->trigger();const bool afterRedo=f.project.canvas->selectionDraft()&&f.project.canvas->selectionDraft()->points==points;std::cout<<"draft_after_undo="<<afterUndo<<" draft_after_redo="<<afterRedo<<'\n';require(f.project.history.undoCount()==1&&f.project.document->layers.size()==2,"source Redo restores later document");require(afterUndo&&afterRedo,"EditorSession.restore467-472 preserves lassoDraft across history restore");}
void escape_mode_reset(){Fixture f;command(f.window,"tool.polygon")->trigger();f.send(QEvent::MouseButtonPress,{10,10},Qt::AltModifier);f.send(QEvent::MouseButtonRelease,{10,10},Qt::AltModifier);QTest::keyRelease(f.project.canvas,Qt::Key_Alt);auto* box=ui::findSegmentedControl(&f.window,"selectionMode");require(box&&box->currentIndex()==2,"source draft keeps initial Subtract badge");QTest::keyClick(f.project.canvas,Qt::Key_Escape);std::cout<<"selection_mode_after_escape="<<box->currentIndex()<<'\n';require(!f.project.canvas->selectionDraft()&&box->currentIndex()==0,"Selection.swift119 displays rememberedNew after cancel and releasedAlt");}
void expand_range(){Fixture f;command(f.window,"selection.all")->trigger();double maximum=-1;QTimer::singleShot(0,&f.window,[&]{if(auto* dialog=f.window.findChild<QInputDialog*>()){maximum=dialog->doubleMaximum();dialog->reject();}});command(f.window,"selection.expand")->trigger();std::cout<<"expand_maximum="<<maximum<<'\n';require(maximum==500,"Selection.swift285 maximum expand/contract amount500");}
QAction* barFeather(MainWindow& w){auto* bar=w.findChild<QToolBar*>("selectionOptions");require(bar,"selection options bar");for(auto* a:bar->actions())if(a->property("commandId").toString()=="selection.feather")return a;throw std::runtime_error("missing bar Feather");}
void feather_bar(){Fixture f;auto* feather=barFeather(f.window);auto* amount=f.window.findChild<QSpinBox*>("selectionFeatherAmount");require(amount&&amount->value()==2&&amount->minimum()==0&&amount->maximum()==250,"Feather field defaults to 2 px within 0..250");require(!feather->isEnabled()&&!amount->isEnabled(),"Feather is unavailable without a selection");command(f.window,"tool.marquee")->trigger();f.send(QEvent::MouseButtonPress,{30,30});f.send(QEvent::MouseMove,{70,70});f.send(QEvent::MouseButtonRelease,{70,70});require(f.coverage(30,50)==255&&f.coverage(29,50)==0,"hard-edged marquee fixture");require(feather->isEnabled()&&amount->isEnabled(),"Feather is available with a selection");const auto before=f.project.history.undoCount();amount->setValue(4);feather->trigger();require(f.project.history.undoCount()==before+1&&f.project.history.undoName()=="Feather","one Feather undo step");require(f.coverage(30,50)<255&&f.coverage(29,50)>0&&f.coverage(50,50)==255,"Feather softens the edge by the field's radius");command(f.window,"edit.undo")->trigger();require(f.project.history.undoCount()==before&&f.coverage(30,50)==255&&f.coverage(29,50)==0,"undo restores the hard edge exactly");}
void feather_dialog_default(){Fixture f;command(f.window,"selection.all")->trigger();auto* amount=f.window.findChild<QSpinBox*>("selectionFeatherAmount");amount->setValue(7);double shown=-1;QTimer::singleShot(0,&f.window,[&]{if(auto* dialog=f.window.findChild<QDialog*>("featherDialog"))if(auto* field=dialog->findChild<QSpinBox*>("featherRadius")){shown=field->value();field->setValue(9);dialog->accept();}});for(auto* a:f.window.findChildren<QAction*>())if(a->property("commandId").toString()=="selection.feather"&&a!=barFeather(f.window)){a->trigger();break;}require(shown==7,"Feather dialog starts from the bar value");require(amount->value()==9,"Feather dialog value is remembered in the bar");}
void wand_replace(){Fixture f;auto& l=f.project.document->layers.front();std::vector<Pixel> pixels(10000);for(int y=0;y<100;++y)for(int x=0;x<100;++x)pixels[size_t(y)*100+x]=x<55?Pixel{255,0,0,255}:Pixel{0,0,255,255};l.raster=Raster::fromRgba(100,100,reinterpret_cast<const uint8_t*>(pixels.data()),400);f.project.document->selection=editing::rasterSelection(editing::SelectionOutline::rectangle({40,40,20,20}),100,100);command(f.window,"tool.wand")->trigger();f.send(QEvent::MouseButtonPress,{45,45});auto* box=ui::findSegmentedControl(&f.window,"selectionMode");require(box,"selection mode fixture");box->segment(1)->click();f.send(QEvent::MouseButtonRelease,{45,45},Qt::AltModifier);std::cout<<"wand_10,10="<<f.coverage(10,10)<<" wand_58,50="<<f.coverage(58,50)<<'\n';require(f.coverage(10,10)==255&&f.coverage(58,50)==0,"EditorCanvas1406 unmovedWand usesReplace regardless releaseflags or changedchoice");}
void segmented_slide(){Fixture f;auto* kind=ui::findSegmentedControl(&f.window,"selectionMode");require(kind,"selection mode control");command(f.window,"tool.lasso")->trigger();QApplication::processEvents();
 const auto first=kind->segment(0)->geometry(),last=kind->segment(2)->geometry();require(kind->pillRect()==QRectF(first),"pill starts on the first choice");
 QTest::mouseClick(kind->segment(2),Qt::LeftButton);const auto start=kind->pillRect();require(start.left()<last.left()&&kind->currentIndex()==2,"pill leaves the old choice instead of jumping");
 bool arrived=false;for(int i=0;i<100&&!arrived;++i){QTest::qWait(10);arrived=kind->pillRect()==QRectF(last);}
 require(arrived,"pill reaches the clicked choice");
 kind->setCurrentIndex(0);require(kind->pillRect()==QRectF(first),"setCurrentIndex moves the pill at once");}
void lasso_kind_slide(){Fixture f;command(f.window,"tool.lasso")->trigger();QApplication::processEvents();auto* kind=ui::findSegmentedControl(&f.window,"lassoKind");require(kind&&kind->isVisible(),"lasso kind control");
 const auto polygonal=kind->segment(1)->geometry();QTest::mouseClick(kind->segment(1),Qt::LeftButton);
 require(kind->currentIndex()==1&&kind->pillRect().left()<polygonal.left(),"pill slides after the lasso kind click although the tool refreshes");
 bool arrived=false;for(int i=0;i<100&&!arrived;++i){QTest::qWait(10);arrived=kind->pillRect()==QRectF(polygonal);}require(arrived,"pill reaches Polygonal");}
void marquee_bar(){Fixture f;command(f.window,"tool.marquee")->trigger();QApplication::processEvents();
 auto* shape=ui::findSegmentedControl(&f.window,"marqueeShape");auto* mode=ui::findSegmentedControl(&f.window,"selectionMode");auto* aa=f.window.findChild<QCheckBox*>("selectionAntialias");
 require(shape&&shape->isVisible()&&mode&&mode->isVisible()&&aa,"Marquee shows its shape and mode choices");
 require(shape->segment(0)->text()=="Rectangle"&&shape->segment(1)->text()=="Ellipse"&&shape->currentIndex()==0,"Rectangle and Ellipse, Rectangle first");
 require(!aa->isVisible()&&!ui::findSegmentedControl(&f.window,"lassoKind")->isVisible(),"Marquee has no Anti-alias or lasso kind");
 QTest::mouseClick(shape->segment(1),Qt::LeftButton);QApplication::processEvents();require(shape->currentIndex()==1,"Ellipse chosen");
 command(f.window,"tool.lasso")->trigger();QApplication::processEvents();require(aa->isVisible()&&!shape->isVisible(),"Lasso shows Anti-alias and hides the shape");}
// Pressing and holding Marquee, Lasso, Wand or Brush offers the other kind of the tool and applies it like the options bar does.
void tool_flyout(){Fixture f;
 const auto button=[&](const char* name){for(auto* b:f.window.findChildren<QToolButton*>())if(auto* flyout=dynamic_cast<ui::ToolFlyoutButton*>(b);flyout&&b->accessibleName()==name)return flyout;throw std::runtime_error("tool button missing");};
 const auto offer=[&](const char* name){auto* menu=button(name)->showFlyout();require(menu&&menu->isVisible()&&menu->actions().size()==1,"flyout offers one alternate tool");const auto text=menu->actions()[0]->text();return std::pair{menu,text};};
 const auto pick=[&](const char* name,const char* text){auto [menu,offered]=offer(name);require(offered==text,text);menu->actions()[0]->trigger();QApplication::processEvents();};
 pick("Marquee (M)","Ellipse Marquee");require(ui::findSegmentedControl(&f.window,"marqueeShape")->currentIndex()==1&&button("Marquee (M)")->isChecked(),"Ellipse chosen and the Marquee tool active");
 pick("Marquee (M)","Rectangular Marquee");require(ui::findSegmentedControl(&f.window,"marqueeShape")->currentIndex()==0,"Rectangle chosen again");
 pick("Lasso (L)","Polygonal Lasso");require(ui::findSegmentedControl(&f.window,"lassoKind")->currentIndex()==1,"Polygonal chosen");
 pick("Lasso (L)","Freehand Lasso");require(ui::findSegmentedControl(&f.window,"lassoKind")->currentIndex()==0&&button("Lasso (L)")->isChecked(),"Freehand chosen again");
 pick("Wand (W)","Object Selection");require(ui::findSegmentedControl(&f.window,"wandKind")->currentIndex()==1&&button("Wand (W)")->isChecked(),"Object chosen and the Wand tool active");
 pick("Wand (W)","Magic Wand");require(ui::findSegmentedControl(&f.window,"wandKind")->currentIndex()==0,"Wand chosen again");
 pick("Brush (B)","Eraser");require(ui::findSegmentedControl(&f.window,"brushMode")->currentIndex()==1,"Erase chosen");
 pick("Brush (B)","Brush");require(ui::findSegmentedControl(&f.window,"brushMode")->currentIndex()==0&&button("Brush (B)")->isChecked(),"Paint chosen again");
 // Shape offers the two kinds it is not drawing.
 {const auto kinds=[&]{auto* menu=button("Shape (U)")->showFlyout();require(menu&&menu->isVisible(),"Shape flyout opens");QStringList names;for(auto* action:menu->actions())names<<action->text();return std::pair{menu,names};};
  auto [menu,names]=kinds();require(names==QStringList({"Ellipse","Line"}),"Rectangle is current, so Ellipse and Line are offered");menu->actions()[0]->trigger();QApplication::processEvents();
  require(ui::findSegmentedControl(&f.window,"shapeKind")->currentIndex()==1&&button("Shape (U)")->isChecked(),"Ellipse chosen and the Shape tool active");
  auto [again,namesAgain]=kinds();require(namesAgain==QStringList({"Rectangle","Line"}),"Ellipse is current, so Rectangle and Line are offered");again->actions()[1]->trigger();QApplication::processEvents();
  require(ui::findSegmentedControl(&f.window,"shapeKind")->currentIndex()==2,"Line chosen");
  auto [last,namesLast]=kinds();last->actions()[0]->trigger();QApplication::processEvents();require(ui::findSegmentedControl(&f.window,"shapeKind")->currentIndex()==0,"Rectangle chosen again");}
 command(f.window,"tool.move")->trigger();QApplication::processEvents();
 auto* marquee=button("Marquee (M)");QTest::mousePress(marquee,Qt::LeftButton);QTest::qWait(700);
 require(marquee->findChild<QMenu*>()&&marquee->findChild<QMenu*>()->isVisible(),"holding the button opens the popup");
 QTest::mouseRelease(marquee,Qt::LeftButton);marquee->findChild<QMenu*>()->hide();QApplication::processEvents();
 require(!button("Marquee (M)")->isChecked(),"the long press does not select the tool");
 QTest::mouseClick(marquee,Qt::LeftButton);QApplication::processEvents();
 require(button("Marquee (M)")->isChecked()&&!marquee->findChild<QMenu*>()->isVisible(),"a short click selects the tool without a popup");
 require(!button("Crop (C)")->hasFlyout()&&button("Wand (W)")->hasFlyout(),"only the grouped tools have a popup");}
void wand_bar(){Fixture f;command(f.window,"tool.wand")->trigger();QApplication::processEvents();
 auto* bar=f.window.findChild<QToolBar*>("selectionOptions");require(bar&&bar->isVisible(),"selection bar shows for the Wand");require(!f.window.findChild<QToolBar*>("wandOptions"),"Wand has no bar of its own");
 auto* title=f.window.findChild<QLabel*>("selectionToolTitle");require(title&&title->text()=="Magic","title is Magic");
 auto* kind=ui::findSegmentedControl(&f.window,"wandKind");auto* scope=ui::findSegmentedControl(&f.window,"wandScope");auto* tolerance=f.window.findChild<QSpinBox*>("wandTolerance");auto* sample=f.window.findChild<QComboBox*>("wandSampleSize");
 require(kind&&kind->isVisible()&&scope&&scope->isVisible()&&tolerance&&tolerance->isVisible()&&sample&&sample->isVisible(),"Wand controls are visible");
 require(tolerance->value()==32&&tolerance->buttonSymbols()==QAbstractSpinBox::NoButtons&&tolerance->suffix().isEmpty(),"tolerance is a plain 32 field");
 require(!ui::findSegmentedControl(&f.window,"lassoKind")->isVisible()&&!ui::findSegmentedControl(&f.window,"marqueeShape")->isVisible(),"Lasso and Marquee controls are hidden");
 scope->segment(1)->click();kind->segment(1)->click();command(f.window,"tool.lasso")->trigger();QApplication::processEvents();
 require(!kind->isVisible()&&!scope->isVisible()&&!tolerance->isVisible(),"Wand controls hide for another tool");}
}
void feather_dialog_row(){Fixture f;command(f.window,"selection.all")->trigger();auto* amount=f.window.findChild<QSpinBox*>("selectionFeatherAmount");amount->setValue(4);bool row=false;int scrubbed=-1,afterEscape=-1;QTimer::singleShot(0,&f.window,[&]{auto* dialog=f.window.findChild<QDialog*>("featherDialog");if(!dialog)return;auto* field=dialog->findChild<QSpinBox*>("featherRadius");QLabel* label=nullptr;for(auto* candidate:dialog->findChildren<QLabel*>())if(candidate->text()=="Radius")label=candidate;if(!field||!label){dialog->reject();return;}QApplication::processEvents();const auto middle=[](QWidget* w){return w->mapToGlobal(w->rect().center()).y();};row=std::abs(middle(label)-middle(field))<=2&&label->mapToGlobal(QPoint()).x()<field->mapToGlobal(QPoint()).x();const auto send=[&](QEvent::Type type,double dx){const QPointF local(label->width()/2.,label->height()/2.);QMouseEvent event(type,local,label->mapToGlobal(local)+QPointF(dx,0),Qt::LeftButton,type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton,{});QApplication::sendEvent(label,&event);};send(QEvent::MouseButtonPress,0);send(QEvent::MouseMove,6);send(QEvent::MouseButtonRelease,6);scrubbed=field->value();send(QEvent::MouseButtonPress,0);send(QEvent::MouseMove,20);QTest::keyClick(field,Qt::Key_Escape);send(QEvent::MouseButtonRelease,20);afterEscape=dialog->isVisible()?field->value():-1;dialog->accept();});for(auto* a:f.window.findChildren<QAction*>())if(a->property("commandId").toString()=="selection.feather"&&a!=barFeather(f.window)){a->trigger();break;}require(row,"Radius and its field share one row");require(scrubbed==10,"dragging Radius right raises the radius");require(afterEscape==10,"Escape during a drag restores the value and keeps the dialog open");require(amount->value()==10,"the scrubbed radius is applied and remembered");}
int main(int argc,char** argv){QApplication app(argc,argv);try{require(argc==2,"provide case");const std::string c=argv[1];if(c=="undo_redo_draft")undo_redo_draft();else if(c=="escape_mode_reset")escape_mode_reset();else if(c=="expand_range")expand_range();else if(c=="wand_replace")wand_replace();else if(c=="feather_bar")feather_bar();else if(c=="segmented_slide")segmented_slide();else if(c=="wand_bar")wand_bar();else if(c=="marquee_bar")marquee_bar();else if(c=="tool_flyout")tool_flyout();else if(c=="lasso_kind_slide")lasso_kind_slide();else if(c=="feather_dialog_default")feather_dialog_default();else if(c=="feather_dialog_row")feather_dialog_row();else throw std::runtime_error("unknown case");std::cout<<"PASS "<<c<<'\n';return 0;}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
