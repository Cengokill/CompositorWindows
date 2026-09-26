#include "PaletteSwatches.h"
#include <QAction>
#include <QPainter>
#include <QPainterPath>
#include <QToolButton>

namespace compositor::ui {
namespace {
QPainterPath rounded(QRectF rectangle,double radius){QPainterPath path;path.addRoundedRect(rectangle,radius,radius);return path;}
class SwatchButton final:public QToolButton {
    QColor color_;
public:
    explicit SwatchButton(QWidget* parent):QToolButton(parent){setFixedSize(24,24);setFocusPolicy(Qt::StrongFocus);setAutoRaise(true);}
    void setColor(QColor value){if(color_!=value){color_=value;update();}}
protected:
    bool hitButton(const QPoint& point)const override{return rounded(QRectF(0,0,24,24),6).contains(point);}
    void paintEvent(QPaintEvent*)override{
        QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);
        if(!isEnabled())painter.setOpacity(.45);
        painter.fillPath(rounded(QRectF(0,0,24,24),6),color_);
        painter.setBrush(Qt::NoBrush);
        // strokeBorder: its center is inset by half the stroke's own width.
        painter.setPen(QPen(Qt::white,1.5));painter.drawPath(rounded(QRectF(1.75,1.75,20.5,20.5),4.25));
        painter.setPen(QPen(Qt::black,1));painter.drawPath(rounded(QRectF(.5,.5,23,23),5.5));
        if(hasFocus()){painter.setPen(QPen(palette().highlight().color(),1,Qt::DotLine));painter.drawPath(rounded(QRectF(3.5,3.5,17,17),2.5));}
    }
};
class UtilityButton final:public QToolButton {
    bool reset_;
public:
    UtilityButton(bool reset,QWidget* parent):QToolButton(parent),reset_(reset){setFixedSize(12,12);setAutoRaise(true);setFocusPolicy(Qt::StrongFocus);}
protected:
    void paintEvent(QPaintEvent*)override{
        QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(palette().color(isEnabled()?QPalette::Active:QPalette::Disabled,QPalette::WindowText),1,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
        painter.setOpacity(isEnabled()?.7:.35);painter.setBrush(Qt::NoBrush);
        if(reset_){
            painter.drawArc(QRectF(2.5,2.5,7,7),-30*16,285*16);
            QPainterPath arrow;arrow.moveTo(1.5,1.5);arrow.lineTo(1.5,5);arrow.lineTo(5,5);painter.drawPath(arrow);
        }else{
            painter.translate(6,6);painter.rotate(45);
            QPainterPath arrows;arrows.moveTo(-4,-2);arrows.lineTo(4,-2);arrows.moveTo(2,-4);arrows.lineTo(4,-2);arrows.lineTo(2,0);
            arrows.moveTo(4,2);arrows.lineTo(-4,2);arrows.moveTo(-2,0);arrows.lineTo(-4,2);arrows.lineTo(-2,4);painter.drawPath(arrows);
        }
        if(hasFocus()){painter.resetTransform();painter.setPen(QPen(palette().highlight().color(),1,Qt::DotLine));painter.drawRect(QRectF(.5,.5,11,11));}
    }
};
}
PaletteSwatches::PaletteSwatches(const std::array<QAction*,4>& actions,QWidget* parent):QWidget(parent){
    setStyleSheet("QToolButton { padding: 0; border: 0; min-width: 0; min-height: 0; background: transparent; }");setObjectName("paletteSwatches");setAccessibleName("Foreground and background colors");
    // Nominal source frame36² has utility targets extending left1, right3,
    // top3 and bottom3. Reserve that overflow instead of clipping Qt children.
    setFixedSize(40,42);
    buttons_[1]=new SwatchButton(this);buttons_[1]->move(13,15);
    buttons_[0]=new SwatchButton(this);buttons_[0]->move(1,3);
    buttons_[2]=new UtilityButton(false,this);buttons_[2]->move(28,0);
    buttons_[3]=new UtilityButton(true,this);buttons_[3]->move(0,30);
    const std::array<const char*,4> names{"Foreground color","Background color","Swap colors","Default colors"};
    const std::array<const char*,4> tips{"Foreground color","Background color","Swap foreground and background (X)","Default colors (D)"};
    for(size_t index=0;index<buttons_.size();++index){actions[index]->setToolTip(tips[index]);buttons_[index]->setDefaultAction(actions[index]);buttons_[index]->setAccessibleName(names[index]);buttons_[index]->setToolTip(tips[index]);}
    setTabOrder(buttons_[0],buttons_[1]);setTabOrder(buttons_[1],buttons_[2]);setTabOrder(buttons_[2],buttons_[3]);
    setColors(Qt::black,Qt::white);
}
void PaletteSwatches::setColors(QColor foreground,QColor background){static_cast<SwatchButton*>(buttons_[0])->setColor(foreground);static_cast<SwatchButton*>(buttons_[1])->setColor(background);}
QWidget* PaletteSwatches::swatch(bool background)const{return buttons_[size_t(background)];}
}
