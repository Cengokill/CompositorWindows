#include "ui/MainWindow.h"
#include "ui/NativeCanvas.h"
#include "ui/SegmentedControl.h"
#include "ui/PaletteDialog.h"
#include "ui/ToolbarControls.h"
#include "ui/LayerPanelWidgets.h"
#include "ui/LayerPanelTokens.h"
#include <QComboBox>
#include <QApplication>
#include <QColorDialog>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QTest>
#include <QStatusBar>
#include <QPushButton>
#include <QToolButton>
#include <QLabel>
#include <QSlider>
#include <QKeyEvent>
#include <QTimer>
#include <QToolBar>
#include <QWidgetAction>
#include <cmath>
#include <iostream>
#include <map>
#include <stdexcept>
using namespace compositor;
#define REQUIRE(...) do{if(!(__VA_ARGS__))throw std::runtime_error(std::string("line ")+std::to_string(__LINE__)+": " #__VA_ARGS__);}while(false)
Document fixture(){Document d;d.id="shape-witness";d.width=100;d.height=80;for(const char* id:{"bottom","middle","top"}){Layer l;l.id=id;l.name=id;l.transform={0,0,8,8};l.raster=Raster::filled(8,8,{50,60,70,255});d.layers.push_back(l);}return d;}
void shapeTool(MainWindow& w){for(auto* action:w.findChildren<QAction*>())if(action->text()=="Shape (U)"){action->trigger();return;}throw std::runtime_error("Shape tool absent");}
QDoubleSpinBox* radius(MainWindow&w){for(auto* value:w.findChildren<QDoubleSpinBox*>())if(value->accessibleName()=="Corner radius")return value;throw std::runtime_error("Shape radius absent");}
ui::SegmentedControl* kind(MainWindow&w){auto* value=ui::findSegmentedControl(&w,"shapeKind");REQUIRE(value);return value;}
void down(EditorProject&p,Point at){p.canvas->pointerDown({at.x,at.y},{});}
void move(EditorProject&p,Point at,Qt::KeyboardModifiers flags={}){p.canvas->pointerMove({at.x,at.y},flags);}
void up(EditorProject&p,Point at,Qt::KeyboardModifiers flags={}){p.canvas->pointerUp({at.x,at.y},flags);}
void draw(EditorProject&p,Point start={10,12},Point end={20,24},Qt::KeyboardModifiers flags={}){down(p,start);move(p,end,flags);up(p,end,flags);}
const Layer& added(const EditorProject&p){auto it=std::find_if(p.document->layers.begin(),p.document->layers.end(),[&](const Layer& l){return l.id==p.active;});REQUIRE(it!=p.document->layers.end()&&!it->shapeJson.empty());return *it;}
void shape_insert_sibling(){MainWindow w(true);auto d=fixture();auto&p=w.addProject(d);p.active="middle";p.selected={p.active};shapeTool(w);draw(p);REQUIRE(p.document->layers.size()==4);REQUIRE(p.document->layers[2].id==p.active&&added(p).parentId.empty());REQUIRE(p.document->layers[3].id=="top");}
void shape_insert_folder(){MainWindow w(true);auto d=fixture();d.layers[0].raster.reset();d.layers[0].group=true;d.layers[1].parentId="bottom";auto&p=w.addProject(d);p.active="bottom";p.selected={p.active};p.collapsedGroups.insert("bottom");shapeTool(w);draw(p);REQUIRE(p.document->layers.size()==4);REQUIRE(p.document->layers[1].id==p.active&&added(p).parentId=="bottom");REQUIRE(p.document->layers[2].id=="middle"&&p.collapsedGroups.contains("bottom"));}
void shape_insert_nested_child(){MainWindow w(true);auto d=fixture();d.layers[0].raster.reset();d.layers[0].group=true;d.layers[1].parentId="bottom";auto&p=w.addProject(d);p.active="middle";p.selected={p.active};shapeTool(w);draw(p);REQUIRE(p.document->layers[2].id==p.active&&added(p).parentId=="bottom"&&p.document->layers[3].id=="top");}
void shape_fractional_anchor(){MainWindow w(true);auto&p=w.addProject(fixture());shapeTool(w);draw(p,{10.6,12.4},{20.2,22.6});REQUIRE(added(p).transform==Transform{11,12,9,11});REQUIRE(added(p).raster->width==9&&added(p).raster->height==11);}
void shape_center_square(){MainWindow w(true);auto&p=w.addProject(fixture());shapeTool(w);draw(p,{-10.6,12.4},{-4.2,16.4},Qt::ShiftModifier|Qt::AltModifier);REQUIRE(added(p).transform==Transform{-18,5,14,14});}
void shape_kind_change_cancels(){MainWindow w(true);auto d=fixture();auto&p=w.addProject(d);shapeTool(w);down(p,{10,12});move(p,{20,24});kind(w)->choose(1);up(p,{20,24});REQUIRE(p.document==d&&p.history.undoCount()==0);draw(p);REQUIRE(editing::decodeShapeStyle(added(p).shapeJson).kind==editing::ShapeKind::Ellipse);}
void shape_radius_limit(){MainWindow w(true);auto&p=w.addProject(fixture());shapeTool(w);REQUIRE(radius(w)->minimum()==0&&radius(w)->maximum()==5000);radius(w)->setValue(9000);REQUIRE(radius(w)->value()==5000);draw(p);REQUIRE(editing::decodeShapeStyle(added(p).shapeJson).cornerRadius==5000);}
void shape_radius_frozen(){MainWindow w(true);auto&p=w.addProject(fixture());shapeTool(w);radius(w)->setValue(3);down(p,{10,12});move(p,{20,24});radius(w)->setValue(8);up(p,{20,24});REQUIRE(editing::decodeShapeStyle(added(p).shapeJson).cornerRadius==3);REQUIRE(radius(w)->value()==8);}
void shape_draft_immutable(){MainWindow w(true);auto d=fixture();auto&p=w.addProject(d);const auto active=p.active;shapeTool(w);down(p,{10,12});move(p,{20,24});REQUIRE(p.document==d&&p.active==active&&p.history.undoCount()==0);p.canvas->pointerCancel();REQUIRE(p.document==d&&p.active==active&&p.history.undoCount()==0);}
void shape_click_noop(){MainWindow w(true);auto d=fixture();auto&p=w.addProject(d);shapeTool(w);down(p,{10.6,12.4});up(p,{10.6,12.4});REQUIRE(p.document==d&&p.history.undoCount()==0);}
void shape_history_selection(){MainWindow w(true);auto d=fixture();d.selection=editing::rasterSelection(editing::SelectionOutline::rectangle({1,2,3,4},false),100,80);d.layers.back().mask=Mask{std::make_shared<GrayRaster>(GrayRaster{1,1,{255}})};auto&p=w.addProject(d);p.maskSelected=true;shapeTool(w);draw(p);REQUIRE(p.document->selection==d.selection&&!p.maskSelected);REQUIRE(p.history.undoCount()==1&&p.history.undoName()=="Rectangle");auto snapshot=p.history.undo();REQUIRE(snapshot&&snapshot->document==d);}
void shape_stroke_draw(){MainWindow w(true);auto&p=w.addProject(fixture());shapeTool(w);auto*checkbox=w.findChild<QCheckBox*>("shapeStroke");REQUIRE(checkbox);checkbox->setChecked(true);w.findChild<QDoubleSpinBox*>("shapeStrokeWidth")->setValue(3);draw(p);const auto style=editing::decodeShapeStyle(added(p).shapeJson);REQUIRE(style.strokeEnabled&&style.strokeWidth==3);REQUIRE(added(p).raster->pixel(0,0).a==255);kind(w)->choose(2);draw(p,{30,30},{40,40});REQUIRE(!editing::decodeShapeStyle(added(p).shapeJson).strokeEnabled);}
void shape_style_loads_from_selected_layer(){MainWindow w(true);auto d=fixture();editing::ShapeStyle style{editing::ShapeKind::Rectangle,0,0,0,0};style.strokeEnabled=true;style.strokeWidth=3;d.layers.push_back(*editing::createShapeLayer({10,10,20,20},style,"Stroked"));auto&p=w.addProject(d);p.active=d.layers.back().id;p.selected={p.active};shapeTool(w);REQUIRE(w.findChild<QCheckBox*>("shapeStroke")->isChecked());REQUIRE(w.findChild<QDoubleSpinBox*>("shapeStrokeWidth")->value()==3);}
void shape_controls_edit_selected_layer(){MainWindow w(true);auto&p=w.addProject(fixture());shapeTool(w);draw(p);const auto id=p.active;const auto fill=editing::decodeShapeStyle(added(p).shapeJson);w.findChild<QCheckBox*>("shapeStroke")->setChecked(true);w.findChild<QDoubleSpinBox*>("shapeStrokeWidth")->setValue(3);const auto style=editing::decodeShapeStyle(added(p).shapeJson);REQUIRE(p.active==id&&style.strokeEnabled&&style.strokeWidth==3);REQUIRE(style.red==fill.red&&style.green==fill.green&&style.blue==fill.blue);REQUIRE(p.history.undoName()=="Shape Style");}
QAction* command(MainWindow&w,const char*id){for(auto*action:w.findChildren<QAction*>())if(action->property("commandId").toString()==id)return action;throw std::runtime_error(std::string("missing registered command ")+id);}
QAction* labeledCommand(MainWindow& w,const char* id,const QString& text){for(auto* action:w.findChildren<QAction*>())if(action->property("commandId").toString()==id&&action->text()==text)return action;throw std::runtime_error("missing labeled command");}
PaletteDialog* palettePicker(MainWindow& w){for(auto* widget:w.findChildren<QWidget*>())if(auto* dialog=dynamic_cast<PaletteDialog*>(widget);dialog&&dialog->isVisible())return dialog;throw std::runtime_error("visible palette picker");}
void chooseShapeBackground(MainWindow& w,const char* hex,bool accept){
    const auto rgb=effects_tools::PaletteColor::fromHex(hex);REQUIRE(rgb);const QColor color=QColor::fromRgbF(rgb->red,rgb->green,rgb->blue);
    bool opened=false;QString title;QTimer poll;poll.setInterval(10);QElapsedTimer elapsed;elapsed.start();
    QObject::connect(&poll,&QTimer::timeout,&w,[&]{if(auto* picker=qobject_cast<QColorDialog*>(QApplication::activeModalWidget())){opened=true;title=picker->windowTitle();picker->setCurrentColor(color);if(accept)picker->accept();else picker->reject();}else if(elapsed.elapsed()>5000){if(auto* modal=qobject_cast<QDialog*>(QApplication::activeModalWidget()))modal->reject();poll.stop();}});
    poll.start();labeledCommand(w,"palette.background","Background Color")->trigger();poll.stop();REQUIRE(opened&&title=="Background color");QApplication::processEvents();
}
std::string paletteHex(MainWindow& w,const char* id){const QString text=QString(id)=="palette.background"?"Background":"Foreground";labeledCommand(w,id,text)->trigger();QApplication::processEvents();const auto hex=palettePicker(w)->color().hex();palettePicker(w)->reject();QApplication::processEvents();return hex;}
const Layer& layerById(const EditorProject& p,const std::string& id){auto it=std::find_if(p.document->layers.begin(),p.document->layers.end(),[&](const Layer& layer){return layer.id==id;});REQUIRE(it!=p.document->layers.end());return *it;}
void shape_undo_resyncs_bar(){MainWindow w(true);auto&p=w.addProject(fixture());shapeTool(w);draw(p);auto*stroke=w.findChild<QCheckBox*>("shapeStroke");auto*width=w.findChild<QDoubleSpinBox*>("shapeStrokeWidth");stroke->setChecked(true);REQUIRE(editing::decodeShapeStyle(added(p).shapeJson).strokeEnabled);command(w,"edit.undo")->trigger();REQUIRE(!editing::decodeShapeStyle(added(p).shapeJson).strokeEnabled&&!stroke->isChecked());width->setValue(4);REQUIRE(!editing::decodeShapeStyle(added(p).shapeJson).strokeEnabled);}
void shape_smoothing_draft(){
    MainWindow w(true);
    auto d=fixture();
    auto& p=w.addProject(d);
    shapeTool(w);
    auto* smoothing=w.findChild<QCheckBox*>("shapeSmoothing");
    REQUIRE(smoothing&&!smoothing->isChecked()&&!smoothing->isHidden());
    kind(w)->choose(1);
    REQUIRE(smoothing->isHidden());
    kind(w)->choose(2);
    REQUIRE(smoothing->isHidden());
    kind(w)->choose(0);
    REQUIRE(!smoothing->isHidden());
    radius(w)->setValue(15);
    smoothing->setChecked(true);

    down(p,{10,12});
    move(p,{90,72});
    REQUIRE(p.canvas->shapeDraft()&&p.canvas->shapeDraft()->style.smoothing);
    p.canvas->pointerCancel();
    REQUIRE(!p.canvas->shapeDraft()&&p.document==d&&p.history.undoCount()==0);

    down(p,{10,12});
    move(p,{90,72});
    smoothing->setChecked(false);
    REQUIRE(p.canvas->shapeDraft()&&p.canvas->shapeDraft()->style.smoothing);
    up(p,{90,72});
    REQUIRE(!p.canvas->shapeDraft()&&p.history.undoCount()==1);
    const auto style=editing::decodeShapeStyle(added(p).shapeJson);
    REQUIRE(style.smoothing&&style.cornerRadius==15);
    auto ordinary=style;
    ordinary.smoothing=false;
    REQUIRE(added(p).raster->rgba()!=editing::shapeRaster(ordinary,80,60)->rgba());
    REQUIRE(smoothing->isChecked());

    radius(w)->setValue(20);
    emit radius(w)->editingFinished();
    auto resizedCorners=style;
    resizedCorners.cornerRadius=20;
    REQUIRE(editing::decodeShapeStyle(added(p).shapeJson)==resizedCorners&&smoothing->isChecked());
    REQUIRE(p.history.undoCount()==2&&p.history.undoName()=="Shape Style");

    down(p,{14,16});
    move(p,{86,68});
    REQUIRE(p.canvas->shapeDraft()&&!p.canvas->shapeDraft()->style.smoothing);
    p.canvas->pointerCancel();
    REQUIRE(smoothing->isChecked()&&p.history.undoCount()==2);
    draw(p,{14,16},{86,68});
    REQUIRE(!editing::decodeShapeStyle(added(p).shapeJson).smoothing&&!smoothing->isChecked());
}
void shape_smoothing_history(){
    MainWindow w(true);
    auto& p=w.addProject(fixture());
    shapeTool(w);
    radius(w)->setValue(15);
    draw(p,{10,12},{90,72});
    const auto ordinary=added(p);
    auto* smoothing=w.findChild<QCheckBox*>("shapeSmoothing");
    REQUIRE(smoothing&&!smoothing->isChecked());
    smoothing->setChecked(true);
    const auto smoothed=added(p);
    REQUIRE(p.history.undoCount()==2&&p.history.undoName()=="Shape Style");
    REQUIRE(smoothed.id==ordinary.id&&smoothed.raster->rgba()!=ordinary.raster->rgba());
    REQUIRE(editing::decodeShapeStyle(smoothed.shapeJson).smoothing);
    command(w,"edit.undo")->trigger();
    REQUIRE(added(p)==ordinary&&!smoothing->isChecked());
    command(w,"edit.redo")->trigger();
    REQUIRE(added(p)==smoothed&&smoothing->isChecked());
    kind(w)->choose(1);
    REQUIRE(smoothing->isHidden()&&!editing::decodeShapeStyle(added(p).shapeJson).smoothing);
    command(w,"edit.undo")->trigger();
    REQUIRE(added(p)==smoothed&&!smoothing->isHidden()&&smoothing->isChecked());
}
void shape_width_steps_coalesce(){MainWindow w(true);auto&p=w.addProject(fixture());shapeTool(w);draw(p);const auto base=p.history.undoCount();w.findChild<QCheckBox*>("shapeStroke")->setChecked(true);auto*width=w.findChild<QDoubleSpinBox*>("shapeStrokeWidth");const auto before=editing::decodeShapeStyle(added(p).shapeJson).strokeWidth;width->setValue(3);width->setValue(4);width->setValue(5);REQUIRE(p.history.undoCount()==base+1);emit width->editingFinished();REQUIRE(p.history.undoCount()==base+2&&p.history.undoName()=="Shape Style");REQUIRE(editing::decodeShapeStyle(added(p).shapeJson).strokeWidth==5);command(w,"edit.undo")->trigger();REQUIRE(editing::decodeShapeStyle(added(p).shapeJson).strokeWidth==before&&editing::decodeShapeStyle(added(p).shapeJson).strokeEnabled);}
void shape_line_controls(){
    MainWindow w(true);auto& p=w.addProject(fixture());shapeTool(w);kind(w)->choose(2);
    auto* style=w.findChild<QComboBox*>("shapeLineStyle");auto* cap=w.findChild<QComboBox*>("shapeLineCap");auto* width=w.findChild<QDoubleSpinBox*>("shapeLineWidth");REQUIRE(style&&cap&&width);
    style->setCurrentIndex(1);cap->setCurrentIndex(1);width->setValue(6);emit width->editingFinished();
    const auto before=*p.document;down(p,{10,30});move(p,{80,30});REQUIRE(p.canvas->shapeDraft());p.canvas->pointerCancel();REQUIRE(!p.canvas->shapeDraft()&&*p.document==before&&p.history.undoCount()==0);
    draw(p,{10,30},{80,30});const auto line=added(p);const auto settings=editing::decodeShapeStyle(line.shapeJson);REQUIRE(settings.lineStyle==editing::ShapeLineStyle::Dashed&&settings.lineCap==editing::ShapeLineCap::Square&&settings.lineWidth==6);REQUIRE(p.history.undoCount()==1);
    style->setCurrentIndex(5);REQUIRE(editing::decodeShapeStyle(added(p).shapeJson).lineStyle==editing::ShapeLineStyle::Dotted&&p.history.undoCount()==2);command(w,"edit.undo")->trigger();REQUIRE(added(p)==line&&style->currentIndex()==1);command(w,"edit.redo")->trigger();REQUIRE(style->currentIndex()==5);
    const auto prior=added(p);const auto priorStyle=editing::decodeShapeStyle(prior.shapeJson);auto endpoint=[](const Layer& l,const editing::ShapeStyle& s){return l.transform.fromUnit({s.startX*(l.raster->width-1)/l.raster->width,s.startY*(l.raster->height-1)/l.raster->height});};const auto start=endpoint(prior,priorStyle);width->setValue(30);emit width->editingFinished();const auto changed=added(p);const auto changedStyle=editing::decodeShapeStyle(changed.shapeJson);const auto moved=endpoint(changed,changedStyle);REQUIRE(std::hypot(start.x-moved.x,start.y-moved.y)<.001);command(w,"edit.undo")->trigger();REQUIRE(added(p)==prior);
}
// The drawing bar shows one group of options per tool; a widget counts as hidden when its group is.
bool hiddenInBar(MainWindow& w,const char* name){auto* bar=w.findChild<QToolBar*>("drawingOptions");auto* widget=w.findChild<QWidget*>(name);REQUIRE(bar&&widget);return !widget->isVisibleTo(bar);}
void shape_bar_limits_selected_shape(){
    MainWindow w(true);auto& p=w.addProject(fixture());shapeTool(w);draw(p,{10,12},{90,72});
    const auto hidden=[&](const char* name){return hiddenInBar(w,name);};
    for(const char* name:{"gradientTitle","gradientShape","gradientStyle","gradientReverse","gradientOpacityGroup","gradientOpacity","gradientOpacitySlider","gradientCancelButton","gradientApplyButton"})REQUIRE(hidden(name));
    for(const char* name:{"shapeLineWidth","shapeLineStyle","shapeLineCap","cropRatioChoice","shapeStrokeWidth","shapeStrokeColor"})REQUIRE(hidden(name));
    for(const char* name:{"shapeTitle","shapeKind","shapeFillColor","shapeCornerRadius","shapeSmoothing","shapeStroke"})REQUIRE(!hidden(name));
    kind(w)->choose(1);
    REQUIRE(hidden("shapeCornerRadius")&&hidden("shapeSmoothing"));
    REQUIRE(!hidden("shapeStroke")&&hidden("shapeLineWidth"));
    kind(w)->choose(2);
    REQUIRE(!hidden("shapeLineWidth")&&!hidden("shapeLineStyle")&&!hidden("shapeLineCap"));
    REQUIRE(hidden("shapeStroke")&&hidden("shapeCornerRadius"));
    const auto shapeId=p.active;command(w,"tool.move")->trigger();
    // The gradient options belong to the Gradient tool alone; a selected shape keeps its own options.
    REQUIRE(hidden("gradientShape")&&!hidden("shapeKind"));
    p.active="top";p.selected={"top"};shapeTool(w);
    REQUIRE(hidden("gradientShape")&&!hidden("shapeKind"));
    command(w,"tool.gradient")->trigger();
    REQUIRE(!hidden("gradientShape")&&!hidden("gradientTitle")&&hidden("shapeKind"));
    p.active=shapeId;p.selected={shapeId};command(w,"tool.gradient")->trigger();
    REQUIRE(!hidden("gradientShape")&&hidden("shapeKind"));
}
// The Fill well opens the color dialog; with a shape selected it repaints it in one undo step, with none it sets the next shape's color.
void fill_well_opens_color_dialog(){
    MainWindow w(true);auto& p=w.addProject(fixture());shapeTool(w);
    const auto pick=[&](QColor color){auto* well=w.findChild<QAbstractButton*>("shapeFillColor");REQUIRE(well&&well->isEnabled());bool opened=false;
        QTimer::singleShot(0,&w,[&]{if(auto* dialog=qobject_cast<QColorDialog*>(QApplication::activeModalWidget())){opened=true;dialog->setCurrentColor(color);dialog->accept();}});
        well->click();QApplication::processEvents();REQUIRE(opened);};
    pick(QColor(10,200,30));
    REQUIRE(dynamic_cast<ui::ColorWell*>(w.findChild<QAbstractButton*>("shapeFillColor"))->color()==QColor(10,200,30));
    draw(p,{10,12},{90,72});const auto base=p.history.undoCount();
    pick(QColor(255,0,0));
    REQUIRE(editing::decodeShapeStyle(added(p).shapeJson).red==1&&p.history.undoCount()==base+1);
    kind(w)->choose(2);draw(p,{10,12},{90,72});const auto lineBase=p.history.undoCount();
    pick(QColor(0,0,255));
    REQUIRE(editing::decodeShapeStyle(added(p).shapeJson).blue==1&&p.history.undoCount()==lineBase+1);
}
void shape_background_fill_selected(){
    MainWindow w(true);auto& p=w.addProject(fixture());shapeTool(w);draw(p,{10,12},{90,72});const auto id=p.active;
    auto* stroke=w.findChild<QCheckBox*>("shapeStroke");auto* width=w.findChild<QDoubleSpinBox*>("shapeStrokeWidth");REQUIRE(stroke&&width);
    stroke->setChecked(true);width->setValue(3);emit width->editingFinished();
    const auto before=added(p);const auto beforeStyle=editing::decodeShapeStyle(before.shapeJson);
    REQUIRE(beforeStyle.strokeEnabled&&beforeStyle.strokeWidth==3&&before.raster->width==80&&before.raster->height==60);
    REQUIRE(before.raster->pixel(40,30)==Pixel{0,0,0,255}&&before.raster->pixel(0,0)==Pixel{0,0,0,255});
    const auto base=p.history.undoCount();
    chooseShapeBackground(w,"FF0000",true);
    const auto painted=added(p);const auto style=editing::decodeShapeStyle(painted.shapeJson);
    REQUIRE(painted.id==id&&painted.transform==before.transform);
    REQUIRE(style.red==1&&style.green==0&&style.blue==0);
    REQUIRE(style.strokeEnabled&&style.strokeWidth==beforeStyle.strokeWidth&&style.strokeRed==beforeStyle.strokeRed&&style.strokeGreen==beforeStyle.strokeGreen&&style.strokeBlue==beforeStyle.strokeBlue);
    REQUIRE(style.kind==beforeStyle.kind&&style.cornerRadius==beforeStyle.cornerRadius&&style.smoothing==beforeStyle.smoothing);
    REQUIRE(painted.raster->pixel(40,30)==Pixel{255,0,0,255}&&painted.raster->pixel(0,0)==Pixel{0,0,0,255});
    REQUIRE(p.history.undoCount()==base+1&&p.history.undoName()=="Shape Style");
    REQUIRE(paletteHex(w,"palette.foreground")=="000000"&&paletteHex(w,"palette.background")=="FF0000");
    command(w,"edit.undo")->trigger();REQUIRE(added(p)==before&&paletteHex(w,"palette.background")=="FF0000");
    command(w,"edit.redo")->trigger();REQUIRE(added(p)==painted);
    draw(p,{14,16},{40,40});
    REQUIRE(editing::decodeShapeStyle(added(p).shapeJson).red==0&&editing::decodeShapeStyle(added(p).shapeJson).green==0&&editing::decodeShapeStyle(added(p).shapeJson).blue==0);
    REQUIRE(editing::decodeShapeStyle(layerById(p,id).shapeJson).red==1&&editing::decodeShapeStyle(layerById(p,id).shapeJson).green==0&&editing::decodeShapeStyle(layerById(p,id).shapeJson).blue==0);
}
void shape_background_fill_ellipse_and_line(){
    {MainWindow w(true);auto& p=w.addProject(fixture());shapeTool(w);kind(w)->choose(1);draw(p,{10,12},{90,72});
        const auto before=added(p);const auto beforeStyle=editing::decodeShapeStyle(before.shapeJson);
        REQUIRE(beforeStyle.kind==editing::ShapeKind::Ellipse&&beforeStyle.cornerRadius==0);
        chooseShapeBackground(w,"00FF00",true);
        const auto style=editing::decodeShapeStyle(added(p).shapeJson);
        REQUIRE(style.kind==editing::ShapeKind::Ellipse&&style.cornerRadius==0&&style.red==0&&style.green==1&&style.blue==0);
        REQUIRE(added(p).transform==before.transform&&added(p).raster->pixel(40,30)==Pixel{0,255,0,255}&&p.history.undoName()=="Shape Style");}
    {MainWindow w(true);auto& p=w.addProject(fixture());shapeTool(w);kind(w)->choose(2);draw(p,{10,30},{80,30});
        const auto before=added(p);const auto beforeStyle=editing::decodeShapeStyle(before.shapeJson);int px=-1,py=-1;
        for(int y=0;y<before.raster->height&&px<0;++y)for(int x=0;x<before.raster->width;++x)if(before.raster->pixel(x,y).a==255){px=x;py=y;break;}
        REQUIRE(px>=0&&before.raster->pixel(px,py)==Pixel{0,0,0,255});
        chooseShapeBackground(w,"0000FF",true);
        const auto style=editing::decodeShapeStyle(added(p).shapeJson);
        REQUIRE(style.kind==editing::ShapeKind::Line&&style.lineWidth==beforeStyle.lineWidth&&style.lineStyle==beforeStyle.lineStyle&&style.lineCap==beforeStyle.lineCap);
        REQUIRE(style.startX==beforeStyle.startX&&style.startY==beforeStyle.startY&&style.endX==beforeStyle.endX&&style.endY==beforeStyle.endY);
        REQUIRE(style.red==0&&style.green==0&&style.blue==1&&added(p).transform==before.transform&&added(p).raster->pixel(px,py)==Pixel{0,0,255,255});}
}
void shape_background_fill_cancel(){MainWindow w(true);auto& p=w.addProject(fixture());shapeTool(w);draw(p);const auto before=*p.document;const auto count=p.history.undoCount();chooseShapeBackground(w,"FF0000",false);REQUIRE(*p.document==before&&p.history.undoCount()==count&&paletteHex(w,"palette.background")=="FFFFFF");}
void shape_background_fill_same_color(){MainWindow w(true);auto& p=w.addProject(fixture());shapeTool(w);draw(p);const auto before=added(p);const auto count=p.history.undoCount();chooseShapeBackground(w,"000000",true);REQUIRE(added(p)==before&&p.history.undoCount()==count&&paletteHex(w,"palette.background")=="000000");}
void shape_background_fill_other_tool(){MainWindow w(true);auto& p=w.addProject(fixture());shapeTool(w);draw(p);const auto before=added(p);const auto count=p.history.undoCount();command(w,"tool.move")->trigger();chooseShapeBackground(w,"FF0000",true);REQUIRE(added(p)==before&&p.history.undoCount()==count&&p.history.undoName()=="Rectangle"&&paletteHex(w,"palette.background")=="FF0000");}
void shape_background_fill_pixel_layer(){MainWindow w(true);auto& p=w.addProject(fixture());shapeTool(w);const auto before=*p.document;chooseShapeBackground(w,"FF0000",true);REQUIRE(*p.document==before&&p.history.undoCount()==0&&paletteHex(w,"palette.background")=="FF0000");}
void shape_background_fill_draft(){
    MainWindow w(true);auto& p=w.addProject(fixture());shapeTool(w);draw(p);const auto before=*p.document;const auto count=p.history.undoCount();
    down(p,{30,30});move(p,{70,60});REQUIRE(p.canvas->shapeDraft());
    chooseShapeBackground(w,"FF0000",true);
    REQUIRE(*p.document==before&&p.history.undoCount()==count&&p.canvas->shapeDraft());
    p.canvas->pointerCancel();REQUIRE(!p.canvas->shapeDraft()&&*p.document==before&&p.history.undoCount()==count&&paletteHex(w,"palette.background")=="FF0000");
}
// Every tool bar names its tool and shows only the controls of the active tool.
void tool_bars_show_their_own_controls(){
    MainWindow w(true);auto& mainProject=w.addProject(fixture());w.resize(3200,900);w.show();QApplication::processEvents();
    const auto tool=[&](const char* id){command(w,id)->trigger();QApplication::processEvents();};
    const auto shown=[&](const char* barName,const char* child){auto* bar=w.findChild<QToolBar*>(barName);auto* widget=w.findChild<QWidget*>(child);REQUIRE(bar&&widget);return !bar->isHidden()&&widget->isVisibleTo(bar);};
    const auto title=[&](const char* name){auto* label=w.findChild<QLabel*>(name);REQUIRE(label&&label->font().bold());return label->text();};
    tool("tool.heal");
    REQUIRE(shown("retouchOptions","retouchHealingMode")&&!shown("retouchOptions","retouchSmearMode")&&!shown("retouchOptions","retouchAligned")&&!shown("retouchOptions","retouchCloneHint")&&title("retouchFamily")=="Spot Healing");
    tool("tool.clone");
    REQUIRE(shown("retouchOptions","retouchAligned")&&shown("retouchOptions","retouchAllLayers")&&shown("retouchOptions","retouchCloneHint")&&!shown("retouchOptions","retouchHealingMode")&&title("retouchFamily")=="Clone Stamp");
    tool("tool.retouch");
    REQUIRE(shown("retouchOptions","retouchSmearMode")&&!shown("retouchOptions","retouchAligned")&&!shown("retouchOptions","retouchCloneHint")&&title("retouchFamily")=="Smear"&&w.findChild<QLabel*>("retouchOpacityLabel")->text()=="Strength");
    tool("tool.gradient");
    REQUIRE(shown("drawingOptions","gradientShape")&&shown("drawingOptions","gradientOpacityGroup")&&!shown("drawingOptions","shapeKind")&&title("gradientTitle")=="Gradient");
    REQUIRE(shown("drawingOptions","gradientSwatch"));
    // The Opacity caption sits right against its slider: the group does not stretch.
    {auto* group=w.findChild<QWidget*>("gradientOpacityGroup");auto* caption=group->findChild<QLabel*>();auto* track=w.findChild<QSlider*>("gradientOpacitySlider");REQUIRE(caption&&track);const int gap=track->mapTo(group,QPoint(0,0)).x()-(caption->mapTo(group,QPoint(0,0)).x()+caption->width());REQUIRE(gap>=0&&gap<=10);}
    auto* slider=w.findChild<QSlider*>("gradientOpacitySlider");auto* number=w.findChild<QDoubleSpinBox*>("gradientOpacity");REQUIRE(slider&&number);
    slider->setValue(40);REQUIRE(number->value()==40);number->setValue(65);REQUIRE(slider->value()==65);
    REQUIRE(!command(w,"gradient.apply")->isEnabled());
    REQUIRE(!w.findChild<QPushButton*>("gradientApplyButton")->isEnabled());
    tool("tool.shape");
    REQUIRE(shown("drawingOptions","shapeKind")&&!shown("drawingOptions","gradientShape")&&title("shapeTitle")=="Shape");
    // Rectangle order: Fill, Stroke, Width, Radius, Smooth Corners.
    {auto* bar=w.findChild<QToolBar*>("drawingOptions");const auto x=[&](const char* name){auto* widget=w.findChild<QWidget*>(name);REQUIRE(widget);return widget->mapTo(bar,QPoint(0,0)).x();};
     w.findChild<QCheckBox*>("shapeStroke")->setChecked(true);QApplication::processEvents();
     REQUIRE(x("shapeFillColor")<x("shapeStroke")&&x("shapeStroke")<x("shapeStrokeColor")&&x("shapeStrokeColor")<x("shapeStrokeWidth")&&x("shapeStrokeWidth")<x("shapeCornerRadius")&&x("shapeCornerRadius")<x("shapeSmoothing"));
     REQUIRE(!w.findChild<QCheckBox*>("shapeSmoothing")->isEnabled());
     w.findChild<QDoubleSpinBox*>("shapeCornerRadius")->setValue(10);REQUIRE(w.findChild<QSlider*>("shapeCornerRadiusSlider")->value()==10&&w.findChild<QCheckBox*>("shapeSmoothing")->isEnabled());}
    tool("tool.type");
    REQUIRE(!w.findChild<QToolBar*>("typeOptions")->isHidden());
    REQUIRE(w.findChild<QToolButton*>("textFont")->isVisibleTo(w.findChild<QToolBar*>("typeOptions")));
    REQUIRE(title("typeTitle")=="Type");
    REQUIRE(!shown("typeOptions","textDoneButton"));
    // Without a text being edited, the color well still opens the color dialog and sets the color of the next text.
    {auto* well=dynamic_cast<ui::ColorWell*>(w.findChild<QAbstractButton*>("textColor"));REQUIRE(well&&well->isEnabled());bool opened=false;
     QTimer::singleShot(0,&w,[&]{if(auto* dialog=qobject_cast<QColorDialog*>(QApplication::activeModalWidget())){opened=true;dialog->setCurrentColor(QColor(10,200,30));dialog->accept();}});
     well->click();QApplication::processEvents();REQUIRE(opened&&well->color()==QColor(10,200,30));}
    // The font and style selectors are capsules like the other drop-downs, and the alignment is drawn with icons.
    REQUIRE(dynamic_cast<ui::CapsuleButton*>(w.findChild<QToolButton*>("textFont"))&&dynamic_cast<ui::CapsuleDropdown*>(w.findChild<QComboBox*>("textStyle")));
    REQUIRE(!w.findChild<QToolButton*>("textAlignLeft")->icon().isNull());
    tool("tool.eyedropper");
    REQUIRE(shown("eyedropperOptions","eyedropperSampleRing")&&title("eyedropperTitle")=="Eyedropper"&&w.findChild<QCheckBox*>("eyedropperSampleRing")->isChecked());
    tool("tool.zoom");
    // The status bar shows the view and canvas on the left and a hint for the tool on the right.
    {auto text=[&](const char* name){auto* label=w.findChild<QLabel*>(name);REQUIRE(label);return label->text();};
     REQUIRE(text("statusZoom").endsWith(" %")&&text("statusDimensions").contains(QString::fromUtf8(" × "))&&text("statusProfile")=="sRGB · Transparent");
     REQUIRE(text("statusHint").startsWith("Click to zoom in"));
     tool("tool.marquee");REQUIRE(text("statusHint").startsWith("Drag a rectangle")&&text("statusHint").contains("Alt subtract"));
     tool("tool.zoom");}
    REQUIRE(shown("zoomOptions","zoomPercent")&&title("zoomTitle")=="Zoom"&&w.findChild<QToolBar*>("eyedropperOptions")->isHidden());
    {auto* field=w.findChild<QDoubleSpinBox*>("zoomPercent");auto* canvas=mainProject.canvas;REQUIRE(field&&canvas);
     field->setValue(200);emit field->editingFinished();QApplication::processEvents();REQUIRE(std::abs(canvas->zoom-2)<1e-9);
     canvas->zoomAt(0.5,canvas->contentRect().center());QApplication::processEvents();REQUIRE(field->value()==50);}
    tool("tool.move");
    REQUIRE(w.findChild<QToolBar*>("eyedropperOptions")->isHidden()&&w.findChild<QToolBar*>("zoomOptions")->isHidden()&&w.findChild<QToolBar*>("typeOptions")->isHidden());
}
// Dragging a caption scrubs the value of its field; the helper sends press, move and release the way the mouse does.
void sendMouse(QWidget* label,QEvent::Type type,double globalX,Qt::KeyboardModifiers modifiers={}){
    const QPointF local(label->width()/2.,label->height()/2.);const QPointF global=label->mapToGlobal(local)+QPointF(globalX,0);
    QMouseEvent event(type,local,global,Qt::LeftButton,type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton,modifiers);QApplication::sendEvent(label,&event);
}
QLabel* captionOf(MainWindow& w,const char* group){auto* host=w.findChild<QWidget*>(group);REQUIRE(host);auto* label=host->findChild<QLabel*>();REQUIRE(label);return label;}
void scrub_drag_is_one_undo_step(){
    MainWindow w(true);auto& p=w.addProject(fixture());w.resize(3200,900);w.show();shapeTool(w);draw(p,{10,12},{90,72});
    w.findChild<QCheckBox*>("shapeStroke")->setChecked(true);QApplication::processEvents();
    auto* width=w.findChild<QDoubleSpinBox*>("shapeStrokeWidth");auto* label=captionOf(w,"shapeStrokeGroup");REQUIRE(width&&label->cursor().shape()==Qt::SplitHCursor);
    const double start=width->value();const auto base=p.history.undoCount();
    sendMouse(label,QEvent::MouseButtonPress,0);sendMouse(label,QEvent::MouseMove,10);
    REQUIRE(std::abs(width->value()-(start+10))<1e-9);
    sendMouse(label,QEvent::MouseMove,20,Qt::ShiftModifier);sendMouse(label,QEvent::MouseMove,30,Qt::ShiftModifier);REQUIRE(std::abs(width->value()-(start+11))<1e-9);
    sendMouse(label,QEvent::MouseMove,30);sendMouse(label,QEvent::MouseMove,20);REQUIRE(std::abs(width->value()-(start+1))<1e-9);
    sendMouse(label,QEvent::MouseButtonRelease,20);
    REQUIRE(editing::decodeShapeStyle(added(p).shapeJson).strokeWidth==width->value());
    REQUIRE(p.history.undoCount()==base+1&&p.history.undoName()=="Shape Style");
    command(w,"edit.undo")->trigger();REQUIRE(p.history.undoCount()==base&&std::abs(width->value()-start)<1e-9);
}
void scrub_escape_and_click(){
    MainWindow w(true);auto& p=w.addProject(fixture());w.resize(3200,900);w.show();shapeTool(w);draw(p,{10,12},{90,72});
    w.findChild<QCheckBox*>("shapeStroke")->setChecked(true);QApplication::processEvents();
    auto* width=w.findChild<QDoubleSpinBox*>("shapeStrokeWidth");auto* label=captionOf(w,"shapeStrokeGroup");
    const double start=width->value();const auto base=p.history.undoCount();const auto before=added(p);
    sendMouse(label,QEvent::MouseButtonPress,0);sendMouse(label,QEvent::MouseMove,25);REQUIRE(width->value()!=start);
    QTest::keyClick(&w,Qt::Key_Escape);QApplication::processEvents();
    sendMouse(label,QEvent::MouseButtonRelease,25);
    REQUIRE(width->value()==start&&added(p)==before&&p.history.undoCount()==base);
    // A click without movement leaves the value alone and puts the cursor in the field.
    sendMouse(label,QEvent::MouseButtonPress,0);sendMouse(label,QEvent::MouseButtonRelease,0);QApplication::processEvents();
    REQUIRE(width->value()==start&&p.history.undoCount()==base);
}
void scrub_zoom_title(){
    MainWindow w(true);auto& p=w.addProject(fixture());w.resize(3200,900);w.show();command(w,"tool.zoom")->trigger();QApplication::processEvents();
    auto* title=w.findChild<QLabel*>("zoomTitle");auto* field=w.findChild<QDoubleSpinBox*>("zoomPercent");REQUIRE(title&&field&&title->cursor().shape()==Qt::SplitHCursor);
    p.canvas->zoomAt(1,p.canvas->contentRect().center());QApplication::processEvents();REQUIRE(field->value()==100);
    sendMouse(title,QEvent::MouseButtonPress,0);sendMouse(title,QEvent::MouseMove,50);
    REQUIRE(field->value()==150&&std::abs(p.canvas->zoom-1.5)<1e-9);
    sendMouse(title,QEvent::MouseMove,50,Qt::ControlModifier);sendMouse(title,QEvent::MouseMove,60,Qt::ControlModifier);
    REQUIRE(field->value()==250&&std::abs(p.canvas->zoom-2.5)<1e-9);
    sendMouse(title,QEvent::MouseButtonRelease,60);REQUIRE(std::abs(p.canvas->zoom-2.5)<1e-9);
    // The Brush size and the Tracking captions scrub their fields too.
    for(const char* name:{"brushSize","textTracking"}){auto* field=w.findChild<QWidget*>(name);if(!field){auto any=w.findChildren<QDoubleSpinBox*>();for(auto* box:any)if(box->accessibleName()==(QString(name)=="brushSize"?"Brush diameter":"Tracking"))field=box;}
        REQUIRE(field);auto* label=field->parentWidget()->findChild<QLabel*>();REQUIRE(label&&label->cursor().shape()==Qt::SplitHCursor);}
}
// The Magic Wand sample, Crop ratio and Gradient color lists and the Type font and style lists open as rounded glass panels:
// painted inside, transparent outside the rounded corners.
void options_lists_are_rounded_glass(){
    MainWindow w(true);w.addProject(fixture());w.resize(1600,900);w.show();QApplication::processEvents();
    const auto tool=[&](const char* id){command(w,id)->trigger();QApplication::processEvents();};
    const auto glass=[](QWidget* popup){
        REQUIRE(popup&&popup->isVisible()&&popup->testAttribute(Qt::WA_TranslucentBackground));
        const QImage panel=popup->grab().toImage();const double scale=panel.devicePixelRatio();
        const auto alpha=[&](int x,int y){return panel.pixelColor(int(x*scale),int(y*scale)).alpha();};
        return alpha(2,2)==0&&alpha(popup->width()-3,popup->height()-3)==0&&alpha(popup->width()/2,popup->height()-4)>0;
    };
    for(const auto& [id,name]:{std::pair{"tool.wand","wandSampleSize"},{"tool.crop","cropRatioChoice"},{"tool.gradient","gradientStyle"}}){
        tool(id);auto* box=w.findChild<QComboBox*>(name);REQUIRE(box);
        box->showPopup();QApplication::processEvents();
        REQUIRE(glass(box->view()->window()));
        box->hidePopup();QApplication::processEvents();
    }
    tool("tool.type");
    auto* fonts=w.findChild<QToolButton*>("textFont");REQUIRE(fonts&&fonts->isEnabled());
    fonts->click();QApplication::processEvents();
    auto* popup=w.findChild<QWidget*>("textFontPopup");
    REQUIRE(glass(popup));
    popup->hide();QApplication::processEvents();
    auto* styles=w.findChild<QComboBox*>("textStyle");REQUIRE(styles&&styles->count()>0);
    // The style list opens without the roll animation, which would show an opaque snapshot, and leaves the setting as it was.
    QApplication::setEffectEnabled(Qt::UI_General,true);QApplication::setEffectEnabled(Qt::UI_AnimateCombo,true);
    REQUIRE(QApplication::isEffectEnabled(Qt::UI_AnimateCombo));
    styles->showPopup();QApplication::processEvents();
    REQUIRE(QApplication::isEffectEnabled(Qt::UI_AnimateCombo));
    REQUIRE(glass(styles->view()->window()));
    // Every style name fits beside the check mark, although the list of an editable box starts as wide as the box.
    const int room=styles->view()->viewport()->width()-ui::layer_panel::blendMenuTextInset-8-2*8;
    for(int index=0;index<styles->count();++index)REQUIRE(styles->view()->fontMetrics().horizontalAdvance(styles->itemText(index))<=room);
    styles->hidePopup();QApplication::processEvents();
    // A click on the style field opens its list, and the list stays open once the button is released.
    // Through the window, as a real click is: the release then goes to the open list.
    QWindow* window=w.windowHandle();REQUIRE(window);
    const QPoint field=window->mapFromGlobal(styles->lineEdit()->mapToGlobal(styles->lineEdit()->rect().center()));
    QTest::mousePress(window,Qt::LeftButton,{},field);QApplication::processEvents();
    REQUIRE(styles->view()->isVisible());
    QTest::mouseRelease(window,Qt::LeftButton,{},field);QApplication::processEvents();
    REQUIRE(styles->view()->isVisible());
    styles->hidePopup();QApplication::processEvents();
}
int main(int argc,char**argv){QApplication app(argc,argv);std::map<std::string,void(*)()>tests{{"scrub_drag_is_one_undo_step",scrub_drag_is_one_undo_step},{"scrub_escape_and_click",scrub_escape_and_click},{"scrub_zoom_title",scrub_zoom_title},{"tool_bars_show_their_own_controls",tool_bars_show_their_own_controls},{"options_lists_are_rounded_glass",options_lists_are_rounded_glass},{"fill_well_opens_color_dialog",fill_well_opens_color_dialog},{"shape_controls_edit_selected_layer",shape_controls_edit_selected_layer},{"shape_style_loads_from_selected_layer",shape_style_loads_from_selected_layer},{"shape_stroke_draw",shape_stroke_draw},{"shape_line_controls",shape_line_controls},{"shape_insert_sibling",shape_insert_sibling},{"shape_insert_folder",shape_insert_folder},{"shape_insert_nested_child",shape_insert_nested_child},{"shape_fractional_anchor",shape_fractional_anchor},{"shape_center_square",shape_center_square},{"shape_kind_change_cancels",shape_kind_change_cancels},{"shape_radius_limit",shape_radius_limit},{"shape_radius_frozen",shape_radius_frozen},{"shape_draft_immutable",shape_draft_immutable},{"shape_click_noop",shape_click_noop},{"shape_history_selection",shape_history_selection},{"shape_undo_resyncs_bar",shape_undo_resyncs_bar},{"shape_width_steps_coalesce",shape_width_steps_coalesce},{"shape_smoothing_draft",shape_smoothing_draft},{"shape_smoothing_history",shape_smoothing_history},{"shape_bar_limits_selected_shape",shape_bar_limits_selected_shape},{"shape_background_fill_selected",shape_background_fill_selected},{"shape_background_fill_ellipse_and_line",shape_background_fill_ellipse_and_line},{"shape_background_fill_cancel",shape_background_fill_cancel},{"shape_background_fill_same_color",shape_background_fill_same_color},{"shape_background_fill_other_tool",shape_background_fill_other_tool},{"shape_background_fill_pixel_layer",shape_background_fill_pixel_layer},{"shape_background_fill_draft",shape_background_fill_draft}};int failed=0,passed=0;for(const auto&[name,test]:tests){if(argc>1&&argv[1]!=name)continue;try{test();++passed;std::cout<<"PASS "<<name<<'\n';}catch(const std::exception&e){++failed;std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n';}}if(argc>1&&!tests.contains(argv[1]))return 2;std::cout<<"RESULT passed="<<passed<<" failed="<<failed<<'\n';return failed?1:0;}
