#include "ui/MainWindow.h"
#include "ui/AdjustmentAdvancedControls.h"
#include "effects/Adjustments.h"
#include "editing/PixelEdits.h"
#include "persistence/ProjectStore.h"
#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidgetItemIterator>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>

using namespace compositor;
using namespace compositor::effects_tools;
namespace {
struct SourceRequirementFailure:std::runtime_error {using std::runtime_error::runtime_error;};
struct MissingObservation:std::runtime_error {using std::runtime_error::runtime_error;};
QJsonArray checks;
int failures{};
void check(int line,bool value,const char* kind){
    checks.append(QJsonObject{{"source_line",line},{"kind",kind},{"passed",value}});
    if(!value){++failures;std::fprintf(stderr,"FAIL source %s line%d\n",kind,line);}
}
void expect(int line,bool value){check(line,value,"expect");}
void required(int line,bool value){check(line,value,"require");if(!value)throw SourceRequirementFailure("Source #require failed at"+std::to_string(line));}
void guard(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Fn>void wait(Fn predicate,const char* message){QElapsedTimer timer;timer.start();while(!predicate()){if(timer.elapsed()>30000)throw std::runtime_error(message);QApplication::processEvents(QEventLoop::AllEvents,20);QTest::qWait(1);}}
QJsonObject object(std::string_view json){QJsonParseError error;auto parsed=QJsonDocument::fromJson(QByteArray(json.data(),qsizetype(json.size())),&error);guard(error.error==QJsonParseError::NoError&&parsed.isObject(),"Adjustment JSON object");return parsed.object();}
std::string encoded(const QJsonObject& value){return QJsonDocument(value).toJson(QJsonDocument::Compact).toStdString();}
bool equal(std::string_view a,std::string_view b){return object(a)==object(b);}
std::shared_ptr<const Raster> image(PaletteColor color,std::array<uint8_t,4> alpha={255,255,255,255}){
    std::array<uint8_t,16> data{};
    for(size_t i=0;i<4;++i){data[4*i]=uint8_t(std::lround(color.red*alpha[i]));data[4*i+1]=uint8_t(std::lround(color.green*alpha[i]));data[4*i+2]=uint8_t(std::lround(color.blue*alpha[i]));data[4*i+3]=alpha[i];}
    auto result=Raster::fromRgba(2,2,data.data(),8);
    required(8,result&&result->width==2&&result->height==2);
    return result;
}
std::vector<uint8_t> pixels(const std::shared_ptr<const Raster>& raster){guard(bool(raster),"Native image reader has backing raster");return raster->rgba();}
std::vector<uint8_t> slice(const std::vector<uint8_t>& data,size_t first,size_t last){guard(last<=data.size(),"Native source-byte slice bounds");return {data.begin()+first,data.begin()+last};}
QAction* command(MainWindow& window,const char* id){for(auto* action:window.findChildren<QAction*>())if(action->property("commandId")==id)return action;throw std::runtime_error(std::string("Missing command ")+id);}
void trigger(MainWindow& window,const char* id){window.activateWindow();if(window.canvas())window.canvas()->setFocus();QApplication::processEvents();for(auto* child:window.findChildren<QObject*>())if(auto* registry=dynamic_cast<ui::CommandRegistry*>(child))registry->refresh();auto* action=command(window,id);guard(action->isEnabled(),"Requested native command enabled");action->trigger();QApplication::processEvents();}
QPushButton* apply(QDialog* dialog){auto* box=dialog->findChild<QDialogButtonBox*>();guard(box,"Editor has button box");auto* button=box->button(QDialogButtonBox::Apply);guard(button,"Editor has Apply");return button;}
void ready(QDialog* dialog){wait([&]{return apply(dialog)->isEnabled();},"Editor preview ready");}
void commit(QDialog* dialog){QPointer<QDialog> token=dialog;ready(dialog);apply(dialog)->click();wait([token]{return !token||!token->isVisible();},"Editor Apply finished");QApplication::processEvents();}
template<class T>T* controls(QDialog* dialog){for(auto* child:dialog->findChildren<QObject*>())if(auto* typed=dynamic_cast<T*>(child))return typed;throw std::runtime_error("Native typed editor controls missing");}
QDoubleSpinBox* field(QDialog* dialog,const QString& name){for(auto* child:dialog->findChildren<QDoubleSpinBox*>())if(child->accessibleName()==name)return child;throw std::runtime_error("Native named numeric field missing");}
QCheckBox* flag(QDialog* dialog,const QString& name){for(auto* child:dialog->findChildren<QCheckBox*>())if(child->text()==name)return child;throw std::runtime_error("Native named flag missing");}
QDialog* visiblePanel(MainWindow& window){for(auto* panel:window.findChildren<QDialog*>())if(panel->isVisible()&&panel->objectName()=="adjustmentDialog")return panel;return nullptr;}
ui::EditPanelSession* controller(MainWindow& window,QDialog* panel){for(auto* child:window.findChildren<QObject*>())if(auto* owner=dynamic_cast<ui::EditPanelSession*>(child);owner&&owner->panel()==panel)return owner;throw std::runtime_error("Owned editor controller exists");}
template<class T>std::optional<std::string> originalSettings(const T& owner){if constexpr(requires(const T& item){item.originalAdjustmentSettings();})return owner.originalAdjustmentSettings();else throw MissingObservation("Source133 requires the actual captured original adjustment observer");}
struct Session {
    MainWindow window{true};EditorProject& project;
    Session():project(window.addProject([]{Document d;d.id=newId();d.width=d.height=2;return d;}())){window.show();QApplication::processEvents();}
    Document& document(){guard(project.document.has_value(),"Native session document exists");return *project.document;}
    Layer& active(){auto& layers=document().layers;auto found=std::find_if(layers.begin(),layers.end(),[&](const Layer& l){return l.id==project.active;});guard(found!=layers.end(),"Native active layer exists");return *found;}
    std::string insert(std::shared_ptr<const Raster> raster){Layer layer;layer.id=newId();layer.name="Fixture";layer.raster=std::move(raster);layer.transform={0,0,2,2};project.history.begin("Import",project.document,project.active);document().layers.push_back(layer);project.active=layer.id;project.selected={layer.id};project.history.end(project.document,project.active);trigger(window,"tool.move");return layer.id;}
    void select(const std::string& id){project.active=id;project.selected={id};trigger(window,"tool.move");}
    void toggleVisibility(){auto* tree=window.findChild<QTreeWidget*>();guard(tree,"Native layer tree exists");for(QTreeWidgetItemIterator item(tree);*item;++item)if((*item)->data(0,Qt::UserRole).toString().toStdString()==project.active){(*item)->setCheckState(0,active().visible?Qt::Unchecked:Qt::Checked);QApplication::processEvents();return;}throw std::runtime_error("Native visibility row exists");}
    void blend(Blend value){auto* picker=window.findChild<QComboBox*>("layerBlend");guard(picker&&picker->isEnabled(),"Native blend picker enabled");picker->setCurrentIndex(int(value));QApplication::processEvents();}
    QDialog* open(const std::string& commandId){trigger(window,commandId.c_str());QDialog* result=nullptr;wait([&]{result=visiblePanel(window);return result!=nullptr;},"Native adjustment editor opens");ready(result);return result;}
    std::string add(const char* kind){auto* panel=open(std::string("adjust.new.")+kind);panel->reject();QApplication::processEvents();required(23,!project.active.empty());return project.active;}
    void settings(const std::string& id,std::string value){effects::validateAdjustmentJson(value);for(auto& layer:document().layers)if(layer.id==id){layer.adjustmentJson=std::move(value);return;}throw std::runtime_error("Native adjustment target exists");}
    std::string effective(){project.canvas->viewportProvider(0,0,2,2,1);const auto& shown=project.effectPreview?*project.effectPreview:document();auto found=std::find_if(shown.layers.begin(),shown.layers.end(),[&](const Layer& l){return l.id==project.active;});guard(found!=shown.layers.end(),"Effective preview target exists");return found->adjustmentJson;}
};
std::vector<uint8_t> rendered(Session& session){required(19,session.project.document.has_value());validateDocument(session.document());return pixels(SoftwareRenderer().render(session.document(),0,0,2,2));}
void global_live(){
    Session s;s.insert(image({1,1,1}));required(28,!s.project.active.empty());const auto base=s.project.active;const auto adjustment=s.add("levels");
    LevelsSettings levels;levels.ranges[0].outputWhite=0;s.settings(adjustment,withLevelsSettings(effects::defaultAdjustmentJson("Levels"),levels));
    expect(32,rendered(s)==std::vector<uint8_t>({0,0,0,255,0,0,0,255,0,0,0,255,0,0,0,255}));
    s.insert(image({1,0,0},{255,0,0,0}));expect(34,slice(rendered(s),0,4)==std::vector<uint8_t>({255,0,0,255}));
    s.document().layers[0].raster=image({0,0,1});expect(36,s.document().layers[0].id==base);expect(37,slice(rendered(s),4,8)==std::vector<uint8_t>({0,0,0,255}));
    s.select(adjustment);s.toggleVisibility();expect(39,slice(rendered(s),4,8)==std::vector<uint8_t>({0,0,255,255}));
}
void clipped_curve(){
    Session s;s.insert(image({0,0,1}));s.insert(image({0,1,0},{255,0,128,0}));const auto adjustment=s.add("curves");CurvesSettings curve;curve.channels[0]={{0,255},{255,0}};
    s.settings(adjustment,withCurvesSettings(effects::defaultAdjustmentJson("Curves"),curve));trigger(s.window,"layer.clipping");const auto result=rendered(s);
    expect(51,slice(result,0,4)==std::vector<uint8_t>({255,0,255,255}));expect(52,slice(result,4,8)==std::vector<uint8_t>({0,0,255,255}));
    const auto copied=editing::copyPixels(s.document(),std::nullopt,SoftwareRenderer());required(53,copied&&copied->raster);expect(53,pixels(copied->raster)==result);
    trigger(s.window,"layer.clipping");expect(55,slice(rendered(s),4,8)==std::vector<uint8_t>({255,255,0,255}));
}
std::string legacyHue(double hue){auto value=object(effects::defaultAdjustmentJson("Hue/Saturation"));value["hue"]=hue;return encoded(value);}
void hue_mask(){
    Session s;s.insert(image({1,0,0}));const auto original=s.document().layers[0].raster;const auto id=s.add("hue_saturation");s.settings(id,legacyHue(120));const auto result=rendered(s);
    expect(65,result[1]>250&&result[0]<5&&result[2]<5);s.active().opacity=0;expect(67,slice(rendered(s),0,4)==std::vector<uint8_t>({255,0,0,255}));s.active().opacity=1;
    s.active().mask=Mask{std::make_shared<GrayRaster>(GrayRaster{1,1,{0}})};expect(70,slice(rendered(s),0,4)==std::vector<uint8_t>({255,0,0,255}));expect(71,s.document().layers[0].raster==original);
}
void persistence(){
    Session s;s.insert(image({1,1,1}));const auto id=s.add("curves");CurvesSettings curve;curve.channels[0].insert(curve.channels[0].begin()+1,{128,190});const auto value=withCurvesSettings(effects::defaultAdjustmentJson("Curves"),curve);
    s.project.history.begin("Edit Curves",s.project.document,s.project.active);s.settings(id,value);s.project.history.end(s.project.document,s.project.active);
    trigger(s.window,"edit.undo");expect(79,curvesFromAdjustmentJson(s.active().adjustmentJson)==CurvesSettings{});trigger(s.window,"edit.redo");expect(80,equal(s.active().adjustmentJson,value));
    trigger(s.window,"layer.duplicate");expect(81,equal(s.active().adjustmentJson,value));QTemporaryDir directory;guard(directory.isValid(),"Native temporary project directory");const auto path=std::filesystem::path(directory.filePath("Adjustment.comp").toStdWString());
    ProjectStore store(makeWicProjectCodec());required(84,s.project.document.has_value());store.save(path,s.document(),s.project.active);const auto loaded=store.load(path);expect(86,equal(loaded.document.layers.back().adjustmentJson,value));
    Session restored;restored.project.document=loaded.document;restored.project.active=loaded.activeLayer;expect(88,rendered(restored)==rendered(s));
}
void curves_image(){
    const auto asset=image({.4,.7,.1},{255,128,32,0});expect(92,pixels(effects::applyAdjustment(asset,effects::defaultAdjustmentJson("Curves")))==pixels(asset));Session s;s.insert(asset);auto* panel=s.open("adjust.curves");auto* control=controls<CurvesAdvancedControls>(panel);
    CurvesSettings curve;curve.channels[0]={{0,255},{255,255}};auto settings=withCurvesSettings(control->adjustmentJson(),curve);control->setAdjustmentJson(settings);control->onChanged(settings);commit(panel);required(97,bool(s.active().raster));const auto p=pixels(s.active().raster);
    expect(98,p==std::vector<uint8_t>({255,255,255,255,128,128,128,128,32,32,32,32,0,0,0,0}));trigger(s.window,"edit.undo");expect(99,s.active().raster==asset);
}
void blend_coverage(){
    Session s;s.insert(image({1,0,0},{255,128,32,0}));const auto id=s.add("hue_saturation");s.settings(id,legacyHue(120));s.blend(Blend::Multiply);
    expect(108,rendered(s)==std::vector<uint8_t>({0,0,0,255,0,0,0,128,0,0,0,32,0,0,0,0}));s.blend(Blend::Normal);auto gray=std::make_shared<GrayRaster>(GrayRaster{1,1,{128}});required(112,gray&&gray->pixels.size()==1);s.active().mask=Mask{gray};const auto p=rendered(s);
    expect(114,p[3]==255&&p[7]==128&&p[11]==32&&p[15]==0);expect(115,std::abs(int(p[0])-127)<=2&&std::abs(int(p[1])-128)<=2);
}
template<class T>void exactHistogram(T& controls){
    // This observation is deliberately unavailable until the production read-only
    // accessor exists; an independent histogram would not test the async editor.
    if constexpr(requires(const T& item){item.histogramReady();item.histogram();}){
        wait([&]{return controls.histogramReady();},"Live Levels histogram completes");expect(139,controls.histogramReady());expect(140,controls.histogram()[0][255]==4);
    }else throw MissingObservation("Source139-140 requires actual Levels histogram/ready const accessors");
}
QJsonObject settingPart(std::string_view json,const char* key){auto value=object(json);if(value.contains(key))return value[key].toObject();if(std::string_view(key)=="exposureSettings")return {{"exposure",0},{"offset",0},{"gamma",1}};if(std::string_view(key)=="gradientMapSettings")return {{"shadows",QJsonObject{{"red",0},{"green",0},{"blue",0}}},{"highlights",QJsonObject{{"red",1},{"green",1},{"blue",1}}},{"reversed",false}};return {{"amount",25},{"size",1.5},{"roughness",50},{"seed",0}};}
bool threePartsEqual(std::string_view a,std::string_view b){return settingPart(a,"exposureSettings")==settingPart(b,"exposureSettings")&&settingPart(a,"gradientMapSettings")==settingPart(b,"gradientMapSettings")&&settingPart(a,"grainSettings")==settingPart(b,"grainSettings");}
template<class T>void resetFullGrain(T& session,std::string_view current){
    auto value=object(current);value["grainSettings"]=QJsonObject{{"amount",25},{"size",1.5},{"roughness",50},{"seed",0}};
    if constexpr(requires(T& item,const std::string& json){item.updateAdjustmentSettings(json,true);}){
        guard(session.updateAdjustmentSettings(encoded(value),true),"Controller accepted valid source FilterSettings reset");
    }else throw MissingObservation("Source193 full Grain settings reset includes seed=0; controller settings API unavailable");
}
void shared_editor(const char* commandKind,const char* title){
    Session s;const auto asset=image({1,1,1});s.insert(asset);required(127,!s.project.active.empty());const auto baseID=s.project.active;QPointer<QDialog> panel=s.open(std::string("adjust.new.")+commandKind);
    required(129,panel&&!s.project.active.empty());const auto id=s.project.active;required(131,!s.active().adjustmentJson.empty());const auto created=s.active().adjustmentJson;
    auto* owner=controller(s.window,panel);const auto captured=originalSettings(*owner);
    expect(133,captured&&object(*captured)["kind"].toString()==title);expect(134,!s.project.projectBusy&&!owner->committing());
    const std::string kind=commandKind;
    if(kind=="levels"){
        auto* edit=controls<LevelsAdvancedControls>(panel);required(137,edit!=nullptr);exactHistogram(*edit);auto settings=levelsFromAdjustmentJson(edit->adjustmentJson());settings.ranges[0].outputWhite=0;
        auto json=withLevelsSettings(edit->adjustmentJson(),settings);edit->setAdjustmentJson(json);edit->onChanged(json);ready(panel);expect(144,levelsFromAdjustmentJson(s.effective())==settings);
        panel->findChild<QCheckBox*>("adjustmentPreviewEnabled")->setChecked(false);ready(panel);expect(146,equal(s.effective(),effects::defaultAdjustmentJson("Levels")));commit(panel);
    }else if(kind=="curves"){
        auto* edit=controls<CurvesAdvancedControls>(panel);required(149,edit!=nullptr);auto settings=curvesFromAdjustmentJson(edit->adjustmentJson());settings.channels[0]={{0,0},{255,0}};auto json=withCurvesSettings(edit->adjustmentJson(),settings);edit->setAdjustmentJson(json);edit->onChanged(json);ready(panel);expect(152,curvesFromAdjustmentJson(s.effective())==settings);commit(panel);
    }else if(kind=="hue_saturation"){
        auto* edit=controls<HueAdvancedControls>(panel);required(167,edit!=nullptr);auto settings=hueFromAdjustmentJson(edit->adjustmentJson());settings.range=ColorRange::Reds;settings.adjustments[1].hue=80;settings.bands[1]=settings.bands[1].centered(25);settings.invertRange=true;
        auto json=withHueSettings(edit->adjustmentJson(),settings);edit->setAdjustmentJson(json);edit->onChanged(json);ready(panel);expect(173,hueFromAdjustmentJson(s.effective())==settings);commit(panel);
    }else{
        expect(155,panel->windowTitle()==title);required(156,panel&&!s.active().adjustmentJson.empty());auto settings=object(created);
        if(kind=="exposure"){auto part=settingPart(created,"exposureSettings");part["exposure"]=1;settings["exposureSettings"]=part;field(panel,"Exposure")->setValue(1);}
        else if(kind=="gradient_map"){auto part=settingPart(created,"gradientMapSettings");part["reversed"]=true;settings["gradientMapSettings"]=part;flag(panel,"Reverse")->setChecked(true);}
        else {auto part=settingPart(created,"grainSettings");part["amount"]=70;settings["grainSettings"]=part;field(panel,"Amount")->setValue(70);}
        ready(panel);const auto live=s.effective();required(163,!live.empty());expect(164,threePartsEqual(live,encoded(settings)));commit(panel);
    }
    required(176,!s.active().adjustmentJson.empty());const auto saved=s.active().adjustmentJson;expect(177,!visiblePanel(s.window));auto base=std::find_if(s.document().layers.begin(),s.document().layers.end(),[&](const Layer& l){return l.id==baseID;});expect(178,base!=s.document().layers.end()&&base->raster==asset);expect(179,!s.active().raster);
    const auto decoded=encoded(object(saved));effects::validateAdjustmentJson(decoded);expect(181,equal(decoded,saved));s.select(id);panel=s.open("adjust.edit");
    if(kind=="levels"){
        auto* edit=controls<LevelsAdvancedControls>(panel);expect(186,levelsFromAdjustmentJson(edit->adjustmentJson())==levelsFromAdjustmentJson(saved));auto json=withLevelsSettings(edit->adjustmentJson(),{});edit->setAdjustmentJson(json);edit->onChanged(json);ready(panel);panel->reject();
    }else if(kind=="hue_saturation"){
        auto* edit=controls<HueAdvancedControls>(panel);expect(196,hueFromAdjustmentJson(edit->adjustmentJson())==hueFromAdjustmentJson(saved));auto json=withHueSettings(edit->adjustmentJson(),{});edit->setAdjustmentJson(json);edit->onChanged(json);ready(panel);panel->reject();
    }else{
        // Read the actual reopened controller settings; saved canonical metadata
        // must not conceal a missing or incorrect editor-state reload.
        const auto reopened=controller(s.window,panel)->adjustmentSettings();required(190,!reopened.empty());expect(191,curvesFromAdjustmentJson(reopened)==curvesFromAdjustmentJson(saved)&&threePartsEqual(reopened,saved));
        const auto defaults=effects::defaultAdjustmentJson(title);
        guard(kind=="curves"||curvesFromAdjustmentJson(reopened)==CurvesSettings{},"Untouched source FilterSettings curves already default");
        guard(kind=="exposure"||settingPart(reopened,"exposureSettings")==settingPart(defaults,"exposureSettings"),"Untouched source FilterSettings exposure already default");
        if(kind=="gradient_map"){auto reset=settingPart(reopened,"gradientMapSettings");reset["reversed"]=false;guard(reset==settingPart(defaults,"gradientMapSettings"),"Source default palette endpoint precondition");}
        else guard(settingPart(reopened,"gradientMapSettings")==settingPart(defaults,"gradientMapSettings"),"Untouched source FilterSettings gradient map already default");
        guard(kind=="grain"||settingPart(reopened,"grainSettings")==settingPart(defaults,"grainSettings"),"Untouched source FilterSettings grain already default");
        if(kind=="curves"){auto* edit=controls<CurvesAdvancedControls>(panel);auto json=withCurvesSettings(edit->adjustmentJson(),{});edit->setAdjustmentJson(json);edit->onChanged(json);}
        else if(kind=="exposure"){field(panel,"Exposure")->setValue(0);field(panel,"Offset")->setValue(0);field(panel,"Gamma")->setValue(1);}
        else if(kind=="gradient_map")flag(panel,"Reverse")->setChecked(false);
        else resetFullGrain(*controller(s.window,panel),reopened);
        ready(panel);panel->reject();
    }
    QApplication::processEvents();expect(200,equal(s.active().adjustmentJson,saved));expect(201,!visiblePanel(s.window));trigger(s.window,"edit.undo");expect(203,equal(s.active().adjustmentJson,created));trigger(s.window,"edit.redo");expect(205,equal(s.active().adjustmentJson,saved));
}
void legacy_hsv(){const auto data=legacyHue(120);const auto decoded=encoded(object(data));effects::validateAdjustmentJson(decoded);expect(211,!object(decoded).contains("hsvSettings")||object(decoded)["hsvSettings"].isNull());expect(212,hueFromAdjustmentJson(decoded).adjustments[0].hue==120);}
}
int main(int argc,char** argv){
    QApplication app(argc,argv);const std::map<std::string,std::function<void()>> cases{{"global_live",global_live},{"clipped_curve",clipped_curve},{"hue_mask",hue_mask},{"persistence",persistence},{"curves_image",curves_image},{"blend_coverage",blend_coverage},{"editor_hsv",[]{shared_editor("hue_saturation","Hue/Saturation");}},{"editor_levels",[]{shared_editor("levels","Levels");}},{"editor_curves",[]{shared_editor("curves","Curves");}},{"editor_exposure",[]{shared_editor("exposure","Exposure");}},{"editor_gradient_map",[]{shared_editor("gradient_map","Gradient Map");}},{"editor_grain",[]{shared_editor("grain","Grain");}},{"legacy_hsv",legacy_hsv}};
    if(argc!=3||!cases.contains(argv[1])){std::fprintf(stderr,"Usage: adjustment_source_tests CASE NEW_OUTPUT.json\n");return 2;}
    QString status="passed",error;int code=0;
    try{cases.at(argv[1])();if(failures){status="failed";code=1;}}
    catch(const SourceRequirementFailure& e){status="failed";error=e.what();code=1;}
    catch(const MissingObservation& e){status="incomplete";error=e.what();code=2;}
    catch(const std::exception& e){status="error";error=e.what();code=2;}
    QFile output(QString::fromLocal8Bit(argv[2]));if(!output.open(QIODevice::WriteOnly|QIODevice::NewOnly)){std::fprintf(stderr,"Fresh report output required\n");return 2;}
    const auto report=QJsonObject{{"case",argv[1]},{"status",status},{"error",error},{"source_expect_failures",failures},{"checks",checks},{"adapter","immutable effective preview document; actual UI controls/history/renderer/project codecs"}};output.write(QJsonDocument(report).toJson());output.close();
    std::printf("%s %s checks=%lld failures=%d\n",code==0?"PASS":"FAIL",argv[1],static_cast<long long>(checks.size()),failures);if(!error.isEmpty())std::fprintf(stderr,"%s\n",error.toUtf8().constData());return code;
}
