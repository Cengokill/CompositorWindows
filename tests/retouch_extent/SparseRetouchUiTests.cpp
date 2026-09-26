#include "ui/MainWindow.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <cstdio>
#include <map>
#include <stdexcept>
using namespace compositor;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class T>T* control(MainWindow& window,const char* name){auto* object=window.findChild<T*>(name);require(object!=nullptr,"missing retouch control");return object;}
void command(MainWindow& window,const char* id){for(auto* action:window.findChildren<QAction*>())if(action->property("commandId")==id){require(action->isEnabled(),"retouch history command disabled");action->trigger();return;}throw std::runtime_error("missing retouch command");}
void exercise(int mode){
    MainWindow window(true);Document d;d.id=newId();d.width=d.height=30000;Layer l;l.id=newId();l.transform={15000,12000,64,64};
    std::vector<Pixel> pixels(64*64);for(int y=0;y<64;++y)for(int x=0;x<64;++x)pixels[size_t(y)*64+x]={uint8_t(x*3),uint8_t(y*3),20,220};
    l.raster=Raster::filled(64,64)->replacing(0,0,64,64,pixels.data(),64);d.layers.push_back(l);
    if(mode==7){auto overlay=l;overlay.id=newId();overlay.raster=Raster::filled(64,64,{0,80,0,120});d.layers.push_back(overlay);}
    auto& p=window.addProject(d);p.active=l.id;p.selected={l.id};
    control<QAction>(window,mode==0||mode==7?"retouchClone":mode>=4?"retouchHeal":"retouchSmear")->trigger();
    if(mode>=1&&mode<=3)control<QComboBox>(window,"retouchSmearMode")->setCurrentIndex(mode==1?1:mode==2?2:0);
    if(mode>=4&&mode<=6)control<QComboBox>(window,"retouchHealingMode")->setCurrentIndex(mode-4);
    control<QDoubleSpinBox>(window,"retouchDiameter")->setValue(12);control<QDoubleSpinBox>(window,"retouchHardness")->setValue(50);control<QDoubleSpinBox>(window,"retouchOpacity")->setValue(60);
    if(mode==0||mode==7){control<QCheckBox>(window,"retouchAllLayers")->setChecked(mode==7);p.canvas->pointerDown({15014,12025},Qt::AltModifier);p.canvas->pointerUp({15014,12025},Qt::AltModifier);}
    const auto before=p.document;
    const auto stroke=[&]{p.canvas->pointerDown({15028,12025},{});p.canvas->pointerMove({15034,12028},{});};
    stroke();require(bool(p.retouchPreview),"large canvas did not publish retouch preview");require(p.document==before&&p.history.undoCount()==0,"retouch preview mutated canonical document");
    const auto preview=p.retouchPreview;const auto frame=p.canvas->viewportProvider(15016,12016,32,32,1);require(frame.raster&&frame.unitsPerPixel==1&&frame.raster->tiles.size()<=4&&frame.documentX<=15016&&frame.documentY<=12016&&frame.documentX+frame.raster->width>=15048&&frame.documentY+frame.raster->height>=12048,"large retouch viewport is unavailable"); const auto expected=SoftwareRenderer(preview).render(*p.document,15016,12016,32,32);for(int y=0;y<32;++y)for(int x=0;x<32;++x)require(frame.raster->pixel(15016+x-int(frame.documentX),12016+y-int(frame.documentY))==expected->pixel(x,y),"actual viewport pixels differ from owned retouch preview");
    p.canvas->pointerCancel();require(p.document==before&&!p.retouchPreview&&p.history.undoCount()==0,"large retouch cancel failed");
    require(SoftwareRenderer(preview).render(*p.document,15016,12016,32,32)->width==32,"cancel invalidated published preview");
    stroke();p.canvas->pointerUp({15034,12028},{});require(!p.retouchPreview&&p.history.undoCount()==1,"large retouch commit did not create one history entry");
    const auto after=p.document;require(after!=before,"retouch made no change");command(window,"edit.undo");require(p.document==before,"large retouch Undo differs from original");command(window,"edit.redo");require(p.document==after,"large retouch Redo differs from committed snapshot");
}
}
int main(int argc,char**argv){QApplication app(argc,argv);const std::map<std::string,int> cases{{"clone",0},{"blur",1},{"smudge",2},{"liquify",3},{"heal_content",4},{"heal_texture",5},{"heal_proximity",6},{"clone_all",7}};try{require(argc==2&&cases.contains(argv[1]),"provide large retouch UI case");exercise(cases.at(argv[1]));std::printf("PASS %s\n",argv[1]);return 0;}catch(const std::exception&e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
