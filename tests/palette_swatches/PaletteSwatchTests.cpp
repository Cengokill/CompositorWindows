// Unconfigured Release41 proposal. Run unchanged against coherent Release40 first.
// Source: ColorPaletteControls.swift and Document/ColorPalette.swift, pinned a19db901.
#include "ui/MainWindow.h"
#include "ui/PaletteDialog.h"
#include <QApplication>
#include <QAbstractButton>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QPointer>
#include <QTest>
#include <QToolButton>
#include <QTimer>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <map>

using namespace compositor;
namespace {
std::filesystem::path output;
QJsonObject observations;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void pump(){QApplication::processEvents();}
QAction* command(MainWindow& window,const char* id){
    for(auto* action:window.findChildren<QAction*>())
        if(action->property("commandId")==id&&action->isVisible())return action;
    throw std::runtime_error(std::string("visible command missing: ")+id);
}
void trigger(MainWindow& window,const char* id){auto* action=command(window,id);require(action->isEnabled(),"command is enabled");action->trigger();pump();}
QToolBar* rail(MainWindow& window){auto* value=window.findChild<QToolBar*>("tools");require(value,"Tools rail exists");return value;}
QToolButton* button(MainWindow& window,const char* id){
    auto* action=command(window,id);
    for(auto* value:rail(window)->findChildren<QToolButton*>())if(value->defaultAction()==action)return value;
    throw std::runtime_error(std::string("rail button missing: ")+id);
}
Document document(){
    Document value;value.id=newId();value.width=64;value.height=48;
    Layer layer;layer.id=newId();layer.name="Swatch fixture";layer.transform={0,0,64,48};layer.raster=Raster::filled(64,48,{23,111,197,255});
    auto mask=std::make_shared<GrayRaster>();mask->width=mask->height=1;mask->pixels={255};layer.mask=Mask{mask};value.layers={layer};return value;
}
PaletteDialog* visiblePicker(MainWindow& window){
    for(auto* widget:window.findChildren<QWidget*>())if(auto* value=dynamic_cast<PaletteDialog*>(widget);value&&value->isVisible())return value;
    return nullptr;
}
PaletteDialog* picker(MainWindow& window){auto* value=visiblePicker(window);require(value,"palette picker is visible");return value;}
void sample(MainWindow& window,const char* hex){const auto value=effects_tools::PaletteColor::fromHex(hex);require(value.has_value(),"valid fixture RGB");picker(window)->sample(*value);pump();}
void choose(MainWindow& window,bool background,const char* hex){trigger(window,background?"palette.background":"palette.foreground");sample(window,hex);picker(window)->accept();pump();}
void save(QWidget& widget,const QString& name){require(widget.grab().save(QString::fromStdWString((output/name.toStdWString()).wstring())),"save actual widget image");}
QColor observedColor(MainWindow& window,bool background,const QString& name){
    auto* value=button(window,background?"palette.background":"palette.foreground");
    const auto image=value->grab().toImage().convertToFormat(QImage::Format_RGBA8888);
    require(!image.isNull(),"actual swatch capture exists");
    require(image.save(QString::fromStdWString((output/(name+".png").toStdWString()).wstring())),"save actual swatch image");
    const QColor actual=image.pixelColor(image.width()/2,image.height()/2);
    observations[name]=actual.name(QColor::HexRgb);return actual;
}
void expectColor(MainWindow& window,bool background,const char* hex,const QString& name){
    const QColor expected(QString("#")+hex);const auto actual=observedColor(window,background,name);
    observations[name+"_expected"]=expected.name(QColor::HexRgb);
    require(std::abs(actual.red()-expected.red())<=1&&std::abs(actual.green()-expected.green())<=1&&std::abs(actual.blue()-expected.blue())<=1&&actual.alpha()==255,"swatch center represents palette RGB within one byte");
}
void target(MainWindow& window,bool mask){
    trigger(window,"tool.brush");
    for(auto* value:window.findChildren<QComboBox*>())if(value->accessibleName()=="Editing target"){
        require(value->isEnabled(),"mask target control enabled");value->setCurrentIndex(mask?1:0);pump();return;
    }
    throw std::runtime_error("editing target control missing");
}
struct Fixture {
    MainWindow window{true};EditorProject& project;std::optional<Document> original;
    Fixture():project(window.addProject(document())),original(project.document){window.show();pump();save(*rail(window),"rail-initial.png");}
    ~Fixture(){if(auto* value=visiblePicker(window))value->reject();}
    void immutable(){require(project.document==original&&project.history.undoCount()==0&&!project.history.modified(),"palette controls preserve canonical document and history");}
};
void layout_overlap(){
    Fixture f;auto* fg=button(f.window,"palette.foreground");auto* bg=button(f.window,"palette.background");
    auto* swap=button(f.window,"palette.swap");auto* defaults=button(f.window,"palette.defaults");
    const auto origin=fg->mapToGlobal(QPoint(0,0));
    observations["foreground_width"]=fg->width();observations["foreground_height"]=fg->height();
    observations["background_dx"]=bg->mapToGlobal(QPoint(0,0)).x()-origin.x();observations["background_dy"]=bg->mapToGlobal(QPoint(0,0)).y()-origin.y();
    require(fg->size()==QSize(24,24)&&bg->size()==QSize(24,24),"source swatches are 24 by 24 view points");
    require(bg->mapToGlobal(QPoint(0,0))-origin==QPoint(12,12),"background swatch offset is 12 by 12");
    require(swap->size()==QSize(12,12)&&defaults->size()==QSize(12,12),"source utility hit targets are 12 by 12");
    require(swap->mapToGlobal(QPoint(0,0))-origin==QPoint(27,-3),"swap source relative placement");
    require(defaults->mapToGlobal(QPoint(0,0))-origin==QPoint(-1,27),"defaults source relative placement");
    auto* hit=rail(f.window)->childAt(rail(f.window)->mapFromGlobal(origin+QPoint(18,18)));
    require(hit==fg||fg->isAncestorOf(hit),"foreground owns overlap hit area");f.immutable();
}
void rounded_hit_region(){
    Fixture f;auto* fg=button(f.window,"palette.foreground");
    // QTest treats a null QPoint as its default center; use the actual corner pixel(1,1).
    QTest::mouseClick(fg,Qt::LeftButton,Qt::NoModifier,QPoint(1,1));pump();
    const bool cornerOpened=visiblePicker(f.window)!=nullptr;observations["corner_opened_picker"]=cornerOpened;
    if(auto* value=visiblePicker(f.window))value->reject();
    require(!cornerOpened,"rounded swatch corner is outside the source hit shape");
    QTest::mouseClick(fg,Qt::LeftButton,Qt::NoModifier,fg->rect().center());pump();
    require(picker(f.window)->windowTitle()=="Color Picker (Foreground Color)","foreground center opens foreground picker");picker(f.window)->reject();pump();
    auto* bg=button(f.window,"palette.background");
    QTest::mouseClick(bg,Qt::LeftButton,Qt::NoModifier,QPoint(bg->width()-6,bg->height()-6));pump();
    require(picker(f.window)->windowTitle()=="Color Picker (Background Color)","exposed background opens background picker");picker(f.window)->reject();f.immutable();
}
void labels_tooltips(){
    Fixture f;
    struct Entry{const char* id;const char* label;const char* tip;};
    for(auto entry:{Entry{"palette.foreground","Foreground color","Foreground color"},Entry{"palette.background","Background color","Background color"},Entry{"palette.swap","Swap colors","Swap foreground and background (X)"},Entry{"palette.defaults","Default colors","Default colors (D)"}}){
        auto* value=button(f.window,entry.id);observations[QString(entry.id)+"_label"]=value->accessibleName();observations[QString(entry.id)+"_tooltip"]=value->toolTip();
        require(value->accessibleName()==entry.label,"source accessible palette label");require(value->toolTip()==entry.tip,"source palette tooltip");
    }f.immutable();
}
void colors(){Fixture f;expectColor(f.window,false,"000000","default-foreground");expectColor(f.window,true,"FFFFFF","default-background");choose(f.window,false,"B42D3C");choose(f.window,true,"1464D2");expectColor(f.window,false,"B42D3C","custom-foreground");expectColor(f.window,true,"1464D2","custom-background");f.immutable();}
void picker_apply_cancel(){
    Fixture f;choose(f.window,false,"B42D3C");trigger(f.window,"palette.foreground");sample(f.window,"28C84B");
    expectColor(f.window,false,"B42D3C","working-palette-uncommitted");picker(f.window)->reject();pump();expectColor(f.window,false,"B42D3C","cancel-restored");
    choose(f.window,false,"28C84B");expectColor(f.window,false,"28C84B","apply-published");f.immutable();
}
void swap_reset(){Fixture f;choose(f.window,false,"B42D3C");choose(f.window,true,"1464D2");trigger(f.window,"palette.swap");expectColor(f.window,false,"1464D2","swapped-foreground");expectColor(f.window,true,"B42D3C","swapped-background");trigger(f.window,"palette.defaults");expectColor(f.window,false,"000000","reset-foreground");expectColor(f.window,true,"FFFFFF","reset-background");f.immutable();}
void mask_palette(){
    Fixture f;choose(f.window,false,"B42D3C");choose(f.window,true,"1464D2");target(f.window,true);
    expectColor(f.window,false,"000000","mask-foreground");expectColor(f.window,true,"FFFFFF","mask-background");trigger(f.window,"palette.swap");expectColor(f.window,false,"FFFFFF","mask-swapped-foreground");expectColor(f.window,true,"000000","mask-swapped-background");
    trigger(f.window,"palette.defaults");expectColor(f.window,false,"000000","mask-reset-foreground");expectColor(f.window,true,"FFFFFF","mask-reset-background");target(f.window,false);expectColor(f.window,false,"B42D3C","image-foreground-preserved");expectColor(f.window,true,"1464D2","image-background-preserved");f.immutable();
}
void mask_chooser_nonmodal(){
    Fixture f;target(f.window,true);
    for(bool background:{false,true}){
        bool visited=false,modal=false;std::exception_ptr failure;
        // The same driver closes the old modal QMessageBox and the future source popover.
        // Its closure completes before the locals leave scope, even on an assertion failure.
        QTimer poll;poll.setInterval(5);QElapsedTimer elapsed;elapsed.start();
        QObject::connect(&poll,&QTimer::timeout,&f.window,[&]{
            try{
                require(elapsed.elapsed()<3000,"mask chooser appears within 3 seconds");
                for(auto* white:f.window.findChildren<QAbstractButton*>())if(white->isVisible()&&white->text()==QString::fromUtf8("White · Reveal")){
                    visited=true;modal=QApplication::activeModalWidget()!=nullptr;poll.stop();white->click();return;
                }
            }catch(...){failure=std::current_exception();poll.stop();if(auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget()))dialog->reject();}
        });
        poll.start();trigger(f.window,background?"palette.background":"palette.foreground");
        while(!visited&&!failure){QTest::qWait(5);}poll.stop();if(failure)std::rethrow_exception(failure);
        observations[background?"background_modal":"foreground_modal"]=modal;
        require(visited&&!modal,"mask chooser is a source popover without a modal document loop");
        expectColor(f.window,background,"FFFFFF",background?"chosen-mask-background":"chosen-mask-foreground");expectColor(f.window,!background,"000000",background?"chosen-mask-foreground-complement":"chosen-mask-background-complement");
    }f.immutable();
}
void mask_target_closes_picker(){
    Fixture f;choose(f.window,false,"B42D3C");trigger(f.window,"tool.brush");trigger(f.window,"palette.foreground");sample(f.window,"28C84B");target(f.window,true);
    observations["picker_visible_after_mask_target"]=visiblePicker(f.window)!=nullptr;
    require(!visiblePicker(f.window),"mask target change closes RGB picker without commit");target(f.window,false);trigger(f.window,"palette.foreground");require(picker(f.window)->color().hex()=="B42D3C","mask target change discarded RGB working color");picker(f.window)->reject();f.immutable();
}
void stroke_disabled(){
    Fixture f;trigger(f.window,"tool.brush");f.project.canvas->pointerDown({24,24},Qt::NoModifier);pump();
    require(f.project.brushPreview!=nullptr,"actual brush gesture is active");
    for(auto id:{"palette.foreground","palette.background","palette.swap","palette.defaults"})require(!button(f.window,id)->isEnabled(),"palette control disabled during brush gesture");
    f.project.canvas->pointerCancel();pump();for(auto id:{"palette.foreground","palette.background","palette.swap","palette.defaults"})require(button(f.window,id)->isEnabled(),"palette control restored after gesture cancel");f.immutable();
}
void project_restore(){
    Fixture f;choose(f.window,false,"B42D3C");choose(f.window,true,"1464D2");auto& second=f.window.addProject(document(),"Second");pump();expectColor(f.window,false,"000000","second-default-foreground");expectColor(f.window,true,"FFFFFF","second-default-background");choose(f.window,false,"28C84B");
    auto* tabs=f.window.findChild<QTabWidget*>();require(tabs,"project tabs");tabs->setCurrentWidget(f.project.page);pump();expectColor(f.window,false,"B42D3C","first-restored-foreground");expectColor(f.window,true,"1464D2","first-restored-background");tabs->setCurrentWidget(second.page);pump();expectColor(f.window,false,"28C84B","second-restored-foreground");require(second.history.undoCount()==0&&!second.history.modified(),"second palette has no document edit");f.immutable();
}
void eyedropper_refresh(){
    Fixture f;choose(f.window,false,"B42D3C");trigger(f.window,"tool.eyedropper");f.project.canvas->pointerDown({24,24},Qt::NoModifier);pump();expectColor(f.window,false,"176FC5","live-eyedropper-foreground");f.project.canvas->pointerUp({24,24},Qt::NoModifier);pump();expectColor(f.window,false,"176FC5","finished-eyedropper-foreground");f.immutable();
}
}
int main(int argc,char** argv){
    QApplication app(argc,argv);
    const std::map<std::string,void(*)()> cases{{"layout_overlap",layout_overlap},{"rounded_hit_region",rounded_hit_region},{"labels_tooltips",labels_tooltips},{"colors",colors},{"picker_apply_cancel",picker_apply_cancel},{"swap_reset",swap_reset},{"mask_palette",mask_palette},{"mask_chooser_nonmodal",mask_chooser_nonmodal},{"mask_target_closes_picker",mask_target_closes_picker},{"stroke_disabled",stroke_disabled},{"project_restore",project_restore},{"eyedropper_refresh",eyedropper_refresh}};
    if(argc!=3||!cases.contains(argv[1])){std::cerr<<"usage: palette_swatch_tests <case> <fresh-output-directory>\n";return 2;}
    output=std::filesystem::path(argv[2]);if(std::filesystem::exists(output)){std::cerr<<"output directory already exists\n";return 2;}std::filesystem::create_directories(output);
    bool passed=false;std::string error;
    try{cases.at(argv[1])();passed=true;}catch(const std::exception& value){error=value.what();}
    observations["schema"]="PALETTE_SWATCH_SOURCE_V1";observations["case"]=argv[1];observations["passed"]=passed;observations["error"]=QString::fromStdString(error);observations["mac_differential"]=false;
    QFile report(QString::fromStdWString((output/L"results.json").wstring()));if(!report.open(QIODevice::WriteOnly))return 3;report.write(QJsonDocument(observations).toJson());
    std::cout<<(passed?"PASS ":"FAIL ")<<argv[1]<<" "<<error<<'\n';return passed?0:1;
}
