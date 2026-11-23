#include "SegmentedControl.h"
#include "LayerPanelTokens.h"
#include <QButtonGroup>
#include <QHBoxLayout>
#include <QPainter>
#include <QToolButton>
#include <QVariantAnimation>

namespace compositor::ui {
namespace t = layer_panel;
namespace {
constexpr int slideMilliseconds = 120;
QColor dimmed(const QColor& color) { return QColor::fromRgbF((color.redF() + QColor(t::control).redF()) / 2, (color.greenF() + QColor(t::control).greenF()) / 2, (color.blueF() + QColor(t::control).blueF()) / 2); }
}
SegmentedControl::SegmentedControl(const QStringList& items, QWidget* parent):QWidget(parent),group_(new QButtonGroup(this)),slide_(new QVariantAnimation(this)){
    setFixedHeight(t::controlHeight);
    auto* row=new QHBoxLayout(this);row->setContentsMargins(t::segmentInset,t::segmentInset,t::segmentInset,t::segmentInset);row->setSpacing(0);
    group_->setExclusive(true);
    for(int i=0;i<items.size();++i){
        // The buttons stay transparent: this control paints the capsule and the sliding pill behind them (see the segment rules in ToolbarControls.cpp).
        auto* button=new QToolButton(this);button->setText(items[i]);button->setAccessibleName(items[i]);button->setCheckable(true);button->setProperty("segment",true);
        group_->addButton(button,i);row->addWidget(button);
    }
    if(auto* first=group_->button(0))first->setChecked(true);
    slide_->setDuration(slideMilliseconds);slide_->setEasingCurve(QEasingCurve::OutCubic);slide_->setStartValue(0.);slide_->setEndValue(1.);
    connect(slide_,&QVariantAnimation::valueChanged,this,[this]{update();});
    connect(group_,&QButtonGroup::idClicked,this,[this](int index){
        if(index==shown_)return;
        from_=pillRect();to_=segment(index)->geometry();shown_=index;
        slide_->stop();if(isVisible())slide_->start();else update();
        if(changed)changed(index);
    });
}
int SegmentedControl::currentIndex() const{return group_->checkedId();}
void SegmentedControl::setCurrentIndex(int index){
    // A refresh that re-applies the choice just made must not cut the slide short.
    if(index==shown_&&group_->checkedId()==index)return;
    if(auto* button=group_->button(index)){slide_->stop();button->setChecked(true);shown_=index;update();}
}
void SegmentedControl::choose(int index){if(!group_->button(index)||index==shown_)return;setCurrentIndex(index);if(changed)changed(index);}
QToolButton* SegmentedControl::segment(int index) const{return qobject_cast<QToolButton*>(group_->button(index));}
QRectF SegmentedControl::pillRect() const{
    if(slide_->state()==QAbstractAnimation::Running){const auto k=slide_->currentValue().toReal();return QRectF(from_.topLeft()+(to_.topLeft()-from_.topLeft())*k,from_.size()+(to_.size()-from_.size())*k);}
    const auto* button=segment(shown_);return button?QRectF(button->geometry()):QRectF();
}
void SegmentedControl::paintEvent(QPaintEvent*){
    QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(t::control));painter.drawRoundedRect(QRectF(rect()),height()/2.,height()/2.);
    const auto pill=pillRect();
    if(pill.isValid()){painter.setBrush(isEnabled()?QColor(t::accent):dimmed(QColor(t::accent)));painter.drawRoundedRect(pill,pill.height()/2,pill.height()/2);}
}
SegmentedControl* findSegmentedControl(const QObject* root,const char* objectName){return dynamic_cast<SegmentedControl*>(root->findChild<QWidget*>(objectName));}
}
