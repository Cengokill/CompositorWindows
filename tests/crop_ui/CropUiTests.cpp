// Source-derived UI contracts: Crop.swift, CropControls.swift and
// EditorCanvas.beginCropDrag/dragCrop at pinned a19db901.
#include "ui/MainWindow.h"
#include <QApplication>
#include <QLabel>
#include <iostream>
#include <map>
#include <stdexcept>
using namespace compositor;
#define REQUIRE(...) do{if(!(__VA_ARGS__))throw std::runtime_error(std::string("line ")+std::to_string(__LINE__)+": " #__VA_ARGS__);}while(false)
Document fixture(){Document d;d.id="crop-witness";d.width=100;d.height=80;Layer l;l.id="base";l.name="Base";l.transform={0,0,100,80};l.raster=Raster::filled(100,80,{70,120,190,255});d.layers={l};return d;}
QAction* command(MainWindow&w,const QString&id){for(auto*a:w.findChildren<QAction*>())if(a->property("commandId").toString()==id)return a;return nullptr;}
void cropTool(MainWindow&w,EditorProject&p){auto*a=command(w,"tool.crop");REQUIRE(a&&a->isEnabled());a->trigger();p.canvas->zoomAt(p.canvas->devicePixelRatioF(),p.canvas->rect().center());}
void drag(EditorProject&p,Point start,Point end,Qt::KeyboardModifiers flags={}){p.canvas->pointerDown({start.x,start.y},flags);p.canvas->pointerMove({end.x,end.y},flags);p.canvas->pointerUp({end.x,end.y},flags);}
void apply(MainWindow&w){auto*a=command(w,"crop.apply");REQUIRE(a&&a->isEnabled());a->trigger();}
void crop_fresh_entry_fullframe_free(){
    MainWindow w(true);auto d=fixture();auto&p=w.addProject(d);cropTool(w,p);
    auto* ratio=w.findChild<QComboBox*>("cropRatioChoice");auto* dimensions=w.findChild<QLabel*>("cropDimensions");
    REQUIRE(ratio&&ratio->currentText()=="Free");REQUIRE(dimensions&&!dimensions->isHidden()&&dimensions->text()==QString::fromUtf8("100 × 80 px"));
    REQUIRE(command(w,"crop.apply")->isEnabled()&&command(w,"crop.cancel")->isEnabled());REQUIRE(p.canvas->cropOverlay()==editing::Rect{0,0,100,80});
    REQUIRE(p.document==d&&p.history.undoCount()==0);
    ratio->setCurrentText("4:3");command(w,"tool.crop")->trigger();REQUIRE(ratio->currentText()=="4:3");
    command(w,"crop.cancel")->trigger();REQUIRE(!command(w,"crop.apply")->isEnabled()&&!command(w,"crop.cancel")->isEnabled());REQUIRE(p.canvas->cropOverlay()==editing::Rect{0,0,100,80});
    REQUIRE(dimensions->isHidden()&&p.document==d&&p.history.undoCount()==0);
    command(w,"tool.crop")->trigger();REQUIRE(ratio->currentText()=="Free"&&command(w,"crop.apply")->isEnabled()&&!dimensions->isHidden());
    REQUIRE(dimensions->text()==QString::fromUtf8("100 × 80 px")&&p.document==d&&p.history.undoCount()==0);
}
void crop_cancel_retains_document(){MainWindow w(true);auto d=fixture();auto&p=w.addProject(d);cropTool(w,p);drag(p,{20,20},{60,50},Qt::ControlModifier);auto*a=command(w,"crop.cancel");REQUIRE(a&&a->isEnabled());a->trigger();REQUIRE(p.document==d&&p.history.undoCount()==0);REQUIRE(!a->isEnabled()&&!command(w,"crop.apply")->isEnabled());}
void crop_pending_dimensions(){MainWindow w(true);auto&p=w.addProject(fixture());cropTool(w,p);drag(p,{20,20},{60,50},Qt::ControlModifier);auto* label=w.findChild<QLabel*>("cropDimensions");REQUIRE(label&&label->text()==QString::fromUtf8("40 × 30 px"));REQUIRE(!label->isHidden());}
void crop_square_corner_hit(){MainWindow w(true);auto&p=w.addProject(fixture());cropTool(w,p);drag(p,{-9,-9},{-20,-20},Qt::ControlModifier);apply(w);REQUIRE(p.document->width==111&&p.document->height==91);REQUIRE(p.document->layers.front().transform==Transform{11,11,100,80});}
void crop_control_disables_snap(){MainWindow w(true);auto&p=w.addProject(fixture());cropTool(w,p);drag(p,{20,20},{96,65},Qt::ControlModifier);apply(w);REQUIRE(p.document->width==76&&p.document->height==45);REQUIRE(p.document->layers.front().transform==Transform{-20,-20,100,80});}
void crop_eight_dip_snap_distance(){MainWindow w(true);auto&p=w.addProject(fixture());cropTool(w,p);drag(p,{20,20},{91,61});apply(w);REQUIRE(p.document->width==71&&p.document->height==41);}
void crop_invalid_drag_keeps_last_valid(){MainWindow w(true);auto&p=w.addProject(fixture());cropTool(w,p);p.canvas->pointerDown({20,20},Qt::ControlModifier);p.canvas->pointerMove({50,40},Qt::ControlModifier);p.canvas->pointerMove({40000,40},Qt::ControlModifier);p.canvas->pointerUp({40000,40},Qt::ControlModifier);apply(w);REQUIRE(p.document->width==30&&p.document->height==20);}
void crop_entire_edge_hit(){MainWindow w(true);auto&p=w.addProject(fixture());cropTool(w,p);drag(p,{25,0},{25,12},Qt::ControlModifier);apply(w);REQUIRE(p.document->width==100&&p.document->height==68);REQUIRE(p.document->layers.front().transform==Transform{0,-12,100,80});}
void crop_apply_once(){MainWindow w(true);auto d=fixture();auto&p=w.addProject(d);cropTool(w,p);drag(p,{20,20},{60,50},Qt::ControlModifier);REQUIRE(p.document==d&&p.history.undoCount()==0);apply(w);REQUIRE(p.document->width==40&&p.document->height==30);REQUIRE(p.document->layers.front().raster==d.layers.front().raster);REQUIRE(p.history.undoCount()==1&&p.history.undoName()=="Crop");REQUIRE(!command(w,"crop.apply")->isEnabled());auto snapshot=p.history.undo();REQUIRE(snapshot&&snapshot->document==d);}
int main(int argc,char**argv){QApplication app(argc,argv);const std::map<std::string,void(*)()> tests{{"crop_fresh_entry_fullframe_free",crop_fresh_entry_fullframe_free},{"crop_cancel_retains_document",crop_cancel_retains_document},{"crop_pending_dimensions",crop_pending_dimensions},{"crop_square_corner_hit",crop_square_corner_hit},{"crop_control_disables_snap",crop_control_disables_snap},{"crop_eight_dip_snap_distance",crop_eight_dip_snap_distance},{"crop_invalid_drag_keeps_last_valid",crop_invalid_drag_keeps_last_valid},{"crop_entire_edge_hit",crop_entire_edge_hit},{"crop_apply_once",crop_apply_once}};if(argc>1&&!tests.contains(argv[1]))return 2;int passed=0,failed=0;for(const auto&[name,test]:tests){if(argc>1&&name!=argv[1])continue;try{test();++passed;std::cout<<"PASS "<<name<<'\n';}catch(const std::exception&e){++failed;std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n';}}std::cout<<"RESULT passed="<<passed<<" failed="<<failed<<'\n';return failed?1:0;}
