#include "PaletteDialog.h"
#include <QPainter>
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
class ColorField:public QWidget {
public:
    PickerHSB value;bool hue{};std::function<void(double,double)> changed;
    ColorField(bool strip,QWidget*parent):QWidget(parent),hue(strip){setFixedSize(hue?34:256,256);setFocusPolicy(Qt::StrongFocus);setAccessibleName(hue?"Hue":"Saturation and brightness");}
    void paintEvent(QPaintEvent*)override{QPainter p(this);if(hue){QLinearGradient gradient(0,0,0,height());for(int i=0;i<=6;++i)gradient.setColorAt(i/6.,rgb(PickerHSB{360-i*60.,1,1}.rgb()));p.fillRect(QRect(7,0,20,height()),gradient);double y=(1-value.hue/360)*height();p.setPen(QPen(Qt::white,2));p.drawLine(0,int(y),width(),int(y));}else{QLinearGradient horizontal(0,0,width(),0);horizontal.setColorAt(0,Qt::white);horizontal.setColorAt(1,rgb(PickerHSB{value.hue,1,1}.rgb()));p.fillRect(rect(),horizontal);QLinearGradient vertical(0,0,0,height());vertical.setColorAt(0,QColor(0,0,0,0));vertical.setColorAt(1,Qt::black);p.fillRect(rect(),vertical);p.setRenderHint(QPainter::Antialiasing);QPointF at(value.saturation*width(),(1-value.brightness)*height());p.setPen(QPen(Qt::black,3));p.drawEllipse(at,6,6);p.setPen(QPen(Qt::white,1.5));p.drawEllipse(at,6,6);}p.setPen(QColor(0,0,0,180));p.drawRect(rect().adjusted(0,0,-1,-1));}
    void choose(QPointF point){if(changed)changed(std::clamp(point.x()/width(),0.,1.),std::clamp(point.y()/height(),0.,1.));}
    void mousePressEvent(QMouseEvent*event)override{if(event->button()==Qt::LeftButton)choose(event->position());}
    void mouseMoveEvent(QMouseEvent*event)override{if(event->buttons()&Qt::LeftButton)choose(event->position());}
};
}
struct PaletteDialog::Impl {
    PaletteDialog* owner;PickerHSB hsb;PaletteColor original;ColorField *field,*hue;std::array<QSpinBox*,3> channels{};QLineEdit*hex;QLabel*preview;
    Impl(PaletteDialog*dialog,PaletteColor start):owner(dialog),original(start){hsb.setRGB(start);auto*row=new QHBoxLayout(owner);field=new ColorField(false,owner);field->setObjectName("paletteField");hue=new ColorField(true,owner);hue->setObjectName("paletteHue");row->addWidget(field);row->addWidget(hue);auto*side=new QVBoxLayout;row->addLayout(side);preview=new QLabel;preview->setFixedSize(96,64);preview->setAccessibleName("New and original colors");side->addWidget(preview);auto*grid=new QGridLayout;side->addLayout(grid);const char*labels[]{"Red","Green","Blue"};for(int i=0;i<3;++i){auto*spin=new QSpinBox;channels[i]=spin;spin->setRange(0,255);spin->setObjectName(QString("paletteRGB%1").arg(i));spin->setAccessibleName(labels[i]);grid->addWidget(new QLabel(labels[i]),i,0);grid->addWidget(spin,i,1);connect(spin,&QSpinBox::valueChanged,owner,[this,i](int v){auto color=hsb.rgb().quantized();double*component[]{&color.red,&color.green,&color.blue};*component[i]=v/255.;hsb.setRGB(color);sync();});}hex=new QLineEdit;hex->setObjectName("paletteHex");hex->setAccessibleName("Hex color");grid->addWidget(new QLabel("#"),3,0);grid->addWidget(hex,3,1);connect(hex,&QLineEdit::editingFinished,owner,[this]{if(auto color=PaletteColor::fromHex(hex->text().toStdString()))hsb.setRGB(*color);sync();});side->addWidget(new QLabel("Click the canvas to sample"));auto*buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);side->addWidget(buttons);connect(buttons,&QDialogButtonBox::accepted,owner,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,owner,&QDialog::reject);field->changed=[this](double x,double y){hsb.saturation=x;hsb.brightness=1-y;sync();};hue->changed=[this](double,double y){hsb.hue=(1-y)*360;sync();};}
    void sync(){auto color=hsb.rgb().quantized();const double values[]{color.red,color.green,color.blue};for(int i=0;i<3;++i){QSignalBlocker block(channels[i]);channels[i]->setValue(int(std::round(values[i]*255)));}if(!hex->hasFocus()){QSignalBlocker block(hex);hex->setText(QString::fromStdString(color.hex()));}field->value=hue->value=hsb;field->update();hue->update();QPixmap swatch(preview->size());swatch.fill(rgb(original));QPainter p(&swatch);p.fillRect(0,0,swatch.width(),swatch.height()/2,rgb(color));p.end();preview->setPixmap(swatch);if(owner->onPreview)owner->onPreview(color);}
};
PaletteDialog::PaletteDialog(PaletteColor original,const QString&title,QWidget*parent):QDialog(parent,Qt::Tool),impl_(std::make_unique<Impl>(this,original)){impl_->sync();setWindowTitle(title);setModal(false);setSizeGripEnabled(false);layout()->setSizeConstraint(QLayout::SetFixedSize);}
PaletteDialog::~PaletteDialog()=default;
PaletteColor PaletteDialog::color()const{return impl_->hsb.rgb().quantized();}
void PaletteDialog::sample(PaletteColor color){impl_->hsb.setRGB(color);impl_->sync();}
}
