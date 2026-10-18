#pragma once
#include <QDoubleSpinBox>
#include <QLocale>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QSlider>
#include <QStyle>
#include <QStyleOptionSlider>
#include <functional>
#include <optional>

namespace compositor::ui {
// ContentView.ArrowStepper uses Shift, regardless of the platform spinbox's
// default acceleration modifier. Return/Escape release the property field.
class PropertyNumber final:public QDoubleSpinBox {
    bool stepping_{};
    bool compactDisplay_{};
public:
    explicit PropertyNumber(QWidget* parent=nullptr):QDoubleSpinBox(parent){setLocale(QLocale::C);}
    std::function<void()> releaseFocus;
    void setCompactDisplay(bool value){compactDisplay_=value;}
    void synchronize(double value) {
        const bool typing=hasFocus()&&!stepping_;
        const auto text=lineEdit()->text();const int cursor=lineEdit()->cursorPosition();
        QDoubleSpinBox::setValue(value);
        if(typing){const QSignalBlocker blocked(lineEdit());lineEdit()->setText(text);lineEdit()->setCursorPosition(cursor);}
    }
protected:
    QString textFromValue(double value)const override {
        auto text=QDoubleSpinBox::textFromValue(value);
        if(!compactDisplay_)return text;
        const auto separator=locale().decimalPoint();
        if(!text.contains(separator))return text;
        while(text.endsWith('0'))text.chop(1);
        if(text.endsWith(separator))text.chop(1);
        return text;
    }
    void stepBy(int steps) override {
        const bool prior=stepping_;stepping_=true;QDoubleSpinBox::stepBy(steps);stepping_=prior;
    }
    void keyPressEvent(QKeyEvent* event) override {
        if(!isReadOnly()&&(event->key()==Qt::Key_Up||event->key()==Qt::Key_Down)){
            stepBy((event->key()==Qt::Key_Up?1:-1)*(event->modifiers().testFlag(Qt::ShiftModifier)?10:1));
            event->accept();return;
        }
        if(event->key()==Qt::Key_Return||event->key()==Qt::Key_Enter||event->key()==Qt::Key_Escape){
            interpretText();clearFocus();if(releaseFocus)releaseFocus();event->accept();return;
        }
        QDoubleSpinBox::keyPressEvent(event);
    }
};
inline void synchronizeNumber(QDoubleSpinBox* number,double value){
    if(auto* property=dynamic_cast<PropertyNumber*>(number))property->synchronize(value);else number->setValue(value);
}

// SliderSnap.swift: center the knob under a track press immediately, preserving
// native knob dragging, keyboard behavior, direction and style geometry.
// Double-click restores the default value through the same press/value/release
// transaction as a drag, so owners commit history exactly once.
class TrackSlider:public QSlider {
    std::optional<int> defaultValue_;
public:
    using QSlider::QSlider;
    void setDefaultValue(int value){defaultValue_=value;}
protected:
    void mouseDoubleClickEvent(QMouseEvent* event) override {
        if(!defaultValue_){QSlider::mouseDoubleClickEvent(event);return;}
        if(isEnabled()&&event->button()==Qt::LeftButton){
            setSliderDown(true);
            setValue(*defaultValue_);
            setSliderDown(false);
        }
        event->accept();
    }
    void mousePressEvent(QMouseEvent* event) override {
        if(isEnabled()&&orientation()==Qt::Horizontal&&event->button()==Qt::LeftButton&&maximum()>minimum()){
            QStyleOptionSlider option;initStyleOption(&option);
            const auto knob=style()->subControlRect(QStyle::CC_Slider,&option,QStyle::SC_SliderHandle,this);
            const auto track=style()->subControlRect(QStyle::CC_Slider,&option,QStyle::SC_SliderGroove,this);
            const int travel=track.width()-knob.width();
            if(!knob.contains(event->position().toPoint())&&travel>0){
                const int position=int(event->position().x())-track.x()-knob.width()/2;
                // Start the transaction before publishing the snapped value.
                setSliderDown(true);
                setValue(QStyle::sliderValueFromPosition(minimum(),maximum(),position,travel,option.upsideDown));
            }
        }
        QSlider::mousePressEvent(event);
    }
};
}
