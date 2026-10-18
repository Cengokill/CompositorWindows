#include "ui/CameraRawPanel.h"
#include "ui/MainWindow.h"
#include "ui/PropertyControls.h"
#include <QApplication>
#include <QLabel>
#include <QLineEdit>
#include <QSlider>
#include <QTest>
#include <QToolBar>
#include <cstdio>
#include <map>
#include <stdexcept>
using namespace compositor;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
QAction* action(MainWindow& window,const char* id){for(auto* result:window.findChildren<QAction*>())if(result->property("commandId").toString()==id)return result;throw std::runtime_error("command missing");}
void invoke(MainWindow& window,const char* id){auto* command=action(window,id);require(command->isEnabled(),"command disabled");command->trigger();QApplication::processEvents();}
QDoubleSpinBox* number(MainWindow& window,const char* name){for(auto* field:window.findChildren<QDoubleSpinBox*>())if(field->accessibleName()==name)return field;throw std::runtime_error("numeric control missing");}
Document sample(){Document document;document.id="property-document";document.width=64;document.height=64;Layer layer;layer.id="image";layer.transform={0,0,64,64};layer.raster=Raster::filled(64,64,{80,120,160,255});document.layers.push_back(layer);return document;}
struct Fixture {MainWindow window{true};EditorProject& project;Document before;
    Fixture():project(window.addProject(sample())),before(*project.document){window.show();window.activateWindow();QApplication::processEvents();}
};
void transformTyping(){
    Fixture f;auto* angle=number(f.window,"Angle");angle->setFocus();QApplication::processEvents();
    QTest::keyClick(angle,Qt::Key_A,Qt::ControlModifier);QTest::keyClicks(angle,"360");
    require(angle->findChild<QLineEdit*>()->text()=="360","focused transform text was overwritten by normalized draft");
    require(f.project.document->layers[0].transform.rotation==0&&f.project.history.undoCount()==0,"typing should update the pending rotation without committing history");
    QTest::keyClick(angle,Qt::Key_Up,Qt::ShiftModifier);
    require(f.project.document->layers[0].transform.rotation==10,"Shift arrow must add ten degrees to normalized draft");
    QTest::keyClick(angle,Qt::Key_Escape);QApplication::processEvents();
    require(f.project.canvas->hasFocus()&&f.project.history.undoCount()==0,"Escape in a field returns canvas focus while retaining transform draft");
    auto* cancel=f.window.findChild<QAction*>("cancelTransform");require(cancel&&cancel->isEnabled(),"field edit should remain cancellable");cancel->trigger();
    require(f.project.document==f.before&&f.project.history.undoCount()==0,"Cancel must restore exact pre-field document");
}
void opacityTrack(){
    Fixture f;auto* slider=f.window.findChild<QSlider*>("layerOpacity");require(slider&&slider->isVisible(),"opacity slider missing");
    const QPoint at(slider->width()/4,slider->height()/2);QTest::mousePress(slider,Qt::LeftButton,{},at);
    require(slider->value()>15&&slider->value()<35,"track press must jump near the clicked quarter immediately");
    require(f.project.history.undoCount()==0&&f.project.document->layers[0].opacity==slider->value()/100.,"track draft must use one pending opacity edit");
    QTest::mouseRelease(slider,Qt::LeftButton,{},at);require(f.project.history.undoCount()==1,"track release must commit exactly one edit");
    invoke(f.window,"edit.undo");require(f.project.document==f.before,"opacity Undo restores exact document");
}
void opacityDoubleClick(){
    Fixture f;auto* slider=f.window.findChild<QSlider*>("layerOpacity");require(slider&&slider->isVisible(),"opacity slider missing");
    slider->setValue(30);
    const Document baseline=*f.project.document;const auto steps=f.project.history.undoCount();
    // Windows delivers Press, Release, DblClick, Release; QTest::mouseDClick alone sends only the DblClick.
    const QPoint at(slider->width()*4/5,slider->height()/2);
    QTest::mouseClick(slider,Qt::LeftButton,{},at);QTest::mouseDClick(slider,Qt::LeftButton,{},at);QTest::mouseRelease(slider,Qt::LeftButton,{},at);
    QApplication::processEvents();
    require(slider->value()==100&&f.project.document->layers[0].opacity==1.,"double-click must reset layer opacity to 100 percent");
    require(f.project.history.undoCount()==steps+1,"double-click away from the knob must add exactly one history step");
    invoke(f.window,"edit.undo");require(f.project.document==baseline,"one undo must restore the opacity from before the double-click");
}
void brushField(){
    Fixture f;invoke(f.window,"tool.brush");auto* opacity=number(f.window,"Brush opacity");require(opacity->minimum()==1,"source brush opacity minimum is one percent");
    opacity->setValue(5);opacity->setFocus();QTest::keyClick(opacity,Qt::Key_Down,Qt::ShiftModifier);require(opacity->value()==1,"Shift decrement clamps brush opacity to one");
    auto* diameter=number(f.window,"Brush diameter");diameter->setValue(40);diameter->setFocus();QTest::keyClick(diameter,Qt::Key_Up,Qt::ShiftModifier);require(diameter->value()==50,"real brush field must step ten pixels");
    QTest::keyClick(diameter,Qt::Key_Return);QApplication::processEvents();require(f.project.canvas->hasFocus(),"Return must give brush keyboard commands back to canvas");
    require(f.project.document==f.before&&f.project.history.undoCount()==0,"tool option editing must preserve document history");
}
QSlider* sliderNamed(QWidget& root,const char* name){for(auto* slider:root.findChildren<QSlider*>())if(slider->accessibleName()==name)return slider;throw std::runtime_error("slider missing");}
void sliderDoubleClick(){
    compositor::ui::TrackSlider slider(Qt::Horizontal);slider.setRange(0,10000);slider.setDefaultValue(10000);slider.setValue(2500);slider.resize(300,24);slider.show();QApplication::processEvents();
    int withdrawals=0;slider.withdrawPressStep=[&]{++withdrawals;};
    QTest::mouseClick(&slider,Qt::LeftButton,{},QPoint(60,12));QTest::mouseDClick(&slider,Qt::LeftButton,{},QPoint(60,12));QTest::mouseRelease(&slider,Qt::LeftButton,{},QPoint(60,12));QApplication::processEvents();
    require(slider.value()==10000&&!slider.isSliderDown(),"double-click must restore the default value and end the drag");
    require(withdrawals==1,"a double-click after a track snap must withdraw that snap exactly once");
}
void cameraRawAlignment(){
    compositor::ui::CameraRawPanel panel(compositor::filters::CameraRawSettings{});panel.show();QApplication::processEvents();
    const auto labels=panel.findChildren<QLabel*>("sliderLabel");require(labels.size()>10,"camera raw sliders missing");
    const int width=labels.front()->minimumWidth();
    for(auto* label:labels)require(label->minimumWidth()==width&&label->maximumWidth()==width,"camera raw label columns must share one width");
}
void cameraRawTrackAnchors(){
    compositor::ui::CameraRawPanel panel(compositor::filters::CameraRawSettings{});panel.resize(440,900);panel.show();QApplication::processEvents();
    auto* temperature=sliderNamed(panel,"Temperature slider");require(temperature->isVisible(),"temperature slider must be visible");
    // A (left) and B (right) must stay at the groove ends; moving the knob only changes how much is dimmed.
    temperature->setValue(3000);const auto low=temperature->grab().toImage();
    temperature->setValue(5000);const auto mid=temperature->grab().toImage();
    const int y=temperature->height()/2;
    require(low.pixel(10,y)==mid.pixel(10,y),"track start colour moved with the knob");
    require(low.pixel(low.width()-11,y)==mid.pixel(mid.width()-11,y),"track end colour moved with the knob");
}
}
int main(int argc,char** argv){QApplication app(argc,argv);const std::map<std::string,void(*)()> cases{{"transform_typing",transformTyping},{"opacity_track_history",opacityTrack},{"brush_field_focus",brushField},{"slider_double_click",sliderDoubleClick},{"camera_raw_alignment",cameraRawAlignment},{"camera_raw_track_anchors",cameraRawTrackAnchors},{"opacity_double_click_history",opacityDoubleClick}};try{require(argc==2&&cases.contains(argv[1]),"provide named property UI case");cases.at(argv[1])();std::printf("PASS %s\n",argv[1]);return 0;}catch(const std::exception& error){std::fprintf(stderr,"FAIL %s\n",error.what());return 1;}}
