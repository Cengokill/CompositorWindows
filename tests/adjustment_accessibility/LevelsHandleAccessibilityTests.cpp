#include "ui/AdjustmentAdvancedControls.h"
#include "effects/Adjustments.h"
#include <QAccessible>
#include <QApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QTest>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
using namespace compositor;
using namespace compositor::effects_tools;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void close(double actual,double expected){require(std::abs(actual-expected)<1e-10,"Accessible value equals normalized Levels setting");}
struct Fixture {
    LevelsAdvancedControls widget;
    Fixture(){widget.setAdjustmentJson(effects::defaultAdjustmentJson("Levels"));widget.resize(480,360);widget.show();widget.activateWindow();QApplication::processEvents();}
    QWidget* handle(const char* suffix){auto* object=widget.findChild<QWidget*>(QString("levelsHandle")+suffix);require(object,"Named handle widget exists");return object;}
    QAccessibleInterface* provider(const char* suffix){auto* result=QAccessible::queryAccessibleInterface(handle(suffix));require(result&&result->isValid()&&result->role()==QAccessible::Slider,"Handle exposes valid Slider role");require(result->valueInterface()&&result->actionInterface(),"Handle exposes value and action interfaces");return result;}
    LevelRange range(int channel=0){return levelsFromAdjustmentJson(widget.adjustmentJson()).ranges[size_t(channel)];}
};
void values(){Fixture f;const char* names[]{"Inputblack","Gamma","Inputwhite","Outputblack","Outputwhite"};const double current[]{0,1,255,0,255},minimum[]{0,.1,1,0,0},maximum[]{254,9.99,255,255,255};for(int i=0;i<5;++i){auto* v=f.provider(names[i])->valueInterface();close(v->currentValue().toDouble(),current[i]);close(v->minimumValue().toDouble(),minimum[i]);close(v->maximumValue().toDouble(),maximum[i]);close(v->minimumStepSize().toDouble(),i==1?.01:1);}
    auto* black=f.provider("Inputblack")->valueInterface();black->setCurrentValue(17.6);close(f.range().black,18);black->setCurrentValue(900);close(f.range().black,254);
    auto* white=f.provider("Inputwhite")->valueInterface();white->setCurrentValue(-20);close(f.range().white,255);
    auto* gamma=f.provider("Gamma")->valueInterface();gamma->setCurrentValue(2.25);close(f.range().gamma,2.25);gamma->setCurrentValue(99);close(f.range().gamma,9.99);gamma->setCurrentValue(-5);close(f.range().gamma,.1);
    gamma->setCurrentValue(std::numeric_limits<double>::quiet_NaN());gamma->setCurrentValue(QString("invalid"));close(f.range().gamma,.1);
    f.provider("Outputblack")->valueInterface()->setCurrentValue(230);f.provider("Outputwhite")->valueInterface()->setCurrentValue(25);close(f.range().outputBlack,230);close(f.range().outputWhite,25);
}
void actions(){Fixture f;int changed=0;f.widget.onChanged=[&](const std::string&){++changed;};auto* p=f.provider("Gamma");auto* actions=p->actionInterface();require(actions->actionNames().contains(QAccessibleActionInterface::increaseAction())&&actions->actionNames().contains(QAccessibleActionInterface::decreaseAction()),"Both accessible increment actions exposed");actions->doAction(QAccessibleActionInterface::increaseAction());close(f.range().gamma,1.01);actions->doAction(QAccessibleActionInterface::decreaseAction());close(f.range().gamma,1);require(changed==2,"Each effective action emits one production change");f.widget.setEnabled(false);require(p->state().disabled,"Disabled parent disables accessible handle");actions->doAction(QAccessibleActionInterface::increaseAction());p->valueInterface()->setCurrentValue(2);close(f.range().gamma,1);require(changed==2,"Disabled controls cannot mutate settings");}
void keyboard(){Fixture f;auto* black=f.handle("Inputblack");f.provider("Inputblack")->actionInterface()->doAction(QAccessibleActionInterface::setFocusAction());QApplication::processEvents();require(black->hasFocus(),"Accessible focus action focuses actual handle");QTest::keyClick(black,Qt::Key_Right);close(f.range().black,1);QTest::keyClick(black,Qt::Key_Tab);auto* gamma=f.handle("Gamma");require(gamma->hasFocus(),"Tab moves from black to Gamma handle");QTest::keyClick(gamma,Qt::Key_Up);close(f.range().gamma,1.01);QTest::keyClick(gamma,Qt::Key_Home);close(f.range().gamma,.1);QTest::keyClick(gamma,Qt::Key_End);close(f.range().gamma,9.99);QTest::keyClick(gamma,Qt::Key_Tab);require(f.handle("Inputwhite")->hasFocus(),"Tab retains source handle order");}
void channel(){Fixture f;auto* provider=f.provider("Inputblack");const auto token=QAccessible::uniqueId(provider);provider->valueInterface()->setCurrentValue(20);auto state=levelsFromAdjustmentJson(f.widget.adjustmentJson());state.channel=LevelsChannel::Blue;state.ranges[3].black=40;f.widget.setAdjustmentJson(withLevelsSettings(f.widget.adjustmentJson(),state));require(f.provider("Inputblack")==provider&&QAccessible::uniqueId(provider)==token,"Channel changes preserve handle provider identity");close(provider->valueInterface()->currentValue().toDouble(),40);provider->actionInterface()->doAction(QAccessibleActionInterface::increaseAction());close(f.range(3).black,41);close(f.range(0).black,20);auto* graph=f.widget.findChild<QWidget*>("levelsHistogramGraph");require(graph,"Histogram exists");const auto childRect=provider->rect();require(childRect.contains(f.handle("Inputblack")->mapToGlobal(f.handle("Inputblack")->rect().center())),"Accessible bounds match the actual handle");}
void lifetime(){auto owner=std::make_unique<Fixture>();auto* provider=owner->provider("Gamma");const auto token=QAccessible::uniqueId(provider);QPointer<QWidget> handle=owner->handle("Gamma");owner.reset();QApplication::processEvents();require(!handle,"Handle destroyed with controls");require(QAccessible::accessibleInterface(token)==nullptr,"Qt removes cached provider after owning widget destruction");Fixture next;close(next.provider("Gamma")->valueInterface()->currentValue().toDouble(),1);}
}
int main(int argc,char** argv){QApplication app(argc,argv);const std::map<std::string,void(*)()> cases{{"values",values},{"actions",actions},{"keyboard",keyboard},{"channel",channel},{"lifetime",lifetime}};const std::string key=argc>1?argv[1]:"";QJsonObject result{{"case",QString::fromStdString(key)}};int code=1;try{require(argc==3&&cases.contains(key),"Provide case and output path");cases.at(key)();result["status"]="passed";code=0;std::cout<<"PASS "<<key<<'\n';}catch(const std::exception& error){result["status"]="failed";result["error"]=error.what();std::cerr<<"FAIL "<<key<<": "<<error.what()<<'\n';}if(argc==3){QFile file(QString::fromLocal8Bit(argv[2]));if(!file.open(QIODevice::WriteOnly)||file.write(QJsonDocument(result).toJson())<0)return 2;}return code;}
