#include "ui/MainWindow.h"
#include <QApplication>
#include <QAction>
#include <QCheckBox>
#include <functional>
#include <iostream>
#include <stdexcept>

using namespace compositor;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class T>T* control(MainWindow& window,const char* name){auto* value=window.findChild<T*>(name);require(value!=nullptr,"Transform control missing");return value;}
void action(MainWindow& window,const char* name){control<QAction>(window,name)->trigger();}
void down(EditorProject& project,Point p,Qt::KeyboardModifiers modifiers=Qt::NoModifier){project.canvas->pointerDown({p.x,p.y},modifiers);}
void move(EditorProject& project,Point p,Qt::KeyboardModifiers modifiers=Qt::NoModifier){project.canvas->pointerMove({p.x,p.y},modifiers);}
void up(EditorProject& project,Point p,Qt::KeyboardModifiers modifiers=Qt::NoModifier){project.canvas->pointerUp({p.x,p.y},modifiers);}
void drag(EditorProject& project,Point a,Point b,Qt::KeyboardModifiers modifiers=Qt::NoModifier){down(project,a,modifiers);move(project,b,modifiers);up(project,b,modifiers);}
std::shared_ptr<const GrayRaster> selection(){auto gray=std::make_shared<GrayRaster>();gray->width=64;gray->height=48;gray->pixels.resize(64*48);for(int y=10;y<20;++y)for(int x=10;x<20;++x)gray->pixels[size_t(y)*64+x]=255;return gray;}
Document example(){Document document;document.id=newId();document.width=64;document.height=48;Layer layer;layer.id=newId();layer.name="Transform witness";layer.transform={0,0,64,48};layer.transform.sampling=Transform::Sampling::Nearest;std::vector<Pixel> pixels(64*48,{0,0,255,255});for(int y=10;y<20;++y)for(int x=10;x<20;++x)pixels[size_t(y)*64+x]={255,0,0,255};layer.raster=Raster::fromRgba(64,48,reinterpret_cast<const uint8_t*>(pixels.data()),64*4);document.layers.push_back(layer);return document;}
Layer& originalLayer(EditorProject& project){return project.document->layers.front();}
void setup(MainWindow& window,EditorProject& project){project.canvas->zoom=10;control<QCheckBox>(window,"transformSnapping")->setChecked(false);}
void selectionTool(MainWindow& window){for(auto* item:window.findChildren<QAction*>())if(item->text()=="Marquee (M)"){item->trigger();return;}throw std::runtime_error("Marquee action missing");}
}
int main(int argc,char** argv){QApplication app(argc,argv);int passed=0,failed=0;
    auto test=[&](const char* name,const std::function<void()>& run){try{run();++passed;std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& error){++failed;std::cout<<"FAIL "<<name<<": "<<error.what()<<'\n';}};
    test("ui_persistent_transform_multiple_drags_apply_cancel",[]{
        MainWindow window(true);auto& project=window.addProject(example());setup(window,project);const auto source=originalLayer(project).raster;
        action(window,"freeTransform");drag(project,{32,24},{37,27});require(project.history.undoCount()==0&&!project.history.canUndo(),"Persistent transform committed on mouse-up");drag(project,{37,27},{40,31});require(originalLayer(project).transform.x==8&&originalLayer(project).transform.y==7,"Second gesture drifted from its draft");action(window,"applyTransform");require(project.history.undoCount()==1&&originalLayer(project).raster==source,"Transform did not close one immutable history entry");
        const auto before=project.document;action(window,"freeTransform");drag(project,{40,31},{45,36});action(window,"cancelTransform");require(project.document==before&&project.history.undoCount()==1,"Cancel failed to restore exact pending snapshot");
    });
    test("ui_floating_selection_live_stack_apply_without_ghost",[]{
        MainWindow window(true);auto document=example();document.selection=Selection{selection()};auto& project=window.addProject(document);setup(window,project);const auto sourceId=project.active;const auto before=project.document;
        action(window,"freeTransform");require(project.document->layers.size()==2&&project.active!=sourceId,"Selection was not lifted onto a floating layer");require(originalLayer(project).raster->pixel(15,15).a==0,"Selected source pixels were not cleared");
        drag(project,{15,15},{25,15});require(project.document->layers.size()==2,"Floating preview added extra layers");drag(project,{25,15},{35,15});action(window,"applyTransform");require(project.document->layers.size()==1&&project.active==sourceId,"Apply left a floating layer");require(originalLayer(project).raster->pixel(15,15).a==0,"Apply restored lifted source pixels");require(originalLayer(project).raster->pixel(25,15)==Pixel{0,0,255,255},"Earlier floating position left a ghost");require(originalLayer(project).raster->pixel(35,15)==Pixel{255,0,0,255},"Final floating pixels absent");require(project.document->selection->coverage->pixel(35,15)==255&&project.document->selection->coverage->pixel(15,15)==0,"Selection did not move with applied pixels");require(project.history.undoCount()==1,"Floating transform had multiple undo entries");auto undo=project.history.undo();require(undo&&undo->document==before,"Floating transform undo did not restore immutable original");
    });
    test("ui_floating_cancel_and_unchanged_soft_selection",[]{
        MainWindow window(true);auto document=example();document.selection=Selection{selection()};auto& project=window.addProject(document);setup(window,project);const auto before=project.document;const auto id=project.active;
        action(window,"freeTransform");drag(project,{15,15},{23,15});project.canvas->pointerCancel();require(project.document==before&&project.active==id&&project.history.undoCount()==0,"Floating Escape/capture cancel failed");action(window,"freeTransform");action(window,"applyTransform");require(project.document==before&&project.history.undoCount()==0,"Unchanged floating apply added a seam or history");
    });
    test("ui_selected_pixel_ctrl_move_and_alt_duplicate",[]{
        MainWindow window(true);auto document=example();document.selection=Selection{selection()};auto& project=window.addProject(document);setup(window,project);selectionTool(window);
        drag(project,{15,15},{25,15},Qt::ControlModifier|Qt::AltModifier);require(project.document->layers.size()==1,"Pixel duplicate duplicated the layer");require(originalLayer(project).raster->pixel(15,15)==Pixel{255,0,0,255}&&originalLayer(project).raster->pixel(25,15)==Pixel{255,0,0,255},"Ctrl+Alt did not duplicate selected pixels");require(project.history.undoName()=="Duplicate Pixels"&&project.history.undoCount()==1,"Pixel duplicate history");
        drag(project,{25,15},{35,15},Qt::ControlModifier);require(originalLayer(project).raster->pixel(25,15).a==0&&originalLayer(project).raster->pixel(35,15)==Pixel{255,0,0,255},"Ctrl did not move selected pixels");require(project.history.undoName()=="Move Pixels"&&project.history.undoCount()==2,"Pixel move history");
    });
    test("ui_distort_apply_cancel_and_layer_duplicate",[]{
        MainWindow window(true);auto& project=window.addProject(example());setup(window,project);const auto before=project.document;
        action(window,"distortTransform");drag(project,{0,0},{8,3});require(project.history.undoCount()==0,"Distort committed on mouse-up");action(window,"cancelTransform");require(project.document==before,"Distort cancel changed original");
        action(window,"distortTransform");drag(project,{0,0},{8,3});action(window,"applyTransform");require(project.history.undoCount()==1&&originalLayer(project).raster!=before->layers.front().raster,"Distort Apply did not rasterize");require(originalLayer(project).raster->pixel(0,0).a==0,"Distort failed to cut away old corner");
        const auto count=project.document->layers.size();down(project,{32,24},Qt::AltModifier);up(project,{32,24},Qt::AltModifier);require(project.document->layers.size()==count,"Alt click duplicated without movement");drag(project,{32,24},{36,24},Qt::AltModifier);require(project.document->layers.size()==count+1,"Alt drag failed to duplicate layer");require(project.history.undoCount()==2,"Layer duplicate history was not one step");
    });
    test("ui_linked_and_unlinked_mask_transform_scope",[]{
        MainWindow window(true);auto document=example();document.layers.front().mask=Mask{selection()};auto& project=window.addProject(document);setup(window,project);project.maskSelected=true;
        drag(project,{32,24},{37,24});require(originalLayer(project).transform.x==5&&originalLayer(project).mask->linked,"Linked selected mask did not move with image");
        originalLayer(project).mask->linked=false;const auto imageTransform=originalLayer(project).transform;drag(project,{37,24},{40,24});require(originalLayer(project).transform==imageTransform&&originalLayer(project).mask->placement->x==8,"Unlinked selected mask moved the image");
    });
    std::cout<<"{\"suite\":\"transform_ui\",\"passed\":"<<passed<<",\"failed\":"<<failed<<",\"mac_differential\":false,\"visual_acceptance\":false}\n";return failed?1:0;
}
