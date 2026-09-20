#include "ui/MainWindow.h"
#include <QApplication>
#include <QInputDialog>
#include <QMouseEvent>
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
void escape_mode_reset(){Fixture f;command(f.window,"tool.polygon")->trigger();f.send(QEvent::MouseButtonPress,{10,10},Qt::AltModifier);f.send(QEvent::MouseButtonRelease,{10,10},Qt::AltModifier);QTest::keyRelease(f.project.canvas,Qt::Key_Alt);auto* box=f.window.findChild<QComboBox*>("selectionMode");require(box&&box->currentIndex()==2,"source draft keeps initial Subtract badge");QTest::keyClick(f.project.canvas,Qt::Key_Escape);std::cout<<"selection_mode_after_escape="<<box->currentIndex()<<'\n';require(!f.project.canvas->selectionDraft()&&box->currentIndex()==0,"Selection.swift119 displays rememberedNew after cancel and releasedAlt");}
void expand_range(){Fixture f;command(f.window,"selection.all")->trigger();double maximum=-1;QTimer::singleShot(0,&f.window,[&]{if(auto* dialog=f.window.findChild<QInputDialog*>()){maximum=dialog->doubleMaximum();dialog->reject();}});command(f.window,"selection.expand")->trigger();std::cout<<"expand_maximum="<<maximum<<'\n';require(maximum==500,"Selection.swift285 maximum expand/contract amount500");}
void wand_replace(){Fixture f;auto& l=f.project.document->layers.front();std::vector<Pixel> pixels(10000);for(int y=0;y<100;++y)for(int x=0;x<100;++x)pixels[size_t(y)*100+x]=x<55?Pixel{255,0,0,255}:Pixel{0,0,255,255};l.raster=Raster::fromRgba(100,100,reinterpret_cast<const uint8_t*>(pixels.data()),400);f.project.document->selection=editing::rasterSelection(editing::SelectionOutline::rectangle({40,40,20,20}),100,100);command(f.window,"tool.wand")->trigger();f.send(QEvent::MouseButtonPress,{45,45});auto* box=f.window.findChild<QComboBox*>("selectionMode");require(box,"selection mode fixture");box->setCurrentIndex(1);f.send(QEvent::MouseButtonRelease,{45,45},Qt::AltModifier);std::cout<<"wand_10,10="<<f.coverage(10,10)<<" wand_58,50="<<f.coverage(58,50)<<'\n';require(f.coverage(10,10)==255&&f.coverage(58,50)==0,"EditorCanvas1406 unmovedWand usesReplace regardless releaseflags or changedchoice");}
}
int main(int argc,char** argv){QApplication app(argc,argv);try{require(argc==2,"provide case");const std::string c=argv[1];if(c=="undo_redo_draft")undo_redo_draft();else if(c=="escape_mode_reset")escape_mode_reset();else if(c=="expand_range")expand_range();else if(c=="wand_replace")wand_replace();else throw std::runtime_error("unknown case");std::cout<<"PASS "<<c<<'\n';return 0;}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
