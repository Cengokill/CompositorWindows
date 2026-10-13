#include "ui/MainWindow.h"
#include "ui/ViewportCompositeJob.h"
#include <QApplication>
#include <QAction>
#include <QCheckBox>
#include <QElapsedTimer>
#include <atomic>
#include <cmath>
#include <functional>
#include <iostream>
#include <memory>
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
QAction* historyAction(MainWindow& window,QKeySequence shortcut,const char* label){for(auto* item:window.findChildren<QAction*>())if(item->shortcut()==shortcut||item->shortcuts().contains(shortcut))return item;throw std::runtime_error(label);}
void showCanvas(MainWindow& window){window.show();QCoreApplication::processEvents();require(window.canvas()&&window.canvas()->width()>0&&window.canvas()->height()>0,"Canvas has no size");}
void pump(int budgetMs){QElapsedTimer timer;timer.start();while(timer.elapsed()<budgetMs)QCoreApplication::processEvents(QEventLoop::AllEvents,20);}
ui::ViewportCompositeJob* compositeJob(MainWindow& window){for(auto* child:window.findChildren<QObject*>())if(auto* job=dynamic_cast<ui::ViewportCompositeJob*>(child))return job;throw std::runtime_error("Viewport composite job missing");}
void waitPreview(EditorProject& project,const std::string& layerId){
    QElapsedTimer timer;timer.start();
    while(timer.elapsed()<4000){QCoreApplication::processEvents(QEventLoop::AllEvents,20);if(project.canvas->hasMovePreview()&&project.canvas->previewLayerId()==layerId)return;}
    throw std::runtime_error("Timed out waiting for the move preview");
}
std::shared_ptr<const GrayRaster> solidMask(int width,int height){auto gray=std::make_shared<GrayRaster>();gray->width=width;gray->height=height;gray->pixels.assign(size_t(width*height),255);return gray;}
void expectShiftedPlate(const Document& document,const std::string& id,int dx,int dy,double units){
    const Layer* moving=nullptr;for(const auto& layer:document.layers)if(layer.id==id)moving=&layer;require(moving!=nullptr,"Plate layer missing");
    const auto bounds=ui::movingLayerPlateBounds(*moving,units);require(bounds.valid,"Moving layer plate bounds were rejected");
    const int wide=std::max(1,int(std::ceil(document.width/units))),high=std::max(1,int(std::ceil(document.height/units)));
    Document moved=document;for(auto& layer:moved.layers)if(layer.id==id){layer.transform.x+=dx;layer.transform.y+=dy;}
    auto full=SoftwareRenderer().renderScaled(moved,0,0,wide,high,units);
    auto hidden=document;for(auto& layer:hidden.layers)if(layer.id==id)layer.visible=false;
    auto back=SoftwareRenderer().renderScaled(hidden,0,0,wide,high,units);
    auto alone=document;for(auto& layer:alone.layers)if(layer.id!=id)layer.visible=false;
    auto plate=SoftwareRenderer().renderScaled(alone,bounds.x,bounds.y,bounds.pixelsWide,bounds.pixelsHigh,units);
    for(int y=0;y<high;++y)for(int x=0;x<wide;++x){
        Pixel under=back->pixel(x,y);
        const int sx=int(std::floor(((x+.5)*units-(bounds.x+dx))/units));
        const int sy=int(std::floor(((y+.5)*units-(bounds.y+dy))/units));
        if(sx>=0&&sy>=0&&sx<bounds.pixelsWide&&sy<bounds.pixelsHigh)under=blendPixel(under,plate->pixel(sx,sy),Blend::Normal);
        if(under!=full->pixel(x,y))throw std::runtime_error("Shifted layer plate did not match the full composite at "+std::to_string(x)+","+std::to_string(y)+" units "+std::to_string(units));
    }
}
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
    test("ui_move_keeps_preview_until_composite",[]{
        MainWindow window(true);auto& project=window.addProject(example());setup(window,project);showCanvas(window);
        auto gate=std::make_shared<std::atomic_bool>(false);compositeJob(window)->setProceedGate(gate);
        drag(project,{32,24},{40,30});
        require(project.history.undoCount()==1&&project.history.undoName()=="Transform Layer","Plain move did not commit one history step");
        require(originalLayer(project).transform.x==8&&originalLayer(project).transform.y==6,"Plain move did not commit the release position");
        waitPreview(project,originalLayer(project).id);
        require(project.canvas->hasMovePreview(),"Move preview was cleared before the composite was ready");
        gate->store(true);pump(3000);
        require(!project.canvas->hasMovePreview()&&!project.canvas->compositeHeld(),"Composite did not replace the move preview");
        require(project.composite.lastViewportComposedTiles()>0,"Finished composite was not installed");
        require(originalLayer(project).transform.x==8&&project.history.undoCount()==1,"Composite changed the committed move");
        historyAction(window,QKeySequence::Undo,"Undo action missing")->trigger();require(originalLayer(project).transform.x==0&&originalLayer(project).transform.y==0&&project.history.undoCount()==0,"Undo did not restore the single move");
        historyAction(window,QKeySequence("Ctrl+Y"),"Redo action missing")->trigger();require(originalLayer(project).transform.x==8&&project.history.undoCount()==1,"Redo did not restore the single move");
    });
    test("ui_move_undo_discards_stale_composite",[]{
        MainWindow window(true);auto& project=window.addProject(example());setup(window,project);showCanvas(window);
        auto gate=std::make_shared<std::atomic_bool>(false);compositeJob(window)->setProceedGate(gate);
        const auto before=*project.document;drag(project,{32,24},{42,28});
        waitPreview(project,originalLayer(project).id);
        require(project.canvas->hasMovePreview()&&project.history.undoCount()==1,"Move was not committed behind its preview");
        historyAction(window,QKeySequence::Undo,"Undo action missing")->trigger();require(*project.document==before&&!project.canvas->hasMovePreview(),"Undo did not restore the document and clear the preview");
        gate->store(true);pump(3000);require(*project.document==before&&project.history.undoCount()==0,"Stale composite overwrote undo");
    });
    test("ui_masked_move_coalesces_one_history_step",[]{
        MainWindow window(true);auto document=example();document.layers.front().mask=Mask{selection()};auto& project=window.addProject(document);setup(window,project);showCanvas(window);
        auto gate=std::make_shared<std::atomic_bool>(false);compositeJob(window)->setProceedGate(gate);
        const auto before=*project.document;drag(project,{32,24},{40,28});
        waitPreview(project,originalLayer(project).id);
        require(project.canvas->hasMovePreview(),"Masked move left the pixels behind the frame");
        require(project.history.undoCount()==1&&originalLayer(project).transform.x==8,"Masked move did not commit one step");
        historyAction(window,QKeySequence::Undo,"Undo action missing")->trigger();require(*project.document==before,"Masked move undo failed");
        gate->store(true);pump(3000);require(*project.document==before&&project.history.undoCount()==0,"Stale masked composite overwrote undo");
    });
    test("ui_covered_move_keeps_preview",[]{
        MainWindow window(true);auto document=example();Layer cover;cover.id=newId();cover.name="Cover";cover.transform={0,0,8,8};cover.transform.sampling=Transform::Sampling::Nearest;cover.raster=Raster::filled(8,8,{0,255,0,255});document.layers.push_back(std::move(cover));
        auto& project=window.addProject(document);setup(window,project);showCanvas(window);control<QCheckBox>(window,"transformAutoSelect")->setChecked(false);
        project.active=project.document->layers.front().id;project.selected={project.active};
        auto gate=std::make_shared<std::atomic_bool>(false);compositeJob(window)->setProceedGate(gate);
        drag(project,{32,24},{40,30});
        waitPreview(project,project.document->layers.front().id);
        require(project.canvas->hasMovePreview(),"Covered layer move did not keep the pixels with the frame");
        require(project.history.undoCount()==1&&originalLayer(project).transform.x==8,"Covered layer move did not commit one step");
        gate->store(true);pump(3000);require(!project.canvas->hasMovePreview(),"Covered layer preview survived the composite");
    });
    test("translated_mask_preview_matches_composite",[]{
        Document document=example();auto mask=std::make_shared<GrayRaster>();mask->width=64;mask->height=48;mask->pixels.assign(64*48,0);for(int y=10;y<20;++y)for(int x=10;x<20;++x)mask->pixels[size_t(y)*64+x]=255;
        document.layers.front().mask=Mask{mask};Layer floor;floor.id=newId();floor.name="Floor";floor.transform={0,0,64,48};floor.transform.sampling=Transform::Sampling::Nearest;floor.raster=Raster::filled(64,48,{0,0,80,255});document.layers.insert(document.layers.begin(),std::move(floor));
        const std::string id=document.layers.back().id;const int dx=5,dy=3;
        Document moved=document;for(auto& layer:moved.layers)if(layer.id==id){layer.transform.x+=dx;layer.transform.y+=dy;}
        auto full=SoftwareRenderer().renderScaled(moved,0,0,64,48,1);
        auto hide=[&](Document source,const std::string& only){if(only.empty()){for(auto& layer:source.layers)if(layer.id==id)layer.visible=false;}else for(auto& layer:source.layers)if(layer.id!=only)layer.visible=false;return source;};
        auto back=SoftwareRenderer().renderScaled(hide(document,""),0,0,64,48,1);
        auto plate=SoftwareRenderer().renderScaled(hide(document,id),0,0,64,48,1);
        for(int y=0;y<48;++y)for(int x=0;x<64;++x){Pixel under=back->pixel(x,y);const int sx=x-dx,sy=y-dy;if(sx>=0&&sy>=0&&sx<64&&sy<48)under=blendPixel(under,plate->pixel(sx,sy),Blend::Normal);require(under==full->pixel(x,y),"Shifted masked layer did not match the canonical composite");}
    });
    test("ui_second_drag_keeps_its_own_preview",[]{
        MainWindow window(true);auto document=example();Layer other;other.id=newId();other.name="Second";other.transform={40,0,16,16};other.transform.sampling=Transform::Sampling::Nearest;other.raster=Raster::filled(16,16,{0,255,0,255});document.layers.push_back(std::move(other));
        auto& project=window.addProject(document);setup(window,project);showCanvas(window);control<QCheckBox>(window,"transformAutoSelect")->setChecked(false);
        auto gate=std::make_shared<std::atomic_bool>(false);compositeJob(window)->setProceedGate(gate);
        const auto firstId=project.document->layers.front().id;const auto secondId=project.document->layers.back().id;
        project.active=firstId;project.selected={firstId};drag(project,{8,40},{16,40});waitPreview(project,firstId);
        project.active=secondId;project.selected={secondId};down(project,{48,8});move(project,{52,14});waitPreview(project,secondId);
        gate->store(true);pump(3000);require(project.canvas->previewLayerId()==secondId,"The first layer's composite replaced the second layer's preview");
        up(project,{52,14});require(project.document->layers.back().transform.x==44&&project.document->layers.back().transform.y==6,"Second drag did not commit its own translation");
    });
    test("ui_cancel_then_other_layer_discards_stale_preview",[]{
        MainWindow window(true);auto document=example();Layer other;other.id=newId();other.name="Second";other.transform={40,0,16,16};other.transform.sampling=Transform::Sampling::Nearest;other.raster=Raster::filled(16,16,{0,255,0,255});document.layers.push_back(std::move(other));
        auto& project=window.addProject(document);setup(window,project);showCanvas(window);control<QCheckBox>(window,"transformAutoSelect")->setChecked(false);
        auto gate=std::make_shared<std::atomic_bool>(false);compositeJob(window)->setProceedGate(gate);
        const auto firstId=project.document->layers.front().id;const auto secondId=project.document->layers.back().id;
        project.active=firstId;project.selected={firstId};down(project,{8,40});move(project,{18,40});waitPreview(project,firstId);
        project.canvas->pointerCancel();require(!project.canvas->hasMovePreview()&&project.document->layers.front().transform.x==0,"Cancel left the first layer preview in place");
        project.active=secondId;project.selected={secondId};drag(project,{48,8},{56,12});waitPreview(project,secondId);
        historyAction(window,QKeySequence::Undo,"Undo action missing")->trigger();require(!project.canvas->hasMovePreview()&&project.document->layers.back().transform.x==40,"Undo restored a stale move preview");
        historyAction(window,QKeySequence("Ctrl+Y"),"Redo action missing")->trigger();require(!project.canvas->hasMovePreview()&&project.document->layers.back().transform.x==48,"Redo resurrected another layer's preview");
    });
    test("ui_ineligible_layer_does_not_reuse_preview",[]{
        MainWindow window(true);auto document=example();Layer other;other.id=newId();other.name="Multiply";other.blend=Blend::Multiply;other.transform={40,0,16,16};other.transform.sampling=Transform::Sampling::Nearest;other.raster=Raster::filled(16,16,{0,255,0,255});document.layers.insert(document.layers.begin(),std::move(other));
        auto& project=window.addProject(document);setup(window,project);showCanvas(window);control<QCheckBox>(window,"transformAutoSelect")->setChecked(false);
        auto gate=std::make_shared<std::atomic_bool>(false);compositeJob(window)->setProceedGate(gate);
        const auto multiplyId=project.document->layers.front().id;const auto imageId=project.document->layers.back().id;
        project.active=imageId;project.selected={imageId};drag(project,{8,40},{14,40});waitPreview(project,imageId);
        project.active=multiplyId;project.selected={multiplyId};down(project,{48,8});move(project,{54,10});pump(400);require(!project.canvas->hasMovePreview(),"An ineligible layer kept the previous layer's preview");
        up(project,{54,10});
    });
    test("masked_plate_includes_pixels_outside_the_viewport",[]{
        Document document;document.id=newId();document.width=64;document.height=48;
        Layer floor;floor.id=newId();floor.name="Floor";floor.transform={0,0,64,48};floor.transform.sampling=Transform::Sampling::Nearest;floor.raster=Raster::filled(64,48,{0,0,80,255});
        Layer moving;moving.id=newId();moving.name="Partial";moving.transform={-12,8,16,16};moving.transform.sampling=Transform::Sampling::Nearest;moving.raster=Raster::filled(16,16,{255,0,0,255});moving.mask=Mask{solidMask(16,16)};
        const auto id=moving.id;document.layers.push_back(std::move(floor));document.layers.push_back(std::move(moving));
        const auto bounds=ui::movingLayerPlateBounds(document.layers.back(),1);require(bounds.valid&&bounds.x<0,"Plate bounds stayed inside the viewport");
        expectShiftedPlate(document,id,20,4,1);expectShiftedPlate(document,id,20,4,.5);
        document.layers.back().transform={8,8,48,48,30};document.layers.back().transform.sampling=Transform::Sampling::Smooth;document.layers.back().raster=Raster::filled(16,16,{255,0,0,255});
        const auto rotated=ui::movingLayerPlateBounds(document.layers.back(),.25);require(rotated.valid&&rotated.x<8,"Rotated plate did not cover the layer");
        const int wide=std::max(1,int(std::ceil(document.width/.25))),high=std::max(1,int(std::ceil(document.height/.25)));
        Document moved=document;moved.layers.back().transform.x+=6;moved.layers.back().transform.y+=3;
        auto full=SoftwareRenderer().renderScaled(moved,0,0,wide,high,.25);
        auto hidden=document;hidden.layers.back().visible=false;auto back=SoftwareRenderer().renderScaled(hidden,0,0,wide,high,.25);
        auto alone=document;alone.layers.front().visible=false;auto plate=SoftwareRenderer().renderScaled(alone,rotated.x,rotated.y,rotated.pixelsWide,rotated.pixelsHigh,.25);
        auto solid=[&](int x,int y){if(x<0||y<0||x>=wide||y>=high)return false;const auto pixel=full->pixel(x,y);return pixel.r>200&&pixel.g<30&&pixel.b<30;};
        int holes=0;int hx=-1,hy=-1,hsx=-1,hsy=-1;Pixel hunder{},hexpected{};
        for(int y=0;y<high;++y)for(int x=0;x<wide;++x){Pixel under=back->pixel(x,y);const int sx=int(std::floor(((x+.5)*.25-(rotated.x+6))/.25));const int sy=int(std::floor(((y+.5)*.25-(rotated.y+3))/.25));if(sx>=0&&sy>=0&&sx<rotated.pixelsWide&&sy<rotated.pixelsHigh)under=blendPixel(under,plate->pixel(sx,sy),Blend::Normal);if(solid(x,y)&&solid(x-2,y)&&solid(x+2,y)&&solid(x,y-2)&&solid(x,y+2)&&under.r<40){if(!holes){hx=x;hy=y;hsx=sx;hsy=sy;hunder=under;hexpected=full->pixel(x,y);}++holes;}}
        if(holes)throw std::runtime_error("Rotated plate holes="+std::to_string(holes)+" at "+std::to_string(hx)+","+std::to_string(hy)+" sample "+std::to_string(hsx)+","+std::to_string(hsy)+" bounds "+std::to_string(rotated.x)+","+std::to_string(rotated.y)+" "+std::to_string(rotated.pixelsWide)+"x"+std::to_string(rotated.pixelsHigh));
    });
    test("ui_scaled_rotated_sprite_is_not_clipped",[]{
        MainWindow window(true);auto document=example();document.layers.front().raster=Raster::filled(16,16,{255,0,0,255});document.layers.front().transform={16,8,32,32,25};document.layers.front().transform.sampling=Transform::Sampling::Smooth;
        auto& project=window.addProject(document);setup(window,project);window.resize(1600,1000);showCanvas(window);
        auto gate=std::make_shared<std::atomic_bool>(false);compositeJob(window)->setProceedGate(gate);
        const auto id=originalLayer(project).id;drag(project,{32,24},{36,28});waitPreview(project,id);
        QImage frame;try{frame=project.canvas->captureRendered();}catch(const std::exception& error){const std::string message=error.what();if(message.find("swap chain")==std::string::npos&&message.find("graphics device")==std::string::npos)throw;return;}
        require(!frame.isNull(),"Scaled sprite capture failed");
        const auto placed=originalLayer(project).transform;const double sx=frame.width()/double(project.canvas->width()),sy=frame.height()/double(project.canvas->height());
        for(Point unit:{Point{.12,.12},Point{.5,.5},Point{.88,.2}}){
            const auto documentPoint=placed.fromUnit(unit);const auto view=project.canvas->viewMapping().toView(documentPoint);
            const int px=int(std::lround(view.x*sx)),py=int(std::lround(view.y*sy));
            require(px>=0&&py>=0&&px<frame.width()&&py<frame.height(),"Sprite sample fell outside the capture");
            const QRgb pixel=frame.pixel(px,py);require(qRed(pixel)>180&&qGreen(pixel)<40&&qBlue(pixel)<40,"Scaled or rotated sprite was clipped");
        }
    });
    std::cout<<"{\"suite\":\"transform_ui\",\"passed\":"<<passed<<",\"failed\":"<<failed<<",\"mac_differential\":false,\"visual_acceptance\":false}\n";return failed?1:0;
}
