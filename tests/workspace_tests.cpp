#include "ui/MainWindow.h"
#include <QApplication>
#include <QClipboard>
#include <QPushButton>
#include <QInputDialog>
#include <QTimer>
#include "ui/LayerPanel.h"
#include "core/DocumentExport.h"
#include "persistence/ProjectStore.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QImage>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QTemporaryDir>
#include <QTest>
#include <filesystem>
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
 control<QDoubleSpinBox>(p.welcome,"newCanvasWidth")->setValue(91);control<QDoubleSpinBox>(p.welcome,"newCanvasHeight")->setValue(67);control<QPushButton>(p.welcome,"createCanvas")->click();
 check(tabs->count()==1&&p.document&&p.document->width==91&&p.document->height==67&&p.document->layers.size()==1&&!p.document->layers[0].raster&&p.history.undoCount()==1&&p.history.modified(),"Create did not initialize same project and history");
 QTest::keyClick(&w,Qt::Key_Z,Qt::ControlModifier); // QAction shortcut needs active window; use registered action as fallback below.
 if(p.document)action(w,"Undo New Canvas");
 check(!p.document&&p.page->currentWidget()==p.welcome&&!p.canvas->viewportProvider(0,0,91,67,1).raster,"Undo kept old canvas visible");
 action(w,"Redo New Canvas");check(p.document&&p.page->currentWidget()==p.canvas&&p.document->width==91,"Redo failed to restore canvas");
});
test("new_tab_clipboard_defaults_and_close_last",[]{
 QImage image(23,17,QImage::Format_ARGB32);image.fill(Qt::green);QApplication::clipboard()->setImage(image);
 MainWindow w(true);auto&first=w.addEmptyProject();check(control<QDoubleSpinBox>(first.welcome,"newCanvasWidth")->value()==1920,"Initial tab used clipboard suggestion");
 action(w,"&New Canvas…");auto*tabs=w.findChild<QTabWidget*>();check(tabs->count()==2,"New did not create an empty tab");auto*page=tabs->currentWidget();check(control<QDoubleSpinBox>(page,"newCanvasWidth")->value()==23&&control<QDoubleSpinBox>(page,"newCanvasHeight")->value()==17&&control<QComboBox>(page,"newCanvasPreset")->currentIndex()==0,"New tab omitted clipboard dimensions");
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
test("new_canvas_presets_units_and_resolution_keep_pixels",[]{
 MainWindow w(true);auto&p=w.addEmptyProject();auto*tabs=w.findChild<QTabWidget*>();
 auto*preset=control<QComboBox>(p.welcome,"newCanvasPreset");auto*unit=control<QComboBox>(p.welcome,"newCanvasUnit");auto*dpi=control<QSpinBox>(p.welcome,"newCanvasResolution");auto*width=control<QDoubleSpinBox>(p.welcome,"newCanvasWidth");auto*height=control<QDoubleSpinBox>(p.welcome,"newCanvasHeight");auto*create=control<QPushButton>(p.welcome,"createCanvas");
 check(width->value()==1920&&height->value()==1080&&preset->currentText()=="Full HD · 1920 × 1080"&&dpi->value()==72,"Default is 1920 × 1080 px at 72 DPI");
 preset->setCurrentIndex(preset->findText("4K UHD · 3840 × 2160"));check(width->value()==3840&&height->value()==2160,"Preset sets the pixel size");
 width->setValue(1000);check(preset->currentIndex()==0,"Editing a dimension selects Custom");
 for(int index:{1,2,3,0,1,0})unit->setCurrentIndex(index);check(width->value()==1000&&height->value()==2160,"Repeated unit switches keep the pixel size");
 unit->setCurrentIndex(1);dpi->setValue(20000);check(dpi->value()==9600,"Resolution is limited to 9600 DPI");
 dpi->setValue(300);width->setValue(8.5);height->setValue(11);
 dpi->setValue(3);dpi->setValue(30);dpi->setValue(300);check(width->value()==8.5&&height->value()==11,"Changing resolution keeps the pixel size and no decimal drift accumulates");
 create->click();
 check(tabs->count()==1&&p.document&&p.document->width==2550&&p.document->height==3300&&p.document->resolution==300&&p.document->layers.size()==1&&p.document->layers[0].name=="Layer 1"&&!p.document->layers[0].raster,"8.5 × 11 in at 300 DPI creates a transparent 2550 × 3300 canvas");
 const auto id=p.document->layers[0].id;check(p.active==id&&p.selected==std::vector<std::string>{id},"Initial layer is selected");
 action(w,"Undo New Canvas");check(!p.document&&p.page->currentWidget()==p.welcome,"Undo returns to the welcome card");
 action(w,"Redo New Canvas");check(p.document&&p.document->width==2550&&p.document->resolution==300&&p.active==id&&p.selected==std::vector<std::string>{id},"Redo restores the same document and selection");
});
test("new_canvas_category_presets_set_units_and_resolution",[]{
 MainWindow w(true);auto&p=w.addEmptyProject();
 auto*preset=control<QComboBox>(p.welcome,"newCanvasPreset");auto*unit=control<QComboBox>(p.welcome,"newCanvasUnit");auto*dpi=control<QSpinBox>(p.welcome,"newCanvasResolution");auto*width=control<QDoubleSpinBox>(p.welcome,"newCanvasWidth");auto*height=control<QDoubleSpinBox>(p.welcome,"newCanvasHeight");auto*create=control<QPushButton>(p.welcome,"createCanvas");
 auto* model=static_cast<QStandardItemModel*>(preset->model());check(model&&!model->item(preset->findText("Photo"))->isEnabled()&&!model->item(preset->findText("Web"))->isEnabled(),"Category headings are not selectable");
 preset->setCurrentIndex(preset->findText("5 × 7 in · portrait"));
 check(unit->currentData().toInt()==int(compositor::editing::PrintUnit::Inches)&&dpi->value()==300&&width->value()==5&&height->value()==7,"Photo preset sets inches at 300 DPI");
 create->click();check(p.document&&p.document->width==1500&&p.document->height==2100&&p.document->resolution==300,"5 × 7 in at 300 DPI is 1500 × 2100 px");
 auto&q=w.addEmptyProject(false);auto*preset2=control<QComboBox>(q.welcome,"newCanvasPreset");auto*unit2=control<QComboBox>(q.welcome,"newCanvasUnit");auto*dpi2=control<QSpinBox>(q.welcome,"newCanvasResolution");auto*width2=control<QDoubleSpinBox>(q.welcome,"newCanvasWidth");auto*height2=control<QDoubleSpinBox>(q.welcome,"newCanvasHeight");auto*create2=control<QPushButton>(q.welcome,"createCanvas");
 preset2->setCurrentIndex(preset2->findText("A4 · 210 × 297 mm"));
 check(unit2->currentData().toInt()==int(compositor::editing::PrintUnit::Millimeters)&&dpi2->value()==300&&width2->value()==210&&height2->value()==297,"A4 preset sets millimeters at 300 DPI");
 create2->click();check(q.document&&q.document->width==2480&&q.document->height==3508,"A4 at 300 DPI is 2480 × 3508 px");
 auto&r=w.addEmptyProject(false);auto*preset3=control<QComboBox>(r.welcome,"newCanvasPreset");auto*dpi3=control<QSpinBox>(r.welcome,"newCanvasResolution");auto*width3=control<QDoubleSpinBox>(r.welcome,"newCanvasWidth");
 preset3->setCurrentIndex(preset3->findText("Web · 1366 × 768"));check(dpi3->value()==72&&width3->value()==1366,"Web preset uses pixels at 72 DPI");
});
test("new_canvas_solid_background_color_and_budget",[]{
 MainWindow w(true);auto&p=w.addEmptyProject();
 auto*unit=control<QComboBox>(p.welcome,"newCanvasUnit");auto*dpi=control<QSpinBox>(p.welcome,"newCanvasResolution");auto*width=control<QDoubleSpinBox>(p.welcome,"newCanvasWidth");auto*height=control<QDoubleSpinBox>(p.welcome,"newCanvasHeight");auto*background=control<QComboBox>(p.welcome,"newCanvasBackground");auto*create=control<QPushButton>(p.welcome,"createCanvas");auto*preset=control<QComboBox>(p.welcome,"newCanvasPreset");
 unit->setCurrentIndex(3);dpi->setValue(300);width->setValue(210);height->setValue(297);background->setCurrentIndex(2);
 check(create->isEnabled(),"Black print-size canvas is enabled");create->click();
 check(p.document&&p.document->width==2480&&p.document->height==3508&&p.document->layers.size()==1,"210 × 297 mm at 300 DPI rounds to 2480 × 3508 px");
 const auto& black=p.document->layers[0];check(black.name=="Background"&&black.transform==Transform{0,0,2480,3508}&&black.raster&&black.raster->pixel(0,0)==Pixel{0,0,0,255}&&black.raster->pixel(2479,3507)==Pixel{0,0,0,255}&&black.raster->pixel(1240,1754)==Pixel{0,0,0,255},"Black background is opaque across the full canvas");
 auto&q=w.addEmptyProject(false);auto*preset2=control<QComboBox>(q.welcome,"newCanvasPreset");auto*background2=control<QComboBox>(q.welcome,"newCanvasBackground");auto*create2=control<QPushButton>(q.welcome,"createCanvas");
 preset2->setCurrentIndex(preset2->findText("HD · 1280 × 720"));background2->setCurrentIndex(1);create2->click();
 check(q.document&&q.document->width==1280&&q.document->height==720&&q.document->layers[0].raster&&q.document->layers[0].raster->pixel(1279,719)==Pixel{255,255,255,255},"White preset is opaque");
 auto&r=w.addEmptyProject(false);auto*width3=control<QDoubleSpinBox>(r.welcome,"newCanvasWidth");auto*height3=control<QDoubleSpinBox>(r.welcome,"newCanvasHeight");auto*background3=control<QComboBox>(r.welcome,"newCanvasBackground");auto*create3=control<QPushButton>(r.welcome,"createCanvas");
 width3->setValue(0);check(width3->value()==1,"Zero width is clamped to one pixel");width3->setValue(30000);height3->setValue(30000);background3->setCurrentIndex(1);
 check(!create3->isEnabled(),"Solid background above the raster budget cannot be created");create3->click();check(!r.document,"Rejected solid background leaves the welcome card");
 background3->setCurrentIndex(0);check(create3->isEnabled(),"Transparent canvas has no raster budget");
});
test("new_canvas_background_saves_reopens_and_exports",[]{
 MainWindow w(true);auto&p=w.addEmptyProject();
 control<QComboBox>(p.welcome,"newCanvasUnit")->setCurrentIndex(1);control<QSpinBox>(p.welcome,"newCanvasResolution")->setValue(300);control<QDoubleSpinBox>(p.welcome,"newCanvasWidth")->setValue(8.5);control<QDoubleSpinBox>(p.welcome,"newCanvasHeight")->setValue(11);control<QComboBox>(p.welcome,"newCanvasBackground")->setCurrentIndex(1);control<QPushButton>(p.welcome,"createCanvas")->click();
 check(p.document&&p.document->layers.size()==1,"White 8.5 × 11 in canvas created");
 QTemporaryDir dir;check(dir.isValid(),"temporary project directory");
 const auto project=std::filesystem::path(dir.filePath("White.comp").toStdWString());ProjectStore store(makeWicProjectCodec());store.save(project,*p.document,p.active);
 const auto loaded=store.load(project).document;
 check(loaded.width==2550&&loaded.height==3300&&loaded.resolution==300&&loaded.layers.size()==1&&loaded.layers[0].name=="Background"&&loaded.layers[0].raster&&loaded.layers[0].raster->pixel(0,0)==Pixel{255,255,255,255},"Saved background reopens at full size, resolution and opacity");
 const auto png=dir.filePath("White.png");exportDocumentAtomic(loaded,std::filesystem::path(png.toStdWString()));
 QImage image(png);check(!image.isNull()&&image.width()==2550&&image.height()==3300,"PNG has the document size");
 check(image.pixelColor(0,0)==QColor(255,255,255,255)&&image.pixelColor(1275,1650)==QColor(255,255,255,255)&&image.pixelColor(2549,3299)==QColor(255,255,255,255),"PNG corners and center are opaque white");
 check(image.dotsPerMeterX()==qRound(300/0.0254)&&image.dotsPerMeterY()==qRound(300/0.0254),"PNG records 300 DPI");
});
std::cout<<"{\"passed\":"<<passed<<",\"failed\":"<<failed<<",\"native_window\":false}\n";return failed?1:0;
}
