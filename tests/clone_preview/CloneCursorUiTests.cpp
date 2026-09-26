#include "ui/MainWindow.h"
#include <QApplication>
#include <iostream>
#include <map>
using namespace compositor;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void run(const std::string& name){
 MainWindow w(true);Document d;d.id="clone-cursor-shape";d.width=d.height=64;Layer l;l.id="base";l.transform={0,0,64,64};l.raster=Raster::filled(64,64,{80,120,160,255});d.layers={l};auto&p=w.addProject(d);auto&c=*p.canvas;
 QAction* clone=nullptr;for(auto*a:w.findChildren<QAction*>())if(a->property("commandId").toString()=="tool.clone"&&a->isVisible()&&a->isEnabled()){clone=a;break;}require(clone&&clone->isEnabled(),"Clone action unavailable");clone->trigger();
 if(name!="without_source_crosshair"){c.pointerDown({20,20},Qt::AltModifier);c.pointerUp({20,20},Qt::AltModifier);}
 c.pointerHover({100,100},name=="alt_source_crosshair"?Qt::AltModifier:Qt::NoModifier);
 if(name=="leave_restores_arrow")c.pointerLeave();
 const auto expected=name=="with_source_hidden"?Qt::BlankCursor:name=="leave_restores_arrow"?Qt::ArrowCursor:Qt::CrossCursor;
 std::cout<<"actual_cursor="<<int(c.cursor().shape())<<" expected_cursor="<<int(expected)<<'\n';
 require(c.cursor().shape()==expected,"Clone native cursor differs from resetCursorRects");require(p.document==d&&p.history.undoCount()==0,"Cursor policy edited document/history");
}
}
int main(int argc,char**argv){QApplication app(argc,argv);const std::array<const char*,4> names{"without_source_crosshair","with_source_hidden","alt_source_crosshair","leave_restores_arrow"};if(argc>1&&std::find(names.begin(),names.end(),std::string(argv[1]))==names.end())return 2;int failed=0;for(auto name:names)if(argc==1||name==std::string(argv[1]))try{run(name);std::cout<<"PASS "<<name<<'\n';}catch(const std::exception&e){++failed;std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}return failed?1:0;}
