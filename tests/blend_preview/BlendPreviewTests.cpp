#include "ui/MainWindow.h"
#include <QAbstractItemView>
#include <QApplication>
#include <QTest>
#include <QTabWidget>
#include <cstdio>
#include <map>
#include <stdexcept>
#include <string>
using namespace compositor;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void undo(MainWindow& window){
    for(auto* action:window.findChildren<QAction*>())if(action->property("commandId")=="edit.undo"){
        require(action->isEnabled(),"undo action disabled");action->trigger();return;
    }
    throw std::runtime_error("undo action missing");
}
Document sample(){
    Document d;d.id="blend-document";d.width=16;d.height=16;
    Layer a;a.id="base";a.name="Base";a.transform={0,0,16,16};a.raster=Raster::filled(16,16,{100,150,200,255});
    Layer b=a;b.id="top";b.name="Top";b.raster=Raster::filled(16,16,{200,100,50,255});d.layers={a,b};return d;
}
struct Fixture {
    MainWindow window{true}; EditorProject& project; QComboBox* combo{};
    Fixture():project(window.addProject(sample())){
        window.show();QApplication::processEvents();combo=window.findChild<QComboBox*>("layerBlend");require(combo,"blend control missing");
        require(project.active=="top"&&combo->isEnabled(),"active top layer must allow appearance editing");
    }
    Pixel visible(){auto view=project.canvas->viewportProvider(0,0,16,16,1);return view.raster->pixel(8,8);}
    void hover(int index){
        combo->showPopup();QApplication::processEvents();auto* view=combo->view();const auto modelIndex=combo->model()->index(index,0);
        view->scrollTo(modelIndex);QApplication::processEvents();const auto point=view->visualRect(modelIndex).center();
        QTest::mouseMove(view->viewport(),QPoint(1,1));QTest::mouseMove(view->viewport(),point);QApplication::processEvents();
        require(view->currentIndex()==modelIndex,"real popup hover did not highlight the requested mode");
    }
    void choose(int index){auto* view=combo->view();QTest::mouseClick(view->viewport(),Qt::LeftButton,{},view->visualRect(combo->model()->index(index,0)).center());QApplication::processEvents();}
    void escape(){QTest::keyClick(combo->view(),Qt::Key_Escape);QApplication::processEvents();}
};
void hoverCancel(){
    Fixture f;const auto before=f.project.document;const auto ordinary=f.visible();auto expected=*before;expected.layers.back().blend=Blend::Multiply;
    const auto multiply=SoftwareRenderer().render(expected,0,0,16,16)->pixel(8,8);require(multiply!=ordinary,"fixture does not distinguish blend preview");
    f.hover(int(Blend::Multiply));require(f.project.document==before&&f.project.history.undoCount()==0&&!f.project.history.modified(),"hover changed canonical document/history");
    require(f.visible()==multiply,"hover did not render the highlighted blend");f.escape();
    require(f.project.document==before&&f.visible()==ordinary&&f.project.history.undoCount()==0,"popup cancellation did not restore original rendering");
}
void commitOnce(){
    Fixture f;const auto before=f.project.document;f.hover(int(Blend::Multiply));f.hover(int(Blend::Screen));
    require(f.project.history.undoCount()==0,"preview created history entries");f.choose(int(Blend::Screen));
    require(f.project.document->layers.back().blend==Blend::Screen&&f.project.history.undoCount()==1,"selection must commit exactly one blend edit");
    undo(f.window);require(f.project.document==before,"blend undo did not restore exact document");
}
void noopPreservesRedo(){
    Fixture f;f.combo->setCurrentIndex(int(Blend::Multiply));undo(f.window);
    require(f.project.history.canRedo(),"fixture redo missing");f.hover(int(Blend::Screen));f.escape();
    require(f.project.history.canRedo()&&f.project.history.undoCount()==0,"cancelled hover destroyed redo");
    f.hover(int(Blend::Normal));f.choose(int(Blend::Normal));require(f.project.history.canRedo()&&f.project.history.undoCount()==0,"choosing canonical mode destroyed redo");
}
void targetGuard(){
    Fixture f;const auto canonical=f.project.document;f.hover(int(Blend::Multiply));
    f.project.active="base";f.project.selected={"base"};
    const auto expected=SoftwareRenderer().render(*canonical,0,0,16,16)->pixel(8,8);
    require(f.visible()==expected,"preview followed an unrelated active layer");f.escape();
    require(f.project.document==canonical&&f.project.history.undoCount()==0,"target change committed hover");
}
}
int main(int argc,char** argv){
    QApplication app(argc,argv);const std::map<std::string,void(*)()> cases{{"hover_cancel",hoverCancel},{"commit_once",commitOnce},{"noop_preserves_redo",noopPreservesRedo},{"target_guard",targetGuard}};
    try{require(argc==2&&cases.contains(argv[1]),"provide a named blend preview case");cases.at(argv[1])();std::printf("PASS %s\n",argv[1]);return 0;}
    catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}
}
