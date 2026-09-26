#include "ui/MainWindow.h"
#include <QApplication>
#include <QLineEdit>
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
void brushField(){
    Fixture f;invoke(f.window,"tool.brush");auto* opacity=number(f.window,"Brush opacity");require(opacity->minimum()==1,"source brush opacity minimum is one percent");
    opacity->setValue(5);opacity->setFocus();QTest::keyClick(opacity,Qt::Key_Down,Qt::ShiftModifier);require(opacity->value()==1,"Shift decrement clamps brush opacity to one");
    auto* diameter=number(f.window,"Brush diameter");diameter->setValue(40);diameter->setFocus();QTest::keyClick(diameter,Qt::Key_Up,Qt::ShiftModifier);require(diameter->value()==50,"real brush field must step ten pixels");
    QTest::keyClick(diameter,Qt::Key_Return);QApplication::processEvents();require(f.project.canvas->hasFocus(),"Return must give brush keyboard commands back to canvas");
    require(f.project.document==f.before&&f.project.history.undoCount()==0,"tool option editing must preserve document history");
}
}
int main(int argc,char** argv){QApplication app(argc,argv);const std::map<std::string,void(*)()> cases{{"transform_typing",transformTyping},{"opacity_track_history",opacityTrack},{"brush_field_focus",brushField}};try{require(argc==2&&cases.contains(argv[1]),"provide named property UI case");cases.at(argv[1])();std::printf("PASS %s\n",argv[1]);return 0;}catch(const std::exception& error){std::fprintf(stderr,"FAIL %s\n",error.what());return 1;}}
