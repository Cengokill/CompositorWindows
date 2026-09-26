#include "ui/AdjustmentAdvancedControls.h"
#include "effects/Adjustments.h"
#include <QAccessible>
#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QTest>
#include <cmath>
#include <iostream>
#include <map>
#include <stdexcept>
using namespace compositor;
using namespace compositor::effects_tools;
namespace {
QJsonObject observations;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
QWidget* graph(QWidget& owner,const char* name){auto* value=owner.findChild<QWidget*>(name);require(value,"Graph widget exists");return value;}
QAccessibleInterface* accessible(QObject* object){auto* value=QAccessible::queryAccessibleInterface(object);require(value&&value->isValid(),"Qt accessible interface exists");return value;}
void collect(QAccessibleInterface* value,QJsonArray& out,int depth=0){if(!value||depth>12)return;out.append(QJsonObject{{"name",value->text(QAccessible::Name)},{"help",value->text(QAccessible::Help)},{"description",value->text(QAccessible::Description)},{"role",int(value->role())},{"focusable",bool(value->state().focusable)},{"children",value->childCount()},{"value_interface",value->valueInterface()!=nullptr}});for(int i=0;i<value->childCount();++i)collect(value->child(i),out,depth+1);}
void show(QWidget& widget){widget.resize(480,360);widget.show();widget.activateWindow();QApplication::processEvents();}
void activate(QPushButton* button){require(button&&button->isEnabled(),"Source button enabled");button->setFocus();QApplication::processEvents();require(button->hasFocus(),"Keyboard focus reaches source button");QTest::keyClick(button,Qt::Key_Space);QApplication::processEvents();}
struct LevelsFixture {
    LevelsAdvancedControls widget;QWidget* chart;
    LevelsFixture(){widget.setAdjustmentJson(effects::defaultAdjustmentJson("Levels"));show(widget);chart=graph(widget,"levelsHistogramGraph");}
    void record(){QJsonArray tree;collect(accessible(chart),tree);observations["graph_accessibility"]=tree;observations["graph_focus_policy"]=int(chart->focusPolicy());}
};
void levelsHandles(){LevelsFixture f;f.record();QJsonArray tree;collect(accessible(f.chart),tree);QJsonArray missing;for(const char* name:{"Input black","Gamma","Input white","Output black","Output white"}){bool found=false;for(const auto value:tree)if(value.toObject()["name"].toString()==name)found=true;if(!found)missing.append(name);}observations["missing_source_handle_names"]=missing;require(missing.isEmpty(),"Levels must expose all five individually named source handles");}
void levelsHelp(){LevelsFixture f;f.record();auto* value=accessible(f.chart);const auto help=value->text(QAccessible::Help)+value->text(QAccessible::Description);require(help.contains("Linear histogram")&&help.contains("0 to 255"),"Histogram must expose source explanation of scale and full tone range");}
void levelsChannel(){LevelsFixture f;auto settings=levelsFromAdjustmentJson(f.widget.adjustmentJson());settings.channel=LevelsChannel::Blue;f.widget.setAdjustmentJson(withLevelsSettings(f.widget.adjustmentJson(),settings));f.record();require(accessible(f.chart)->text(QAccessible::Name).contains("Original Blue histogram"),"Histogram accessible name follows active channel");}
void levelsSampler(){LevelsFixture f;activate(f.widget.findChild<QPushButton*>("levelsSampleGray"));require(f.widget.sampleMode()==LevelsSample::Gray,"Keyboard toggles Gray sampler on");activate(f.widget.findChild<QPushButton*>("levelsSampleGray"));require(!f.widget.sampleMode(),"Keyboard toggles Gray sampler off");}
void levelsAuto(){LevelsFixture f;LevelsHistogram bins{};for(auto& channel:bins){channel[30]=1;channel[200]=1;}f.widget.setHistogram(bins);activate(f.widget.findChild<QPushButton*>("levelsAutoContrast"));const auto settings=levelsFromAdjustmentJson(f.widget.adjustmentJson());require(settings.ranges[0].black==30&&settings.ranges[0].white==200,"Keyboard Auto Contrast applies source histogram bounds");}
struct CurvesFixture {
    CurvesAdvancedControls widget;QWidget* chart;
    CurvesFixture(){widget.setAdjustmentJson(effects::defaultAdjustmentJson("Curves"));show(widget);chart=graph(widget,"curvesGraph");}
    void insert(){QTest::mouseClick(chart,Qt::LeftButton,{},QPoint(int(std::lround(8+128./255*(chart->width()-16))),int(std::lround(8+(1-190./255)*(chart->height()-16)))));QApplication::processEvents();require(curvesFromAdjustmentJson(widget.adjustmentJson()).channels[0].size()==3,"Fixture inserts interior point through actual graph event");}
    void record(){QJsonArray tree;collect(accessible(chart),tree);observations["graph_accessibility"]=tree;observations["graph_focus_policy"]=int(chart->focusPolicy());const auto before=widget.adjustmentJson();QTest::keyClick(chart,Qt::Key_Up);observations["up_key_changes_curve"]=widget.adjustmentJson()!=before;}
};
void curvesCoordinates(){CurvesFixture f;f.insert();f.record();const auto point=curvesFromAdjustmentJson(f.widget.adjustmentJson()).channels[0][1];bool found=false;for(auto* label:f.widget.findChildren<QLabel*>()){const auto name=accessible(label)->text(QAccessible::Name);if(name.contains(QString("Input %1").arg(int(point.x)))&&name.contains(QString("Output %1").arg(int(point.y))))found=true;}require(found,"Selected source Input/Output coordinates are exposed as accessible text");}
void curvesRemove(){CurvesFixture f;f.insert();activate(f.widget.findChild<QPushButton*>("curvesRemovePoint"));require(curvesFromAdjustmentJson(f.widget.adjustmentJson()).channels[0].size()==2,"Keyboard Remove deletes selected interior point");require(!f.widget.findChild<QPushButton*>("curvesRemovePoint")->isEnabled(),"Remove is disabled after selection clears");}
void curvesReset(){CurvesFixture f;f.insert();activate(f.widget.findChild<QPushButton*>("curvesResetCurve"));require(curvesFromAdjustmentJson(f.widget.adjustmentJson())==CurvesSettings{},"Keyboard Reset restores source identity curve");require(!f.widget.findChild<QPushButton*>("curvesRemovePoint")->isEnabled(),"Reset clears point selection");}
}
int main(int argc,char** argv){QApplication app(argc,argv);const std::map<std::string,void(*)()> cases{{"levels_handle_names",levelsHandles},{"levels_histogram_help",levelsHelp},{"levels_channel_name",levelsChannel},{"levels_sampler_keyboard",levelsSampler},{"levels_auto_keyboard",levelsAuto},{"curves_selected_coordinates",curvesCoordinates},{"curves_remove_keyboard",curvesRemove},{"curves_reset_keyboard",curvesReset}};const std::string key=argc>1?argv[1]:"";QJsonObject result{{"case",QString::fromStdString(key)}};int code=1;try{require(argc==3&&cases.contains(key),"Provide case and output path");cases.at(key)();result["status"]="passed";code=0;std::cout<<"PASS "<<key<<'\n';}catch(const std::exception& error){result["status"]="failed";result["error"]=error.what();std::cerr<<"FAIL "<<key<<": "<<error.what()<<'\n';}result["observations"]=observations;if(argc==3){QFile file(QString::fromLocal8Bit(argv[2]));if(!file.open(QIODevice::WriteOnly)||file.write(QJsonDocument(result).toJson())<0)return 2;}return code;}
