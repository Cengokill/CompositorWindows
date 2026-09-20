#include "AdjustmentDialog.h"
#include "AdjustmentAdvancedControls.h"
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
struct HistogramResult {std::shared_ptr<const Raster> sample;Transform transform;effects_tools::LevelsHistogram bins{};QString error;};
struct PreviewResult { std::optional<AdjustmentDialogResult> value; QImage image; QString error; };
PreviewResult makePreview(Document doc,std::string active,const std::string&json,bool live,bool existing,const std::string&newId,bool showPreview){
    PreviewResult result;const Document before=doc;
    try{
        auto found=std::find_if(doc.layers.begin(),doc.layers.end(),[&](const Layer&l){return l.id==active;});
        if(live){
            if(existing){if(found==doc.layers.end()||found->adjustmentJson.empty())throw std::runtime_error("Adjustment layer no longer exists");found->adjustmentJson=json;}
            else {Layer layer;layer.id=newId;layer.name=QJsonDocument::fromJson(QByteArray::fromStdString(json)).object()["kind"].toString().toStdString();layer.adjustmentJson=json;layer.transform={0,0,double(doc.width),double(doc.height)};
                if(found!=doc.layers.end())layer.parentId=found->group?found->id:found->parentId;
                doc.layers.insert(found==doc.layers.end()?doc.layers.end():found+1,layer);active=layer.id;}
        }else{
            if(found==doc.layers.end()||!found->raster)throw std::runtime_error("Select an image layer");
            auto selection=editing::mappedCoverage(doc,found->transform,found->raster->width,found->raster->height);
            auto changed=effects::applyAdjustment(found->raster,json,selection.get());if(changed!=found->raster){found->raster=changed;found->shapeJson.clear();}
        }
        validateDocument(doc);
        auto rendered=SoftwareRenderer().render(showPreview?doc:before,0,0,doc.width,doc.height);auto bytes=rendered->rgba();
        result.image=QImage(bytes.data(),doc.width,doc.height,doc.width*4,QImage::Format_RGBA8888_Premultiplied).scaled(600,460,Qt::KeepAspectRatio,Qt::SmoothTransformation);
        result.value=AdjustmentDialogResult{std::move(doc),std::move(active)};
    }catch(const std::exception&e){result.error=e.what();}
    return result;
}
}
std::optional<AdjustmentDialogResult> showAdjustmentDialog(QWidget*parent,const Document&original,const std::string&active,const QString&requestedKind,bool live,bool existing,const AdjustmentDialogOptions& options){
    Object settings;
    if(existing){auto it=std::find_if(original.layers.begin(),original.layers.end(),[&](const Layer&l){return l.id==active;});if(it==original.layers.end())return {};settings=QJsonDocument::fromJson(QByteArray::fromStdString(it->adjustmentJson)).object();}
    else settings=QJsonDocument::fromJson(QByteArray::fromStdString(!live&&!options.initialAdjustmentJson.empty()?options.initialAdjustmentJson:effects::defaultAdjustmentJson(requestedKind.toStdString()))).object();
    const QString kind=settings["kind"].toString();
    if(kind=="Exposure"&&!settings.contains("exposureSettings"))settings["exposureSettings"]=Object{{"exposure",0},{"offset",0},{"gamma",1}};
    if(kind=="Gradient Map"&&!settings.contains("gradientMapSettings"))settings["gradientMapSettings"]=Object{{"shadows",Object{{"red",0},{"green",0},{"blue",0}}},{"highlights",Object{{"red",1},{"green",1},{"blue",1}}},{"reversed",false}};
    if(kind=="Grain"&&!settings.contains("grainSettings"))settings["grainSettings"]=Object{{"amount",25},{"size",1.5},{"roughness",50},{"seed",0}};
    QDialog dialog(parent);dialog.setObjectName("adjustmentDialog");dialog.setWindowTitle(kind);dialog.resize(840,560);
    QHBoxLayout layout(&dialog);QWidget fields;QFormLayout form(&fields);layout.addWidget(&fields);
    QWidget right;QVBoxLayout rightLayout(&right);AdjustmentPreview preview;preview.documentWidth=original.width;preview.documentHeight=original.height;preview.setMinimumSize(400,300);preview.setAlignment(Qt::AlignCenter);rightLayout.addWidget(&preview,1);QLabel status;status.setWordWrap(true);rightLayout.addWidget(&status);
    QCheckBox previewEnabled("Preview");previewEnabled.setObjectName("adjustmentPreviewEnabled");previewEnabled.setChecked(true);rightLayout.addWidget(&previewEnabled);
    QDialogButtonBox buttons(QDialogButtonBox::Apply|QDialogButtonBox::Cancel);rightLayout.addWidget(&buttons);layout.addWidget(&right,1);
    auto*apply=buttons.button(QDialogButtonBox::Apply);apply->setEnabled(false);
    QTimer debounce;debounce.setSingleShot(true);debounce.setInterval(100);
    QFutureWatcher<PreviewResult> watcher;QFutureWatcher<HistogramResult> histogramWatcher;std::optional<AdjustmentDialogResult> completed;uint64_t revision=0,runningRevision=0;bool pending=false,closing=false;
    const auto insertedId=newId();
    auto changed=[&]{++revision;apply->setEnabled(false);status.setText("Updating preview…");debounce.start();};
    QObject::connect(&previewEnabled,&QCheckBox::toggled,&dialog,changed);
    auto number=[&](const QString&label,double value,double lo,double hi,int decimals,std::function<void(double)>setter){
        auto*spin=new QDoubleSpinBox;spin->setRange(lo,hi);spin->setDecimals(decimals);spin->setValue(value);spin->setAccessibleName(label);form.addRow(label,spin);
        QObject::connect(spin,&QDoubleSpinBox::valueChanged,&dialog,[&,setter](double v){setter(v);changed();});return spin;};
    auto flag=[&](const QString&label,bool value,std::function<void(bool)>setter){auto*box=new QCheckBox(label);box->setChecked(value);form.addRow(box);QObject::connect(box,&QCheckBox::toggled,&dialog,[&,setter](bool v){setter(v);changed();});return box;};
    auto property=[&](const QString&group,const QString&key,const QString&label,double fallback,double lo,double hi,int decimals){return number(label,settings[group].toObject().value(key).toDouble(fallback),lo,hi,decimals,[&,group,key](double v){auto o=settings[group].toObject();o[key]=v;settings[group]=o;});};
    if(kind=="Exposure"){
        property("exposureSettings","exposure","Exposure",0,-20,20,2);property("exposureSettings","offset","Offset",0,-.5,.5,4);property("exposureSettings","gamma","Gamma",1,.01,9.99,2);
    }else if(kind=="Grain"){
        property("grainSettings","amount","Amount",25,0,100,1);property("grainSettings","size","Size",1.5,.5,20,2);property("grainSettings","roughness","Roughness",50,0,100,1);
    }else if(kind=="Gradient Map"){
        for(const QString key:{"shadows","highlights"}){auto*button=new QPushButton;auto update=[&,button,key]{auto c=settings["gradientMapSettings"].toObject()[key].toObject();button->setText(QColor::fromRgbF(c["red"].toDouble(key=="highlights"),c["green"].toDouble(key=="highlights"),c["blue"].toDouble(key=="highlights")).name());};update();form.addRow(key=="shadows"?"Shadows":"Highlights",button);
            QObject::connect(button,&QPushButton::clicked,&dialog,[&,key,update]{auto o=settings["gradientMapSettings"].toObject();auto c=o[key].toObject();auto color=QColorDialog::getColor(QColor::fromRgbF(c["red"].toDouble(),c["green"].toDouble(),c["blue"].toDouble()),&dialog,key);if(color.isValid()){o[key]=Object{{"red",color.redF()},{"green",color.greenF()},{"blue",color.blueF()}};settings["gradientMapSettings"]=o;update();changed();}});}
        flag("Reverse",settings["gradientMapSettings"].toObject()["reversed"].toBool(),[&](bool v){auto o=settings["gradientMapSettings"].toObject();o["reversed"]=v;settings["gradientMapSettings"]=o;});
    }else if(kind=="Levels"){
        auto* channel=new QComboBox;channel->setObjectName("levelsChannel");channel->addItems({"RGB","Red","Green","Blue"});form.addRow("Channel",channel);
        auto* controls=new LevelsAdvancedControls;controls->setAdjustmentJson(encoded(settings));form.addRow(controls);
        std::array<QDoubleSpinBox*,5> values{};const QStringList labels{"Input black","Gamma","Input white","Output black","Output white"};
        for(int i=0;i<5;++i){values[size_t(i)]=number(labels[i],i==1?1:i>=2?255:0,i==1?.1:0,i==1?9.99:255,i==1?2:1,[&,controls,i](double value){auto typed=effects_tools::levelsFromAdjustmentJson(encoded(settings));auto& range=typed.ranges[size_t(typed.channel)];double* fields[]{&range.black,&range.gamma,&range.white,&range.outputBlack,&range.outputWhite};*fields[i]=value;settings=QJsonDocument::fromJson(QByteArray::fromStdString(effects_tools::withLevelsSettings(encoded(settings),typed))).object();controls->setAdjustmentJson(encoded(settings));});values[size_t(i)]->setObjectName(QString("levelsValue%1").arg(i));}
        auto load=[&,channel,values,controls]{auto typed=effects_tools::levelsFromAdjustmentJson(encoded(settings));QSignalBlocker block(channel);channel->setCurrentIndex(int(typed.channel));auto range=typed.ranges[size_t(typed.channel)].normalized();const double inputs[]{range.black,range.gamma,range.white,range.outputBlack,range.outputWhite};for(int i=0;i<5;++i){QSignalBlocker spin(values[size_t(i)]);values[size_t(i)]->setValue(inputs[i]);}controls->setAdjustmentJson(encoded(settings));};load();
        QObject::connect(channel,&QComboBox::currentIndexChanged,&dialog,[&,channel,load]{auto typed=effects_tools::levelsFromAdjustmentJson(encoded(settings));typed.channel=effects_tools::LevelsChannel(channel->currentIndex());settings=QJsonDocument::fromJson(QByteArray::fromStdString(effects_tools::withLevelsSettings(encoded(settings),typed))).object();load();changed();});
        for(auto* spin:values)QObject::connect(spin,&QDoubleSpinBox::valueChanged,&dialog,[load]{load();});
        controls->onChanged=[&,load](const std::string& json){settings=QJsonDocument::fromJson(QByteArray::fromStdString(json)).object();load();changed();};
        auto* reset=new QPushButton("Reset Levels");reset->setObjectName("levelsReset");form.addRow(reset);QObject::connect(reset,&QPushButton::clicked,&dialog,[&,load]{settings=QJsonDocument::fromJson(QByteArray::fromStdString(effects_tools::withLevelsSettings(encoded(settings),{}))).object();load();changed();});
        auto sample=std::make_shared<HistogramResult>();
        controls->onSampleModeChanged=[&](auto mode){preview.setCursor(mode?Qt::CrossCursor:Qt::ArrowCursor);};
        preview.onClick=[&,controls,sample](Point point){if(sample->sample&&controls->sampleMode())if(auto rgb=effects_tools::sampleOriginalRGB(*sample->sample,sample->transform,point,original.width,original.height))controls->applySample(*rgb);};
        QObject::connect(&histogramWatcher,&QFutureWatcher<HistogramResult>::finished,&dialog,[&,controls,sample]{if(closing)return;*sample=histogramWatcher.result();if(!sample->error.isEmpty()){status.setText(sample->error);return;}controls->setHistogram(sample->bins);});
        histogramWatcher.setFuture(QtConcurrent::run([original,active,live,existing,insertedId]{HistogramResult result;try{result.transform={0,0,double(original.width),double(original.height)};auto found=std::find_if(original.layers.begin(),original.layers.end(),[&](const Layer& layer){return layer.id==active;});if(!live){if(found==original.layers.end()||!found->raster)throw std::runtime_error("Select an image layer");result.sample=found->raster;result.transform=found->transform;}else result.sample=SoftwareRenderer().render(levelsInput(original,active,existing,insertedId),0,0,original.width,original.height);auto selection=live?std::shared_ptr<const GrayRaster>{}:editing::mappedCoverage(original,result.transform,result.sample->width,result.sample->height);result.bins=effects_tools::levelsHistogram(*result.sample,selection.get());}catch(const std::exception& error){result.error=error.what();}return result;}));
    }else if(kind=="Curves"){
        auto* channel=new QComboBox;channel->setObjectName("curvesChannel");channel->addItems({"RGB","Red","Green","Blue"});channel->setCurrentIndex(int(effects_tools::curvesFromAdjustmentJson(encoded(settings)).channel));form.addRow("Channel",channel);
        auto* controls=new CurvesAdvancedControls;controls->setAdjustmentJson(encoded(settings));form.addRow(controls);
        controls->onChanged=[&](const std::string& json){settings=QJsonDocument::fromJson(QByteArray::fromStdString(json)).object();changed();};
        QObject::connect(channel,&QComboBox::currentIndexChanged,&dialog,[&,channel,controls]{auto typed=effects_tools::curvesFromAdjustmentJson(encoded(settings));typed.channel=effects_tools::LevelsChannel(channel->currentIndex());settings=QJsonDocument::fromJson(QByteArray::fromStdString(effects_tools::withCurvesSettings(encoded(settings),typed))).object();controls->setAdjustmentJson(encoded(settings));changed();});
    }else if(kind=="Hue/Saturation"){
        settings=QJsonDocument::fromJson(QByteArray::fromStdString(effects_tools::withHueSettings(encoded(settings),effects_tools::hueFromAdjustmentJson(encoded(settings))))).object();
        auto* range=new QComboBox;range->setObjectName("hueRange");range->addItems({"Master","Reds","Yellows","Greens","Cyans","Blues","Magentas"});form.addRow("Range",range);
        auto* controls=new HueAdvancedControls;controls->setAdjustmentJson(encoded(settings));
        std::array<QDoubleSpinBox*,3> spins{};const QStringList labels{"Hue","Saturation","Lightness"};
        for(int i=0;i<3;++i){spins[size_t(i)]=number(labels[i],0,i==0?-180:-100,i==0?180:100,1,[&,controls,i](double value){auto typed=effects_tools::hueFromAdjustmentJson(encoded(settings));auto& adjustment=typed.adjustments[size_t(typed.range)];if(i==0)adjustment.hue=value;else if(i==1)adjustment.saturation=value;else adjustment.lightness=value;settings=QJsonDocument::fromJson(QByteArray::fromStdString(effects_tools::withHueSettings(encoded(settings),typed))).object();controls->setAdjustmentJson(encoded(settings));});spins[size_t(i)]->setObjectName(QString("hueValue%1").arg(i));}
        form.addRow(controls);auto* outside=new QCheckBox("Apply outside this range instead");outside->setObjectName("hueInvertRange");form.addRow(outside);auto* colorize=new QCheckBox("Colorize");colorize->setObjectName("hueColorize");form.addRow(colorize);
        auto load=[&,range,controls,spins,outside,colorize]{auto typed=effects_tools::hueFromAdjustmentJson(encoded(settings));QSignalBlocker rangeBlock(range),outsideBlock(outside),colorizeBlock(colorize);range->setCurrentIndex(int(typed.range));range->setEnabled(!typed.colorize);outside->setChecked(typed.invertRange);outside->setVisible(typed.range!=effects_tools::ColorRange::Master&&!typed.colorize);colorize->setChecked(typed.colorize);auto value=typed.adjustments[size_t(typed.range)];double inputs[]{value.hue,value.saturation,value.lightness};for(int i=0;i<3;++i){QSignalBlocker spin(spins[size_t(i)]);spins[size_t(i)]->setRange(i==0?(typed.colorize?0:-180):i==1&&typed.colorize?0:-100,i==0?(typed.colorize?360:180):100);spins[size_t(i)]->setValue(inputs[i]);}controls->setAdjustmentJson(encoded(settings));};load();
        auto store=[&,load](const effects_tools::HueSettings& typed){settings=QJsonDocument::fromJson(QByteArray::fromStdString(effects_tools::withHueSettings(encoded(settings),typed))).object();load();changed();};
        QObject::connect(range,&QComboBox::currentIndexChanged,&dialog,[&,range,store]{auto typed=effects_tools::hueFromAdjustmentJson(encoded(settings));typed.range=effects_tools::ColorRange(range->currentIndex());store(typed);});
        QObject::connect(outside,&QCheckBox::toggled,&dialog,[&,store](bool value){auto typed=effects_tools::hueFromAdjustmentJson(encoded(settings));typed.invertRange=value;store(typed);});
        QObject::connect(colorize,&QCheckBox::toggled,&dialog,[store](bool value){store(value?effects_tools::HueSettings::colorizeStart():effects_tools::HueSettings{});});
        auto* reset=new QPushButton("Reset Hue/Saturation");reset->setObjectName("hueReset");form.addRow(reset);QObject::connect(reset,&QPushButton::clicked,&dialog,[&,store]{store(effects_tools::hueFromAdjustmentJson(encoded(settings)).colorize?effects_tools::HueSettings::colorizeStart():effects_tools::HueSettings{});});
        controls->onChanged=[&,load](const std::string& json){settings=QJsonDocument::fromJson(QByteArray::fromStdString(json)).object();load();changed();};
        auto cursor=[&,controls]{preview.setCursor(controls->sampleMode()||controls->targeting()?Qt::CrossCursor:Qt::ArrowCursor);};controls->onSampleModeChanged=[cursor](auto){cursor();};controls->onTargetingChanged=[cursor](bool){cursor();};
        auto colorAt=[&](Point point)->std::optional<effects_tools::PaletteColor>{try{return effects_tools::sampleCompositeColor(previewEnabled.isChecked()&&completed?completed->document:original,point,SoftwareRenderer());}catch(const std::exception& error){status.setText(error.what());return {};}};
        preview.onClick=[controls,colorAt](Point point){if(controls->sampleMode())if(auto color=colorAt(point))controls->applySample(*color);};
        preview.onBeginDrag=[controls,colorAt](Point point){if(!controls->targeting())return false;if(auto color=colorAt(point))return controls->beginTarget(*color);return false;};
        preview.onDrag=[controls](double delta,bool hue){controls->dragTarget(delta,hue);};preview.onEndDrag=[controls]{controls->endTarget();};
    }
    std::function<void()> start=[&]{if(closing)return;if(watcher.isRunning()){pending=true;return;}pending=false;runningRevision=revision;const auto json=encoded(settings);const bool showPreview=previewEnabled.isChecked();status.setText("Updating preview...");watcher.setFuture(QtConcurrent::run([original,active,json,live,existing,insertedId,showPreview]{return makePreview(original,active,json,live,existing,insertedId,showPreview);}));};
    QObject::connect(&debounce,&QTimer::timeout,&dialog,start);
    QObject::connect(&watcher,&QFutureWatcher<PreviewResult>::finished,&dialog,[&]{if(closing)return;auto result=watcher.result();if(pending||runningRevision!=revision){start();return;}if(!result.error.isEmpty()){status.setText(result.error);apply->setEnabled(false);return;}completed=std::move(result.value);preview.setPixmap(QPixmap::fromImage(result.image));status.setText(previewEnabled.isChecked()?"Preview":"Original image  -  preview off");apply->setEnabled(true);});
    QObject::connect(apply,&QPushButton::clicked,&dialog,[&]{
        if(!live){
            const auto exposure=settings["exposureSettings"].toObject(),grain=settings["grainSettings"].toObject();
            if((kind=="Exposure"&&exposure.value("exposure").toDouble()==0&&exposure.value("offset").toDouble()==0&&exposure.value("gamma").toDouble(1)==1)||(kind=="Grain"&&grain.value("amount").toDouble(25)==0)){dialog.reject();return;}
            if(options.onApply)options.onApply(encoded(settings));
        }
        dialog.accept();
    });QObject::connect(&buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);start();
    int answer=dialog.exec();closing=true;debounce.stop();watcher.waitForFinished();histogramWatcher.waitForFinished();
    if(answer!=QDialog::Accepted)return {};return completed;
}
}
