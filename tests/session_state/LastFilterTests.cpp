#define main rememberedSettingsBaseMain
#include "RememberedSettingsTests.cpp"
#undef main
#include "ui/CommandRegistry.h"
#include "filters/PixelFilters.h"
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QKeySequence>
#include <QMenu>
#include <QTest>
#include <QTimer>
#include <set>
#include <iostream>
namespace {
using namespace compositor;
void waitFor(const std::function<bool()>& ready,const char* failure){
    QEventLoop loop;QTimer poll;QElapsedTimer elapsed;elapsed.start();poll.setInterval(5);
    QObject::connect(&poll,&QTimer::timeout,&loop,[&]{
        if(ready()){loop.quit();return;}
        if(elapsed.elapsed()>15000)loop.quit();
    });
    poll.start();loop.exec();poll.stop();
    require(ready(),failure);
}
QAction* lastFilterAction(MainWindow& window){
    for(auto* menu:window.findChildren<QMenu*>())if(menu->title().remove('&')=="Filters")
        for(auto* action:menu->actions())if(action->property("commandId").toString()=="filter.last")return action;
    throw std::runtime_error("missing Last Filter action");
}
void gaussian_repeat_one_undo(){
    Fixture f;
    f.modal("filter.gaussian",[](QDialog& d){field<QDoubleSpinBox>(d,"Radius")->setValue(3);},true);
    require(f.a->history.undoCount()==1,"initial filter Apply one undo step");
    f.trigger("filter.last");
    waitFor([&]{return f.a->history.undoCount()==2;},"Last Filter did not finish");
    require(f.a->history.undoCount()==2,"Last Filter commits one additional undo step");
    f.trigger("edit.undo");
    require(f.a->history.undoCount()==1,"undo removes only the repeated filter");
}
void adjustment_does_not_set_last(){
    Fixture f;
    f.modal("adjust.exposure",[](QDialog& d){field<QDoubleSpinBox>(d,"Exposure")->setValue(1.25);},true);
    require(!f.a->toolState.lastFilterKind.has_value(),"adjustment Apply does not remember Last Filter");
    require(!command(f.window,"filter.last")->isEnabled(),"Last Filter stays disabled without a filter");
}
void content_aware_excluded(){
    Fixture f;
    f.a->document->selection=editing::rasterSelection(editing::SelectionOutline::rectangle({10,5,10,6}),40,20);
    f.modal("filter.gaussian",[](QDialog& d){field<QDoubleSpinBox>(d,"Radius")->setValue(3);},true);
    require(f.a->toolState.lastFilterKind==filters::Kind::GaussianBlur,"Gaussian becomes last filter");
    f.modal("filter.content_aware",[](QDialog&){},true);
    require(f.a->toolState.lastFilterKind==filters::Kind::GaussianBlur,"Content-Aware Fill does not replace last filter");
}
void menu_label_shows_name(){
    Fixture f;
    f.modal("filter.gaussian",[](QDialog& d){field<QDoubleSpinBox>(d,"Radius")->setValue(3);},true);
    auto* action=lastFilterAction(f.window);
    for(auto* menu:f.window.findChildren<QMenu*>())if(menu->title().remove('&')=="Filters"){menu->aboutToShow();break;}
    require(action->text().contains("Gaussian Blur"),"menu shows the remembered filter name");
}
void shortcut_is_last_filter_only(){
    Fixture f;
    std::map<QString,QString> owners;
    for(auto* menu:f.window.findChildren<QMenu*>()){
        for(auto* action:menu->actions()){
            const auto id=action->property("commandId").toString();
            if(id.isEmpty())continue;
            for(const auto& shortcut:action->shortcuts()){
                const auto key=shortcut.toString(QKeySequence::PortableText).toLower();
                if(key.isEmpty())continue;
                const auto message=std::string("unique menu shortcut ")+key.toStdString();
                require(owners.insert({key,id}).second,message.c_str());
            }
        }
    }
    require(owners.at("ctrl+alt+f")=="filter.last","Ctrl+Alt+F belongs to Last Filter alone");
}
void shortcut_triggers_command(){
    Fixture f;
    f.modal("filter.gaussian",[](QDialog& d){field<QDoubleSpinBox>(d,"Radius")->setValue(3);},true);
    const auto before=f.a->history.undoCount();
    f.window.show();
    f.a->canvas->setFocus();
    QApplication::processEvents();
    QTest::keyClick(f.a->canvas,Qt::Key_F,Qt::ControlModifier|Qt::AltModifier);
    waitFor([&]{return f.a->history.undoCount()==before+1;},"Ctrl+Alt+F did not finish");
    require(f.a->history.undoCount()==before+1,"Ctrl+Alt+F repeats the last filter");
}
}
int main(int argc,char** argv){
    QApplication app(argc,argv);
    const std::map<std::string,void(*)()> cases{
        {"gaussian_repeat_one_undo",gaussian_repeat_one_undo},
        {"adjustment_does_not_set_last",adjustment_does_not_set_last},
        {"content_aware_excluded",content_aware_excluded},
        {"menu_label_shows_name",menu_label_shows_name},
        {"shortcut_is_last_filter_only",shortcut_is_last_filter_only},
        {"shortcut_triggers_command",shortcut_triggers_command},
    };
    try{
        require(argc==2&&cases.contains(argv[1]),"provide named case");
        cases.at(argv[1])();
        std::cout<<"PASS "<<argv[1]<<'\n';
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"FAIL "<<e.what()<<'\n';
        return 1;
    }
}
