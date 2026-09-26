#include "ui/MainWindow.h"
#include <QApplication>
#include <QTest>
#include <QTemporaryDir>
#include "persistence/ProjectStore.h"
#include <functional>
#include <iostream>
#include <stdexcept>
using namespace compositor;
namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
template<class T>T* named(MainWindow&w,const char*name){auto* result=w.findChild<T*>(name);require(result,"Missing named control");return result;}
void action(MainWindow&w,const QString&text){for(auto*a:w.findChildren<QAction*>())if(a->text()==text){a->trigger();return;}throw std::runtime_error("Missing action");}
Document example(){Document d;d.id=newId();d.width=128;d.height=96;Layer l;l.id=newId();l.name="Layer";l.transform={0,0,128,96};l.raster=Raster::filled(128,96,{80,110,130,255});d.layers.push_back(l);return d;}
void drag(EditorProject&p,Point a,Point b){p.canvas->pointerDown({a.x,a.y},{});p.canvas->pointerMove({b.x,b.y},{});p.canvas->pointerUp({b.x,b.y},{});}
}
int main(int argc,char**argv){QApplication app(argc,argv);std::cout<<std::unitbuf;int passed=0,failed=0;
auto test=[&](const char*name,const std::function<void()>&fn){if(argc>1&&std::string(argv[1])!=name)return;std::cout<<"START "<<name<<'\n';try{fn();++passed;std::cout<<"PASS "<<name<<'\n';}catch(const std::exception&e){++failed;std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n';}};
test("appearance_cannot_modify_pending_transform",[]{
 MainWindow w(true);auto&p=w.addProject(example());auto*blend=named<QComboBox>(w,"layerBlend");auto*opacity=named<QSlider>(w,"layerOpacity");
 named<QAction>(w,"freeTransform")->trigger();require(!blend->isEnabled()&&!opacity->isEnabled(),"Appearance enabled during transform");
 blend->setCurrentIndex(3);opacity->setValue(34);QTest::keyClick(&w,Qt::Key_5);
 require(p.document->layers.front().blend==Blend::Normal&&p.document->layers.front().opacity==1,"Pending appearance callback mutated draft");
 named<QAction>(w,"applyTransform")->trigger();require(p.history.undoCount()==0&&blend->isEnabled()&&opacity->isEnabled(),"No-op transform added hidden appearance edit");
 opacity->setValue(61);require(p.document->layers.front().opacity==.61&&p.history.undoCount()==1,"Appearance unavailable after transform");
});
test("appearance_locked_for_captured_brush",[]{
 MainWindow w(true);auto&p=w.addProject(example());action(w,"Brush (B)");auto*blend=named<QComboBox>(w,"layerBlend");auto*opacity=named<QSlider>(w,"layerOpacity");
 const auto before=p.document;p.canvas->pointerDown({30,30},{});require(!blend->isEnabled()&&!opacity->isEnabled(),"Brush did not lock appearance");
 opacity->setValue(40);blend->setCurrentIndex(2);p.canvas->pointerCancel();require(p.document==before&&p.history.undoCount()==0,"Brush cancel leaked appearance edit");
});
test("gradient_remains_pending_and_redrag_uses_original",[]{
 MainWindow w(true);auto&p=w.addProject(example());action(w,"Gradient (G)");p.canvas->zoom=1;
 const auto before=p.document;drag(p,{10,10},{90,10});auto first=p.gradientPreview;
 require(first&&p.document==before&&p.history.undoCount()==0&&!p.history.canUndo(),"Mouse-up prematurely committed gradient");
 drag(p,{20,70},{110,70});require(p.gradientPreview!=first&&p.document==before&&p.history.undoCount()==0,"New line did not replace pending preview");
 QTest::keyClick(&w,Qt::Key_Escape);require(p.document==before&&p.history.undoCount()==0,"Gradient cancel did not restore original");
});
test("gradient_settings_and_palette_rebuild_before_one_apply",[]{
 MainWindow w(true);auto&p=w.addProject(example());action(w,"Gradient (G)");const auto before=p.document;
 drag(p,{10,30},{110,30});const auto linear=p.gradientPreview;
 named<QComboBox>(w,"gradientShape")->setCurrentIndex(1);require(p.gradientPreview!=linear&&p.document==before,"Shape control did not update pending gradient");
 const auto radial=p.gradientPreview;action(w,"Swap (X)");require(p.gradientPreview!=radial&&p.document==before,"Palette did not update pending gradient");
 QTest::keyClick(&w,Qt::Key_Return);require(p.history.undoCount()==1&&p.history.undoName()=="Gradient","Gradient apply did not commit one transaction");
 auto undo=p.history.undo();require(undo&&undo->document==before,"Gradient undo lost exact original");
});
test("gradient_handle_drag_click_cancel_and_tool_resolve",[]{
 MainWindow w(true);auto&p=w.addProject(example());action(w,"Gradient (G)");p.canvas->zoom=1;
 const auto before=p.document;drag(p,{10,40},{100,40});drag(p,{100,40},{110,60});require(p.history.undoCount()==0,"Endpoint drag committed");
 action(w,"Brush (B)");require(p.history.undoCount()==1&&p.document!=before,"Tool switch failed to apply gradient");
 action(w,"Gradient (G)");auto committed=p.document;drag(p,{64,48},{64,48});require(p.document==committed&&p.history.undoCount()==1,"Click created a pending gradient edit");
});
test("gradient_mask_uses_binary_palette_and_preserves_image",[]{
 MainWindow w(true);auto d=example();auto mask=std::make_shared<GrayRaster>();mask->width=128;mask->height=96;mask->pixels.resize(128*96,255);d.layers.front().mask=Mask{mask};auto&p=w.addProject(d);p.maskSelected=true;
 action(w,"Gradient (G)");const auto raster=p.document->layers.front().raster;drag(p,{10,40},{100,40});require(p.document->layers.front().raster==raster&&p.document->layers.front().mask==d.layers.front().mask&&p.gradientPreview&&p.gradientPreview->materializeLayer().mask!=d.layers.front().mask,"Mask gradient changed image or omitted preview");
 QTest::keyClick(&w,Qt::Key_Return);require(p.history.undoName()=="Gradient Mask"&&p.history.undoCount()==1,"Mask gradient history target wrong");
});
test("gradient_save_keeps_preview_out_of_package_and_history",[]{
 MainWindow w(true);auto&p=w.addProject(example());action(w,"Gradient (G)");const auto original=p.document;
 drag(p,{10,30},{110,30});require(p.document==original&&p.gradientPreview,"Gradient preview entered document");
 const auto preview=p.gradientPreview;QTemporaryDir directory;require(directory.isValid(),"Temporary save directory unavailable");p.path=directory.filePath("Pending.comp");
 action(w,"&Save Project");ProjectStore store(makeWicProjectCodec());auto loaded=store.load(std::filesystem::path(p.path.toStdWString()));
 require(loaded.document.layers.size()==original->layers.size(),"Saved layer count changed");
 for(size_t i=0;i<loaded.document.layers.size();++i){auto& layer=loaded.document.layers[i];const auto& source=original->layers[i];require(layer.raster&&source.raster&&layer.raster->rgba()==source.raster->rgba(),"Saved pixels include pending gradient");layer.raster=source.raster;}
 require(loaded.document==*original,"Saved metadata changed");
 require(p.gradientPreview==preview&&p.document==original&&p.history.undoCount()==0&&!p.history.modified(),"Save changed pending gradient or history");
 named<QAction>(w,"applyGradient")->trigger();require(p.history.undoCount()==1&&p.history.modified()&&!p.gradientPreview,"Apply did not create one dirty gradient edit after Save");
 action(w,"Undo Gradient");require(p.document==original&&!p.history.modified(),"Gradient undo did not restore saved document");
});
test("blank_30000_brush_preview_is_bounded_and_commit_grows_source",[]{
 MainWindow w(true);Document d;d.id=newId();d.width=d.height=30000;Layer l;l.id=newId();l.transform={0,0,30000,30000};d.layers.push_back(l);auto&p=w.addProject(d);action(w,"Brush (B)");
 Raster::resetMaterializationCount();p.canvas->pointerDown({15000,15000},{});p.canvas->pointerMove({15020,15000},{});
 require(!p.document->layers.front().raster&&p.brushPreview,"Brush preview mutated canonical document");
 auto patch=p.canvas->viewportProvider(14900,14900,256,256,1);require(patch.raster&&patch.raster->tiles.size()<=64,"Brush viewport exceeded tile budget");
 p.canvas->pointerUp({15020,15000},{});const auto&painted=p.document->layers.front();require(painted.raster&&painted.raster->width<100&&painted.raster->height<100&&p.history.undoCount()==1,"Blank brush allocated full canvas or lost history");
 require(Raster::materializationCount()==0&&!p.brushPreview,"Sparse brush flattened rgba or kept preview after commit");auto undo=p.history.undo();require(undo&&undo->document==std::optional(d),"Blank growth undo lost original");
});
test("brush_outside_existing_source_grows_without_moving_original_pixels",[]{
 MainWindow w(true);auto d=example();d.layers.front().transform={40,30,32,32};d.layers.front().raster=Raster::filled(32,32,{180,80,20,255});auto&p=w.addProject(d);action(w,"Brush (B)");
 auto before=SoftwareRenderer().render(d,0,0,128,96);drag(p,{20,45},{22,45});const auto&layer=p.document->layers.front();require(layer.transform.x<40&&layer.raster->width>32,"Brush was clipped to source extent");
 auto after=SoftwareRenderer().render(*p.document,0,0,128,96);require(after->pixel(60,45)==before->pixel(60,45)&&after->pixel(20,45).a>0,"Growth moved original or omitted new pixels");
});
std::cout<<"{\"passed\":"<<passed<<",\"failed\":"<<failed<<",\"mac_differential\":false}\n";return failed?1:0;
}
