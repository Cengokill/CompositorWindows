#pragma once
#include <QComboBox>
#include <QAbstractItemView>
#include <QEvent>
#include <functional>

namespace compositor::ui {
// Menu preview is deliberately separate from the selected value. Closing the
// popup by Escape, focus loss, or selection always clears temporary rendering.
class PreviewComboBox final:public QComboBox {
    bool open_{};
    void dismiss(){if(!open_)return;open_=false;if(dismissed)dismissed();}
protected:
    bool eventFilter(QObject* watched,QEvent* event) override {
        if(event->type()==QEvent::Hide&&watched==view()->window())dismiss();
        return QComboBox::eventFilter(watched,event);
    }
public:
    std::function<void(int)> preview;
    std::function<void()> dismissed;
    explicit PreviewComboBox(QWidget* parent=nullptr):QComboBox(parent){
        connect(this,&QComboBox::highlighted,this,[this](int index){if(open_&&preview)preview(index);});
    }
    void showPopup() override {view()->window()->installEventFilter(this);open_=true;QComboBox::showPopup();}
    void hidePopup() override {QComboBox::hidePopup();dismiss();}
};
}
