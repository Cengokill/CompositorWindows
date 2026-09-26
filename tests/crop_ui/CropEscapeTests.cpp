#include "ui/MainWindow.h"
#include <QApplication>
#include <QKeyEvent>
#include <iostream>
#include <map>
#include <stdexcept>
using namespace compositor;
void require(bool value,const char*message){if(!value)throw std::runtime_error(message);}
QAction* command(MainWindow&w,const QString&id){for(auto*a:w.findChildren<QAction*>())if(a->property("commandId").toString()==id)return a;throw std::runtime_error("Missing command");}
void exercise(int mode){MainWindow w(true);Document d;d.id="crop-escape";d.width=100;d.height=80;Layer l;l.id="base";l.transform={0,0,100,80};l.raster=Raster::filled(100,80,{80,120,160,255});d.layers={l};auto&p=w.addProject(d);command(w,"tool.crop")->trigger();p.canvas->pointerDown({20,20},Qt::ControlModifier);p.canvas->pointerMove({60,50},Qt::ControlModifier);p.canvas->pointerUp({60,50},Qt::ControlModifier);auto*apply=command(w,"crop.apply");auto*cancel=command(w,"crop.cancel");require(apply->isEnabled()&&cancel->isEnabled(),"Pending crop actions unavailable");
if(mode==0){QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(&w,&escape);}else if(mode==1)p.canvas->pointerCancel();else cancel->trigger();
require(p.document==d&&p.history.undoCount()==0,"Crop cancellation changed canonical document/history");require(p.canvas->cropOverlay()==std::optional(editing::Rect{0,0,100,80}),"Crop fallback frame was not restored");require(!apply->isEnabled()&&!cancel->isEnabled(),"Crop actions remain enabled after pending crop was cleared");}
int main(int argc,char**argv){QApplication app(argc,argv);const std::map<std::string,int>cases{{"crop_escape_action_state",0},{"crop_pointer_cancel_action_state",1},{"crop_cancel_action_control",2}};if(argc>1&&!cases.contains(argv[1]))return 2;int failed=0;for(const auto&[name,mode]:cases){if(argc>1&&name!=argv[1])continue;try{exercise(mode);std::cout<<"PASS "<<name<<'\n';}catch(const std::exception&e){++failed;std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n';}}return failed?1:0;}
