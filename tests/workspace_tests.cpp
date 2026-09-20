#include "ui/MainWindow.h"
#include <QApplication>
#include <QClipboard>
#include <QPushButton>
#include <QInputDialog>
#include <QTimer>
#include "ui/LayerPanel.h"
#include <QTest>
#include <functional>
#include <iostream>
using namespace compositor;
namespace {
void check(bool value,const char*message){if(!value)throw std::runtime_error(message);}
template<class T>T* control(QObject*object,const char*name){auto*p=object->findChild<T*>(name);check(p,"Control missing");return p;}
void action(MainWindow&w,const char*text){for(auto*a:w.findChildren<QAction*>())if(a->text()==text){a->trigger();return;}throw std::runtime_error("Action missing");}
}
int main(int argc,char**argv){QApplication app(argc,argv);std::cout<<std::unitbuf;int passed=0,failed=0;
auto test=[&](const char*name,std::function<void()>fn){try{fn();++passed;std::cout<<"PASS "<<name<<'\n';}catch(const std::exception&e){++failed;std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n';}};
test("initial_empty_tab_create_undo_redo_same_tab",[]{
 MainWindow w(true);auto&p=w.addEmptyProject();auto*tabs=w.findChild<QTabWidget*>();check(tabs->count()==1&&!p.document&&p.page->currentWidget()==p.welcome,"Initial window is not one empty welcome tab");
 control<QSpinBox>(p.welcome,"newCanvasWidth")->setValue(91);control<QSpinBox>(p.welcome,"newCanvasHeight")->setValue(67);control<QPushButton>(p.welcome,"createCanvas")->click();
 check(tabs->count()==1&&p.document&&p.document->width==91&&p.document->height==67&&p.document->layers.size()==1&&!p.document->layers[0].raster&&p.history.undoCount()==1&&p.history.modified(),"Create did not initialize same project and history");
 QTest::keyClick(&w,Qt::Key_Z,Qt::ControlModifier); // QAction shortcut needs active window; use registered action as fallback below.
 if(p.document)action(w,"Undo New Canvas");
 check(!p.document&&p.page->currentWidget()==p.welcome&&!p.canvas->viewportProvider(0,0,91,67,1).raster,"Undo kept old canvas visible");
 action(w,"Redo New Canvas");check(p.document&&p.page->currentWidget()==p.canvas&&p.document->width==91,"Redo failed to restore canvas");
});
test("new_tab_clipboard_defaults_and_close_last",[]{
 QImage image(23,17,QImage::Format_ARGB32);image.fill(Qt::green);QApplication::clipboard()->setImage(image);
 MainWindow w(true);auto&first=w.addEmptyProject();check(control<QSpinBox>(first.welcome,"newCanvasWidth")->value()==1920,"Initial tab used clipboard suggestion");
 action(w,"&New Canvas…");auto*tabs=w.findChild<QTabWidget*>();check(tabs->count()==2,"New did not create an empty tab");auto*page=tabs->currentWidget();check(control<QSpinBox>(page,"newCanvasWidth")->value()==23&&control<QSpinBox>(page,"newCanvasHeight")->value()==17,"New tab omitted clipboard dimensions");
 action(w,"Close Project");action(w,"Close Project");check(tabs->count()==1&&tabs->currentWidget()->findChild<QWidget*>("newCanvasWelcome"),"Closing final tab did not retain empty workspace");
});
test("pending_gradient_prevents_tab_switch",[]{
 MainWindow w(true);Document d;d.id=newId();d.width=80;d.height=60;Layer layer;layer.id=newId();layer.transform={0,0,80,60};layer.raster=Raster::filled(80,60,{80,120,160,255});d.layers.push_back(layer);auto&first=w.addProject(d);w.addEmptyProject(false);auto*tabs=w.findChild<QTabWidget*>();tabs->setCurrentWidget(first.page);action(w,"Gradient (G)");const auto before=first.document;
 first.canvas->pointerDown({10,10},{});first.canvas->pointerMove({60,10},{});first.canvas->pointerUp({60,10},{});tabs->setCurrentIndex(1);check(tabs->currentWidget()==first.page&&first.history.undoCount()==0,"Tab switch resolved or lost pending gradient");
 action(w,"&New Canvas…");check(tabs->count()==2&&first.history.undoCount()==0,"New bypassed workspace guard");QTest::keyClick(&w,Qt::Key_Escape);check(first.document==before,"Escape failed to cancel pending gradient");tabs->setCurrentIndex(1);check(tabs->currentIndex()==1,"Cancelled gradient kept tabs locked");
});
test("blank_layer_uses_parent_topmost_descendant_and_unique_name",[]{
 MainWindow w(true);Document d;d.id=newId();d.width=96;d.height=64;Layer folder;folder.id="folder";folder.group=true;folder.name="Folder";folder.transform={0,0,96,64};Layer child;child.id="child";child.parentId="folder";child.name="Layer 1";child.transform=folder.transform;Layer top;top.id="top";top.name="Top";top.transform=folder.transform;d.layers={folder,child,top};auto&p=w.addProject(d);
 p.active="folder";p.selected={"folder"};p.collapsedGroups.insert("folder");action(w,"New Layer");
 check(p.document->layers.size()==4&&p.document->layers[2].id==p.active&&p.document->layers[2].parentId=="folder"&&p.document->layers[2].name=="Layer 2"&&!p.collapsedGroups.contains("folder"),"Blank layer insertion/parent/name wrong");
 check(!p.document->layers[2].raster&&p.history.undoName()=="New Blank Layer","Blank layer allocated pixels or wrong transaction");
});
test("paste_inherits_parent_floors_external_center_and_duplicate_shares_source",[]{
 MainWindow w(true);Document d;d.id=newId();d.width=96;d.height=64;Layer group;group.id="g";group.group=true;group.transform={0,0,96,64};Layer layer;layer.id="a";layer.parentId="g";layer.name="Layer 1";layer.transform=group.transform;layer.raster=Raster::filled(96,64,{20,50,70,255});d.layers={group,layer};auto&p=w.addProject(d);
 QImage image(23,17,QImage::Format_ARGB32);image.fill(Qt::red);QApplication::clipboard()->setImage(image);action(w,"Paste");
 const auto& pasted=p.document->layers.back();check(pasted.parentId=="g"&&pasted.name=="Layer 2"&&pasted.transform.x==36&&pasted.transform.y==23,"Paste parent/name/floor origin wrong");
 const auto source=pasted.raster;const auto transform=pasted.transform;action(w,"Layer via Copy");check(p.document->layers.size()==4&&p.document->layers.back().raster==source&&p.document->layers.back().transform==transform&&p.document->layers.back().parentId=="g","Layer via Copy without selection resampled or lost parent");
});
test("copy_layer_to_empty_project_and_undo_to_welcome",[]{
 MainWindow w(true);Document d;d.id=newId();d.width=96;d.height=64;Layer layer;layer.id="source";layer.transform={0,0,96,64};layer.raster=Raster::filled(96,64,{20,50,70,255});d.layers={layer};auto&source=w.addProject(d);auto&target=w.addEmptyProject(false);auto*tabs=w.findChild<QTabWidget*>();tabs->setCurrentWidget(source.page);
 QTimer::singleShot(0,[]{for(auto* widget:QApplication::topLevelWidgets())if(auto* dialog=qobject_cast<QInputDialog*>(widget)){auto* combo=dialog->findChild<QComboBox*>();check(combo&&combo->count()==2,"Copy destinations omitted empty tab");combo->setCurrentIndex(1);dialog->accept();}});
 action(w,"Copy Layer to Project…");check(target.document&&target.document->width==96&&target.document->height==64&&target.document->layers.size()==2&&target.document->layers.back().raster==layer.raster&&target.history.undoCount()==1,"Copy to empty target failed");
 action(w,"Undo Copy Layers from Project");check(!target.document&&target.page->currentWidget()==target.welcome&&source.document==std::optional(d),"Copy undo lost empty target or changed source");
});
std::cout<<"{\"passed\":"<<passed<<",\"failed\":"<<failed<<",\"native_window\":false}\n";return failed?1:0;
}
