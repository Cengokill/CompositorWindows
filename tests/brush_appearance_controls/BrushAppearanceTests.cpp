#include "ui/MainWindow.h"
#include <QAbstractButton>
#include <QApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <QTreeWidgetItemIterator>
#include <cmath>
#include <iostream>
#include <map>
#include <stdexcept>

using namespace compositor;
namespace {
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void equalNumber(double actual,double expected,const char* message){require(std::abs(actual-expected)<1e-9,message);}
void events(){QApplication::processEvents();}
QAction* command(MainWindow& window,const char* id){for(auto* action:window.findChildren<QAction*>())if(action->property("commandId").toString()==id)return action;throw std::runtime_error(std::string("Missing command: ")+id);}
void invoke(MainWindow& window,const char* id){auto* action=command(window,id);require(action->isEnabled(),"Fixture command must be enabled");action->trigger();events();}
template<class T>T* control(MainWindow& window,const char* name){if(auto* value=window.findChild<T*>(name))return value;for(auto* value:window.findChildren<T*>())if(value->accessibleName()==name)return value;throw std::runtime_error(std::string("Missing source control: ")+name);}
Document document(){Document d;d.id=newId();d.width=d.height=64;for(const char* name:{"Lower","Upper"}){Layer l;l.id=newId();l.name=name;l.transform={0,0,64,64};l.raster=Raster::filled(64,64,{80,120,160,255});d.layers.push_back(std::move(l));}return d;}
Layer& layer(EditorProject& project,const std::string& id){for(auto& value:project.document->layers)if(value.id==id)return value;throw std::runtime_error("Fixture layer missing");}
struct Fixture {
    MainWindow window{true};EditorProject& project;Document before;
    Fixture():project(window.addProject(document(),"Brush appearance A")),before(*project.document){window.resize(1500,900);window.show();window.activateWindow();events();project.canvas->setFocus();events();}
    void select(EditorProject& value){window.findChild<QTabWidget*>()->setCurrentWidget(value.page);events();require(window.findChild<QTabWidget*>()->currentWidget()==value.page,"Fixture project switch");}
};
QAction* rail(MainWindow& window,ProjectTool tool){auto* bar=control<QToolBar>(window,"tools");for(auto* action:bar->actions())if(action->property("editorTool").isValid()&&action->property("editorTool").toInt()==int(tool))return action;throw std::runtime_error("Fixture rail tool missing");}
void clickRail(MainWindow& window,ProjectTool tool){auto* bar=control<QToolBar>(window,"tools");auto* action=rail(window,tool);require(action->isEnabled(),"Fixture rail enabled");auto* button=bar->widgetForAction(action);require(button&&button->isVisible(),"Fixture visible rail button");QTest::mouseClick(button,Qt::LeftButton);events();}
bool erasing(MainWindow& window){return rail(window,ProjectTool::Eraser)->isChecked();}
void key(Fixture& f,Qt::Key value){f.project.canvas->setFocus();events();QTest::keyClick(f.project.canvas,value);events();}
void unchanged(const Fixture& f){require(f.project.document==f.before&&f.project.history.undoCount()==0,"Tool choices must not mutate document or history");}
double sliderPercent(QSlider* slider){return double(slider->value())*100/slider->maximum();}
void setPercent(QSlider* slider,double percent){slider->setValue(int(std::lround(slider->maximum()*percent/100)));events();}
void sliders(const char* tool,bool retouch){
    Fixture f;invoke(f.window,tool);
    auto* hardness=control<QSlider>(f.window,retouch?"retouchHardnessSlider":"brushHardnessSlider");
    auto* opacity=control<QSlider>(f.window,retouch?"retouchOpacitySlider":"brushOpacitySlider");
    auto* hardNumber=control<QDoubleSpinBox>(f.window,retouch?"retouchHardness":"Brush hardness");
    auto* opacityNumber=control<QDoubleSpinBox>(f.window,retouch?"retouchOpacity":"Brush opacity");
    require(hardness->isVisible()&&opacity->isVisible(),"Both source sliders must be visible for this brush family");
    require(hardness->minimum()==0&&hardness->maximum()>0,"Hardness slider range starts at zero");
    equalNumber(double(opacity->minimum())/opacity->maximum(),.01,"Opacity slider minimum is one percent");
    setPercent(hardness,37);setPercent(opacity,24);equalNumber(hardNumber->value(),37,"Hardness slider synchronizes numeric field");equalNumber(opacityNumber->value(),24,"Opacity slider synchronizes numeric field");
    hardNumber->setValue(63);opacityNumber->setValue(81);events();equalNumber(sliderPercent(hardness),63,"Numeric hardness synchronizes slider");equalNumber(sliderPercent(opacity),81,"Numeric opacity synchronizes slider");
    QTest::mousePress(hardness,Qt::LeftButton,{},QPoint(hardness->width()/4,hardness->height()/2));
    require(hardNumber->value()>10&&hardNumber->value()<40,"Actual track press jumps near clicked quarter");
    QTest::mouseRelease(hardness,Qt::LeftButton,{},QPoint(hardness->width()/4,hardness->height()/2));unchanged(f);
}
void sliderProjectIsolation(){Fixture f;invoke(f.window,"tool.brush");setPercent(control<QSlider>(f.window,"brushHardnessSlider"),37);setPercent(control<QSlider>(f.window,"brushOpacitySlider"),24);auto& other=f.window.addProject(document(),"Brush appearance B");invoke(f.window,"tool.brush");equalNumber(control<QDoubleSpinBox>(f.window,"Brush hardness")->value(),100,"Other session default hardness");equalNumber(control<QDoubleSpinBox>(f.window,"Brush opacity")->value(),100,"Other session default opacity");f.select(f.project);equalNumber(sliderPercent(control<QSlider>(f.window,"brushHardnessSlider")),37,"Restored session hardness slider");equalNumber(sliderPercent(control<QSlider>(f.window,"brushOpacitySlider")),24,"Restored session opacity slider");unchanged(f);require(other.history.undoCount()==0,"Other session history unchanged");}
void tipFamilies(){Fixture f;invoke(f.window,"tool.brush");setPercent(control<QSlider>(f.window,"brushHardnessSlider"),37);setPercent(control<QSlider>(f.window,"brushOpacitySlider"),24);invoke(f.window,"tool.heal");equalNumber(sliderPercent(control<QSlider>(f.window,"retouchHardnessSlider")),37,"Spot Healing shares Brush hardness");equalNumber(sliderPercent(control<QSlider>(f.window,"retouchOpacitySlider")),24,"Spot Healing shares Brush opacity");invoke(f.window,"tool.clone");equalNumber(sliderPercent(control<QSlider>(f.window,"retouchHardnessSlider")),0,"Clone starts soft independently");setPercent(control<QSlider>(f.window,"retouchHardnessSlider"),61);invoke(f.window,"tool.retouch");equalNumber(sliderPercent(control<QSlider>(f.window,"retouchHardnessSlider")),0,"Smear starts soft independently");setPercent(control<QSlider>(f.window,"retouchHardnessSlider"),82);invoke(f.window,"tool.clone");equalNumber(sliderPercent(control<QSlider>(f.window,"retouchHardnessSlider")),61,"Clone retains its tip");invoke(f.window,"tool.brush");equalNumber(sliderPercent(control<QSlider>(f.window,"brushHardnessSlider")),37,"Brush retains its shared tip");unchanged(f);}
QLineEdit* percentage(Fixture& f){auto* field=control<QLineEdit>(f.window,"layerOpacityPercent");require(field->isVisible()&&field->isEnabled(),"Layer percentage field visible and enabled");return field;}
void type(QLineEdit* field,const char* text){field->setFocus();events();require(field->hasFocus(),"Fixture percentage focus");QTest::keyClick(field,Qt::Key_A,Qt::ControlModifier);QTest::keyClick(field,Qt::Key_Backspace);QTest::keyClicks(field,text);}
void percentCommit(int finish){Fixture f;const auto id=f.project.active;auto* field=percentage(f);type(field,"37.5");equalNumber(layer(f.project,id).opacity,1,"Typing waits for source commit boundary");require(f.project.history.undoCount()==0,"Typing has no history entry");QTest::keyClick(field,Qt::Key(finish));events();equalNumber(layer(f.project,id).opacity,.375,"Committed fractional opacity is preserved");require(f.project.history.undoCount()==1,"Percentage commit has one history entry");require(f.project.canvas->hasFocus(),"Commit releases focus to canvas");require(field->text()=="38","Only displayed percentage is rounded");invoke(f.window,"edit.undo");require(f.project.document==f.before,"Percentage Undo restores exact document");}
void percentFocusLoss(){Fixture f;auto* field=percentage(f);type(field,"42");f.project.canvas->setFocus();events();equalNumber(layer(f.project,f.project.active).opacity,.42,"Losing percentage focus applies typed text");require(f.project.history.undoCount()==1,"Focus loss creates one edit");}
void percentInvalid(){Fixture f;auto* field=percentage(f);for(const char* invalid:{"garbage","nan","inf",""}){type(field,invalid);QTest::keyClick(field,Qt::Key_Return);events();equalNumber(layer(f.project,f.project.active).opacity,1,"Invalid or nonfinite percent preserves opacity");require(field->text()=="100","Invalid input resynchronizes display");}unchanged(f);}
void percentClamp(){Fixture f;auto* field=percentage(f);type(field,"150");QTest::keyClick(field,Qt::Key_Return);events();equalNumber(layer(f.project,f.project.active).opacity,1,"Percentage upper clamp");require(f.project.history.undoCount()==0,"Clamped unchanged opacity is no-op");type(field,"-20");QTest::keyClick(field,Qt::Key_Return);events();equalNumber(layer(f.project,f.project.active).opacity,0,"Percentage lower clamp");require(f.project.history.undoCount()==1,"Lower clamp commits once");}
void percentArrows(){Fixture f;auto* field=percentage(f);type(field,"50");QTest::keyClick(field,Qt::Key_Return);events();field->setFocus();events();QTest::keyClick(field,Qt::Key_Up);equalNumber(layer(f.project,f.project.active).opacity,.51,"Opacity Up adds one percent");QTest::keyClick(field,Qt::Key_Up,Qt::ShiftModifier);equalNumber(layer(f.project,f.project.active).opacity,.61,"Opacity Shift-Up adds ten percent");QTest::keyClick(field,Qt::Key_Down,Qt::ShiftModifier);equalNumber(layer(f.project,f.project.active).opacity,.51,"Opacity Shift-Down subtracts ten percent");QTest::keyClick(field,Qt::Key_Down,Qt::ControlModifier);equalNumber(layer(f.project,f.project.active).opacity,.50,"Control does not multiply opacity steps");}
void percentTargetGuard(){Fixture f;const auto original=f.project.active;const auto other=f.project.document->layers.front().id==original?f.project.document->layers.back().id:f.project.document->layers.front().id;auto* field=percentage(f);type(field,"17");auto* tree=control<QTreeWidget>(f.window,"layersTree");QTreeWidgetItem* row=nullptr;for(QTreeWidgetItemIterator item(tree);*item;++item)if((*item)->data(0,Qt::UserRole).toString().toStdString()==other){row=*item;break;}require(row,"Fixture other layer row");tree->setCurrentItem(row);events();require(f.project.active==other,"Actual layer selection changed target");QTest::keyClick(field,Qt::Key_Return);events();equalNumber(layer(f.project,original).opacity,1,"Stale text cannot modify original after selection changes");equalNumber(layer(f.project,other).opacity,1,"Stale text cannot modify replacement target");require(f.project.history.undoCount()==0,"Stale percentage edit produces no history");}
void percentTextKeys(){Fixture f;auto* field=percentage(f);type(field,"be");require(field->text()=="be","Brush shortcuts cannot steal percentage text");require(rail(f.window,ProjectTool::Move)->isChecked(),"Text key leaves selected tool unchanged");QTest::keyClick(field,Qt::Key_Return);events();unchanged(f);}
void modeSelector(){Fixture f;invoke(f.window,"tool.brush");auto* paint=control<QAbstractButton>(f.window,"brushModePaint");auto* erase=control<QAbstractButton>(f.window,"brushModeErase");require(paint->isVisible()&&erase->isVisible()&&paint->isChecked()&&!erase->isChecked(),"Source segmented Brush mode defaults to Paint");QTest::mouseClick(erase,Qt::LeftButton);events();require(erase->isChecked()&&!paint->isChecked()&&erasing(f.window),"Erase mode targets eraser");QTest::mouseClick(paint,Qt::LeftButton);events();require(paint->isChecked()&&!erase->isChecked()&&!erasing(f.window),"Paint mode targets brush");unchanged(f);}
void railRemembers(){Fixture f;key(f,Qt::Key_E);require(erasing(f.window),"E selects Erase");invoke(f.window,"tool.move");clickRail(f.window,ProjectTool::Brush);require(erasing(f.window),"Returning through Brush rail retains Erase mode");unchanged(f);}
void explicitShortcuts(){Fixture f;key(f,Qt::Key_E);require(erasing(f.window),"E selects Erase explicitly");key(f,Qt::Key_B);require(!erasing(f.window)&&rail(f.window,ProjectTool::Brush)->isChecked(),"B selects Paint explicitly");key(f,Qt::Key_E);require(erasing(f.window),"E restores Erase explicitly");unchanged(f);}
void modeProjectIsolation(){Fixture f;key(f,Qt::Key_E);invoke(f.window,"tool.move");auto& other=f.window.addProject(document(),"Brush mode B");clickRail(f.window,ProjectTool::Brush);require(!erasing(f.window),"Other project's Brush mode defaults to Paint");f.select(f.project);clickRail(f.window,ProjectTool::Brush);require(erasing(f.window),"Project restores remembered Erase while another tool was selected");unchanged(f);require(other.history.undoCount()==0,"Mode choices preserve other history");}
}
int main(int argc,char** argv){
    QApplication app(argc,argv);QTemporaryDir preferences;require(preferences.isValid(),"Fixture settings directory");QCoreApplication::setOrganizationName("CompositorFixture");QCoreApplication::setApplicationName("BrushAppearance");QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,preferences.path());QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,preferences.path());
    const std::map<std::string,void(*)()> cases{{"brush_sliders",[]{sliders("tool.brush",false);}},{"heal_sliders",[]{sliders("tool.heal",true);}},{"clone_sliders",[]{sliders("tool.clone",true);}},{"smear_sliders",[]{sliders("tool.retouch",true);}},{"slider_project_isolation",sliderProjectIsolation},{"tip_family_retention",tipFamilies},{"layer_percent_commit",[]{percentCommit(Qt::Key_Return);}},{"layer_percent_escape",[]{percentCommit(Qt::Key_Escape);}},{"layer_percent_focus_loss",percentFocusLoss},{"layer_percent_invalid",percentInvalid},{"layer_percent_clamp",percentClamp},{"layer_percent_arrows",percentArrows},{"layer_percent_target_guard",percentTargetGuard},{"layer_percent_text_keys",percentTextKeys},{"brush_mode_selector",modeSelector},{"brush_rail_remembers",railRemembers},{"brush_shortcuts_explicit",explicitShortcuts},{"brush_mode_project_isolation",modeProjectIsolation}};
    const std::string name=argc>1?argv[1]:"";QJsonObject report{{"case",QString::fromStdString(name)}};int result=1;
    try{require(argc==3&&cases.contains(name),"Provide named case and report path");cases.at(name)();report["status"]="passed";result=0;std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& error){report["status"]="failed";report["error"]=error.what();std::cerr<<"FAIL "<<name<<": "<<error.what()<<'\n';}
    if(argc==3){QFile file(QString::fromLocal8Bit(argv[2]));if(!file.open(QIODevice::WriteOnly)||file.write(QJsonDocument(report).toJson())<0)return 2;}return result;
}
