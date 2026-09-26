#pragma once
#include <QFocusEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPointer>
#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <string>

namespace compositor::ui {
// LayerAppearanceControls.swift: text is a draft until focus leaves. Its
// captured layer identity prevents a stale field from editing a new selection.
class LayerOpacityField final:public QLineEdit {
public:
    struct Target {QPointer<QObject> owner;std::string layer;double opacity{};};
    using QLineEdit::QLineEdit;
    std::function<std::optional<Target>()> target;
    std::function<void(const Target&,double)> apply;
    std::function<void()> releaseFocus;
    void detach(){target={};apply={};releaseFocus={};editing_.reset();}
    void synchronize(){if(!hasFocus())sync();}
protected:
    void focusInEvent(QFocusEvent* event)override {
        editing_=target?target():std::nullopt;QLineEdit::focusInEvent(event);
    }
    void focusOutEvent(QFocusEvent* event)override {
        const auto current=matchingTarget();
        bool valid=false;const double percent=text().toDouble(&valid);
        if(current&&valid&&std::isfinite(percent)&&apply)apply(*current,std::clamp(percent/100.,0.,1.));
        editing_.reset();sync();QLineEdit::focusOutEvent(event);
    }
    void keyPressEvent(QKeyEvent* event)override {
        if(event->key()==Qt::Key_Up||event->key()==Qt::Key_Down){
            if(const auto current=matchingTarget();current&&apply){
                const double step=(event->key()==Qt::Key_Up?1.:-1.)*(event->modifiers().testFlag(Qt::ShiftModifier)?10:1);
                apply(*current,std::clamp(std::round(current->opacity*100)+step,0.,100.)/100.);sync();
            }
            event->accept();return;
        }
        if(event->key()==Qt::Key_Return||event->key()==Qt::Key_Enter||event->key()==Qt::Key_Escape){
            clearFocus();if(releaseFocus)releaseFocus();event->accept();return;
        }
        QLineEdit::keyPressEvent(event);
    }
private:
    std::optional<Target> editing_;
    std::optional<Target> matchingTarget()const {
        const auto current=target?target():std::nullopt;
        if(!editing_||!editing_->owner||!current||current->owner!=editing_->owner||current->layer!=editing_->layer)return {};
        return current;
    }
    void sync(){const auto current=target?target():std::nullopt;setText(QString::number(std::lround((current?current->opacity:1)*100)));}
};
}
