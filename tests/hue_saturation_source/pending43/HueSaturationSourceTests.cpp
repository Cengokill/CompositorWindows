#include "ui/MainWindow.h"
#include "ui/AdjustmentAdvancedControls.h"
#include "effects/Adjustments.h"
#include "editing/Selection.h"
#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QTest>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <windows.h>

using namespace compositor;
using namespace compositor::effects_tools;
namespace {
struct SourceRequirementFailure:std::runtime_error {using std::runtime_error::runtime_error;};
QJsonArray checks;
int failures{};
void check(int line,bool value,const char* kind){
    checks.append(QJsonObject{{"source_line",line},{"kind",kind},{"passed",value}});
    if(!value){++failures;std::fprintf(stderr,"FAIL source %s line%d\n",kind,line);}
}
void expect(int line,bool value){check(line,value,"expect");}
void required(int line,bool value){check(line,value,"require");if(!value)throw SourceRequirementFailure("Source #require failed at "+std::to_string(line));}
void guard(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Fn>void wait(Fn predicate,const char* message){
    QElapsedTimer timer;timer.start();
    while(!predicate()){
        if(timer.elapsed()>30000)throw std::runtime_error(message);
        QApplication::processEvents(QEventLoop::AllEvents,20);QTest::qWait(1);
    }
}
void refreshCommands(MainWindow& window){
    for(auto* object:window.findChildren<QObject*>())if(auto* registry=dynamic_cast<ui::CommandRegistry*>(object))registry->refresh();
}
QAction* command(MainWindow& window,const char* id){
    for(auto* action:window.findChildren<QAction*>())if(action->property("commandId")==id)return action;
    throw std::runtime_error(std::string("Missing command ")+id);
}
void trigger(MainWindow& window,const char* id){
    window.activateWindow();if(window.canvas())window.canvas()->setFocus();QApplication::processEvents();refreshCommands(window);
    auto* action=command(window,id);guard(action->isEnabled(),"Requested native command enabled");action->trigger();QApplication::processEvents();
}
QPushButton* applyButton(QDialog* panel){
    guard(panel,"Active panel exists");auto* buttons=panel->findChild<QDialogButtonBox*>();guard(buttons,"Panel button box exists");
    auto* button=buttons->button(QDialogButtonBox::Apply);guard(button,"Panel Apply exists");return button;
}
QPushButton* button(QDialog* panel,const char* name){auto* result=panel->findChild<QPushButton*>(name);guard(result,"Named hue action exists");return result;}
HueAdvancedControls* hueControl(QDialog* panel){
    for(auto* object:panel->findChildren<QObject*>())if(auto* control=dynamic_cast<HueAdvancedControls*>(object))return control;
    throw std::runtime_error("Actual Hue controls exist");
}
std::shared_ptr<const Raster> fixture(bool redBlue){
    std::array<uint8_t,40*20*4> bytes{};
    for(int y=0;y<20;++y)for(int x=0;x<40;++x){
        const Pixel pixel=!redBlue&&y>=16?Pixel{0,0,128,128}:x<20?Pixel{255,0,0,255}:redBlue?Pixel{0,0,255,255}:Pixel{128,128,128,255};
        const auto index=size_t(y*40+x)*4;bytes[index]=pixel.r;bytes[index+1]=pixel.g;bytes[index+2]=pixel.b;bytes[index+3]=pixel.a;
    }
    auto image=Raster::fromRgba(40,20,bytes.data(),160);
    if(redBlue)required(153,image&&image->width==40&&image->height==20);
    else required(21,image&&image->width==40&&image->height==20);
    return image;
}
struct Session {
    MainWindow window{true};
    EditorProject& project;
    explicit Session(bool redBlue=false):project(window.addProject([]{Document d;d.id=newId();d.width=40;d.height=20;return d;}())){
        // The two source setup edits precede every relative history assertion.
        project.history.begin("New Canvas",std::nullopt,{});project.history.end(project.document,{});
        Layer layer;layer.id=newId();layer.name=redBlue?"RedBlue":"Colors";layer.transform={0,0,40,20};layer.raster=fixture(redBlue);
        project.history.begin("Import",project.document,project.active);document().layers.push_back(layer);project.active=layer.id;project.selected={layer.id};project.history.end(project.document,project.active);
        window.show();QApplication::processEvents();trigger(window,"tool.move");
    }
    Document& document(){guard(project.document.has_value(),"Session document exists");return *project.document;}
    Layer& active(){auto& layers=document().layers;auto found=std::find_if(layers.begin(),layers.end(),[&](const Layer& layer){return layer.id==project.active;});guard(found!=layers.end(),"Active source layer exists");return *found;}
    ui::EditPanelSession* currentEditor(){
        for(auto* object:window.findChildren<QObject*>())if(auto* editor=dynamic_cast<ui::EditPanelSession*>(object);editor&&editor->canvas()==project.canvas&&editor->kind()==ui::EditPanelSession::Kind::Hue&&editor->panel()&&editor->panel()->isVisible())return editor;
        return nullptr;
    }
    ui::EditPanelSession& editor(){auto* result=currentEditor();guard(result,"Active Hue controller exists");return *result;}
    bool canEditLayers(){refreshCommands(window);const auto* spec=ui::commandSpecById("layer.new");guard(spec&&spec->gate==ui::CommandGate::Layers,"layer.new directly observes native Layers gate");return command(window,"layer.new")->isEnabled();}
    void ready(){QPointer<QDialog> panel=editor().panel();wait([&]{guard(panel&&panel->isVisible(),"Active editor survived preview");return applyButton(panel)->isEnabled();},"Hue preview ready within30s");}
    void begin(){trigger(window,"adjust.hue_saturation");wait([&]{return currentEditor()!=nullptr;},"Hue editor opened");ready();}
    void update(const HueSettings& settings,bool preview=true){guard(editor().updateAdjustmentSettings(withHueSettings(editor().adjustmentSettings(),settings),preview),"Actual controller accepts full Hue settings");}
    HueSettings settings(){return hueFromAdjustmentJson(editor().adjustmentSettings());}
    void commit(){ready();QPointer<QDialog> panel=editor().panel();applyButton(panel)->click();wait([&]{return !panel||!panel->isVisible();},"Full-resolution Hue Apply completed");QApplication::processEvents();}
    void cancel(){
        QPointer<ui::EditPanelSession> owner=&editor();QPointer<QDialog> panel=owner->panel();panel->reject();
        wait([&]{return !panel||!panel->isVisible();},"Hue Cancel closed active editor");QApplication::processEvents();
        guard(canEditLayers(),"Cancel released the actual host edit ownership");
        if(owner)guard(!owner->samplePress({30,5},30,{}),"Retired controller rejects sample publication");
    }
    void apply(const HueSettings& settings){begin();update(settings);ready();commit();}
    void undo(){trigger(window,"edit.undo");}
    void sample(HueSample mode,Point point){
        const char* name=mode==HueSample::Replace?"hueSampleColor":mode==HueSample::Add?"hueAddColor":"hueRemoveColor";
        auto* control=hueControl(editor().panel());if(control->sampleMode()!=mode)button(editor().panel(),name)->click();
        guard(control->sampleMode()==mode,"Source sample-mode assignment reached controller");
        guard(editor().samplePress(point,point.x,{}),"Sample point routed through production composite sampler");
    }
    void selectLeft(){
        project.history.begin("Select",project.document,project.active);
        const auto outline=editing::applySelection({},editing::SelectionOutline::rectangle({0,0,10,20}),editing::SelectionMode::Replace,40,20);
        document().selection=editing::rasterSelection(outline,40,20);project.history.end(project.document,project.active);
    }
};
using Bytes=std::array<int,4>;
Bytes readPixel(const std::shared_ptr<const Raster>& image,int x,int y,int contextLine,int dataLine){
    required(contextLine,image&&image->width>0&&image->height>0);const auto data=image->rgba();
    required(dataLine,data.size()==size_t(image->width)*image->height*4);
    guard(x>=0&&y>=0&&x<image->width&&y<image->height,"Source pixel coordinates inside image");const auto at=size_t(y*image->width+x)*4;
    return {data[at],data[at+1],data[at+2],data[at+3]};
}
Bytes pixel(Session& session,int x,int y){
    required(26,session.project.document.has_value());validateDocument(session.document());
    return readPixel(SoftwareRenderer().render(session.document(),0,0,session.document().width,session.document().height),x,y,27,31);
}
Bytes previewPixel(ui::EditPanelSession& editor,const std::string& id,int x,int y){
    const auto preview=editor.previewDocument();std::shared_ptr<const Raster> image;
    if(preview)for(const auto& layer:preview->layers)if(layer.id==id)image=layer.raster;
    required(134,bool(image));return readPixel(image,x,y,135,139);
}
bool sourceNear(Bytes value,Bytes target,int tolerance=8){for(size_t i=0;i<4;++i)if(std::abs(value[i]-target[i])>tolerance)return false;return true;}
HueSettings settings(double hue=0,double saturation=0,double lightness=0,ColorRange range=ColorRange::Master){HueSettings value;value.range=range;value.adjustments[size_t(range)]={hue,saturation,lightness};return value;}
void defaults_noop(){
    Session s;const auto before=s.project.document;const auto count=s.project.history.undoCount();s.begin();
    expect(51,s.currentEditor()!=nullptr&&!s.canEditLayers());s.update({});s.commit();
    expect(54,s.project.document==before&&s.project.history.undoCount()==count);expect(55,s.currentEditor()==nullptr&&s.canEditLayers());
}
void hue_ranges(){
    Session s;s.apply(settings(120));expect(61,sourceNear(pixel(s,5,5),{0,255,0,255}));expect(62,sourceNear(pixel(s,30,5),{128,128,128,255}));s.undo();
    s.apply(settings(0,-100));const auto gray=pixel(s,5,5);expect(67,gray[0]==gray[1]&&gray[1]==gray[2]&&gray[3]==255);s.undo();
    s.apply(settings(0,0,100));expect(71,sourceNear(pixel(s,5,5),{255,255,255,255}));s.undo();s.apply(settings(0,0,-100));expect(75,sourceNear(pixel(s,5,5),{0,0,0,255}));
}
void colorize_alpha(){
    Session s;auto value=settings(240,100);value.colorize=true;s.apply(value);const auto left=pixel(s,5,5),right=pixel(s,30,5);
    expect(82,left[2]>left[0]&&right[2]>right[0]);expect(83,left[3]==255);const auto strip=pixel(s,5,18);expect(86,std::abs(strip[3]-128)<=2);
}
void selection_undo(){
    Session s;s.selectLeft();const auto count=s.project.history.undoCount();s.apply(settings(120));
    expect(95,s.project.history.undoCount()==count+1&&s.project.history.undoName()=="Hue/Saturation");expect(96,sourceNear(pixel(s,5,5),{0,255,0,255}));expect(97,sourceNear(pixel(s,15,5),{255,0,0,255}));s.undo();expect(99,sourceNear(pixel(s,5,5),{255,0,0,255}));
}
void preview_original(){
    Session s;const auto before=s.project.document;const auto count=s.project.history.undoCount();s.begin();s.update(settings(120));s.ready();
    QPointer<ui::EditPanelSession> edit=s.currentEditor();required(111,bool(edit));const auto id=s.project.active;
    expect(112,sourceNear(previewPixel(*edit,id,5,5),{0,255,0,255}));expect(113,s.project.document==before&&s.project.history.undoCount()==count);
    s.update(settings(240));s.ready();expect(117,sourceNear(previewPixel(*edit,id,5,5),{0,0,255,255}));
    s.update(settings(240),false);expect(120,!edit->previewDocument());s.cancel();expect(122,s.project.document==before&&s.currentEditor()==nullptr);
    s.begin();s.update(settings(120));s.ready();s.commit();expect(128,s.active().raster&&s.active().raster->width==before->layers.front().raster->width);expect(129,sourceNear(pixel(s,5,5),{0,255,0,255}));
}
void band_weights(){
    const auto reds=defaultHueBand(ColorRange::Reds);expect(160,reds.weight(0)==1&&reds.weight(345)==1&&reds.weight(15)==1);
    expect(161,std::abs(reds.weight(330)-.5)<.001);expect(162,std::abs(reds.weight(30)-.5)<.001);expect(163,reds.weight(315)==0&&reds.weight(45)==0&&reds.weight(180)==0);expect(164,defaultHueBand(ColorRange::Master).weight(123)==1);
    auto band=defaultHueBand(ColorRange::Greens);band.setHandle(1,200);expect(168,band==defaultHueBand(ColorRange::Greens));band.setHandle(1,110);expect(170,band.rangeStart==110);
}
void independent_ranges(){
    Session s(true);auto value=settings(60,0,0,ColorRange::Reds);value.adjustments[size_t(ColorRange::Blues)]={0,-100,0};s.begin();s.update(value);s.ready();s.commit();
    const auto red=pixel(s,5,5),blue=pixel(s,30,5);expect(182,sourceNear(red,{255,255,0,255}));expect(183,blue[0]==blue[1]&&blue[1]==blue[2]);
}
void invert_range(){
    Session s(true);const auto before=pixel(s,30,5);s.apply(settings(0,0,-100,ColorRange::Reds));expect(190,sourceNear(pixel(s,5,5),{0,0,0,255}));expect(191,sourceNear(pixel(s,30,5),before));s.undo();
    auto inverted=settings(0,0,-100,ColorRange::Reds);inverted.invertRange=true;s.apply(inverted);expect(197,sourceNear(pixel(s,5,5),{255,0,0,255}));expect(198,sourceNear(pixel(s,30,5),{0,0,0,255}));
}
void range_settings(){
    HueSettings value;value.adjustments[size_t(value.range)].hue=30;value.range=ColorRange::Greens;expect(205,value.adjustments[size_t(value.range)].hue==0);
    value.adjustments[size_t(value.range)].hue=-40;expect(207,value.adjustments[size_t(ColorRange::Master)].hue==30&&value.adjustments[size_t(ColorRange::Greens)].hue==-40);
    value.range=ColorRange::Master;expect(209,value.adjustments[size_t(value.range)].hue==30&&!value.identity());auto greensOnly=settings(60,0,0,ColorRange::Greens);greensOnly.adjustments[size_t(ColorRange::Master)]={};
    expect(213,std::abs(greensOnly.shiftedHue(120)-180)<.001);expect(214,std::abs(greensOnly.shiftedHue(0)-0)<.001);
}
void eyedroppers(){
    Session s(true);s.begin();s.update(settings(10,0,0,ColorRange::Greens),false);s.sample(HueSample::Replace,{5,5});
    required(223,s.currentEditor()!=nullptr);auto current=s.settings();auto band=current.bands[size_t(current.range)];expect(224,band.weight(0)==1&&band.weight(120)==0);
    s.sample(HueSample::Add,{30,5});required(228,s.currentEditor()!=nullptr);current=s.settings();band=current.bands[size_t(current.range)];expect(229,band.weight(240)==1&&band.weight(0)==1);
    s.sample(HueSample::Remove,{30,5});required(233,s.currentEditor()!=nullptr);current=s.settings();band=current.bands[size_t(current.range)];expect(234,band.weight(240)==0);
    s.cancel();expect(236,s.currentEditor()==nullptr||!hueControl(s.editor().panel())->sampleMode());
}
void targeted_drag(){
    Session s(true);s.begin();button(s.editor().panel(),"hueTargetedAdjustment")->click();
    expect(243,s.editor().samplePress({30,5},30,{}));expect(244,s.settings().range==ColorRange::Blues);
    s.editor().sampleMove({30,5},90,{},false);expect(246,s.settings().adjustments[size_t(ColorRange::Blues)].saturation==30);
    s.editor().sampleMove({30,5},50,{},false);expect(249,s.settings().adjustments[size_t(ColorRange::Blues)].saturation==10);
    s.editor().sampleMove({30,5},-10,Qt::ControlModifier,false);expect(251,s.settings().adjustments[size_t(ColorRange::Blues)].hue==-20);
    s.editor().sampleMove({30,5},-10,Qt::ControlModifier,true);s.cancel();expect(254,s.currentEditor()==nullptr||!hueControl(s.editor().panel())->targeting());
}
void sampling_guards(){
    Session s(true);s.begin();required(260,s.currentEditor()!=nullptr);auto value=s.settings();const auto untouched=value.bands[size_t(value.range)];s.sample(HueSample::Replace,{5,5});value=s.settings();expect(263,value.bands[size_t(value.range)]==untouched);
    Session gray;gray.begin();gray.update(settings(0,0,0,ColorRange::Reds),false);required(268,gray.currentEditor()!=nullptr);value=gray.settings();const auto before=value.bands[size_t(value.range)];gray.sample(HueSample::Replace,{30,5});value=gray.settings();expect(271,value.bands[size_t(value.range)]==before);
}
}
int main(int argc,char** argv){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);
    const std::map<std::string,std::function<void()>> cases{{"defaults_noop",defaults_noop},{"hue_ranges",hue_ranges},{"colorize_alpha",colorize_alpha},{"selection_undo",selection_undo},{"preview_original",preview_original},{"band_weights",band_weights},{"independent_ranges",independent_ranges},{"invert_range",invert_range},{"range_settings",range_settings},{"eyedroppers",eyedroppers},{"targeted_drag",targeted_drag},{"sampling_guards",sampling_guards}};
    if(argc!=3||!cases.contains(argv[1])||!QFileInfo(QString::fromLocal8Bit(argv[2])).isAbsolute()||QFileInfo::exists(QString::fromLocal8Bit(argv[2]))){std::fprintf(stderr,"Usage: hue_saturation_source_tests CASE ABSOLUTE_NEW_OUTPUT.json\n");return 2;}
    QString status="passed",error;int code=0;
    try{cases.at(argv[1])();if(failures){status="failed";code=1;}}
    catch(const SourceRequirementFailure& e){status="failed";error=e.what();code=1;}
    catch(const std::exception& e){status="error";error=e.what();code=2;}
    QFile output(QString::fromLocal8Bit(argv[2]));if(!output.open(QIODevice::WriteOnly|QIODevice::NewOnly)){std::fprintf(stderr,"Fresh report output required\n");return 2;}
    output.write(QJsonDocument(QJsonObject{{"schema","HUE_SATURATION_SOURCE_RESULT_V1"},{"case",argv[1]},{"status",status},{"error",error},{"source_expect_failures",failures},{"checks",checks},{"adapter","Production MainWindow/Hue controller and shared settings, canonical renderer, immutable preview raster, real selection/history; Command modifier becomes Windows Control"}}).toJson());output.close();
    std::printf("%s %s checks=%lld failures=%d\n",code==0?"PASS":"FAIL",argv[1],static_cast<long long>(checks.size()),failures);if(!error.isEmpty())std::fprintf(stderr,"%s\n",error.toUtf8().constData());return code;
}
