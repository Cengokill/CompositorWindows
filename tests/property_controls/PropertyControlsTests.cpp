#include <QApplication>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QSlider>
#include <QStyleOptionSlider>
#include <QTest>
#include <cstdio>
#include <cmath>
#include <QSignalBlocker>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
#if !defined(FROZEN_CONTROLS)
#include "ui/PropertyControls.h"
using Number=compositor::ui::PropertyNumber;
using Slider=compositor::ui::TrackSlider;
#else
using Number=QDoubleSpinBox;
using Slider=QSlider;
#endif
namespace {
void require(bool b,const char* text){if(!b)throw std::runtime_error(text);}
void ready(QWidget& widget){widget.show();widget.activateWindow();widget.setFocus();QApplication::processEvents();}
void typingRefresh(){
    Number number;number.setRange(-10000,10000);number.setDecimals(3);ready(number);
    QObject::connect(&number,&QDoubleSpinBox::valueChanged,[&](double value){
        const QSignalBlocker refresh(number);
#if defined(FROZEN_CONTROLS)
        number.setValue(std::fmod(value,360.));
#else
        number.synchronize(std::fmod(value,360.));
#endif
    });
    QTest::keyClick(&number,Qt::Key_A,Qt::ControlModifier);QTest::keyClicks(&number,"360");
    require(number.findChild<QLineEdit*>()->text()=="360","model refresh must preserve focused text while typing wrapped angle");
    QTest::keyClick(&number,Qt::Key_Up);require(number.value()==1,"arrow must step the applied angle, rather than stale typed text");
}
void shiftSteps(){Number number;number.setRange(-100,100);number.setSingleStep(.25);number.setValue(1.5);ready(number);QTest::keyClick(&number,Qt::Key_Up,Qt::ShiftModifier);require(number.value()==4,"Shift-Up must use ten configured steps");QTest::keyClick(&number,Qt::Key_Down,Qt::ShiftModifier);require(number.value()==1.5,"Shift-Down must use ten configured steps");QTest::keyClick(&number,Qt::Key_Up);require(number.value()==1.75,"ordinary arrow must use one configured step");}
void controlNotAccelerated(){Number number;number.setRange(-100,100);number.setValue(4);ready(number);QTest::keyClick(&number,Qt::Key_Up,Qt::ControlModifier);require(number.value()==5,"source ArrowStepper does not accelerate Control-Up");}
void clamping(){Number number;number.setRange(0,5);number.setValue(4);ready(number);QTest::keyClick(&number,Qt::Key_Up,Qt::ShiftModifier);require(number.value()==5,"upper bound");QTest::keyClick(&number,Qt::Key_Down,Qt::ShiftModifier);require(number.value()==0,"lower bound");number.setReadOnly(true);QTest::keyClick(&number,Qt::Key_Up);require(number.value()==0,"read-only arrow changed value");}
void release(int key){QWidget parent;Number number(&parent);number.setRange(-100,100);parent.resize(240,100);ready(parent);number.setFocus();QApplication::processEvents();require(number.hasFocus(),"fixture numeric focus");QTest::keyClick(&number,Qt::Key_A,Qt::ControlModifier);QTest::keyClicks(&number,"12.5");QTest::keyClick(&number,Qt::Key(key));require(number.value()==12.5&&!number.hasFocus(),"commit/escape must retain typed value and release numeric focus");}
struct SliderFixture:Slider {
    using Slider::Slider;
    QStyleOptionSlider option()const{QStyleOptionSlider result;initStyleOption(&result);return result;}
};
void track(bool rtl,bool inverted){
    SliderFixture slider(Qt::Horizontal);slider.resize(360,40);slider.setRange(0,100);slider.setValue(20);slider.setLayoutDirection(rtl?Qt::RightToLeft:Qt::LeftToRight);slider.setInvertedAppearance(inverted);ready(slider);
    auto option=slider.option();auto knob=slider.style()->subControlRect(QStyle::CC_Slider,&option,QStyle::SC_SliderHandle,&slider);auto groove=slider.style()->subControlRect(QStyle::CC_Slider,&option,QStyle::SC_SliderGroove,&slider);
    const QPoint point(groove.x()+knob.width()/2+(groove.width()-knob.width())*3/4,knob.center().y());
    require(!knob.contains(point),"fixture press must hit track");const int expected=QStyle::sliderValueFromPosition(0,100,point.x()-groove.x()-knob.width()/2,groove.width()-knob.width(),option.upsideDown);
    std::vector<char> events;QObject::connect(&slider,&QSlider::sliderPressed,[&]{events.push_back('b');});QObject::connect(&slider,&QSlider::valueChanged,[&](int){events.push_back('v');});QObject::connect(&slider,&QSlider::sliderReleased,[&]{events.push_back('e');});
    QTest::mousePress(&slider,Qt::LeftButton,{},point);require(slider.value()==expected,"track press must immediately center the knob at the pointer");require(slider.isSliderDown(),"track press must begin dragging");
    QTest::mouseRelease(&slider,Qt::LeftButton,{},point);require(events==std::vector<char>{'b','v','e'},"track transaction ordering must be begin/value/end once");
}
void knob(){SliderFixture slider(Qt::Horizontal);slider.resize(360,40);slider.setRange(0,100);slider.setValue(35);ready(slider);const auto option=slider.option();const auto knob=slider.style()->subControlRect(QStyle::CC_Slider,&option,QStyle::SC_SliderHandle,&slider);QTest::mousePress(&slider,Qt::LeftButton,{},knob.center());require(slider.value()==35&&slider.isSliderDown(),"knob press jumped");QTest::mouseMove(&slider,knob.center()+QPoint(40,0));QTest::mouseRelease(&slider,Qt::LeftButton,{},knob.center()+QPoint(40,0));require(slider.value()>35&&!slider.isSliderDown(),"native knob drag failed");}
}
int main(int argc,char**argv){QApplication app(argc,argv);const std::map<std::string,void(*)()> cases{{"typing_refresh",typingRefresh},{"shift_steps",shiftSteps},{"control_one_step",controlNotAccelerated},{"clamping_readonly",clamping},{"return_focus",[]{release(Qt::Key_Return);}},{"escape_focus",[]{release(Qt::Key_Escape);}},{"track_ltr",[]{track(false,false);}},{"track_rtl",[]{track(true,false);}},{"track_inverted",[]{track(false,true);}},{"knob_drag",knob}};int failed=0;for(const auto&[name,test]:cases){if(argc>1&&argv[1]!=name)continue;try{test();std::printf("PASS %s\n",name.c_str());}catch(const std::exception& error){++failed;std::printf("FAIL %s: %s\n",name.c_str(),error.what());}}if(argc>1&&!cases.contains(argv[1]))return 2;return failed?1:0;}
