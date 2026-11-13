#include "ui/MainWindow.h"
#include "ui/PaletteDialog.h"
#include <QApplication>
#include <QColorDialog>
#include <QElapsedTimer>
#include <QPushButton>
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
QComboBox* kind(MainWindow&w){auto* value=w.findChild<QComboBox*>("shapeKind");REQUIRE(value);return value;}
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
void shape_kind_change_cancels(){MainWindow w(true);auto d=fixture();auto&p=w.addProject(d);shapeTool(w);down(p,{10,12});move(p,{20,24});kind(w)->setCurrentIndex(1);up(p,{20,24});REQUIRE(p.document==d&&p.history.undoCount()==0);draw(p);REQUIRE(editing::decodeShapeStyle(added(p).shapeJson).kind==editing::ShapeKind::Ellipse);}
void shape_radius_limit(){MainWindow w(true);auto&p=w.addProject(fixture());shapeTool(w);REQUIRE(radius(w)->minimum()==0&&radius(w)->maximum()==5000);radius(w)->setValue(9000);REQUIRE(radius(w)->value()==5000);draw(p);REQUIRE(editing::decodeShapeStyle(added(p).shapeJson).cornerRadius==5000);}
void shape_radius_frozen(){MainWindow w(true);auto&p=w.addProject(fixture());shapeTool(w);radius(w)->setValue(3);down(p,{10,12});move(p,{20,24});radius(w)->setValue(8);up(p,{20,24});REQUIRE(editing::decodeShapeStyle(added(p).shapeJson).cornerRadius==3);REQUIRE(radius(w)->value()==8);}
void shape_draft_immutable(){MainWindow w(true);auto d=fixture();auto&p=w.addProject(d);const auto active=p.active;shapeTool(w);down(p,{10,12});move(p,{20,24});REQUIRE(p.document==d&&p.active==active&&p.history.undoCount()==0);p.canvas->pointerCancel();REQUIRE(p.document==d&&p.active==active&&p.history.undoCount()==0);}
void shape_click_noop(){MainWindow w(true);auto d=fixture();auto&p=w.addProject(d);shapeTool(w);down(p,{10.6,12.4});up(p,{10.6,12.4});REQUIRE(p.document==d&&p.history.undoCount()==0);}
void shape_history_selection(){MainWindow w(true);auto d=fixture();d.selection=editing::rasterSelection(editing::SelectionOutline::rectangle({1,2,3,4},false),100,80);d.layers.back().mask=Mask{std::make_shared<GrayRaster>(GrayRaster{1,1,{255}})};auto&p=w.addProject(d);p.maskSelected=true;shapeTool(w);draw(p);REQUIRE(p.document->selection==d.selection&&!p.maskSelected);REQUIRE(p.history.undoCount()==1&&p.history.undoName()=="Rectangle");auto snapshot=p.history.undo();REQUIRE(snapshot&&snapshot->document==d);}
void shape_stroke_draw(){MainWindow w(true);auto&p=w.addProject(fixture());shapeTool(w);auto*checkbox=w.findChild<QCheckBox*>("shapeStroke");REQUIRE(checkbox);checkbox->setChecked(true);w.findChild<QDoubleSpinBox*>("shapeStrokeWidth")->setValue(3);draw(p);const auto style=editing::decodeShapeStyle(added(p).shapeJson);REQUIRE(style.strokeEnabled&&style.strokeWidth==3);REQUIRE(added(p).raster->pixel(0,0).a==255);kind(w)->setCurrentIndex(2);draw(p,{30,30},{40,40});REQUIRE(!editing::decodeShapeStyle(added(p).shapeJson).strokeEnabled);}
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
    kind(w)->setCurrentIndex(1);
    REQUIRE(smoothing->isHidden());
    kind(w)->setCurrentIndex(2);
    REQUIRE(smoothing->isHidden());
    kind(w)->setCurrentIndex(0);
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
    kind(w)->setCurrentIndex(1);
    REQUIRE(smoothing->isHidden()&&!editing::decodeShapeStyle(added(p).shapeJson).smoothing);
    command(w,"edit.undo")->trigger();
    REQUIRE(added(p)==smoothed&&!smoothing->isHidden()&&smoothing->isChecked());
}
void shape_width_steps_coalesce(){MainWindow w(true);auto&p=w.addProject(fixture());shapeTool(w);draw(p);const auto base=p.history.undoCount();w.findChild<QCheckBox*>("shapeStroke")->setChecked(true);auto*width=w.findChild<QDoubleSpinBox*>("shapeStrokeWidth");const auto before=editing::decodeShapeStyle(added(p).shapeJson).strokeWidth;width->setValue(3);width->setValue(4);width->setValue(5);REQUIRE(p.history.undoCount()==base+1);emit width->editingFinished();REQUIRE(p.history.undoCount()==base+2&&p.history.undoName()=="Shape Style");REQUIRE(editing::decodeShapeStyle(added(p).shapeJson).strokeWidth==5);command(w,"edit.undo")->trigger();REQUIRE(editing::decodeShapeStyle(added(p).shapeJson).strokeWidth==before&&editing::decodeShapeStyle(added(p).shapeJson).strokeEnabled);}
void shape_line_controls(){
    MainWindow w(true);auto& p=w.addProject(fixture());shapeTool(w);kind(w)->setCurrentIndex(2);
    auto* style=w.findChild<QComboBox*>("shapeLineStyle");auto* cap=w.findChild<QComboBox*>("shapeLineCap");auto* width=w.findChild<QDoubleSpinBox*>("shapeLineWidth");REQUIRE(style&&cap&&width);
    style->setCurrentIndex(1);cap->setCurrentIndex(1);width->setValue(6);emit width->editingFinished();
    const auto before=*p.document;down(p,{10,30});move(p,{80,30});REQUIRE(p.canvas->shapeDraft());p.canvas->pointerCancel();REQUIRE(!p.canvas->shapeDraft()&&*p.document==before&&p.history.undoCount()==0);
    draw(p,{10,30},{80,30});const auto line=added(p);const auto settings=editing::decodeShapeStyle(line.shapeJson);REQUIRE(settings.lineStyle==editing::ShapeLineStyle::Dashed&&settings.lineCap==editing::ShapeLineCap::Square&&settings.lineWidth==6);REQUIRE(p.history.undoCount()==1);
    style->setCurrentIndex(5);REQUIRE(editing::decodeShapeStyle(added(p).shapeJson).lineStyle==editing::ShapeLineStyle::Dotted&&p.history.undoCount()==2);command(w,"edit.undo")->trigger();REQUIRE(added(p)==line&&style->currentIndex()==1);command(w,"edit.redo")->trigger();REQUIRE(style->currentIndex()==5);
    const auto prior=added(p);const auto priorStyle=editing::decodeShapeStyle(prior.shapeJson);auto endpoint=[](const Layer& l,const editing::ShapeStyle& s){return l.transform.fromUnit({s.startX*(l.raster->width-1)/l.raster->width,s.startY*(l.raster->height-1)/l.raster->height});};const auto start=endpoint(prior,priorStyle);width->setValue(30);emit width->editingFinished();const auto changed=added(p);const auto changedStyle=editing::decodeShapeStyle(changed.shapeJson);const auto moved=endpoint(changed,changedStyle);REQUIRE(std::hypot(start.x-moved.x,start.y-moved.y)<.001);command(w,"edit.undo")->trigger();REQUIRE(added(p)==prior);
}
template<class T>
QAction* barControl(MainWindow& w,T* widget){auto* bar=w.findChild<QToolBar*>("drawingOptions");REQUIRE(bar&&widget);for(auto* action:bar->actions()){if(bar->widgetForAction(action)==widget)return action;if(auto* hosted=qobject_cast<QWidgetAction*>(action);hosted&&hosted->defaultWidget()==widget)return action;}throw std::runtime_error("toolbar control absent");}
bool barCommandVisible(MainWindow& w,const char* id){auto* bar=w.findChild<QToolBar*>("drawingOptions");REQUIRE(bar);for(auto* action:bar->actions())if(action->property("commandId").toString()==id)return action->isVisible();throw std::runtime_error(std::string("toolbar command absent ")+id);}
void shape_bar_limits_selected_shape(){
    MainWindow w(true);auto& p=w.addProject(fixture());shapeTool(w);draw(p,{10,12},{90,72});
    REQUIRE(!barCommandVisible(w,"tool.gradient"));
    REQUIRE(barCommandVisible(w,"tool.shape"));
    REQUIRE(barControl(w,w.findChild<QComboBox*>("gradientShape"))->isVisible()==false);
    REQUIRE(w.findChild<QComboBox*>("gradientStyle")->isHidden());
    REQUIRE(w.findChild<QCheckBox*>("gradientReverse")->isHidden());
    REQUIRE(w.findChild<QDoubleSpinBox*>("gradientOpacity")->isHidden());
    REQUIRE(w.findChild<QDoubleSpinBox*>("shapeLineWidth")->isHidden());
    REQUIRE(w.findChild<QComboBox*>("shapeLineStyle")->isHidden());
    REQUIRE(w.findChild<QComboBox*>("shapeLineCap")->isHidden());
    REQUIRE(w.findChild<QComboBox*>("cropRatioChoice")->isHidden());
    REQUIRE(!w.findChild<QComboBox*>("shapeKind")->isHidden());
    REQUIRE(!w.findChild<QDoubleSpinBox*>("shapeCornerRadius")->isHidden());
    REQUIRE(!w.findChild<QCheckBox*>("shapeSmoothing")->isHidden());
    REQUIRE(!w.findChild<QCheckBox*>("shapeStroke")->isHidden());
    REQUIRE(w.findChild<QDoubleSpinBox*>("shapeStrokeWidth")->isHidden());
    REQUIRE(barControl(w,w.findChild<QPushButton*>("shapeStrokeColor"))->isVisible()==false);
    kind(w)->setCurrentIndex(1);
    REQUIRE(w.findChild<QDoubleSpinBox*>("shapeCornerRadius")->isHidden()&&w.findChild<QCheckBox*>("shapeSmoothing")->isHidden());
    REQUIRE(!w.findChild<QCheckBox*>("shapeStroke")->isHidden()&&w.findChild<QDoubleSpinBox*>("shapeLineWidth")->isHidden());
    kind(w)->setCurrentIndex(2);
    REQUIRE(!w.findChild<QDoubleSpinBox*>("shapeLineWidth")->isHidden());
    REQUIRE(!w.findChild<QComboBox*>("shapeLineStyle")->isHidden()&&!w.findChild<QComboBox*>("shapeLineCap")->isHidden());
    REQUIRE(w.findChild<QCheckBox*>("shapeStroke")->isHidden()&&w.findChild<QDoubleSpinBox*>("shapeCornerRadius")->isHidden());
    REQUIRE(!barCommandVisible(w,"tool.gradient"));
    const auto shapeId=p.active;command(w,"tool.move")->trigger();
    REQUIRE(barCommandVisible(w,"tool.gradient")&&!w.findChild<QComboBox*>("gradientShape")->isHidden());
    p.active="top";p.selected={"top"};shapeTool(w);
    REQUIRE(barCommandVisible(w,"tool.gradient")&&!w.findChild<QComboBox*>("gradientShape")->isHidden());
    p.active=shapeId;p.selected={shapeId};shapeTool(w);
    REQUIRE(!barCommandVisible(w,"tool.gradient")&&w.findChild<QComboBox*>("gradientShape")->isHidden());
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
    {MainWindow w(true);auto& p=w.addProject(fixture());shapeTool(w);kind(w)->setCurrentIndex(1);draw(p,{10,12},{90,72});
        const auto before=added(p);const auto beforeStyle=editing::decodeShapeStyle(before.shapeJson);
        REQUIRE(beforeStyle.kind==editing::ShapeKind::Ellipse&&beforeStyle.cornerRadius==0);
        chooseShapeBackground(w,"00FF00",true);
        const auto style=editing::decodeShapeStyle(added(p).shapeJson);
        REQUIRE(style.kind==editing::ShapeKind::Ellipse&&style.cornerRadius==0&&style.red==0&&style.green==1&&style.blue==0);
        REQUIRE(added(p).transform==before.transform&&added(p).raster->pixel(40,30)==Pixel{0,255,0,255}&&p.history.undoName()=="Shape Style");}
    {MainWindow w(true);auto& p=w.addProject(fixture());shapeTool(w);kind(w)->setCurrentIndex(2);draw(p,{10,30},{80,30});
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
int main(int argc,char**argv){QApplication app(argc,argv);std::map<std::string,void(*)()>tests{{"shape_controls_edit_selected_layer",shape_controls_edit_selected_layer},{"shape_style_loads_from_selected_layer",shape_style_loads_from_selected_layer},{"shape_stroke_draw",shape_stroke_draw},{"shape_line_controls",shape_line_controls},{"shape_insert_sibling",shape_insert_sibling},{"shape_insert_folder",shape_insert_folder},{"shape_insert_nested_child",shape_insert_nested_child},{"shape_fractional_anchor",shape_fractional_anchor},{"shape_center_square",shape_center_square},{"shape_kind_change_cancels",shape_kind_change_cancels},{"shape_radius_limit",shape_radius_limit},{"shape_radius_frozen",shape_radius_frozen},{"shape_draft_immutable",shape_draft_immutable},{"shape_click_noop",shape_click_noop},{"shape_history_selection",shape_history_selection},{"shape_undo_resyncs_bar",shape_undo_resyncs_bar},{"shape_width_steps_coalesce",shape_width_steps_coalesce},{"shape_smoothing_draft",shape_smoothing_draft},{"shape_smoothing_history",shape_smoothing_history},{"shape_bar_limits_selected_shape",shape_bar_limits_selected_shape},{"shape_background_fill_selected",shape_background_fill_selected},{"shape_background_fill_ellipse_and_line",shape_background_fill_ellipse_and_line},{"shape_background_fill_cancel",shape_background_fill_cancel},{"shape_background_fill_same_color",shape_background_fill_same_color},{"shape_background_fill_other_tool",shape_background_fill_other_tool},{"shape_background_fill_pixel_layer",shape_background_fill_pixel_layer},{"shape_background_fill_draft",shape_background_fill_draft}};int failed=0,passed=0;for(const auto&[name,test]:tests){if(argc>1&&argv[1]!=name)continue;try{test();++passed;std::cout<<"PASS "<<name<<'\n';}catch(const std::exception&e){++failed;std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n';}}if(argc>1&&!tests.contains(argv[1]))return 2;std::cout<<"RESULT passed="<<passed<<" failed="<<failed<<'\n';return failed?1:0;}
