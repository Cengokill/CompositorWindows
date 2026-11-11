// Source-derived UI contracts: Crop.swift, CropControls.swift and
// EditorCanvas.beginCropDrag/dragCrop at pinned a19db901.
#include "ui/MainWindow.h"
#include <QApplication>
#include <QDialogButtonBox>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QToolBar>
#include <exception>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
using namespace compositor;
#define REQUIRE(...) do{if(!(__VA_ARGS__))throw std::runtime_error(std::string("line ")+std::to_string(__LINE__)+": " #__VA_ARGS__);}while(false)
Document fixture(int width=100,int height=80){Document d;d.id="crop-witness";d.width=width;d.height=height;Layer l;l.id="base";l.name="Base";l.transform={0,0,double(width),double(height)};l.raster=Raster::filled(width,height,{70,120,190,255});d.layers={l};return d;}
QAction* command(MainWindow&w,const QString&id){for(auto*a:w.findChildren<QAction*>())if(a->property("commandId").toString()==id)return a;return nullptr;}
void cropTool(MainWindow&w,EditorProject&p){auto*a=command(w,"tool.crop");REQUIRE(a&&a->isEnabled());a->trigger();p.canvas->zoomAt(p.canvas->devicePixelRatioF(),p.canvas->rect().center());}
void drag(EditorProject&p,Point start,Point end,Qt::KeyboardModifiers flags={}){p.canvas->pointerDown({start.x,start.y},flags);p.canvas->pointerMove({end.x,end.y},flags);p.canvas->pointerUp({end.x,end.y},flags);}
void apply(MainWindow&w){auto*a=command(w,"crop.apply");REQUIRE(a&&a->isEnabled());a->trigger();}
void customDialog(MainWindow&w,const std::function<void(QDialog&)>& interact){
    auto* ratio=w.findChild<QComboBox*>("cropRatioChoice");REQUIRE(ratio);
    std::exception_ptr failure;
    QTimer::singleShot(0,&w,[&]{auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());try{REQUIRE(dialog);interact(*dialog);}catch(...){failure=std::current_exception();if(dialog)dialog->reject();}});
    ratio->setCurrentText("Custom…");if(failure)std::rethrow_exception(failure);
}
QLineEdit* customInput(QDialog& dialog){auto* input=dialog.findChild<QLineEdit*>("customCropRatioInput");REQUIRE(input);return input;}
QPushButton* customButton(QDialog& dialog,QDialogButtonBox::StandardButton button){auto* box=dialog.findChild<QDialogButtonBox*>();REQUIRE(box);auto* result=box->button(button);REQUIRE(result);return result;}
void crop_fresh_entry_fullframe_free(){
    MainWindow w(true);auto d=fixture();auto&p=w.addProject(d);w.resize(3840,800);w.show();QApplication::processEvents();cropTool(w,p);
    auto* ratio=w.findChild<QComboBox*>("cropRatioChoice");auto* dimensions=w.findChild<QLabel*>("cropDimensions");
    REQUIRE(ratio&&ratio->currentText()=="Free");REQUIRE(dimensions&&!dimensions->isHidden()&&dimensions->text()==QString::fromUtf8("100 × 80 px"));
    REQUIRE(command(w,"crop.apply")->isEnabled()&&command(w,"crop.cancel")->isEnabled());REQUIRE(p.canvas->cropOverlay()==editing::Rect{0,0,100,80});
    REQUIRE(p.document==d&&p.history.undoCount()==0);
    ratio->setCurrentText("4:3");command(w,"tool.crop")->trigger();REQUIRE(ratio->currentText()=="4:3");
    command(w,"crop.cancel")->trigger();REQUIRE(!command(w,"crop.apply")->isEnabled()&&!command(w,"crop.cancel")->isEnabled());REQUIRE(p.canvas->cropOverlay()==editing::Rect{0,0,100,80});
    REQUIRE(dimensions->isHidden()&&p.document==d&&p.history.undoCount()==0);
    command(w,"tool.crop")->trigger();REQUIRE(ratio->currentText()=="Free"&&command(w,"crop.apply")->isEnabled()&&!dimensions->isHidden());
    REQUIRE(dimensions->text()==QString::fromUtf8("100 × 80 px")&&p.document==d&&p.history.undoCount()==0);
    auto* bar=w.findChild<QToolBar*>("drawingOptions");REQUIRE(bar);for(auto* item:bar->actions())if(item->property("hideDuringCrop").toBool())REQUIRE(!item->isVisible());
    auto* gradient=w.findChild<QComboBox*>("gradientShape");auto* shape=w.findChild<QComboBox*>("shapeKind");REQUIRE(gradient&&shape&&!gradient->isVisible()&&!shape->isVisible());
    REQUIRE(ratio->isVisibleTo(bar)&&command(w,"crop.cancel")->isVisible()&&command(w,"crop.apply")->isVisible());
    const auto selectVisibleTool=[&](const char* id){for(auto* item:w.findChildren<QAction*>())if(item->property("commandId").toString()==id&&item->isVisible()&&item->isEnabled()){item->trigger();return;}throw std::runtime_error("Visible tool action not found");};
    selectVisibleTool("tool.gradient");QApplication::processEvents();REQUIRE(!gradient->isHidden());
    selectVisibleTool("tool.shape");QApplication::processEvents();REQUIRE(!shape->isHidden());
    cropTool(w,p);for(auto* item:bar->actions())if(item->property("hideDuringCrop").toBool())REQUIRE(!item->isVisible());
}
void crop_cancel_retains_document(){MainWindow w(true);auto d=fixture();auto&p=w.addProject(d);cropTool(w,p);drag(p,{20,20},{60,50},Qt::ControlModifier);auto*a=command(w,"crop.cancel");REQUIRE(a&&a->isEnabled());a->trigger();REQUIRE(p.document==d&&p.history.undoCount()==0);REQUIRE(!a->isEnabled()&&!command(w,"crop.apply")->isEnabled());}
void crop_pending_dimensions(){MainWindow w(true);auto&p=w.addProject(fixture());cropTool(w,p);drag(p,{20,20},{60,50},Qt::ControlModifier);auto* label=w.findChild<QLabel*>("cropDimensions");REQUIRE(label&&label->text()==QString::fromUtf8("40 × 30 px"));REQUIRE(!label->isHidden());}
void crop_square_corner_hit(){MainWindow w(true);auto&p=w.addProject(fixture());cropTool(w,p);drag(p,{-9,-9},{-20,-20},Qt::ControlModifier);apply(w);REQUIRE(p.document->width==111&&p.document->height==91);REQUIRE(p.document->layers.front().transform==Transform{11,11,100,80});}
void crop_control_disables_snap(){MainWindow w(true);auto&p=w.addProject(fixture());cropTool(w,p);drag(p,{20,20},{96,65},Qt::ControlModifier);apply(w);REQUIRE(p.document->width==76&&p.document->height==45);REQUIRE(p.document->layers.front().transform==Transform{-20,-20,100,80});}
void crop_move_snaps_ratio_frame_to_document_edge(){
    const auto run=[](Qt::KeyboardModifiers modifiers,double expectedX){MainWindow w(true);auto d=fixture(100,100);auto&p=w.addProject(d);cropTool(w,p);auto* ratio=w.findChild<QComboBox*>("cropRatioChoice");REQUIRE(ratio);ratio->setCurrentText("1:1");
        drag(p,{100,100},{50,50});REQUIRE(p.canvas->cropOverlay()==editing::Rect{0,0,50,50});drag(p,{25,25},{73,25},modifiers);
        REQUIRE(p.canvas->cropOverlay()==editing::Rect{expectedX,0,50,50}&&p.document==d&&p.history.undoCount()==0);command(w,"crop.cancel")->trigger();REQUIRE(p.document==d&&p.history.undoCount()==0);
    };
    run(Qt::NoModifier,50);run(Qt::ControlModifier,48);
}
void crop_eight_dip_snap_distance(){MainWindow w(true);auto&p=w.addProject(fixture());cropTool(w,p);drag(p,{20,20},{91,61});apply(w);REQUIRE(p.document->width==71&&p.document->height==41);}
void crop_invalid_drag_keeps_last_valid(){MainWindow w(true);auto&p=w.addProject(fixture());cropTool(w,p);p.canvas->pointerDown({20,20},Qt::ControlModifier);p.canvas->pointerMove({50,40},Qt::ControlModifier);p.canvas->pointerMove({40000,40},Qt::ControlModifier);p.canvas->pointerUp({40000,40},Qt::ControlModifier);apply(w);REQUIRE(p.document->width==30&&p.document->height==20);}
void crop_entire_edge_hit(){MainWindow w(true);auto&p=w.addProject(fixture());cropTool(w,p);drag(p,{25,0},{25,12},Qt::ControlModifier);apply(w);REQUIRE(p.document->width==100&&p.document->height==68);REQUIRE(p.document->layers.front().transform==Transform{0,-12,100,80});}
void crop_apply_once(){MainWindow w(true);auto d=fixture();auto&p=w.addProject(d);cropTool(w,p);drag(p,{20,20},{60,50},Qt::ControlModifier);REQUIRE(p.document==d&&p.history.undoCount()==0);apply(w);REQUIRE(p.document->width==40&&p.document->height==30);REQUIRE(p.document->layers.front().raster==d.layers.front().raster);REQUIRE(p.history.undoCount()==1&&p.history.undoName()=="Crop");REQUIRE(!command(w,"crop.apply")->isEnabled());auto snapshot=p.history.undo();REQUIRE(snapshot&&snapshot->document==d);}
void crop_ratio_menu_and_presets(){
    MainWindow w(true);auto&p=w.addProject(fixture());cropTool(w,p);auto* ratio=w.findChild<QComboBox*>("cropRatioChoice");REQUIRE(ratio&&ratio->currentText()=="Free");
    REQUIRE(ratio->count()==13&&ratio->itemText(0)=="Free"&&ratio->itemText(1)=="Canvas"&&ratio->itemText(3)=="1:1");
    REQUIRE(ratio->itemText(5)=="4:3"&&ratio->itemText(6)=="16:9"&&ratio->itemText(7)=="2.39:1"&&ratio->itemText(9)=="3:4"&&ratio->itemText(10)=="9:16"&&ratio->itemText(12)=="Custom…");
    REQUIRE(ratio->itemText(2).isEmpty()&&ratio->itemText(4).isEmpty()&&ratio->itemText(8).isEmpty()&&ratio->itemText(11).isEmpty());
    ratio->setCurrentText("2.39:1");REQUIRE(p.canvas->cropOverlay()==editing::Rect{0,19,100,42});
    ratio->setCurrentText("Canvas");REQUIRE(p.canvas->cropOverlay()==editing::Rect{0,0,100,80});
    ratio->setCurrentText("Free");REQUIRE(p.canvas->cropOverlay()==editing::Rect{0,0,100,80});
}
void crop_custom_ratio_validates_and_cancels(){
    MainWindow w(true);auto d=fixture();auto&p=w.addProject(d);cropTool(w,p);drag(p,{10,10},{60,40},Qt::ControlModifier);const auto original=p.canvas->cropOverlay();
    customDialog(w,[&](QDialog& dialog){auto* input=customInput(dialog);auto* apply=customButton(dialog,QDialogButtonBox::Ok);
        for(const auto* invalid:{"","5",":5","5:","5:4:1","Free","0:5","-5:4","5:-4","nan:1","inf:1","1e309:1","30001:1","1:30001"}){input->setText(invalid);QApplication::processEvents();REQUIRE(!apply->isEnabled()&&p.canvas->cropOverlay()==original&&p.document==d);}
        for(const auto* valid:{"4:5","2,5:1","1:30000","30000:1"}){input->setText(valid);QApplication::processEvents();REQUIRE(apply->isEnabled());}
        input->setText("2,5:1");QApplication::processEvents();apply->click();
    });
    auto* ratio=w.findChild<QComboBox*>("cropRatioChoice");REQUIRE(ratio&&ratio->currentText()=="2.5:1");REQUIRE(p.canvas->cropOverlay()==editing::Rect{10,15,50,20});
    REQUIRE(p.document==d&&p.history.undoCount()==0);command(w,"crop.cancel")->trigger();REQUIRE(p.document==d&&p.history.undoCount()==0&&p.canvas->cropOverlay()==editing::Rect{0,0,100,80});
    cropTool(w,p);drag(p,{10,10},{60,40},Qt::ControlModifier);const auto cancelDraft=p.canvas->cropOverlay();
    customDialog(w,[&](QDialog& dialog){customInput(dialog)->setText("4:5");customButton(dialog,QDialogButtonBox::Cancel)->click();});
    REQUIRE(ratio->currentText()=="Free"&&p.canvas->cropOverlay()==cancelDraft&&p.document==d&&p.history.undoCount()==0);
}
void crop_custom_ratio_apply_undo_redo(){
    MainWindow w(true);auto d=fixture(40,20);auto&p=w.addProject(d);cropTool(w,p);
    customDialog(w,[&](QDialog& dialog){auto* input=customInput(dialog);input->setText("4:5");customButton(dialog,QDialogButtonBox::Ok)->click();});
    auto* ratio=w.findChild<QComboBox*>("cropRatioChoice");REQUIRE(ratio&&ratio->currentText()=="4:5");REQUIRE(p.canvas->cropOverlay()==editing::Rect{0,-15,40,50});REQUIRE(p.document==d&&p.history.undoCount()==0);
    REQUIRE(ratio->count()==14&&ratio->itemText(11)=="4:5");apply(w);REQUIRE(p.document->width==40&&p.document->height==50&&p.history.undoCount()==1&&p.history.undoName()=="Crop");
    REQUIRE(p.canvas->cropOverlay()==editing::Rect{0,0,40,50});auto* undo=command(w,"edit.undo");auto* redo=command(w,"edit.redo");REQUIRE(undo&&redo);
    undo->trigger();REQUIRE(p.document==d&&redo->isEnabled()&&p.canvas->cropOverlay()==editing::Rect{0,0,40,20});
    redo->trigger();REQUIRE(p.document->width==40&&p.document->height==50&&p.history.undoCount()==1&&undo->isEnabled()&&p.canvas->cropOverlay()==editing::Rect{0,0,40,50});
}
void crop_custom_ratio_resize_preserves_proportions(){
    for(const double centerY:{85.,0.}){
        MainWindow w(true);auto d=fixture(200,200);auto& p=w.addProject(d);cropTool(w,p);
        p.canvas->zoomAt(4*p.canvas->devicePixelRatioF(),p.canvas->rect().center());
        drag(p,{20,centerY-5},{30,centerY+5},Qt::ControlModifier);
        customDialog(w,[&](QDialog& dialog){
            customInput(dialog)->setText("4:5");
            customButton(dialog,QDialogButtonBox::Ok)->click();
        });
        REQUIRE(p.canvas->cropOverlay()==editing::Rect{20,centerY-6,10,12});
        drag(p,{30,centerY},{120,centerY},Qt::ControlModifier);
        const auto preview=p.canvas->cropOverlay();
        REQUIRE(preview&&preview->width==100&&preview->height==125);
        REQUIRE(p.document==d&&p.history.undoCount()==0);
        apply(w);
        REQUIRE(p.document->width==100&&p.document->height==125&&p.history.undoCount()==1);
    }
}
void crop_custom_ratio_fits_limits(){
    MainWindow w(true);auto d=fixture(40,20);auto&p=w.addProject(d);cropTool(w,p);
    customDialog(w,[&](QDialog& dialog){customInput(dialog)->setText("1:30000");customButton(dialog,QDialogButtonBox::Ok)->click();});
    REQUIRE(p.canvas->cropOverlay()==editing::Rect{0,-14990,1,30000});
    customDialog(w,[&](QDialog& dialog){customInput(dialog)->setText("30000:1");customButton(dialog,QDialogButtonBox::Ok)->click();});
    REQUIRE(p.canvas->cropOverlay()==editing::Rect{0,10,30000,1});REQUIRE(p.document==d&&p.history.undoCount()==0);
}
void crop_ratio_snaps_document_edges(){
    const struct {int width,height;const char* ratio;} cases[]{{90,160,"9:16"},{160,90,"16:9"}};
    for(const auto& item:cases){MainWindow w(true);auto d=fixture(item.width,item.height);auto&p=w.addProject(d);cropTool(w,p);auto* ratio=w.findChild<QComboBox*>("cropRatioChoice");REQUIRE(ratio);ratio->setCurrentText(item.ratio);
        drag(p,{double(item.width),double(item.height)},{double(item.width-3),double(item.height-6)});
        REQUIRE(p.canvas->cropOverlay()==editing::Rect{0,0,double(item.width),double(item.height)});
        REQUIRE(p.document==d&&p.history.undoCount()==0&&command(w,"crop.apply")->isEnabled());command(w,"crop.cancel")->trigger();REQUIRE(p.document==d&&p.history.undoCount()==0);
    }
    MainWindow w(true);auto d=fixture(90,160);auto&p=w.addProject(d);cropTool(w,p);w.findChild<QComboBox*>("cropRatioChoice")->setCurrentText("9:16");
    drag(p,{90,160},{87,154},Qt::ControlModifier);REQUIRE(p.canvas->cropOverlay()!=editing::Rect{0,0,90,160}&&p.document==d&&p.history.undoCount()==0);
}
void crop_enter_and_return_apply(){
    for(const auto key:{Qt::Key_Enter,Qt::Key_Return}){MainWindow w(true);auto d=fixture();auto&p=w.addProject(d);cropTool(w,p);drag(p,{20,20},{60,50},Qt::ControlModifier);
        const auto modifiers=key==Qt::Key_Enter?Qt::KeypadModifier:Qt::NoModifier;QKeyEvent event(QEvent::KeyPress,key,modifiers);QApplication::sendEvent(&w,&event);
        REQUIRE(p.document->width==40&&p.document->height==30&&p.history.undoCount()==1&&p.history.undoName()=="Crop");
        REQUIRE(p.canvas->cropOverlay()==editing::Rect{0,0,40,30});
    }
}
int main(int argc,char**argv){QApplication app(argc,argv);const std::map<std::string,void(*)()> tests{{"crop_fresh_entry_fullframe_free",crop_fresh_entry_fullframe_free},{"crop_cancel_retains_document",crop_cancel_retains_document},{"crop_pending_dimensions",crop_pending_dimensions},{"crop_square_corner_hit",crop_square_corner_hit},{"crop_control_disables_snap",crop_control_disables_snap},{"crop_move_snaps_ratio_frame_to_document_edge",crop_move_snaps_ratio_frame_to_document_edge},{"crop_eight_dip_snap_distance",crop_eight_dip_snap_distance},{"crop_invalid_drag_keeps_last_valid",crop_invalid_drag_keeps_last_valid},{"crop_entire_edge_hit",crop_entire_edge_hit},{"crop_apply_once",crop_apply_once},{"crop_ratio_menu_and_presets",crop_ratio_menu_and_presets},{"crop_custom_ratio_validates_and_cancels",crop_custom_ratio_validates_and_cancels},{"crop_custom_ratio_apply_undo_redo",crop_custom_ratio_apply_undo_redo},{"crop_custom_ratio_resize_preserves_proportions",crop_custom_ratio_resize_preserves_proportions},{"crop_custom_ratio_fits_limits",crop_custom_ratio_fits_limits},{"crop_ratio_snaps_document_edges",crop_ratio_snaps_document_edges},{"crop_enter_and_return_apply",crop_enter_and_return_apply}};if(argc>1&&!tests.contains(argv[1]))return 2;int passed=0,failed=0;for(const auto&[name,test]:tests){if(argc>1&&name!=argv[1])continue;try{test();++passed;std::cout<<"PASS "<<name<<'\n';}catch(const std::exception&e){++failed;std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n';}}std::cout<<"RESULT passed="<<passed<<" failed="<<failed<<'\n';return failed?1:0;}
