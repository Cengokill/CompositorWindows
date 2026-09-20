#include "ui/MainWindow.h"
#include <QApplication>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QStatusBar>
#include <functional>
#include <iostream>
#include <stdexcept>

using namespace compositor;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class T>T* control(MainWindow& window,const char* name){auto* value=window.findChild<T*>(name);require(value!=nullptr,"Retouch control missing");return value;}
void tool(MainWindow& window,const char* name){control<QAction>(window,name)->trigger();}
void tip(MainWindow& window,double diameter=8,double hardness=100,double opacity=100){control<QDoubleSpinBox>(window,"retouchDiameter")->setValue(diameter);control<QDoubleSpinBox>(window,"retouchHardness")->setValue(hardness);control<QDoubleSpinBox>(window,"retouchOpacity")->setValue(opacity);}
void down(EditorProject& project,Point p,Qt::KeyboardModifiers modifiers=Qt::NoModifier){project.canvas->pointerDown({p.x,p.y},modifiers);}
void move(EditorProject& project,Point p,Qt::KeyboardModifiers modifiers=Qt::NoModifier){project.canvas->pointerMove({p.x,p.y},modifiers);}
void up(EditorProject& project,Point p,Qt::KeyboardModifiers modifiers=Qt::NoModifier){project.canvas->pointerUp({p.x,p.y},modifiers);}
void click(EditorProject& project,Point p,Qt::KeyboardModifiers modifiers=Qt::NoModifier){down(project,p,modifiers);up(project,p,modifiers);}
std::shared_ptr<const Raster> image(int width,int height,const std::function<Pixel(int,int)>& sample){std::vector<Pixel> pixels(size_t(width)*height);for(int y=0;y<height;++y)for(int x=0;x<width;++x)pixels[size_t(y)*width+x]=sample(x,y);return Raster::fromRgba(width,height,reinterpret_cast<const uint8_t*>(pixels.data()),size_t(width)*4);}
std::shared_ptr<const GrayRaster> gray(int width,int height,const std::function<uint8_t(int,int)>& sample){auto value=std::make_shared<GrayRaster>();value->width=width;value->height=height;value->pixels.resize(size_t(width)*height);for(int y=0;y<height;++y)for(int x=0;x<width;++x)value->pixels[size_t(y)*width+x]=sample(x,y);return value;}
Document example(){Document document;document.id=newId();document.width=80;document.height=40;Layer layer;layer.id=newId();layer.name="Clone witness";layer.transform={0,0,80,40};layer.raster=image(80,40,[](int x,int){return x>=10&&x<20?Pixel{0,255,0,255}:x>=20&&x<30?Pixel{255,0,0,255}:Pixel{0,0,255,255};});document.layers.push_back(layer);return document;}
Layer& target(EditorProject& project){for(auto& layer:project.document->layers)if(layer.id==project.active)return layer;throw std::runtime_error("Active layer missing");}
}
int main(int argc,char** argv){
    QApplication app(argc,argv);int passed=0,failed=0;
    auto test=[&](const char* name,const std::function<void()>& run){try{run();++passed;std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& error){++failed;std::cout<<"FAIL "<<name<<": "<<error.what()<<'\n';}};
    test("ui_clone_source_alignment_history_cancel",[]{
        MainWindow window(true);auto& project=window.addProject(example());tool(window,"retouchClone");tip(window);
        click(project,{60,20});require(project.history.undoCount()==0,"Missing source painted");require(window.statusBar()->currentMessage().contains("Alt-click"),"Missing source instruction absent");
        click(project,{15,20},Qt::AltModifier);require(project.history.undoCount()==0,"Source point entered history");
        const auto original=target(project).raster;click(project,{60,20});require(target(project).raster->pixel(60,20)==Pixel{0,255,0,255},"First UI clone sample");require(original->pixel(60,20)==Pixel{0,0,255,255},"UI mutated original");require(project.history.undoCount()==1&&project.history.undoName()=="Clone Stamp","Clone history transaction");
        click(project,{66,20});require(target(project).raster->pixel(66,20)==Pixel{255,0,0,255},"UI aligned source offset reset");
        control<QCheckBox>(window,"retouchAligned")->setChecked(false);click(project,{50,10});require(target(project).raster->pixel(50,10)==Pixel{0,255,0,255},"UI nonaligned sample");
        const auto beforeCancel=project.document;down(project,{40,10});move(project,{45,10});project.canvas->pointerCancel();require(project.document==beforeCancel&&project.history.undoCount()==3,"Cancel failed to restore exact snapshot");
        auto undo=project.history.undo();require(undo&&undo->document->layers.front().raster->pixel(50,10)==Pixel{0,0,255,255},"Single-step undo failed");
    });
    test("ui_clone_all_layers_selection_and_frozen_sample",[]{
        MainWindow window(true);auto document=example();Layer upper;upper.id=newId();upper.transform={0,0,80,40};upper.opacity=.5;upper.raster=Raster::filled(80,40,{255,0,0,255});document.layers.push_back(upper);auto& project=window.addProject(document);project.active=document.layers.front().id;project.selected={project.active};
        tool(window,"retouchClone");tip(window);control<QCheckBox>(window,"retouchAllLayers")->setChecked(true);click(project,{15,20},Qt::AltModifier);down(project,{60,20});
        const auto sampled=target(project).raster->pixel(60,20);require(sampled==Pixel{128,127,0,255},"All-layer composite was not captured");
        const auto preview=target(project).raster;move(project,{61,20});require(preview->pixel(60,20)==sampled,"Published UI preview mutated");up(project,{61,20});require(project.history.undoCount()==1,"Move/up produced multiple history entries");
        project.document->selection=Selection{gray(80,40,[](int,int){return uint8_t{0};})};const auto original=project.document;click(project,{40,20});require(project.document==original&&project.history.undoCount()==1,"Explicit empty selection retouched");
        project.document->selection=Selection{gray(80,40,[](int,int){return uint8_t{128};})};control<QCheckBox>(window,"retouchAllLayers")->setChecked(false);click(project,{15,20},Qt::AltModifier);click(project,{40,20});require(target(project).raster->pixel(40,20)==Pixel{0,128,127,255},"UI selection applied twice or omitted");
    });
    test("ui_mask_and_hidden_parent_eligibility",[]{
        MainWindow window(true);auto document=example();document.layers.front().mask=Mask{gray(80,40,[](int x,int){return uint8_t(x<40?0:255);})};auto& project=window.addProject(document);project.maskSelected=true;
        tool(window,"retouchClone");tip(window);click(project,{15,20},Qt::AltModifier);const auto original=project.document;click(project,{60,20});require(project.document==original&&project.history.undoCount()==0,"Clone edited a mask");
        tool(window,"retouchSmear");tip(window,16);click(project,{40,20});require(project.history.undoCount()==0,"Liquify edited a mask");
        control<QComboBox>(window,"retouchSmearMode")->setCurrentIndex(1);click(project,{40,20});require(target(project).mask->raster->pixel(40,20)>0&&target(project).mask->raster->pixel(40,20)<255,"Blur mask did not soften edge");require(project.history.undoCount()==1&&project.history.undoName()=="Blur Mask","Blur mask history");
        Layer group;group.id=newId();group.group=true;group.visible=false;target(project).parentId=group.id;project.document->layers.insert(project.document->layers.begin(),group);const auto hidden=project.document;click(project,{38,20});require(project.document==hidden&&project.history.undoCount()==1,"Hidden folder mask target accepted");
    });
    test("ui_retouch_families_submodes_and_healing",[]{
        MainWindow window(true);auto& project=window.addProject(example());tool(window,"retouchClone");tip(window,12,50,25);tool(window,"retouchSmear");tip(window,20,30,75);tool(window,"retouchClone");require(control<QDoubleSpinBox>(window,"retouchDiameter")->value()==12&&control<QDoubleSpinBox>(window,"retouchOpacity")->value()==25,"Clone tip family lost");tool(window,"retouchSmear");require(control<QDoubleSpinBox>(window,"retouchDiameter")->value()==20&&control<QDoubleSpinBox>(window,"retouchHardness")->value()==30,"Smear tip family lost");
        tool(window,"retouchHeal");tip(window,4,100,100);for(int mode=0;mode<3;++mode){control<QComboBox>(window,"retouchHealingMode")->setCurrentIndex(mode);const auto before=target(project).raster;down(project,{15,20});require(target(project).raster->pixel(15,20).g<before->pixel(15,20).g,"Healing wash absent");up(project,{15,20});require(project.history.undoName()=="Spot Healing","Healing submode not committed");}
    });
    test("ui_warp_live_selection_and_exact_cancel",[]{
        MainWindow window(true);auto document=example();document.layers.front().raster=image(80,40,[](int x,int){return Pixel{uint8_t(x*3),0,0,255};});document.layers.front().mask=Mask{gray(80,40,[](int,int){return uint8_t{255};})};document.selection=Selection{gray(80,40,[](int x,int){return uint8_t(x<5?255:0);})};auto& project=window.addProject(document);tool(window,"retouchSmear");control<QComboBox>(window,"retouchSmearMode")->setCurrentIndex(2);tip(window,6,100,100);
        const auto original=project.document;down(project,{10,20});move(project,{11,20});require(target(project).raster->pixel(11,20).r==30,"Live warp was selection clipped");require(target(project).mask->placement==original->layers.front().transform,"Warp preview mask placement changed");up(project,{11,20});require(project.document==original&&project.history.undoCount()==0,"Outside-selection warp changed final snapshot");
        project.document->selection.reset();const auto beforeCancel=project.document;down(project,{10,20});move(project,{12,20});project.canvas->pointerCancel();require(project.document==beforeCancel&&project.history.undoCount()==0,"Warp cancel did not restore original metadata and pixels");
        click(project,{10,20});require(project.history.undoCount()==0,"Smudge click created history");
    });
    test("ui_shift_axis_and_project_local_source",[]{
        MainWindow window(true);auto& first=window.addProject(example());tool(window,"retouchClone");tip(window,2);click(first,{15,20},Qt::AltModifier);down(first,{50,20});move(first,{53,21},Qt::ShiftModifier);move(first,{56,26},Qt::ShiftModifier);up(first,{56,26},Qt::ShiftModifier);require(target(first).raster->pixel(56,26)==Pixel{0,0,255,255},"Shift axis changed after first movement");const auto endpoint=target(first).raster->pixel(56,20);if(endpoint!=Pixel{255,0,0,255})std::cout<<"Shift endpoint RGBA "<<int(endpoint.r)<<','<<int(endpoint.g)<<','<<int(endpoint.b)<<','<<int(endpoint.a)<<'\n';require(endpoint.r>0&&endpoint.g==0&&endpoint.b<255&&endpoint.a==255,"Shift axis did not keep horizontal endpoint");
        auto& second=window.addProject(example());require(!second.cloneAlignment.source(),"Clone source leaked across projects");click(second,{60,20});require(second.history.undoCount()==0,"Second project inherited source");
    });
    std::cout<<"{\"suite\":\"retouch_ui\",\"passed\":"<<passed<<",\"failed\":"<<failed<<",\"mac_differential\":false,\"visual_acceptance\":false}\n";
    return failed?1:0;
}
