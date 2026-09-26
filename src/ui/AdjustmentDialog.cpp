#include "AdjustmentDialog.h"
#include "DocumentPreview.h"
#include "LevelsPreviewHistogram.h"
#include "AdjustmentAdvancedControls.h"
#include "PropertyControls.h"
#include <QEventLoop>
#include <map>
#include "editing/Selection.h"
#include "layers/LayerOperations.h"
#include <QMouseEvent>
#include "effects/Adjustments.h"
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSignalBlocker>
#include <QScopedValueRollback>
#include <QLabel>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QColorDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <QTimer>
#include <QImage>
#include <QPixmap>
#include <QPainter>
#include <algorithm>
#include <atomic>
#include <cmath>

namespace compositor {
namespace {
using Object=QJsonObject;
std::string encoded(const Object&o){return QJsonDocument(o).toJson(QJsonDocument::Compact).toStdString();}
class AdjustmentPreview final:public QLabel {
public:
    int documentWidth{},documentHeight{};
    std::function<void(Point)> onClick;
    std::function<bool(Point)> onBeginDrag;
    std::function<void(double,bool)> onDrag;
    std::function<void()> onEndDrag;
    AdjustmentPreview(){setObjectName("adjustmentPreview");setAccessibleName("Adjustment preview; click to sample, drag for targeted hue adjustment");}
private:
    bool dragging_{};double anchor_{};
    QRectF imageRect()const{
        auto image=pixmap();if(image.isNull())return {};auto size=image.deviceIndependentSize();double scale=std::min(width()/size.width(),height()/size.height());size*=scale;return {(width()-size.width())/2.,(height()-size.height())/2.,size.width(),size.height()};
    }
    void paintEvent(QPaintEvent*)override{QPainter painter(this);painter.fillRect(rect(),palette().color(QPalette::Window));auto image=pixmap();if(!image.isNull()){painter.setRenderHint(QPainter::SmoothPixmapTransform);painter.drawPixmap(imageRect(),image,QRectF(image.rect()));}}
    std::optional<Point> documentPoint(QPointF position)const{
        auto box=imageRect();if(box.isEmpty()||!box.contains(position))return {};
        return Point{(position.x()-box.left())*documentWidth/box.width(),(position.y()-box.top())*documentHeight/box.height()};
    }
    void mousePressEvent(QMouseEvent* event)override{
        if(event->button()!=Qt::LeftButton)return;auto point=documentPoint(event->position());if(!point)return;
        if(onBeginDrag&&onBeginDrag(*point)){dragging_=true;anchor_=event->position().x();return;}if(onClick)onClick(*point);
    }
    void mouseMoveEvent(QMouseEvent* event)override{if(dragging_&&onDrag)onDrag(event->position().x()-anchor_,event->modifiers().testFlag(Qt::ControlModifier));}
    void mouseReleaseEvent(QMouseEvent* event)override{if(event->button()!=Qt::LeftButton||!dragging_)return;dragging_=false;if(onEndDrag)onEndDrag();}
};
// AdjustmentEditing.swift retains hierarchy and live-mask dependencies. Hidden
// records still supply masks; erasing a flat suffix changes or invalidates input.
Document levelsInput(Document document,const std::string& active,bool existing,const std::string& insertedId){
    std::string cutoff=active;
    if(!existing){auto found=std::find_if(document.layers.begin(),document.layers.end(),[&](const Layer& layer){return layer.id==active;});Layer marker;marker.id=insertedId;marker.adjustmentJson=effects::defaultAdjustmentJson("Levels");marker.transform={0,0,double(document.width),double(document.height)};if(found!=document.layers.end())marker.parentId=found->group?found->id:found->parentId;document.layers.insert(found==document.layers.end()?document.layers.end():found+1,marker);cutoff=marker.id;}
    std::unordered_set<std::string> underneath;for(const auto& entry:layers::entries(document)){if(entry.id==cutoff)break;underneath.insert(entry.id);}
    for(auto& layer:document.layers)if(!layer.group&&!underneath.contains(layer.id))layer.visible=false;
    document.selection.reset();return document;
}
struct HistogramResult {std::shared_ptr<const Raster> sample;std::shared_ptr<const Document> composite;Transform transform;effects_tools::LevelsHistogram bins{};QString error;};
struct PreviewResult { std::optional<AdjustmentDialogResult> value; QImage image; QString error; bool full{}; };
// Levels.swift135 and HueSaturation.swift384 use8000; the shared FilterEdit
// preview uses2048 except full-resolution grain. Drawing samples nearest here,
// matching BrushRaster.draw(.none); full Apply always reads the original.
std::shared_ptr<const Raster> adjustmentPreviewSource(const std::shared_ptr<const Raster>& source,const std::string& json,const std::function<bool()>& cancelled){
    auto check=[&]{if(cancelled&&cancelled())throw std::runtime_error("Adjustment cancelled");};check();
    const auto kind=QJsonDocument::fromJson(QByteArray::fromStdString(json)).object()["kind"].toString();
    if(kind=="Grain")return source;
    const int limit=kind=="Levels"||kind=="Hue/Saturation"?8000:2048;
    const double factor=std::min(1.,double(limit)/std::max(source->width,source->height));if(factor==1)return source;
    const int width=std::max(1,int(source->width*factor)),height=std::max(1,int(source->height*factor));
    std::vector<Pixel> pixels(size_t(width)*height);
    for(int y=0;y<height;++y){check();for(int x=0;x<width;++x)
        pixels[size_t(y)*width+x]=source->pixel(std::min(source->width-1,int((x+.5)*source->width/width)),std::min(source->height-1,int((y+.5)*source->height/height)));}
    check();
    return Raster::fromRgba(width,height,reinterpret_cast<const uint8_t*>(pixels.data()),size_t(width)*4);
}
PreviewResult makePreview(Document doc,std::string active,const std::string&json,bool live,bool existing,const std::string&newId,bool showPreview,bool full,const std::function<bool()>& cancelled){
    auto check=[&]{if(cancelled&&cancelled())throw std::runtime_error("Adjustment cancelled");};
    PreviewResult result;result.full=full;const Document before=doc;
    try{
        check();auto found=std::find_if(doc.layers.begin(),doc.layers.end(),[&](const Layer&l){return l.id==active;});
        if(live){
            if(existing){if(found==doc.layers.end()||found->adjustmentJson.empty())throw std::runtime_error("Adjustment layer no longer exists");found->adjustmentJson=json;}
            else {Layer layer;layer.id=newId;layer.name=QJsonDocument::fromJson(QByteArray::fromStdString(json)).object()["kind"].toString().toStdString();layer.adjustmentJson=json;layer.transform={0,0,double(doc.width),double(doc.height)};
                if(found!=doc.layers.end())layer.parentId=found->group?found->id:found->parentId;
                doc.layers.insert(found==doc.layers.end()?doc.layers.end():found+1,layer);active=layer.id;}
        }else{
            if(found==doc.layers.end()||!found->raster)throw std::runtime_error("Select an image layer");
            if(!full)found->raster=adjustmentPreviewSource(found->raster,json,cancelled);
            auto selection=editing::mappedCoverage(doc,found->transform,found->raster->width,found->raster->height);
            auto changed=effects::applyAdjustment(found->raster,json,selection.get(),{},cancelled);if(changed!=found->raster){found->raster=changed;found->shapeJson.clear();}
        }
        check();validateDocument(doc);
        result.image=fittedDocumentPreview(showPreview?doc:before,{600,460});
        check();result.value=AdjustmentDialogResult{std::move(doc),std::move(active)};
    }catch(const std::exception&e){result.error=e.what();}
    return result;
}
}

namespace {
class AdjustmentWindow final:public QDialog {
public:
    using QDialog::QDialog;std::function<bool()> mayReject;
    void reject()override{if(!mayReject||mayReject())QDialog::reject();}
};
class AdjustmentPanel final:public ui::EditPanelSession {
    Document original;
    std::string active;
    QString kind;
    bool live{},existing{},modal{},committing_{},closed_{},targetDragging_{},loadingSettings_{};
    double targetAnchor_{};
    AdjustmentDialogOptions options;
    Object settings;
    AdjustmentWindow dialog;
    QHBoxLayout layout;
    QWidget fields;
    QFormLayout form;
    QWidget right;
    QVBoxLayout rightLayout;
    AdjustmentPreview preview;
    QLabel status;
    QCheckBox previewEnabled;
    QDialogButtonBox buttons;
    QPushButton* apply{};
    QTimer debounce;
    QFutureWatcher<PreviewResult> watcher;
    QFutureWatcher<HistogramResult> histogramWatcher;
    std::optional<AdjustmentDialogResult> completed;
    uint64_t revision{},runningRevision{};
    bool pending{},closing{};
    std::shared_ptr<std::atomic_bool> previewCancelled=std::make_shared<std::atomic_bool>(false);
    std::shared_ptr<std::atomic_bool> histogramCancelled=std::make_shared<std::atomic_bool>(false);
    std::string insertedId=newId();
    std::function<void()> changed,start;
    std::vector<std::function<void()>> reloadControls;
    LevelsAdvancedControls* levelsControls{};
    HueAdvancedControls* hueControls{};
    static std::map<Kind,QPoint>& positions(){static std::map<Kind,QPoint> value;return value;}
    void retire(){if(closed_&&!watcher.isRunning()&&!histogramWatcher.isRunning())deleteLater();}
    void finish(int answer){
        if(closed_)return;closed_=true;closing=true;previewCancelled->store(true);histogramCancelled->store(true);debounce.stop();
        if(host_.closeColor)host_.closeColor(answer==QDialog::Accepted);
        if(!modal)positions()[kind_]=dialog.pos();
        try{if(answer==QDialog::Accepted&&completed&&(!host_.valid||host_.valid())&&host_.commit)host_.commit({std::move(completed->document),std::move(completed->active),false});}
        catch(const std::exception& error){if(host_.error)host_.error(QString::fromUtf8(error.what()));}
        if(host_.preview)host_.preview({});if(host_.closed)host_.closed();host_={};retire();
    }
    void initialize(const QString& requestedKind){
    if(existing){auto it=std::find_if(original.layers.begin(),original.layers.end(),[&](const Layer&l){return l.id==active;});if(it==original.layers.end())throw std::runtime_error("Adjustment layer no longer exists");settings=QJsonDocument::fromJson(QByteArray::fromStdString(it->adjustmentJson)).object();}
    else settings=QJsonDocument::fromJson(QByteArray::fromStdString(!live&&!options.initialAdjustmentJson.empty()?options.initialAdjustmentJson:effects::defaultAdjustmentJson(requestedKind.toStdString()))).object();
    kind=settings["kind"].toString();kind_=kind=="Levels"?Kind::Levels:kind=="Hue/Saturation"?Kind::Hue:Kind::Filter;
    if(kind=="Exposure"&&!settings.contains("exposureSettings"))settings["exposureSettings"]=Object{{"exposure",0},{"offset",0},{"gamma",1}};
    if(kind=="Gradient Map"&&!settings.contains("gradientMapSettings"))settings["gradientMapSettings"]=Object{{"shadows",Object{{"red",0},{"green",0},{"blue",0}}},{"highlights",Object{{"red",1},{"green",1},{"blue",1}}},{"reversed",false}};
    if(kind=="Grain"&&!settings.contains("grainSettings"))settings["grainSettings"]=Object{{"amount",25},{"size",1.5},{"roughness",50},{"seed",0}};
    dialog.mayReject=[this]{return !committing_;};dialog.setObjectName("adjustmentDialog");dialog.setWindowTitle(kind);dialog.resize(840,560);
    layout.addWidget(&fields);
    preview.documentWidth=original.width;preview.documentHeight=original.height;preview.setMinimumSize(400,300);preview.setAlignment(Qt::AlignCenter);rightLayout.addWidget(&preview,1);status.setWordWrap(true);rightLayout.addWidget(&status);
    previewEnabled.setObjectName("adjustmentPreviewEnabled");previewEnabled.setChecked(true);rightLayout.addWidget(&previewEnabled);
    rightLayout.addWidget(&buttons);layout.addWidget(&right,1);
    apply=buttons.button(QDialogButtonBox::Apply);apply->setEnabled(false);
    debounce.setSingleShot(true);debounce.setInterval(100);
    changed=[this]{++revision;if(watcher.isRunning())previewCancelled->store(true);apply->setEnabled(false);if(!previewEnabled.isChecked()&&host_.preview)host_.preview({});status.setText("Updating preview…");debounce.start();};
    QObject::connect(&previewEnabled,&QCheckBox::toggled,&dialog,changed);
    auto number=[&](const QString&label,double value,double lo,double hi,int decimals,std::function<void(double)>setter){
        const int stepDecimals=kind=="Grain"?(label=="Size"?1:0):kind=="Hue/Saturation"?0:kind=="Levels"&&label!="Gamma"?0:decimals;
        auto*spin=new ui::PropertyNumber;spin->setSingleStep(std::pow(10.,-stepDecimals));spin->setRange(lo,hi);spin->setDecimals(decimals);spin->setValue(value);spin->setAccessibleName(label);
        if(kind=="Exposure"||kind=="Grain"){
            auto* row=new QWidget;auto* rowLayout=new QHBoxLayout(row);rowLayout->setContentsMargins(0,0,0,0);auto* slider=new ui::TrackSlider(Qt::Horizontal);slider->setRange(0,10000);slider->setAccessibleName(label+" slider");
            const bool logarithmic=label=="Gamma"||label=="Size";const double first=logarithmic?std::log(lo):lo,last=logarithmic?std::log(hi):hi;
            auto position=[first,last,logarithmic](double v){return int(std::lround(((logarithmic?std::log(v):v)-first)/(last-first)*10000));};
            slider->setValue(position(spin->value()));rowLayout->addWidget(slider,1);rowLayout->addWidget(spin);form.addRow(label,row);
            QObject::connect(slider,&QSlider::valueChanged,&dialog,[spin,first,last,logarithmic,stepDecimals](int v){const double normalized=first+(last-first)*v/10000.;const double factor=std::pow(10.,stepDecimals);spin->setValue(std::round((logarithmic?std::exp(normalized):normalized)*factor)/factor);});
            QObject::connect(spin,&QDoubleSpinBox::valueChanged,&dialog,[slider,position](double v){QSignalBlocker block(slider);slider->setValue(position(v));});
        }else form.addRow(label,spin);
        QObject::connect(spin,&QDoubleSpinBox::valueChanged,&dialog,[&,setter](double v){if(loadingSettings_)return;setter(v);changed();});return spin;};
    auto flag=[&](const QString&label,bool value,std::function<void(bool)>setter){auto*box=new QCheckBox(label);box->setChecked(value);form.addRow(box);QObject::connect(box,&QCheckBox::toggled,&dialog,[&,setter](bool v){if(loadingSettings_)return;setter(v);changed();});return box;};
    auto property=[&](const QString&group,const QString&key,const QString&label,double fallback,double lo,double hi,int decimals){auto* spin=number(label,settings[group].toObject().value(key).toDouble(fallback),lo,hi,decimals,[&,group,key](double v){auto o=settings[group].toObject();o[key]=v;settings[group]=o;});reloadControls.push_back([this,spin,group,key,fallback]{spin->setValue(settings[group].toObject().value(key).toDouble(fallback));});return spin;};
    if(kind=="Exposure"){
        property("exposureSettings","exposure","Exposure",0,-20,20,2);property("exposureSettings","offset","Offset",0,-.5,.5,4);property("exposureSettings","gamma","Gamma",1,.01,9.99,2);
    }else if(kind=="Grain"){
        property("grainSettings","amount","Amount",25,0,100,1);property("grainSettings","size","Size",1.5,.5,20,2);property("grainSettings","roughness","Roughness",50,0,100,1);
    }else if(kind=="Gradient Map"){
        for(const QString key:{"shadows","highlights"}){auto*button=new QPushButton;auto update=[&,button,key]{auto c=settings["gradientMapSettings"].toObject()[key].toObject();button->setText(QColor::fromRgbF(c["red"].toDouble(key=="highlights"),c["green"].toDouble(key=="highlights"),c["blue"].toDouble(key=="highlights")).name());};update();reloadControls.push_back(update);form.addRow(key=="shadows"?"Shadows":"Highlights",button);
            QObject::connect(button,&QPushButton::clicked,&dialog,[&,key,update]{auto o=settings["gradientMapSettings"].toObject();auto c=o[key].toObject();
                if(host_.openColor){QPointer<AdjustmentPanel> owner=this;host_.openColor({c["red"].toDouble(),c["green"].toDouble(),c["blue"].toDouble()},key=="highlights"?"Color Picker (Gradient Map Highlights)":"Color Picker (Gradient Map Shadows)",[owner,key,update](effects_tools::PaletteColor color){if(!owner||owner->closed_||owner->committing_)return;auto next=owner->settings["gradientMapSettings"].toObject();auto value=Object{{"red",color.red},{"green",color.green},{"blue",color.blue}};if(next[key].toObject()==value)return;next[key]=value;owner->settings["gradientMapSettings"]=next;update();owner->changed();});return;}
                auto color=QColorDialog::getColor(QColor::fromRgbF(c["red"].toDouble(),c["green"].toDouble(),c["blue"].toDouble()),&dialog,key);if(color.isValid()){o[key]=Object{{"red",color.redF()},{"green",color.greenF()},{"blue",color.blueF()}};settings["gradientMapSettings"]=o;update();changed();}});}
        auto* reverse=flag("Reverse",settings["gradientMapSettings"].toObject()["reversed"].toBool(),[&](bool v){auto o=settings["gradientMapSettings"].toObject();o["reversed"]=v;settings["gradientMapSettings"]=o;});reloadControls.push_back([this,reverse]{reverse->setChecked(settings["gradientMapSettings"].toObject()["reversed"].toBool());});
    }else if(kind=="Levels"){
        auto* channel=new QComboBox;channel->setObjectName("levelsChannel");channel->addItems({"RGB","Red","Green","Blue"});form.addRow("Channel",channel);
        auto* controls=new LevelsAdvancedControls;levelsControls=controls;controls->setAdjustmentJson(encoded(settings));form.addRow(controls);
        std::array<QDoubleSpinBox*,5> values{};const QStringList labels{"Input black","Gamma","Input white","Output black","Output white"};
        for(int i=0;i<5;++i){values[size_t(i)]=number(labels[i],i==1?1:i>=2?255:0,i==1?.1:0,i==1?9.99:255,i==1?2:1,[&,controls,i](double value){auto typed=effects_tools::levelsFromAdjustmentJson(encoded(settings));auto& range=typed.ranges[size_t(typed.channel)];double* fields[]{&range.black,&range.gamma,&range.white,&range.outputBlack,&range.outputWhite};*fields[i]=value;settings=QJsonDocument::fromJson(QByteArray::fromStdString(effects_tools::withLevelsSettings(encoded(settings),typed))).object();controls->setAdjustmentJson(encoded(settings));});values[size_t(i)]->setObjectName(QString("levelsValue%1").arg(i));}
        auto load=[&,channel,values,controls]{auto typed=effects_tools::levelsFromAdjustmentJson(encoded(settings));QSignalBlocker block(channel);channel->setCurrentIndex(int(typed.channel));auto range=typed.ranges[size_t(typed.channel)].normalized();const double inputs[]{range.black,range.gamma,range.white,range.outputBlack,range.outputWhite};for(int i=0;i<5;++i){QSignalBlocker spin(values[size_t(i)]);values[size_t(i)]->setValue(inputs[i]);}controls->setAdjustmentJson(encoded(settings));};load();reloadControls.push_back(load);
        QObject::connect(channel,&QComboBox::currentIndexChanged,&dialog,[&,channel,load]{auto typed=effects_tools::levelsFromAdjustmentJson(encoded(settings));typed.channel=effects_tools::LevelsChannel(channel->currentIndex());settings=QJsonDocument::fromJson(QByteArray::fromStdString(effects_tools::withLevelsSettings(encoded(settings),typed))).object();load();changed();});
        for(auto* spin:values)QObject::connect(spin,&QDoubleSpinBox::valueChanged,&dialog,[load]{load();});
        controls->onChanged=[&,load](const std::string& json){settings=QJsonDocument::fromJson(QByteArray::fromStdString(json)).object();load();changed();};
        auto* reset=new QPushButton("Reset Levels");reset->setObjectName("levelsReset");form.addRow(reset);QObject::connect(reset,&QPushButton::clicked,&dialog,[&,load]{settings=QJsonDocument::fromJson(QByteArray::fromStdString(effects_tools::withLevelsSettings(encoded(settings),{}))).object();load();changed();});
        auto sample=std::make_shared<HistogramResult>();
        controls->onSampleModeChanged=[&](auto mode){preview.setCursor(mode?Qt::CrossCursor:Qt::ArrowCursor);};
        preview.onClick=[&,controls,sample](Point point){if(!controls->sampleMode())return;try{auto rgb=sample->composite?sampleDocumentOriginalRGB(*sample->composite,point):sample->sample?effects_tools::sampleOriginalRGB(*sample->sample,sample->transform,point,original.width,original.height):std::nullopt;if(rgb)controls->applySample(*rgb);}catch(const std::exception& error){status.setText(error.what());}};
        QObject::connect(&histogramWatcher,&QFutureWatcher<HistogramResult>::finished,&dialog,[&,controls,sample]{if(closing){retire();return;}*sample=histogramWatcher.result();if(!sample->error.isEmpty()){status.setText(sample->error);return;}controls->setHistogram(sample->bins);});
        histogramWatcher.setFuture(QtConcurrent::run([original=original,active=active,live=live,existing=existing,insertedId=insertedId,histogramCancelled=histogramCancelled]{HistogramResult result;try{result.transform={0,0,double(original.width),double(original.height)};auto found=std::find_if(original.layers.begin(),original.layers.end(),[&](const Layer& layer){return layer.id==active;});if(!live){if(found==original.layers.end()||!found->raster)throw std::runtime_error("Select an image layer");result.sample=found->raster;result.transform=found->transform;result.bins=levelsPreviewHistogram(*result.sample,result.transform,original,[histogramCancelled]{return histogramCancelled->load();}).bins;}else{result.composite=std::make_shared<const Document>(levelsInput(original,active,existing,insertedId));result.bins=documentLevelsPreviewHistogram(*result.composite,[histogramCancelled]{return histogramCancelled->load();});}}catch(const std::exception& error){result.error=error.what();}return result;}));
    }else if(kind=="Curves"){
        auto* channel=new QComboBox;channel->setObjectName("curvesChannel");channel->addItems({"RGB","Red","Green","Blue"});channel->setCurrentIndex(int(effects_tools::curvesFromAdjustmentJson(encoded(settings)).channel));form.addRow("Channel",channel);
        auto* controls=new CurvesAdvancedControls;controls->setAdjustmentJson(encoded(settings));form.addRow(controls);
        reloadControls.push_back([this,channel,controls]{QSignalBlocker block(channel);channel->setCurrentIndex(int(effects_tools::curvesFromAdjustmentJson(encoded(settings)).channel));controls->setAdjustmentJson(encoded(settings));});
        controls->onChanged=[&](const std::string& json){settings=QJsonDocument::fromJson(QByteArray::fromStdString(json)).object();changed();};
        QObject::connect(channel,&QComboBox::currentIndexChanged,&dialog,[&,channel,controls]{auto typed=effects_tools::curvesFromAdjustmentJson(encoded(settings));typed.channel=effects_tools::LevelsChannel(channel->currentIndex());settings=QJsonDocument::fromJson(QByteArray::fromStdString(effects_tools::withCurvesSettings(encoded(settings),typed))).object();controls->setAdjustmentJson(encoded(settings));changed();});
    }else if(kind=="Hue/Saturation"){
        settings=QJsonDocument::fromJson(QByteArray::fromStdString(effects_tools::withHueSettings(encoded(settings),effects_tools::hueFromAdjustmentJson(encoded(settings))))).object();
        auto* range=new QComboBox;range->setObjectName("hueRange");range->addItems({"Master","Reds","Yellows","Greens","Cyans","Blues","Magentas"});form.addRow("Range",range);
        auto* controls=new HueAdvancedControls;hueControls=controls;controls->setAdjustmentJson(encoded(settings));
        std::array<QDoubleSpinBox*,3> spins{};const QStringList labels{"Hue","Saturation","Lightness"};
        for(int i=0;i<3;++i){spins[size_t(i)]=number(labels[i],0,i==0?-180:-100,i==0?180:100,1,[&,controls,i](double value){auto typed=effects_tools::hueFromAdjustmentJson(encoded(settings));auto& adjustment=typed.adjustments[size_t(typed.range)];if(i==0)adjustment.hue=value;else if(i==1)adjustment.saturation=value;else adjustment.lightness=value;settings=QJsonDocument::fromJson(QByteArray::fromStdString(effects_tools::withHueSettings(encoded(settings),typed))).object();controls->setAdjustmentJson(encoded(settings));});spins[size_t(i)]->setObjectName(QString("hueValue%1").arg(i));}
        form.addRow(controls);auto* outside=new QCheckBox("Apply outside this range instead");outside->setObjectName("hueInvertRange");form.addRow(outside);auto* colorize=new QCheckBox("Colorize");colorize->setObjectName("hueColorize");form.addRow(colorize);
        auto load=[&,range,controls,spins,outside,colorize]{auto typed=effects_tools::hueFromAdjustmentJson(encoded(settings));QSignalBlocker rangeBlock(range),outsideBlock(outside),colorizeBlock(colorize);range->setCurrentIndex(int(typed.range));range->setEnabled(!typed.colorize);outside->setChecked(typed.invertRange);outside->setVisible(typed.range!=effects_tools::ColorRange::Master&&!typed.colorize);colorize->setChecked(typed.colorize);auto value=typed.adjustments[size_t(typed.range)];double inputs[]{value.hue,value.saturation,value.lightness};for(int i=0;i<3;++i){QSignalBlocker spin(spins[size_t(i)]);spins[size_t(i)]->setRange(i==0?(typed.colorize?0:-180):i==1&&typed.colorize?0:-100,i==0?(typed.colorize?360:180):100);spins[size_t(i)]->setValue(inputs[i]);}controls->setAdjustmentJson(encoded(settings));};load();reloadControls.push_back(load);
        auto store=[&,load](const effects_tools::HueSettings& typed){settings=QJsonDocument::fromJson(QByteArray::fromStdString(effects_tools::withHueSettings(encoded(settings),typed))).object();load();changed();};
        QObject::connect(range,&QComboBox::currentIndexChanged,&dialog,[&,range,store]{auto typed=effects_tools::hueFromAdjustmentJson(encoded(settings));typed.range=effects_tools::ColorRange(range->currentIndex());store(typed);});
        QObject::connect(outside,&QCheckBox::toggled,&dialog,[&,store](bool value){auto typed=effects_tools::hueFromAdjustmentJson(encoded(settings));typed.invertRange=value;store(typed);});
        QObject::connect(colorize,&QCheckBox::toggled,&dialog,[store](bool value){store(value?effects_tools::HueSettings::colorizeStart():effects_tools::HueSettings{});});
        auto* reset=new QPushButton("Reset Hue/Saturation");reset->setObjectName("hueReset");form.addRow(reset);QObject::connect(reset,&QPushButton::clicked,&dialog,[&,store]{store(effects_tools::hueFromAdjustmentJson(encoded(settings)).colorize?effects_tools::HueSettings::colorizeStart():effects_tools::HueSettings{});});
        controls->onChanged=[&,load](const std::string& json){settings=QJsonDocument::fromJson(QByteArray::fromStdString(json)).object();load();changed();};
        auto cursor=[&,controls]{preview.setCursor(controls->sampleMode()||controls->targeting()?Qt::CrossCursor:Qt::ArrowCursor);};controls->onSampleModeChanged=[cursor](auto){cursor();};controls->onTargetingChanged=[cursor](bool){cursor();};
        auto colorAt=[&](Point point)->std::optional<effects_tools::PaletteColor>{try{if(host_.sampleComposite)return host_.sampleComposite(point);return effects_tools::sampleCompositeColor(previewEnabled.isChecked()&&completed?completed->document:original,point,SoftwareRenderer());}catch(const std::exception& error){status.setText(error.what());return {};}};
        preview.onClick=[controls,colorAt](Point point){if(controls->sampleMode())if(auto color=colorAt(point))controls->applySample(*color);};
        preview.onBeginDrag=[controls,colorAt](Point point){if(!controls->targeting())return false;if(auto color=colorAt(point))return controls->beginTarget(*color);return false;};
        preview.onDrag=[controls](double delta,bool hue){controls->dragTarget(delta,hue);};preview.onEndDrag=[controls]{controls->endTarget();};
    }
    start=[this]{if(closing)return;if(watcher.isRunning()){pending=true;previewCancelled->store(true);return;}pending=false;previewCancelled=std::make_shared<std::atomic_bool>(false);runningRevision=revision;const auto json=encoded(settings);const bool showPreview=previewEnabled.isChecked(),full=committing_;status.setText(full?"Applying adjustment…":"Updating preview…");watcher.setFuture(QtConcurrent::run([original=original,active=active,json,live=live,existing=existing,insertedId=insertedId,showPreview,full,previewCancelled=previewCancelled]{return makePreview(original,active,json,live,existing,insertedId,showPreview,full,[previewCancelled]{return previewCancelled->load();});}));};
    QObject::connect(&debounce,&QTimer::timeout,&dialog,start);
    QObject::connect(&watcher,&QFutureWatcher<PreviewResult>::finished,&dialog,[&]{if(closing){retire();return;}auto result=watcher.result();if(pending||runningRevision!=revision){start();return;}if(!result.error.isEmpty()){status.setText(result.error);apply->setEnabled(false);if(result.full){if(host_.error)host_.error(result.error);committing_=false;dialog.reject();}return;}completed=std::move(result.value);if(host_.valid&&!host_.valid()){committing_=false;cancel();return;}if(result.full){dialog.accept();return;}if(host_.preview)host_.preview(previewEnabled.isChecked()?std::make_shared<const Document>(completed->document):nullptr);preview.setPixmap(QPixmap::fromImage(result.image));status.setText(previewEnabled.isChecked()?"Preview":"Original image  -  preview off");apply->setEnabled(true);});
    QObject::connect(apply,&QPushButton::clicked,&dialog,[&]{
        if(host_.valid&&!host_.valid()){cancel();return;}
        if(host_.closeColor)host_.closeColor(true);
        if(!live){
            const auto exposure=settings["exposureSettings"].toObject(),grain=settings["grainSettings"].toObject();
            if((kind=="Exposure"&&exposure.value("exposure").toDouble()==0&&exposure.value("offset").toDouble()==0&&exposure.value("gamma").toDouble(1)==1)||(kind=="Grain"&&grain.value("amount").toDouble(25)==0)){dialog.reject();return;}
            if(options.onApply)options.onApply(encoded(settings));
        }
        committing_=true;apply->setEnabled(false);buttons.button(QDialogButtonBox::Cancel)->setEnabled(false);fields.setEnabled(false);previewEnabled.setEnabled(false);debounce.stop();
        if(host_.preview)host_.preview(previewEnabled.isChecked()&&completed?std::make_shared<const Document>(completed->document):nullptr);
        start();
    });QObject::connect(&buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    QObject::connect(&dialog,&QDialog::finished,this,[this](int answer){finish(answer);});
    if(modal){dialog.setWindowModality(Qt::WindowModal);}else{dialog.setWindowFlags(Qt::Tool|Qt::WindowTitleHint|Qt::WindowCloseButtonHint);dialog.setWindowModality(Qt::NonModal);preview.hide();dialog.resize(440,520);if(auto found=positions().find(kind_);found!=positions().end())dialog.move(found->second);}
    start();dialog.show();dialog.raise();dialog.activateWindow();
    if(auto* first=dialog.findChild<QDoubleSpinBox*>())first->setFocus(Qt::ActiveWindowFocusReason);
    else previewEnabled.setFocus(Qt::ActiveWindowFocusReason);

    }
public:
    AdjustmentPanel(QWidget* parent,const Document& input,const std::string& id,const QString& requestedKind,bool isLive,bool isExisting,const AdjustmentDialogOptions& initial,ui::EditPanelHost host,bool compatibility)
        :EditPanelSession(parent,requestedKind=="Levels"?Kind::Levels:requestedKind=="Hue/Saturation"?Kind::Hue:Kind::Filter,isLive,std::move(host)),
        original(input),active(id),live(isLive),existing(isExisting),modal(compatibility),options(initial),dialog(parent),layout(&dialog),form(&fields),rightLayout(&right),previewEnabled("Preview"),buttons(QDialogButtonBox::Apply|QDialogButtonBox::Cancel){initialize(requestedKind);kind_=kind=="Levels"?Kind::Levels:kind=="Hue/Saturation"?Kind::Hue:Kind::Filter;}
    ~AdjustmentPanel()override{closing=true;previewCancelled->store(true);histogramCancelled->store(true);QObject::disconnect(&watcher,nullptr,&dialog,nullptr);QObject::disconnect(&histogramWatcher,nullptr,&dialog,nullptr);watcher.waitForFinished();histogramWatcher.waitForFinished();}
    QDialog* panel()const override{return const_cast<AdjustmentWindow*>(&dialog);}
    bool committing()const override{return committing_;}
    void cancel()override{if(!committing_&&!closed_)dialog.reject();}
    std::string adjustmentSettings()const override{return encoded(settings);}
    std::optional<std::string> originalAdjustmentSettings()const override{
        if(closed_||!live)return {};
        auto found=std::find_if(original.layers.begin(),original.layers.end(),[this](const Layer& layer){return layer.id==active;});
        if(found==original.layers.end()||found->adjustmentJson.empty())return {};
        return found->adjustmentJson;
    }
    bool updateAdjustmentSettings(const std::string& json,bool previewState)override{
        if(closed_||committing_)return false;
        effects::validateAdjustmentJson(json);
        auto next=QJsonDocument::fromJson(QByteArray::fromStdString(json)).object();
        if(next["kind"].toString()!=kind)return false;
        // A complete state replacement keeps the ordinary control observers,
        // debounced worker revision and eventual Apply/history transaction.
        settings=std::move(next);
        {QScopedValueRollback<bool> loading(loadingSettings_,true);for(const auto& reload:reloadControls)reload();QSignalBlocker block(previewEnabled);previewEnabled.setChecked(previewState);}
        changed();return true;
    }
    bool samplePress(Point point,double viewX,Qt::KeyboardModifiers)override{
        if(closed_||committing_)return false;
        if((levelsControls&&levelsControls->sampleMode())||(hueControls&&hueControls->sampleMode())){if(preview.onClick)preview.onClick(point);dialog.activateWindow();return true;}
        if(hueControls&&hueControls->targeting()&&preview.onBeginDrag&&preview.onBeginDrag(point)){targetDragging_=true;targetAnchor_=viewX;return true;}return false;
    }
    bool sampleMove(Point,double viewX,Qt::KeyboardModifiers modifiers,bool finish)override{
        if(!targetDragging_)return false;if(preview.onDrag)preview.onDrag(viewX-targetAnchor_,modifiers.testFlag(Qt::ControlModifier));if(finish){targetDragging_=false;if(preview.onEndDrag)preview.onEndDrag();}return true;
    }
};
}
ui::EditPanelSession* openAdjustmentPanel(QWidget* parent,const Document& original,const std::string& active,const QString& kind,bool live,bool existing,const AdjustmentDialogOptions& options,ui::EditPanelHost host){
    return new AdjustmentPanel(parent,original,active,kind,live,existing,options,std::move(host),false);
}
std::optional<AdjustmentDialogResult> showAdjustmentDialog(QWidget* parent,const Document& original,const std::string& active,const QString& kind,bool live,bool existing,const AdjustmentDialogOptions& options){
    QEventLoop loop;std::optional<AdjustmentDialogResult> result;ui::EditPanelHost host;
    host.commit=[&](ui::EditPanelResult value){result=AdjustmentDialogResult{std::move(value.document),std::move(value.active)};};host.closed=[&]{loop.quit();};
    new AdjustmentPanel(parent,original,active,kind,live,existing,options,std::move(host),true);loop.exec();return result;
}
}
