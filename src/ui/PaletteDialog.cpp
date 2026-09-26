#include "PaletteDialog.h"
#include <QPainter>
#include <QAccessibleWidget>
#include <QKeyEvent>
#include <mutex>
#include <QMouseEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSpinBox>
#include <QLineEdit>
#include <QLabel>
#include <QDialogButtonBox>
#include <QSignalBlocker>
#include <algorithm>
#include <cmath>
namespace compositor {
using namespace effects_tools;
namespace {
QColor rgb(PaletteColor c){return QColor::fromRgbF(c.red,c.green,c.blue);}
// Scalar controls use real QObject lifetimes, so a removed picker cannot
// mutate a dead widget through a retained value pattern. The strict Windows
// UIA may substitute a default for unsupported reads; clients can query
// without defaults. The superseded exact-HRESULT failure is retained.
class PaletteValueWidget : public QWidget {
public:
    std::function<double()> read;
    std::function<void(double)> write;
    double maximum{100};
    QString unit{QStringLiteral(" percent")};
    bool scalar{};
    using QWidget::QWidget;
    double valueNumber() const { return read ? read() : 0; }
    void setValueNumber(double next) {
        if(isEnabled() && write && std::isfinite(next))write(std::clamp(next,0.,maximum));
    }
    void notifyValue() {
        if(QAccessible::isActive()){QAccessibleValueChangeEvent event(this,valueNumber());QAccessible::updateAccessibility(&event);}
    }
};
class AccessiblePaletteValue final : public QAccessibleWidget, public QAccessibleValueInterface {
public:
    explicit AccessiblePaletteValue(PaletteValueWidget* field):QAccessibleWidget(field,QAccessible::Slider){}
    PaletteValueWidget* field() const { return static_cast<PaletteValueWidget*>(widget()); }
    void* interface_cast(QAccessible::InterfaceType type) override {
        return type==QAccessible::ValueInterface ? static_cast<QAccessibleValueInterface*>(this) : QAccessibleWidget::interface_cast(type);
    }
    QVariant currentValue() const override { return field()->valueNumber(); }
    QVariant minimumValue() const override { return 0.; }
    QVariant maximumValue() const override { return field()->maximum; }
    QVariant minimumStepSize() const override { return 1.; }
    void setCurrentValue(const QVariant& input) override {
        bool valid=false;const auto next=input.toDouble(&valid);if(valid)field()->setValueNumber(next);
    }
    QString text(QAccessible::Text type) const override {
        return type==QAccessible::Value ? QString::number(std::round(field()->valueNumber()))+field()->unit : QAccessibleWidget::text(type);
    }
    QStringList actionNames() const override {
        auto actions=QStringList{increaseAction(),decreaseAction()};
        if(field()->focusPolicy()!=Qt::NoFocus)actions.append(setFocusAction());
        return actions;
    }
    void doAction(const QString& action) override {
        if(!field()->isEnabled())return;
        if(action==increaseAction())field()->setValueNumber(field()->valueNumber()+1);
        else if(action==decreaseAction())field()->setValueNumber(field()->valueNumber()-1);
        else QAccessibleWidget::doAction(action);
    }
    QStringList keyBindingsForAction(const QString& action) const override {
        if(field()->focusPolicy()!=Qt::NoFocus){
            if(action==increaseAction())return {QStringLiteral("Up")};
            if(action==decreaseAction())return {QStringLiteral("Down")};
        }
        return QAccessibleWidget::keyBindingsForAction(action);
    }
};
void installPaletteAccessibility() {
    static std::once_flag installed;
    std::call_once(installed,[]{QAccessible::installFactory([](const QString&,QObject* object)->QAccessibleInterface*{
        if(auto* field=dynamic_cast<PaletteValueWidget*>(object);field&&field->scalar)return new AccessiblePaletteValue(field);
        return nullptr;
    });});
}
class ColorField:public PaletteValueWidget {
public:
    PickerHSB value;bool hue{};std::function<void(double,double)> changed;
    // Components are assigned directly to the retained HSB value. A gray or
    // black color must not erase the hue/saturation chosen through a control.
    std::function<void(int,double)> componentChanged;
    std::array<PaletteValueWidget*,2> axes{};
    ColorField(bool strip,QWidget*parent):PaletteValueWidget(parent),hue(strip){
        installPaletteAccessibility();scalar=hue;setFixedSize(hue?34:256,256);setFocusPolicy(Qt::StrongFocus);
        setAccessibleName(hue?"Hue":"Saturation and brightness");
        if(hue){
            scalar=true;maximum=360;unit=QStringLiteral(" degrees");
            read=[this]{return value.hue;};write=[this](double next){if(componentChanged)componentChanged(0,next);};
            setAccessibleDescription("Up and Down change hue by one degree. Hold Shift for ten degrees.");
        }else{
            setAccessibleDescription("Left and Right change saturation. Up and Down change brightness. Hold Shift for ten percent.");
            for(size_t index=0;index<axes.size();++index){
                auto* axis=axes[index]=new PaletteValueWidget(this);axis->scalar=true;
                axis->setAccessibleName(index==0?"Saturation":"Brightness");
                axis->setObjectName(index==0?"paletteSaturation":"paletteBrightness");
                axis->setGeometry(rect());axis->setAttribute(Qt::WA_TransparentForMouseEvents);axis->setAttribute(Qt::WA_NoSystemBackground);axis->setFocusPolicy(Qt::NoFocus);
                axis->read=[this,index]{return (index==0?value.saturation:value.brightness)*100;};
                axis->write=[this,index](double next){if(componentChanged)componentChanged(int(index)+1,next/100);};
            }
        }
    }
    void changedValues(PickerHSB next){
        const auto old=value;value=next;update();
        if(hue){if(old.hue!=value.hue)notifyValue();}
        else{if(old.saturation!=value.saturation)axes[0]->notifyValue();if(old.brightness!=value.brightness)axes[1]->notifyValue();}
    }
    void paintEvent(QPaintEvent*)override{QPainter p(this);if(hue){QLinearGradient gradient(0,0,0,height());for(int i=0;i<=6;++i)gradient.setColorAt(i/6.,rgb(PickerHSB{360-i*60.,1,1}.rgb()));p.fillRect(QRect(7,0,20,height()),gradient);double y=(1-value.hue/360)*height();p.setPen(QPen(Qt::white,2));p.drawLine(0,int(y),width(),int(y));}else{QLinearGradient horizontal(0,0,width(),0);horizontal.setColorAt(0,Qt::white);horizontal.setColorAt(1,rgb(PickerHSB{value.hue,1,1}.rgb()));p.fillRect(rect(),horizontal);QLinearGradient vertical(0,0,0,height());vertical.setColorAt(0,QColor(0,0,0,0));vertical.setColorAt(1,Qt::black);p.fillRect(rect(),vertical);p.setRenderHint(QPainter::Antialiasing);QPointF at(value.saturation*width(),(1-value.brightness)*height());p.setPen(QPen(Qt::black,3));p.drawEllipse(at,6,6);p.setPen(QPen(Qt::white,1.5));p.drawEllipse(at,6,6);}p.setPen(QColor(0,0,0,180));p.drawRect(rect().adjusted(0,0,-1,-1));if(hasFocus()){p.setPen(QPen(palette().color(QPalette::Highlight),2,Qt::DashLine));p.drawRect(rect().adjusted(2,2,-3,-3));}}
    void choose(QPointF point){if(changed)changed(std::clamp(point.x()/width(),0.,1.),std::clamp(point.y()/height(),0.,1.));}
    void mousePressEvent(QMouseEvent*event)override{if(event->button()==Qt::LeftButton)choose(event->position());}
    void mouseMoveEvent(QMouseEvent*event)override{if(event->buttons()&Qt::LeftButton)choose(event->position());}
    void focusInEvent(QFocusEvent* event)override{QWidget::focusInEvent(event);update();}
    void focusOutEvent(QFocusEvent* event)override{QWidget::focusOutEvent(event);update();}
    void keyPressEvent(QKeyEvent* event)override{
        if(event->modifiers() & ~Qt::ShiftModifier){QWidget::keyPressEvent(event);return;}
        const double step=event->modifiers().testFlag(Qt::ShiftModifier)?10.:1.;
        if(hue&&(event->key()==Qt::Key_Up||event->key()==Qt::Key_Down))setValueNumber(value.hue+(event->key()==Qt::Key_Up?step:-step));
        else if(!hue&&(event->key()==Qt::Key_Left||event->key()==Qt::Key_Right))axes[0]->setValueNumber(value.saturation*100+(event->key()==Qt::Key_Right?step:-step));
        else if(!hue&&(event->key()==Qt::Key_Up||event->key()==Qt::Key_Down))axes[1]->setValueNumber(value.brightness*100+(event->key()==Qt::Key_Up?step:-step));
        else {QWidget::keyPressEvent(event);return;}
        event->accept();
    }
};
class PaletteChannel final : public QSpinBox {
    void keyPressEvent(QKeyEvent* event)override{
        if(event->key()==Qt::Key_Up||event->key()==Qt::Key_Down){
            const int step=event->modifiers().testFlag(Qt::ShiftModifier)?10:1;
            setValue(value()+(event->key()==Qt::Key_Up?step:-step));event->accept();
        }else QSpinBox::keyPressEvent(event);
    }
};
}
struct PaletteDialog::Impl {
    PaletteDialog* owner;PickerHSB hsb;PaletteColor original;ColorField *field,*hue;std::array<QSpinBox*,3> channels{};QLineEdit*hex;QLabel*preview;
    Impl(PaletteDialog*dialog,PaletteColor start):owner(dialog),original(start){hsb.setRGB(start);auto*row=new QHBoxLayout(owner);field=new ColorField(false,owner);field->setObjectName("paletteField");hue=new ColorField(true,owner);hue->setObjectName("paletteHue");row->addWidget(field);row->addWidget(hue);auto*side=new QVBoxLayout;row->addLayout(side);preview=new QLabel;preview->setFixedSize(96,64);preview->setAccessibleName("New and original colors");side->addWidget(preview);auto*grid=new QGridLayout;side->addLayout(grid);const char*labels[]{"Red","Green","Blue"};for(int i=0;i<3;++i){auto*spin=new PaletteChannel;channels[i]=spin;spin->setRange(0,255);spin->setObjectName(QString("paletteRGB%1").arg(i));spin->setAccessibleName(labels[i]);grid->addWidget(new QLabel(labels[i]),i,0);grid->addWidget(spin,i,1);connect(spin,&QSpinBox::valueChanged,owner,[this,i](int v){auto color=hsb.rgb().quantized();double*component[]{&color.red,&color.green,&color.blue};*component[i]=v/255.;hsb.setRGB(color);sync();});}hex=new QLineEdit;hex->setObjectName("paletteHex");hex->setAccessibleName("Hex color");grid->addWidget(new QLabel("#"),3,0);grid->addWidget(hex,3,1);connect(hex,&QLineEdit::editingFinished,owner,[this]{if(auto color=PaletteColor::fromHex(hex->text().toStdString()))hsb.setRGB(*color);sync();});side->addWidget(new QLabel("Click the canvas to sample"));auto*buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);side->addWidget(buttons);connect(buttons,&QDialogButtonBox::accepted,owner,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,owner,&QDialog::reject);field->changed=[this](double x,double y){hsb.saturation=x;hsb.brightness=1-y;sync();};hue->changed=[this](double,double y){hsb.hue=(1-y)*360;sync();};field->componentChanged=hue->componentChanged=[this](int component,double next){if(component==0)hsb.hue=next;else if(component==1)hsb.saturation=next;else hsb.brightness=next;sync();};}
    void sync(){auto color=hsb.rgb().quantized();const double values[]{color.red,color.green,color.blue};for(int i=0;i<3;++i){QSignalBlocker block(channels[i]);channels[i]->setValue(int(std::round(values[i]*255)));}if(!hex->hasFocus()){QSignalBlocker block(hex);hex->setText(QString::fromStdString(color.hex()));}field->changedValues(hsb);hue->changedValues(hsb);QPixmap swatch(preview->size());swatch.fill(rgb(original));QPainter p(&swatch);p.fillRect(0,0,swatch.width(),swatch.height()/2,rgb(color));p.end();preview->setPixmap(swatch);if(owner->onPreview)owner->onPreview(color);}
};
PaletteDialog::PaletteDialog(PaletteColor original,const QString&title,QWidget*parent):QDialog(parent,Qt::Tool),impl_(std::make_unique<Impl>(this,original)){impl_->sync();setWindowTitle(title);setModal(false);setSizeGripEnabled(false);layout()->setSizeConstraint(QLayout::SetFixedSize);}
PaletteDialog::~PaletteDialog()=default;
PaletteColor PaletteDialog::color()const{return impl_->hsb.rgb().quantized();}
void PaletteDialog::sample(PaletteColor color){impl_->hsb.setRGB(color);impl_->sync();}
}
