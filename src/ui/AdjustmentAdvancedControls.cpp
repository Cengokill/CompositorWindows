#include "AdjustmentAdvancedControls.h"
#include <QAccessibleWidget>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <mutex>
#include <stdexcept>

namespace compositor {
using namespace effects_tools;
namespace {
// Real child widgets give Qt's Windows UIA bridge stable object lifetimes and
// keyboard focus. Pointer events still go to the original graph drag handler.
class LevelsHandle final : public QWidget {
public:
    struct Value { double current{}, minimum{}, maximum{}, step{1}; };
    std::function<Value()> read;
    std::function<void(double)> write;
    explicit LevelsHandle(QWidget* parent, const char* name) : QWidget(parent) {
        setAccessibleName(QString::fromUtf8(name));
        setObjectName(QStringLiteral("levelsHandle") + QString::fromUtf8(name).remove(' '));
        setFocusPolicy(Qt::StrongFocus);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
    }
    Value value() const { return read ? read() : Value{}; }
    void setValue(double next) { if (isEnabled() && write && std::isfinite(next)) write(next); }
    void step(int direction) { const auto current=value(); setValue(current.current+direction*current.step); }
private:
    void paintEvent(QPaintEvent*) override {
        if (!hasFocus()) return;
        QPainter painter(this); painter.setPen(QPen(palette().color(QPalette::Highlight),1,Qt::DashLine));
        painter.setBrush(Qt::NoBrush); painter.drawRect(rect().adjusted(1,1,-2,-2));
    }
    void keyPressEvent(QKeyEvent* event) override {
        if(event->key()==Qt::Key_Up||event->key()==Qt::Key_Right)step(1);
        else if(event->key()==Qt::Key_Down||event->key()==Qt::Key_Left)step(-1);
        else if(event->key()==Qt::Key_Home)setValue(value().minimum);
        else if(event->key()==Qt::Key_End)setValue(value().maximum);
        else {QWidget::keyPressEvent(event);return;}
        event->accept();
    }
    void focusInEvent(QFocusEvent* event) override { QWidget::focusInEvent(event); update(); }
    void focusOutEvent(QFocusEvent* event) override { QWidget::focusOutEvent(event); update(); }
};
class AccessibleLevelsHandle final : public QAccessibleWidget, public QAccessibleValueInterface {
public:
    explicit AccessibleLevelsHandle(LevelsHandle* handle):QAccessibleWidget(handle,QAccessible::Slider){}
    LevelsHandle* handle() const { return static_cast<LevelsHandle*>(widget()); }
    void* interface_cast(QAccessible::InterfaceType type) override {
        return type==QAccessible::ValueInterface ? static_cast<QAccessibleValueInterface*>(this) : QAccessibleWidget::interface_cast(type);
    }
    QVariant currentValue() const override { return handle()->value().current; }
    QVariant minimumValue() const override { return handle()->value().minimum; }
    QVariant maximumValue() const override { return handle()->value().maximum; }
    QVariant minimumStepSize() const override { return handle()->value().step; }
    void setCurrentValue(const QVariant& input) override { bool valid=false; const double next=input.toDouble(&valid); if(valid)handle()->setValue(next); }
    QString text(QAccessible::Text type) const override {
        return type==QAccessible::Value ? QString::number(handle()->value().current,'g',12) : QAccessibleWidget::text(type);
    }
    QStringList actionNames() const override {
        return {increaseAction(),decreaseAction(),setFocusAction()};
    }
    void doAction(const QString& action) override {
        if(!handle()->isEnabled())return;
        if(action==increaseAction())handle()->step(1);
        else if(action==decreaseAction())handle()->step(-1);
        else QAccessibleWidget::doAction(action);
    }
    QStringList keyBindingsForAction(const QString& action) const override {
        if(action==increaseAction())return {QStringLiteral("Up"),QStringLiteral("Right")};
        if(action==decreaseAction())return {QStringLiteral("Down"),QStringLiteral("Left")};
        return QAccessibleWidget::keyBindingsForAction(action);
    }
};
void installLevelsHandleAccessibility() {
    static std::once_flag installed;
    std::call_once(installed,[]{QAccessible::installFactory([](const QString&,QObject* object)->QAccessibleInterface*{
        if(auto* handle=dynamic_cast<LevelsHandle*>(object))return new AccessibleLevelsHandle(handle);
        return nullptr;
    });});
}
class LevelsGraph final:public QWidget {
public:
    LevelsSettings settings;
    LevelsHistogram bins{};
    bool ready{};
    std::function<void(LevelRange)> changed;
    explicit LevelsGraph(QWidget* parent):QWidget(parent){
        installLevelsHandleAccessibility();
        setObjectName("levelsHistogramGraph");setAccessibleName("Original RGB histogram and Levels handles");setMinimumSize(240,210);setMouseTracking(true);
        setToolTip("Linear histogram with automatic vertical scaling. Tall spikes may extend beyond the graph; all tones from 0 to 255 remain included.");
        setAccessibleDescription(toolTip());
        const char* names[]{"Input black","Gamma","Input white","Output black","Output white"};
        for(size_t i=0;i<handles_.size();++i){
            auto* handle=handles_[i]=new LevelsHandle(this,names[i]);
            handle->read=[this,i]{const auto r=settings.ranges[size_t(settings.channel)].normalized();
                const double values[]{r.black,r.gamma,r.white,r.outputBlack,r.outputWhite};
                return LevelsHandle::Value{values[i],i==1?.1:i==2?r.black+1:0,i==1?9.99:i==0?r.white-1:255,i==1?.01:1};};
            handle->write=[this,i](double value){auto r=settings.ranges[size_t(settings.channel)].normalized();
                if(i==1){r.gamma=std::clamp(value,.1,9.99);r=r.normalized();}
                else r=moveLevelsHandle(r,i<3?int(i):int(i)-3,i>=3,value);
                if(changed)changed(r);};
        }
        syncHandles();
    }
    void syncHandles(){
        const auto r=settings.ranges[size_t(settings.channel)].normalized();const auto input=levelsInputHandles(r);
        const double positions[]{input[0],input[1],input[2],r.outputBlack,r.outputWhite};
        for(size_t i=0;i<handles_.size();++i){auto* handle=handles_[i];handle->setGeometry(int(std::lround(px(positions[i])))-11,i<3?149:187,22,20);handle->update();
            if(QAccessible::isActive()){QAccessibleValueChangeEvent event(handle,handle->value().current);QAccessible::updateAccessibility(&event);}}
    }
private:
    std::array<LevelsHandle*,5> handles_{};
    int dragging_{-1};bool output_{};
    void resizeEvent(QResizeEvent* event)override{QWidget::resizeEvent(event);syncHandles();}
    double px(double value)const{return 10+value/255*std::max(1,width()-20);}
    double valueAt(double x)const{return std::clamp((x-10)/std::max(1,width()-20)*255,0.,255.);}
    void paintEvent(QPaintEvent*)override{
        QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);QRectF graph(10,5,width()-20,145);painter.fillRect(graph,palette().color(QPalette::Base));int channel=int(settings.channel);const QColor colors[]{QColor(150,150,150),QColor(235,88,88),QColor(93,198,110),QColor(83,141,237)};double peak=histogramDisplayScale(bins[size_t(channel)]);
        if(ready&&peak>0){painter.setPen(Qt::NoPen);painter.setBrush(colors[channel]);for(int i=0;i<256;++i){double h=graph.height()*std::clamp(bins[size_t(channel)][size_t(i)]/peak,0.,1.);painter.drawRect(QRectF(graph.left()+i*graph.width()/256,graph.bottom()-h,graph.width()/256+.1,h));}}
        if(!ready){painter.setPen(palette().color(QPalette::Text));painter.drawText(graph.adjusted(8,8,-8,-8),Qt::AlignTop|Qt::AlignLeft,"Loading histogram...");}
        QLinearGradient ramp(10,0,width()-10,0);ramp.setColorAt(0,Qt::black);ramp.setColorAt(1,Qt::white);painter.fillRect(QRectF(10,174,width()-20,14),ramp);
        auto range=settings.ranges[size_t(channel)].normalized();auto input=levelsInputHandles(range);std::array<double,2> output{range.outputBlack,range.outputWhite};
        auto draw=[&](double position,int y,int i,int count){QPainterPath triangle;triangle.moveTo(px(position),y);triangle.lineTo(px(position)-6,y+10);triangle.lineTo(px(position)+6,y+10);triangle.closeSubpath();painter.setPen(QPen(QColor(128,128,128),1));painter.setBrush(i==0?Qt::black:i==count-1?Qt::white:Qt::gray);painter.drawPath(triangle);};for(int i=0;i<3;++i)draw(input[size_t(i)],153,i,3);for(int i=0;i<2;++i)draw(output[size_t(i)],191,i,2);
    }
    void mousePressEvent(QMouseEvent* event)override{
        if(event->button()!=Qt::LeftButton)return;double y=event->position().y();if(y<148||y>207||(y>169&&y<187))return;output_=y>=187;auto range=settings.ranges[size_t(settings.channel)].normalized();auto input=levelsInputHandles(range);std::array<double,3> positions=output_?std::array<double,3>{range.outputBlack,range.outputWhite,0}:input;double best=12;for(int i=0;i<(output_?2:3);++i){double distance=std::abs(px(positions[size_t(i)])-event->position().x());if(distance<=best){best=distance;dragging_=i;}}if(dragging_>=0)drag(event->position().x());
    }
    void mouseMoveEvent(QMouseEvent* event)override{if(dragging_>=0)drag(event->position().x());}
    void mouseReleaseEvent(QMouseEvent*)override{dragging_=-1;}
    void drag(double x){auto range=moveLevelsHandle(settings.ranges[size_t(settings.channel)],dragging_,output_,valueAt(x));if(changed)changed(range);}
};
}
struct LevelsAdvancedControls::Impl {
    LevelsAdvancedControls* owner;
    std::string json;
    LevelsSettings settings;
    LevelsGraph* graph{};
    std::array<QPushButton*,3> samples{},autos{};
    std::optional<LevelsSample> armed;
    QLabel* caption{};
    explicit Impl(LevelsAdvancedControls* parent):owner(parent){
        auto* layout=new QVBoxLayout(parent);layout->setContentsMargins(0,0,0,0);graph=new LevelsGraph(parent);layout->addWidget(graph);graph->changed=[this](LevelRange range){auto changed=settings;changed.ranges[size_t(changed.channel)]=range;publish(changed);};
        auto* sampling=new QHBoxLayout;sampling->addWidget(new QLabel("Sample original",parent));const char* names[]{"Black","Gray","White"};for(int i=0;i<3;++i){samples[size_t(i)]=new QPushButton(names[i],parent);auto* button=samples[size_t(i)];button->setObjectName(QString("levelsSample%1").arg(names[i]));button->setCheckable(true);sampling->addWidget(button);QObject::connect(button,&QPushButton::clicked,parent,[this,i]{arm(armed==LevelsSample(i)?std::nullopt:std::optional(LevelsSample(i)));});}layout->addLayout(sampling);
        auto* automatic=new QHBoxLayout;const char* labels[]{"Contrast","Color","Color + neutral midtones"};const char* ids[]{"Contrast","Color","Neutral"};for(int i=0;i<3;++i){autos[size_t(i)]=new QPushButton(labels[i],parent);auto* button=autos[size_t(i)];button->setObjectName(QString("levelsAuto%1").arg(ids[i]));button->setEnabled(false);automatic->addWidget(button);QObject::connect(button,&QPushButton::clicked,parent,[this,i]{arm({});publish(autoLevels(graph->bins,LevelsAuto(i)));});}layout->addLayout(automatic);
        caption=new QLabel("Original pixels: histogram weighted by alpha and selection",parent);caption->setWordWrap(true);layout->addWidget(caption);
    }
    void arm(std::optional<LevelsSample> mode){armed=mode;for(int i=0;i<3;++i)samples[size_t(i)]->setChecked(mode==LevelsSample(i));if(owner->onSampleModeChanged)owner->onSampleModeChanged(mode);}
    void publish(const LevelsSettings& next){if(json.empty()||next==settings)return;settings=next;json=withLevelsSettings(json,settings);graph->settings=settings;graph->syncHandles();graph->update();if(owner->onChanged)owner->onChanged(json);}
};
LevelsAdvancedControls::LevelsAdvancedControls(QWidget* parent):QWidget(parent),impl_(std::make_unique<Impl>(this)){setObjectName("levelsAdvancedControls");}
LevelsAdvancedControls::~LevelsAdvancedControls()=default;
void LevelsAdvancedControls::setAdjustmentJson(std::string json){auto settings=levelsFromAdjustmentJson(json);impl_->json=std::move(json);impl_->settings=settings;impl_->graph->settings=settings;const char* names[]{"RGB","Red","Green","Blue"};impl_->graph->setAccessibleName(QString("Original %1 histogram and Levels handles").arg(names[int(settings.channel)]));impl_->graph->syncHandles();impl_->graph->update();}
std::string LevelsAdvancedControls::adjustmentJson()const{return impl_->json;}
void LevelsAdvancedControls::setHistogram(LevelsHistogram histogram){for(const auto& channel:histogram)for(double value:channel)if(!std::isfinite(value)||value<0)throw std::invalid_argument("Invalid Levels histogram");impl_->graph->bins=std::move(histogram);impl_->graph->ready=true;for(auto* button:impl_->autos)button->setEnabled(true);impl_->graph->update();}
const LevelsHistogram& LevelsAdvancedControls::histogram()const{return impl_->graph->bins;}
bool LevelsAdvancedControls::histogramReady()const{return impl_->graph->ready;}
std::optional<LevelsSample> LevelsAdvancedControls::sampleMode()const{return impl_->armed;}
void LevelsAdvancedControls::applySample(std::array<double,3> rgb){if(impl_->armed)impl_->publish(sampleLevels(impl_->settings,rgb,*impl_->armed));}
namespace {
class HueSpectrum final:public QWidget {
public:
    HueSettings settings;
    std::function<void(HueBand)> changed;
    explicit HueSpectrum(QWidget* parent):QWidget(parent){setObjectName("hueSpectrumGraph");setAccessibleName("Hue range before and after spectrum with four band handles");setMinimumSize(240,70);}
private:
    std::optional<int> dragging_;
    void paintEvent(QPaintEvent*)override{
        QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);double w=width();for(bool after:{false,true}){double top=after?44:3;QPainterPath clip;clip.addRoundedRect(QRectF(0,top,w,16),3,3);painter.save();painter.setClipPath(clip);for(int i=0;i<72;++i){double hue=i*360./72;PickerHSB color{after?settings.shiftedHue(hue):hue,1,1};auto rgb=color.rgb();painter.fillRect(QRectF(i*w/72,top,w/72+.5,16),QColor::fromRgbF(rgb.red,rgb.green,rgb.blue));}painter.restore();}
        painter.setPen(Qt::NoPen);painter.setBrush(palette().color(QPalette::WindowText));auto band=settings.bands[size_t(settings.range)].handles();for(int i=0;i<4;++i){double x=band[size_t(i)]/360*w;bool inner=i==1||i==2;painter.drawRect(inner?QRectF(x-1,25,2,12):QRectF(x-3.5,28.5,7,5));}
    }
    void mousePressEvent(QMouseEvent* event)override{if(event->button()!=Qt::LeftButton||event->position().y()<21||event->position().y()>41)return;double degrees=std::clamp(event->position().x()/std::max(1,width())*360,0.,360.);dragging_=nearestHueHandle(settings.bands[size_t(settings.range)],degrees);drag(degrees);}
    void mouseMoveEvent(QMouseEvent* event)override{if(dragging_)drag(std::clamp(event->position().x()/std::max(1,width())*360,0.,360.));}
    void mouseReleaseEvent(QMouseEvent*)override{dragging_.reset();}
    void drag(double degrees){auto band=settings.bands[size_t(settings.range)];band.setHandle(*dragging_,degrees);if(changed)changed(band);}
};
class CurveGraph final:public QWidget {
public:
    CurvesSettings settings;
    std::optional<size_t> selected,dragging;
    std::function<void(const CurvesSettings&)> changed;
    std::function<void()> selectionChanged;
    explicit CurveGraph(QWidget* parent):QWidget(parent){setObjectName("curvesGraph");setAccessibleName("Curves input and output graph");setMinimumSize(240,260);}
private:
    QRectF graphRect()const{return QRectF(8,8,width()-16,height()-16);}
    QPointF position(Point point)const{auto box=graphRect();return {box.x()+point.x/255*box.width(),box.y()+(1-point.y/255)*box.height()};}
    Point value(QPointF point)const{auto box=graphRect();return {std::clamp((point.x()-box.x())/box.width()*255,0.,255.),std::clamp((1-(point.y()-box.y())/box.height())*255,0.,255.)};}
    void paintEvent(QPaintEvent*)override{
        QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);auto box=graphRect();painter.fillRect(rect(),QColor(35,35,35));painter.setPen(QPen(QColor(74,74,74),1));for(int i=0;i<=4;++i){double f=i/4.;painter.drawLine(QPointF(box.x()+f*box.width(),box.top()),QPointF(box.x()+f*box.width(),box.bottom()));painter.drawLine(QPointF(box.left(),box.y()+f*box.height()),QPointF(box.right(),box.y()+f*box.height()));}
        auto graph=curveGraph(settings);QPainterPath line;line.moveTo(position(graph[0]));for(size_t i=1;i<graph.size();++i)line.lineTo(position(graph[i]));painter.setPen(QPen(Qt::white,2));painter.setBrush(Qt::NoBrush);painter.drawPath(line);const auto& points=settings.channels[size_t(settings.channel)];for(size_t i=0;i<points.size();++i){painter.setPen(Qt::NoPen);painter.setBrush(selected==i?palette().color(QPalette::Highlight):Qt::white);painter.drawEllipse(position(points[i]),4,4);}
    }
    void mousePressEvent(QMouseEvent* event)override{if(event->button()!=Qt::LeftButton)return;auto next=settings;try{dragging=beginCurveDrag(next,value(event->position()));setToolTip({});}catch(const std::runtime_error& error){dragging.reset();setToolTip(QString::fromUtf8(error.what()));return;}if(!dragging)return;selected=dragging;if(changed)changed(next);if(selectionChanged)selectionChanged();update();}
    void mouseMoveEvent(QMouseEvent* event)override{if(!dragging)return;auto next=settings;try{moveCurvePoint(next,*dragging,value(event->position()));setToolTip({});}catch(const std::runtime_error& error){setToolTip(QString::fromUtf8(error.what()));return;}if(changed)changed(next);}
    void mouseReleaseEvent(QMouseEvent*)override{dragging.reset();}
};
}
struct HueAdvancedControls::Impl {
    HueAdvancedControls* owner;
    std::string json;
    HueSettings settings;
    HueSpectrum* spectrum{};
    QLabel* angles{};
    std::array<QPushButton*,3> samples{};
    QPushButton* target{};
    std::optional<HueSample> armed;
    std::optional<HueTargetDrag> drag;
    bool targeting{};
    explicit Impl(HueAdvancedControls* parent):owner(parent){auto* layout=new QVBoxLayout(parent);layout->setContentsMargins(0,0,0,0);spectrum=new HueSpectrum(parent);layout->addWidget(spectrum);angles=new QLabel(parent);angles->setAccessibleName("Hue band handle angles");layout->addWidget(angles);spectrum->changed=[this](HueBand band){auto next=settings;next.bands[size_t(next.range)]=band;publish(next);};auto* buttons=new QHBoxLayout;const char* labels[]{"Sample color","Add color","Remove color"};const char* ids[]{"Sample","Add","Remove"};for(int i=0;i<3;++i){auto* button=new QPushButton(labels[i],parent);samples[size_t(i)]=button;button->setObjectName(QString("hue%1Color").arg(ids[i]));button->setCheckable(true);buttons->addWidget(button);QObject::connect(button,&QPushButton::clicked,parent,[this,i]{setTargeting(false);arm(armed==HueSample(i)?std::nullopt:std::optional(HueSample(i)));});}target=new QPushButton("Targeted adjustment",parent);target->setObjectName("hueTargetedAdjustment");target->setCheckable(true);buttons->addWidget(target);QObject::connect(target,&QPushButton::clicked,parent,[this]{arm({});setTargeting(!targeting);});layout->addLayout(buttons);sync();}
    void arm(std::optional<HueSample> mode){armed=mode;for(int i=0;i<3;++i)samples[size_t(i)]->setChecked(armed==HueSample(i));if(owner->onSampleModeChanged)owner->onSampleModeChanged(mode);}
    void setTargeting(bool value){bool was=targeting;targeting=value;target->setChecked(value);if(!value)drag.reset();if(was!=value&&owner->onTargetingChanged)owner->onTargetingChanged(value);}
    void sync(){spectrum->settings=settings;spectrum->update();bool band=settings.range!=ColorRange::Master&&!settings.colorize;spectrum->setVisible(band);angles->setVisible(band);for(auto* button:samples)button->setVisible(band);target->setVisible(!settings.colorize);auto h=settings.bands[size_t(settings.range)].handles();angles->setText(QString("%1\u00b0    %2\u00b0    %3\u00b0    %4\u00b0").arg(std::round(h[0])).arg(std::round(h[1])).arg(std::round(h[2])).arg(std::round(h[3])));}
    void publish(const HueSettings& next){if(json.empty()||next==settings)return;settings=next;json=withHueSettings(json,settings);sync();if(owner->onChanged)owner->onChanged(json);}
};
HueAdvancedControls::HueAdvancedControls(QWidget* parent):QWidget(parent),impl_(std::make_unique<Impl>(this)){setObjectName("hueAdvancedControls");}
HueAdvancedControls::~HueAdvancedControls()=default;
void HueAdvancedControls::setAdjustmentJson(std::string json){auto settings=hueFromAdjustmentJson(json);impl_->json=std::move(json);impl_->settings=settings;impl_->sync();}
std::string HueAdvancedControls::adjustmentJson()const{return impl_->json;}
std::optional<HueSample> HueAdvancedControls::sampleMode()const{return impl_->armed;}
bool HueAdvancedControls::targeting()const{return impl_->targeting;}
void HueAdvancedControls::applySample(PaletteColor color){if(!impl_->armed)return;if(auto hue=sampledHue(color))impl_->publish(sampleHueRange(impl_->settings,*hue,*impl_->armed));}
bool HueAdvancedControls::beginTarget(PaletteColor color){if(!impl_->targeting)return false;auto settings=impl_->settings;impl_->drag=beginHueTargeting(settings,color);if(!impl_->drag)return false;impl_->publish(settings);return true;}
void HueAdvancedControls::dragTarget(double delta,bool hue){if(impl_->drag)impl_->publish(dragHueTargeting(impl_->settings,*impl_->drag,delta,hue));}
void HueAdvancedControls::endTarget(){impl_->drag.reset();}
struct CurvesAdvancedControls::Impl {
    CurvesAdvancedControls* owner;
    std::string json;
    CurvesSettings settings;
    CurveGraph* graph{};
    QLabel* coordinates{};
    QPushButton* remove{};
    explicit Impl(CurvesAdvancedControls* parent):owner(parent){auto* layout=new QVBoxLayout(parent);layout->setContentsMargins(0,0,0,0);graph=new CurveGraph(parent);layout->addWidget(graph);graph->changed=[this](const CurvesSettings& next){publish(next);};graph->selectionChanged=[this]{sync();};layout->addWidget(new QLabel("Click to add a point. Drag to adjust.",parent));auto* row=new QHBoxLayout;coordinates=new QLabel(parent);row->addWidget(coordinates);row->addStretch();remove=new QPushButton("Remove point",parent);remove->setObjectName("curvesRemovePoint");row->addWidget(remove);layout->addLayout(row);QObject::connect(remove,&QPushButton::clicked,parent,[this]{if(!graph->selected)return;auto next=settings;if(removeCurvePoint(next,*graph->selected)){graph->selected.reset();publish(next);}});auto* reset=new QPushButton("Reset curve",parent);reset->setObjectName("curvesResetCurve");layout->addWidget(reset);QObject::connect(reset,&QPushButton::clicked,parent,[this]{auto next=settings;resetCurve(next);graph->selected.reset();publish(next);sync();});sync();}
    void sync(){graph->settings=settings;const auto& points=settings.channels[size_t(settings.channel)];if(graph->selected&&*graph->selected>=points.size())graph->selected.reset();if(graph->selected){auto point=points[*graph->selected];coordinates->setText(QString("Input %1  |  Output %2").arg(int(point.x)).arg(int(point.y)));}else coordinates->clear();remove->setEnabled(graph->selected&&*graph->selected>0&&*graph->selected+1<points.size());graph->update();}
    void publish(const CurvesSettings& next){if(json.empty()||next==settings)return;settings=next;json=withCurvesSettings(json,settings);sync();if(owner->onChanged)owner->onChanged(json);}
};
CurvesAdvancedControls::CurvesAdvancedControls(QWidget* parent):QWidget(parent),impl_(std::make_unique<Impl>(this)){setObjectName("curvesAdvancedControls");}
CurvesAdvancedControls::~CurvesAdvancedControls()=default;
void CurvesAdvancedControls::setAdjustmentJson(std::string json){auto settings=curvesFromAdjustmentJson(json);if(settings.channel!=impl_->settings.channel){impl_->graph->selected.reset();impl_->graph->dragging.reset();}impl_->json=std::move(json);impl_->settings=std::move(settings);impl_->sync();}
std::string CurvesAdvancedControls::adjustmentJson()const{return impl_->json;}

}
