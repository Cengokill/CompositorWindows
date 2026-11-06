#include "ui/MainWindow.h"
#include "persistence/ProjectStore.h"
#include "ui/PaletteDialog.h"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QScrollBar>
#include <QSettings>
#include <QTemporaryDir>
#include <QSpinBox>
#include <QAbstractItemView>
#include <QTest>
#include <QToolButton>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <objbase.h>
#include <functional>
#include <iostream>
#include <stdexcept>
using namespace compositor;
namespace {
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void key(MainWindow& window,int code,Qt::KeyboardModifiers modifiers=Qt::NoModifier,bool release=true){QKeyEvent event(QEvent::KeyPress,code,modifiers);QApplication::sendEvent(&window,&event);if(release){QKeyEvent up(QEvent::KeyRelease,code,modifiers);QApplication::sendEvent(&window,&up);}}
void release(MainWindow& window,int code){QKeyEvent event(QEvent::KeyRelease,code,Qt::NoModifier);QApplication::sendEvent(&window,&event);}
template<class T>T* control(MainWindow& window,const char* name){auto* value=window.findChild<T*>(name);require(value!=nullptr,"Named keyboard control missing");return value;}
QLineEdit* openFontPicker(MainWindow& window,bool keyboard=false){
    auto* button=control<QToolButton>(window,"textFont");
    if(keyboard){
        button->setFocus(Qt::TabFocusReason);
        QTest::keyClick(button,Qt::Key_Space);
    }else QTest::mouseClick(button,Qt::LeftButton);
    auto* search=control<QLineEdit>(window,"textFontSearch");
    require(QTest::qWaitFor([&]{return search->isVisible()&&search->hasFocus();},std::chrono::seconds(2)),
            "Font search did not open with keyboard focus");
    return search;
}
QListWidgetItem* fontRow(QListWidget* results,const QString& label){
    const auto rows=results->findItems(label,Qt::MatchExactly);
    require(rows.size()==1,"Expected one matching installed face");
    return rows.front();
}
void clickFontRow(QListWidget* results,const QString& label){
    auto* item=fontRow(results,label);
    results->scrollToItem(item);
    QTest::mouseClick(results->viewport(),Qt::LeftButton,Qt::NoModifier,results->visualItemRect(item).center());
    require(!results->isVisible(),"Choosing a font did not close the popup");
}
void chooseArial(MainWindow& window){
    window.show();
    window.resize(1100,700);
    QApplication::processEvents();
    openFontPicker(window)->setText("Arial Regular");
    clickFontRow(control<QListWidget>(window,"textFontResults"),"Arial \xE2\x80\x94 Regular");
    QApplication::processEvents();
}
void action(MainWindow& window,const char* name){control<QAction>(window,name)->trigger();}
Document example(){Document document;document.id=newId();document.width=64;document.height=48;Layer layer;layer.id=newId();layer.name="Keyboard witness";layer.transform={0,0,64,48,0,false,false,Transform::Sampling::Nearest};std::vector<Pixel> pixels(64*48,{0,0,255,255});for(int y=10;y<20;++y)for(int x=10;x<20;++x)pixels[size_t(y)*64+x]={255,0,0,255};layer.raster=Raster::fromRgba(64,48,reinterpret_cast<const uint8_t*>(pixels.data()),64*4);document.layers.push_back(layer);return document;}
Selection selection(){return *editing::rasterSelection(editing::SelectionOutline::rectangle({10,10,10,10},false),64,48);}
std::shared_ptr<const GrayRaster> gray(uint8_t value){auto result=std::make_shared<GrayRaster>();result->width=64;result->height=48;result->pixels.assign(64*48,value);return result;}
Layer& layer(EditorProject& project){return project.document->layers.front();}
void drag(EditorProject& project,Point start,Point end){project.canvas->pointerDown({start.x,start.y},{});project.canvas->pointerMove({end.x,end.y},{});project.canvas->pointerUp({end.x,end.y},{});}
}
int main(int argc,char** argv){const auto apartment=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);QApplication app(argc,argv);int passed=0,failed=0;auto test=[&](const char* name,const std::function<void()>& run){try{run();++passed;std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& error){++failed;std::cout<<"FAIL "<<name<<": "<<error.what()<<'\n';}};
    test("keyboard_layer_nudge_linked_and_unlinked_mask",[]{MainWindow window(true);auto doc=example();doc.layers.front().mask=Mask{gray(128),true,true,Transform{3,4,64,48}};auto& project=window.addProject(doc);const auto source=layer(project).raster;key(window,Qt::Key_Right);key(window,Qt::Key_Down,Qt::ShiftModifier);require(layer(project).transform.x==1&&layer(project).transform.y==10,"Arrow layer steps differ");require(layer(project).mask->placement->x==4&&layer(project).mask->placement->y==14,"Linked mask was not carried");require(project.history.undoCount()==2&&layer(project).raster==source,"Nudge did not make one immutable transaction per key");layer(project).mask->linked=false;project.maskSelected=true;const auto image=layer(project).transform;key(window,Qt::Key_Left);require(layer(project).transform==image&&layer(project).mask->placement->x==3,"Unlinked selected mask changed image placement");});
    test("keyboard_outline_nudge_retains_offcanvas_geometry",[]{MainWindow window(true);auto doc=example();doc.selection=selection();auto& project=window.addProject(doc);const auto source=layer(project).raster;key(window,Qt::Key_M);for(int i=0;i<3;++i)key(window,Qt::Key_Left,Qt::ShiftModifier);require(project.document->selection->outline->bounds().x==-20,"Offcanvas outline was clipped");for(int i=0;i<3;++i)key(window,Qt::Key_Right,Qt::ShiftModifier);require(project.document->selection->outline->bounds().x==10&&project.document->selection->coverage->pixel(15,15)==255,"Returning outline lost geometry");require(layer(project).raster==source&&project.history.undoCount()==6,"Outline nudge painted or combined history");});
    test("keyboard_ctrl_arrow_moves_pixels_from_any_tool",[]{MainWindow window(true);auto doc=example();doc.selection=selection();auto& project=window.addProject(doc);const auto original=project.document;key(window,Qt::Key_B);key(window,Qt::Key_Right,Qt::ControlModifier|Qt::ShiftModifier);require(layer(project).raster->pixel(15,15).a==0&&layer(project).raster->pixel(25,15)==Pixel{255,0,0,255},"Ctrl arrow failed to lift/move selected pixels");require(project.document->selection->outline->bounds().x==20&&project.document->selection->coverage->pixel(25,15)==255,"Pixel nudge did not move vector selection");require(project.history.undoName()=="Move Pixels"&&project.history.undoCount()==1,"Pixel nudge history");auto undo=project.history.undo();require(undo&&undo->document==original,"Pixel nudge undo lost original");});
    test("keyboard_persistent_transform_nudge_and_focus_interruption",[]{MainWindow window(true);auto& project=window.addProject(example());project.canvas->zoomAt(10,{200,150});control<QCheckBox>(window,"transformSnapping")->setChecked(false);action(window,"freeTransform");key(window,Qt::Key_Right);require(layer(project).transform.x==1&&project.history.undoCount()==0,"Pending transform nudge committed");project.canvas->pointerDown({32,24},{});project.canvas->pointerMove({37,24},{});require(layer(project).transform.x==6,"Persistent drag preview missing");require(bool(project.canvas->pointerInterrupted),"Root interruption callback missing");project.canvas->pointerInterrupted();require(layer(project).transform.x==1&&control<QAction>(window,"applyTransform")->isEnabled(),"Interruption did not restore draft baseline while retaining session");action(window,"applyTransform");require(project.history.undoCount()==1&&layer(project).transform.x==1,"Apply after interruption lost preceding nudge");});
    test("keyboard_space_pans_without_committing_or_changing_tool",[]{MainWindow window(true);auto& project=window.addProject(example());const auto original=project.document;key(window,Qt::Key_G);drag(project,{10,10},{50,10});const auto preview=project.document;project.canvas->zoomAt(1,{200,150});const auto pan=project.canvas->pan;key(window,Qt::Key_Space,Qt::NoModifier,false);const QPointF view{150,120};auto document=project.canvas->documentPoint(view);project.canvas->pointerDown(document,{});for(int i=1;i<=3;++i){document=project.canvas->documentPoint(view+QPointF(i*10,0));project.canvas->pointerMove(document,{});}document=project.canvas->documentPoint(view+QPointF(30,0));project.canvas->pointerUp(document,{});release(window,Qt::Key_Space);require(project.canvas->pan==pan+QPointF(30,0),"Space repeated motion stalled");require(project.document==preview&&project.history.undoCount()==0&&control<QAction>(window,"applyGradient")->isEnabled(),"Space applied/cancelled pending gradient");project.canvas->pointerInterrupted();require(project.document==preview&&project.history.undoCount()==0,"Focus interruption cancelled pending gradient");key(window,Qt::Key_Escape);require(project.history.undoCount()==0&&project.document==original,"Escape failed to cancel the preserved gradient");});
    test("keyboard_delete_selection_empty_and_mask_palette",[]{MainWindow window(true);auto doc=example();doc.selection=Selection{gray(0)};auto& project=window.addProject(doc);const auto original=project.document;key(window,Qt::Key_Delete);require(project.document==original&&project.document->layers.size()==1,"Explicit empty selection deleted layer");project.document->selection=selection();key(window,Qt::Key_Backspace);require(layer(project).raster->pixel(15,15).a==0&&layer(project).raster->pixel(30,15).a==255,"Delete selection cleared wrong region");layer(project).mask=Mask{gray(255)};project.maskSelected=true;key(window,Qt::Key_X);key(window,Qt::Key_Delete);require(layer(project).mask->raster->pixel(15,15)==0&&layer(project).mask->raster->pixel(30,15)==255,"Delete mask omitted source background palette");project.document->selection.reset();key(window,Qt::Key_Backspace);require(!layer(project).mask&&project.document->layers.size()==1,"Delete without selection removed layer instead of mask");project.maskSelected=false;key(window,Qt::Key_Delete);require(project.document->layers.empty(),"Delete without selection retained layer");});
    test("keyboard_fill_palette_shortcuts_and_blend_wrap",[]{MainWindow window(true);auto doc=example();doc.selection=selection();auto& project=window.addProject(doc);key(window,Qt::Key_Backspace,Qt::AltModifier);require(layer(project).raster->pixel(15,15)==Pixel{0,0,0,255},"Alt Backspace did not fill foreground");key(window,Qt::Key_Backspace,Qt::ControlModifier);require(layer(project).raster->pixel(15,15)==Pixel{255,255,255,255},"Ctrl Backspace did not fill background");key(window,Qt::Key_Minus,Qt::ShiftModifier);require(layer(project).blend==Blend::Divide,"Reverse blend cycle did not wrap");key(window,Qt::Key_Plus,Qt::ShiftModifier);require(layer(project).blend==Blend::Normal,"Forward blend cycle did not wrap");require(project.history.undoCount()==4,"Fill/blend history count");});
    auto typeTool=[&](MainWindow& window){for(auto* action:window.findChildren<QAction*>())if(action->property("editorTool").isValid()&&action->property("editorTool").toInt()==int(ProjectTool::Text)&&action->isChecked())return true;return false;};
    test("keyboard_type_tool_local_undo_escape_and_commit",[&]{MainWindow window(true);auto& project=window.addProject(example());project.canvas->setFocus();const auto before=*project.document;const auto history=project.history.undoCount();key(window,Qt::Key_T);require(typeTool(window),"T did not select the type tool");project.canvas->pointerDown({8,8},{});project.canvas->pointerUp({8,8},{});key(window,Qt::Key_H);key(window,Qt::Key_I);key(window,Qt::Key_Z,Qt::ControlModifier);require(project.history.undoCount()==history&&project.document->layers.size()==before.layers.size(),"Local type undo changed the document");key(window,Qt::Key_Return,Qt::ControlModifier);require(project.history.undoCount()==history+1&&project.history.undoName()=="New Text Layer","Commit was not one New Text Layer step");require(project.document->layers.size()==before.layers.size()+1&&project.document->layers.back().text&&project.document->layers.back().text->value=="h","Committed text did not keep the locally undone letter");key(window,Qt::Key_T);project.canvas->pointerDown({20,20},{});project.canvas->pointerUp({20,20},{});key(window,Qt::Key_Escape);require(project.history.undoCount()==history+1&&project.document->layers.size()==before.layers.size()+1,"Escape added a text layer");});
    test("keyboard_type_reopen_color_and_font_round_trip",[&]{MainWindow window(true);auto& project=window.addProject(example());project.canvas->setFocus();key(window,Qt::Key_T);project.canvas->pointerDown({8,8},{});project.canvas->pointerUp({8,8},{});for(auto letter:{Qt::Key_H,Qt::Key_E,Qt::Key_L,Qt::Key_L,Qt::Key_O})key(window,letter);key(window,Qt::Key_Return,Qt::ControlModifier);require(project.document->layers.back().text&&project.document->layers.back().text->value=="hello","hello was not committed");key(window,Qt::Key_X);project.canvas->pointerDown({12,12},{});project.canvas->pointerUp({12,12},{});key(window,Qt::Key_Home);key(window,Qt::Key_Right,Qt::ShiftModifier);key(window,Qt::Key_Right,Qt::ShiftModifier);chooseArial(window);project.canvas->setFocus();control<QAction>(window,"textApplyForeground")->trigger();key(window,Qt::Key_Return,Qt::ControlModifier);const auto& edited=project.document->layers.back();require(edited.text&&edited.text->colorRuns.size()==1&&edited.text->colorRuns[0].location==0&&edited.text->colorRuns[0].length==2,"Selection color did not become a run");require(edited.text->fontRuns.size()==1&&edited.text->fontRuns[0].fontFamily=="Arial"&&edited.text->fontRuns[0].length==2,"Selection font did not become a run");require(project.history.undoName()=="Edit Text","Reopened text did not commit as Edit Text");control<QAction>(window,"command.edit.undo")->trigger();require(project.document->layers.back().text&&project.document->layers.back().text->colorRuns.empty()&&project.document->layers.back().text->fontRuns.empty(),"Undo did not restore the single-style text");control<QAction>(window,"command.edit.redo")->trigger();require(project.document->layers.back().text&&project.document->layers.back().text->colorRuns.size()==1&&project.document->layers.back().text->fontRuns.size()==1,"Redo lost the text runs");});
    test("keyboard_type_double_click_edit_and_drag_highlight",[&]{MainWindow window(true);auto& project=window.addProject(example());project.canvas->setFocus();key(window,Qt::Key_T);project.canvas->pointerDown({8,8},{});project.canvas->pointerUp({8,8},{});for(auto letter:{Qt::Key_H,Qt::Key_E,Qt::Key_L,Qt::Key_L,Qt::Key_O})key(window,letter);key(window,Qt::Key_Return,Qt::ControlModifier);const auto frame=project.document->layers.back().transform;const QPointF inside{frame.x+std::min(12.,frame.width*.2),frame.y+frame.height*.45};key(window,Qt::Key_V);require(!typeTool(window),"V did not leave the type tool");const auto layersBefore=project.document->layers.size();project.canvas->pointerDoubleClick({1,1},{});project.canvas->pointerUp({1,1},{});require(project.document->layers.size()==layersBefore&&!typeTool(window),"Double-click away from text created a text layer");project.canvas->pointerDoubleClick(inside,{});require(typeTool(window),"Double-click did not edit the text layer");key(window,Qt::Key_Z);key(window,Qt::Key_Return,Qt::ControlModifier);require(project.document->layers.back().text&&project.document->layers.back().text->value=="z","Double-click did not select the word");control<QAction>(window,"command.edit.undo")->trigger();require(project.document->layers.back().text&&project.document->layers.back().text->value=="hello","Undo did not restore the double-clicked word");key(window,Qt::Key_X);key(window,Qt::Key_T);const QPointF across{frame.x+frame.width*.55,inside.y()};project.canvas->pointerDown(inside,{});project.canvas->pointerMove(across,{});project.canvas->pointerUp(across,{});control<QAction>(window,"textApplyForeground")->trigger();key(window,Qt::Key_Return,Qt::ControlModifier);const auto& highlighted=project.document->layers.back().text;require(highlighted&&!highlighted->colorRuns.empty()&&highlighted->colorRuns.front().length>0&&highlighted->colorRuns.front().length<5,"Drag did not highlight part of the text");});
    test("keyboard_shift_u_and_text_focus_guard",[&]{MainWindow window(true);auto& project=window.addProject(example());key(window,Qt::Key_U,Qt::ShiftModifier);require(control<QComboBox>(window,"shapeKind")->currentIndex()==0,"First Shift U toggled before choosing shape");key(window,Qt::Key_U,Qt::ShiftModifier);require(control<QComboBox>(window,"shapeKind")->currentIndex()==1,"Second Shift U did not toggle ellipse");const auto before=project.document;auto* input=new QLineEdit(&window);window.show();input->show();input->setFocus();QApplication::processEvents();require(QApplication::focusWidget()==input,"Text focus witness unavailable");key(window,Qt::Key_Delete);key(window,Qt::Key_Right);key(window,Qt::Key_U,Qt::ShiftModifier);require(project.document==before&&control<QComboBox>(window,"shapeKind")->currentIndex()==1,"Editing keys escaped text focus guard");});
    test("keyboard_paragraph_style_word_and_duplicate",[&]{MainWindow window(true);auto& project=window.addProject(example());project.canvas->setFocus();key(window,Qt::Key_T);drag(project,{200,200},{360,280});for(auto letter:{Qt::Key_H,Qt::Key_E,Qt::Key_L,Qt::Key_L,Qt::Key_O,Qt::Key_Space,Qt::Key_H,Qt::Key_E,Qt::Key_L,Qt::Key_L,Qt::Key_O,Qt::Key_Space,Qt::Key_H,Qt::Key_E,Qt::Key_L,Qt::Key_L,Qt::Key_O})key(window,letter);key(window,Qt::Key_Return,Qt::ControlModifier);const auto& boxed=project.document->layers.back();require(boxed.text&&boxed.text->boxWidth&&*boxed.text->boxWidth>=16&&boxed.text->value=="hello hello hello","Drag did not commit a paragraph box");const auto color=*boxed.text;key(window,Qt::Key_T);const auto frame=boxed.transform;project.canvas->pointerDown({frame.x+frame.width*.3,frame.y+frame.height*.5},{});project.canvas->pointerUp({frame.x+frame.width*.3,frame.y+frame.height*.5},{});control<QToolButton>(window,"textColor")->click();QApplication::processEvents();auto* red=window.findChild<QSpinBox*>("paletteRGB0");require(red!=nullptr,"Text color picker did not open");red->setValue(color.red>0.5?0:255);if(auto* dialog=qobject_cast<QDialog*>(red->window()))dialog->reject();QApplication::processEvents();key(window,Qt::Key_Return,Qt::ControlModifier);require(project.document->layers.back().text&&std::abs(project.document->layers.back().text->red-color.red)<1e-6&&project.document->layers.back().text->colorRuns.empty(),"Cancelling the color picker kept the preview");key(window,Qt::Key_T);project.canvas->pointerDown({8,30},{});project.canvas->pointerUp({8,30},{});for(auto letter:{Qt::Key_A,Qt::Key_B,Qt::Key_Space,Qt::Key_C,Qt::Key_D})key(window,letter);key(window,Qt::Key_Left,Qt::ControlModifier);key(window,Qt::Key_Left,Qt::ControlModifier);key(window,Qt::Key_Z);key(window,Qt::Key_Return,Qt::ControlModifier);require(project.document->layers.back().text&&project.document->layers.back().text->value=="zab cd",("Ctrl+Left did not move by word: "+(project.document->layers.back().text?project.document->layers.back().text->value:std::string{})).c_str());key(window,Qt::Key_T);project.canvas->pointerDown({frame.x+frame.width*.3,frame.y+frame.height*.5},{});project.canvas->pointerUp({frame.x+frame.width*.3,frame.y+frame.height*.5},{});key(window,Qt::Key_Home);key(window,Qt::Key_Right,Qt::ShiftModifier);key(window,Qt::Key_Right,Qt::ShiftModifier);chooseArial(window);project.canvas->setFocus();key(window,Qt::Key_A,Qt::ControlModifier);auto* styles=control<QComboBox>(window,"textStyle");require(control<QToolButton>(window,"textFont")->text()=="Multiple"&&styles->currentText()=="Multiple","Mixed text did not show Multiple");key(window,Qt::Key_Escape);auto& textLayer=project.document->layers[1];textLayer.mask=Mask{gray(180),true,true};textLayer.effects.specified=true;textLayer.effects.stroke.enabled=true;textLayer.effects.stroke.size=4;const auto duplicated=textLayer.text->value;project.active=textLayer.id;project.selected={textLayer.id};const auto copies=project.document->layers.size();auto* duplicate=control<QAction>(window,"command.layer.duplicate");require(duplicate->shortcut()==QKeySequence("Ctrl+Shift+J"),"Duplicate Layer shortcut is not Ctrl+Shift+J");require(duplicate->isEnabled(),"Duplicate Layer is disabled");duplicate->trigger();require(project.document->layers.size()==copies+1,"Ctrl+Shift+J did not duplicate the layer");const auto& copy=project.document->layers[2];require(copy.text&&copy.text->value==duplicated&&copy.mask&&copy.effects.stroke.enabled&&copy.effects.stroke.size==4,"Duplicate dropped text, mask, or stroke");});
    test("keyboard_searchable_font_faces_and_history",[]{
        MainWindow window(true);
        auto& project=window.addProject(example());
        window.resize(1100,700);
        window.show();
        project.canvas->setFocus();
        key(window,Qt::Key_T);
        project.canvas->pointerDown({8,8},{});
        project.canvas->pointerUp({8,8},{});
        for(auto letter:{Qt::Key_H,Qt::Key_E,Qt::Key_L,Qt::Key_L,Qt::Key_O})key(window,letter);
        key(window,Qt::Key_Return,Qt::ControlModifier);
        const auto history=project.history.undoCount();
        const auto original=*project.document->layers.back().text;
        const auto preview=[&]() -> const TextContent& {
            require(project.textPreview&&project.textPreview->layer.text,"Missing editable text preview");
            return *project.textPreview->layer.text;
        };
        const auto& frame=project.document->layers.back().transform;
        const QPointF inside{frame.x+frame.width*.3,frame.y+frame.height*.5};
        project.canvas->pointerDown(inside,{});
        project.canvas->pointerUp(inside,{});

        auto* search=openFontPicker(window,true);
        auto* results=control<QListWidget>(window,"textFontResults");
        require(results->currentItem()&&results->currentItem()->text()=="Segoe UI \xE2\x80\x94 Regular","Picker did not highlight the current face");
        const auto searchPosition=search->pos();
        results->verticalScrollBar()->setValue(results->verticalScrollBar()->maximum());
        require(search->isVisible()&&search->pos()==searchPosition,"The search field moved with the results");
        QTest::keyClick(search,Qt::Key_Z,Qt::ControlModifier);
        require(project.history.undoCount()==history&&search->isVisible(),"Empty search routed Undo to the document");
        QTest::keyClicks(search,"bOlD ArIaL");
        require(search->text()=="bOlD ArIaL"&&results->count()>=2,"Case-insensitive, multiword search did not reach the field");
        for(int row=0;row<results->count();++row){
            const auto label=results->item(row)->text();
            require(label.contains("Arial",Qt::CaseInsensitive)&&label.contains("Bold",Qt::CaseInsensitive),"Search returned an unrelated face");
        }
        search->setText(QString::fromUtf8("\xC3\xA1ri\xC3\xA1l bold italic"));
        auto* wanted=fontRow(results,"Arial \xE2\x80\x94 Bold Italic");
        require(results->currentItem()==wanted,"Search did not prioritize the matching Arial face");
        QTest::keyClick(search,Qt::Key_Up);
        require(QTest::qWaitFor([&]{return project.textPreview&&project.textPreview->layer.text&&project.textPreview->layer.text->fontStyle=="Bold Italic";},std::chrono::seconds(2)),
                "Arrow key did not preview the exact face");
        require(*project.document->layers.back().text==original&&project.history.undoCount()==history,"Highlight changed the document");

        QTest::keyClick(search,Qt::Key_Escape);
        require(search->isVisible()&&search->text().isEmpty()&&preview()==original,"First Escape did not clear search and restore preview");
        QTest::keyClicks(search,"no-such-font-75");
        require(results->count()==0,"An absent family produced a result");
        QTest::keyClick(search,Qt::Key_Return);
        require(search->isVisible()&&preview()==original,"Enter selected a missing result");
        QTest::keyClick(search,Qt::Key_Escape);
        QTest::keyClick(search,Qt::Key_Escape);
        require(QTest::qWaitFor([&]{return !search->isVisible()&&project.canvas->hasFocus();},std::chrono::seconds(2)),
                "Second Escape did not return focus to the canvas");

        search=openFontPicker(window,true);
        QTest::keyClick(search,Qt::Key_Return);
        require(!search->isVisible()&&preview()==original,"Choosing the current face changed the text");
        search=openFontPicker(window,true);
        QTest::keyClicks(search,"Arial Bold");
        QTest::keyClick(search,Qt::Key_Up);
        require(QTest::qWaitFor([&]{return project.textPreview&&project.textPreview->layer.text&&project.textPreview->layer.text->fontStyle=="Bold";},std::chrono::seconds(2)),
                "First Arial face was not previewed");
        QTest::keyClick(search,Qt::Key_Down);
        QTest::keyClick(search,Qt::Key_Return);
        require(QTest::qWaitFor([&]{return !search->isVisible()&&project.canvas->hasFocus();},std::chrono::seconds(2)),
                "Choosing a face did not return focus to the canvas");
        require(preview().fontFamily=="Arial"&&preview().fontStyle=="Bold Italic",
                "Enter kept the preceding preview instead of the highlighted face");
        auto* styles=control<QComboBox>(window,"textStyle");
        require(styles->currentText()=="Bold Italic","Style control did not follow the chosen face");
        key(window,Qt::Key_Z,Qt::ControlModifier);
        require(preview()==original,"One local undo did not restore the starting face");
        key(window,Qt::Key_Z,Qt::ControlModifier);
        key(window,Qt::Key_Y,Qt::ControlModifier);
        require(preview().fontStyle=="Bold Italic","Local redo did not restore the chosen face");
        key(window,Qt::Key_Return,Qt::ControlModifier);
        require(project.history.undoCount()==history+1&&project.document->layers.back().text->fontStyle=="Bold Italic",
                "Done did not commit exactly one text edit");
        action(window,"command.edit.undo");
        require(*project.document->layers.back().text==original,"Document undo did not restore the original face");
        action(window,"command.edit.redo");
        require(project.document->layers.back().text->fontStyle=="Bold Italic","Document redo did not restore the face");

        QTemporaryDir directory;
        require(directory.isValid(),"Missing temporary project directory");
        const auto path=directory.filePath("ChosenFont.comp");
        ProjectStore store(makeWicProjectCodec());
        store.save(std::filesystem::path(path.toStdWString()),*project.document,project.active);
        require(window.openProjectPaths({path}),"Could not reopen the project with the chosen face");
        key(window,Qt::Key_T);
        window.canvas()->pointerDown(inside,{});
        window.canvas()->pointerUp(inside,{});
        require(control<QToolButton>(window,"textFont")->text()=="Arial"&&
                control<QComboBox>(window,"textStyle")->currentText()=="Bold Italic","Reopen did not restore the selected face");
        styles=control<QComboBox>(window,"textStyle");
        QTest::mouseClick(styles->lineEdit(),Qt::LeftButton);
        require(QTest::qWaitFor([&]{return styles->view()->isVisible();},std::chrono::seconds(2)),"Style selector did not open");
        const auto italic=styles->findText("Italic");
        require(italic>=0,"Arial Italic is missing from the Style control");
        const auto row=styles->model()->index(italic,styles->modelColumn(),styles->rootModelIndex());
        styles->view()->scrollTo(row);
        QTest::mouseClick(styles->view()->viewport(),Qt::LeftButton,Qt::NoModifier,styles->view()->visualRect(row).center());
        require(styles->currentText()=="Italic","Style control did not apply the selected variant");
        action(window,"textCancel");
    });
    test("keyboard_font_picker_preserves_missing_faces_on_reopen",[]{
        QTemporaryDir directory;
        require(directory.isValid(),"Missing temporary project directory");
        const auto path=directory.filePath("MissingFont.comp");
        ProjectStore store(makeWicProjectCodec());
        auto document=example();
        auto& textLayer=document.layers.front();
        TextContent original;
        original.value="Hello";
        original.fontFamily="Missing Font 75";
        original.fontStyle="Unavailable Style";
        textLayer.text=original;
        textLayer.raster=text::rasterize(original).raster;
        textLayer.transform.width=textLayer.raster->width;
        textLayer.transform.height=textLayer.raster->height;
        const auto id=textLayer.id;
        store.save(std::filesystem::path(path.toStdWString()),document,id);

        MainWindow window(true);
        window.resize(1100,700);
        window.show();
        require(window.openProjectPaths({path}),"Could not open the saved missing-font project");
        auto* canvas=window.canvas();
        require(canvas!=nullptr,"Missing reopened canvas");
        key(window,Qt::Key_T);
        const auto edit=[&]{
            canvas->pointerDown({20,20},{});
            canvas->pointerUp({20,20},{});
        };
        edit();
        require(control<QToolButton>(window,"textFont")->text()=="Missing Font 75","Toolbar replaced the saved family");
        require(control<QComboBox>(window,"textStyle")->currentText()=="Unavailable Style","Toolbar replaced the saved face");
        auto* search=openFontPicker(window);
        search->setText("Missing Font 75");
        require(control<QListWidget>(window,"textFontResults")->count()==0,"Missing family appeared in the installed catalog");
        QTest::keyClick(search,Qt::Key_Escape);
        QTest::keyClick(search,Qt::Key_Escape);
        action(window,"textDone");
        require(!control<QAction>(window,"command.edit.undo")->isEnabled(),
                "Opening, filtering and finishing rewrote the missing face");
        edit();
        openFontPicker(window)->setText("Arial Regular");
        clickFontRow(control<QListWidget>(window,"textFontResults"),"Arial \xE2\x80\x94 Regular");
        action(window,"textDone");
        require(control<QToolButton>(window,"textFont")->text()=="Arial"&&control<QAction>(window,"command.edit.undo")->isEnabled(),
                "An explicit choice did not replace the missing family");
        edit();
        require(control<QToolButton>(window,"textFont")->text()=="Arial","Reediting did not retain the explicit choice");
        action(window,"textCancel");
    });
    test("keyboard_remapped_shortcuts_restore_and_dispatch",[&]{
        auto toolChecked=[](MainWindow& window,ProjectTool tool){for(auto* action:window.findChildren<QAction*>())if(action->property("editorTool").isValid()&&action->property("editorTool").toInt()==int(tool)&&action->isChecked())return true;return false;};
        auto shortcutsOf=[](MainWindow& window,const char* id){for(auto* action:window.findChildren<QAction*>())if(action->property("commandId").toString()==id&&!action->shortcuts().isEmpty())return action->shortcuts();return QList<QKeySequence>{};};
        QTemporaryDir preferences;require(preferences.isValid(),"Fixture settings directory");QCoreApplication::setOrganizationName("CompositorFixture");QCoreApplication::setApplicationName("KeyboardShortcuts");QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,preferences.path());
        QSettings settings;settings.remove(ui::shortcutSettingsGroup);settings.beginGroup(ui::shortcutSettingsGroup);settings.setValue("tool.move","K");settings.setValue("tool.marquee","");settings.setValue("tool.type","Y");settings.setValue("palette.swap","");settings.setValue("edit.redo","Ctrl+Y");settings.endGroup();settings.sync();
        {
            MainWindow window(true);auto& project=window.addProject(example());const auto history=project.history.undoCount();const auto document=project.document;
            require(shortcutsOf(window,"tool.move")==QList<QKeySequence>{QKeySequence("K")},"Restored Move shortcut missing from its QAction");
            require(shortcutsOf(window,"edit.redo")==QList<QKeySequence>{QKeySequence("Ctrl+Y")},"Restored Redo bindings differ");
            require(shortcutsOf(window,"tool.marquee").isEmpty(),"Cleared Marquee shortcut still on an action");
            auto typeButtonTip=[&]{for(auto* action:window.findChildren<QAction*>())if(action->property("editorTool").toInt()==int(ProjectTool::Text)&&action->property("editorTool").isValid())for(auto* object:action->associatedObjects())if(auto* button=qobject_cast<QToolButton*>(object))return button->toolTip();return QString{};};
            require(typeButtonTip()=="Type (Y)",("Type tool button tooltip not remapped: "+typeButtonTip().toStdString()).c_str());
            for(auto* button:window.findChildren<QToolButton*>())require(!button->toolTip().endsWith("(X)"),"Cleared Swap shortcut still hinted on a button");

            key(window,Qt::Key_B);require(toolChecked(window,ProjectTool::Brush),"Default B did not select the brush");
            key(window,Qt::Key_K);require(toolChecked(window,ProjectTool::Move),"Remapped K did not select Move through the direct path");
            key(window,Qt::Key_B);key(window,Qt::Key_V);require(toolChecked(window,ProjectTool::Brush),"Old V still selected Move");
            key(window,Qt::Key_M);require(!toolChecked(window,ProjectTool::Marquee),"Cleared M still selected Marquee");
            require(project.history.undoCount()==history&&project.document==document,"Shortcut use changed document history");
            auto* input=new QLineEdit(&window);window.show();input->show();input->setFocus();QApplication::processEvents();require(QApplication::focusWidget()==input,"Text focus witness unavailable");
            QTest::keyClick(input,Qt::Key_K);QApplication::processEvents();require(input->text()=="k"&&toolChecked(window,ProjectTool::Brush),"Remapped key escaped the text field");
        }
        settings.remove(ui::shortcutSettingsGroup);settings.sync();
        MainWindow window(true);window.addProject(example());
        require(shortcutsOf(window,"tool.move")==QList<QKeySequence>{QKeySequence("V")}&&shortcutsOf(window,"edit.redo").size()==2,"Defaults not restored in a fresh window");for(auto* button:window.findChildren<QToolButton*>())if(button->defaultAction()&&button->defaultAction()->property("commandId").toString()=="tool.type")require(button->toolTip()=="Type (T)","Default Type hint missing in a fresh window");
        key(window,Qt::Key_M);require(toolChecked(window,ProjectTool::Marquee),"Default M did not select Marquee");
    });    std::cout<<"{\"suite\":\"keyboard_ui\",\"passed\":"<<passed<<",\"failed\":"<<failed<<",\"mac_differential\":false,\"visual_acceptance\":false}\n";if(SUCCEEDED(apartment))CoUninitialize();return failed?1:0;
}
