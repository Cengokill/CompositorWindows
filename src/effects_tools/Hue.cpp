// HueSaturation.swift and HueSaturationSheet.swift at the pinned revision.
// Copyright (c) 2026 Wonder Assembly LLC; MIT notice in graphics/upstream/LICENSE.
#include "Hue.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace compositor::effects_tools {
namespace {
constexpr const char* names[]{"Master","Reds","Yellows","Greens","Cyans","Blues","Magentas"};
int index(ColorRange range){int i=int(range);if(i<0||i>6)throw std::runtime_error("Invalid hue range");return i;}
double finite(double value){if(!std::isfinite(value))throw std::runtime_error("Invalid hue coordinate");return value;}
double wrap(double value){auto r=std::fmod(finite(value),360.);return r<0?r+360:r;}
QJsonObject parse(std::string_view text){if(text.size()>4*1024*1024)throw std::runtime_error("Adjustment JSON exceeds budget");QJsonParseError error;auto doc=QJsonDocument::fromJson(QByteArray(text.data(),qsizetype(text.size())),&error);if(error.error!=QJsonParseError::NoError||!doc.isObject())throw std::runtime_error("Invalid adjustment JSON");return doc.object();}
ColorRange rangeName(QJsonValue value){if(!value.isString())throw std::runtime_error("Invalid hue range name");for(int i=0;i<7;++i)if(value.toString()==names[i])return ColorRange(i);throw std::runtime_error("Unknown hue range");}
double number(QJsonValue value){if(!value.isDouble())throw std::runtime_error("Missing hue settings number");return finite(value.toDouble());}
bool boolean(QJsonValue value){if(!value.isBool())throw std::runtime_error("Missing hue settings boolean");return value.toBool();}
void validate(const HueSettings& s){index(s.range);for(const auto& change:s.adjustments)if(std::abs(finite(change.hue))>360||std::abs(finite(change.saturation))>100||std::abs(finite(change.lightness))>100)throw std::runtime_error("Hue adjustment exceeds source limits");for(const auto& band:s.bands)for(double handle:band.handles())finite(handle);}
}
HueBand defaultHueBand(ColorRange range){static constexpr HueBand bands[]={{0,0,360,360},{315,345,15,45},{15,45,75,105},{75,105,135,165},{135,165,195,225},{195,225,255,285},{255,285,315,345}};return bands[index(range)];}
double HueBand::forward(double from,double to){return wrap(to-from);}
double HueBand::weight(double hue)const{double span=forward(falloffStart,falloffEnd);if(span<=0)return 1;double position=forward(falloffStart,hue);if(position>span)return 0;double ramp=forward(falloffStart,rangeStart),plateau=forward(falloffStart,rangeEnd);if(position<ramp)return ramp>0?position/ramp:1;if(position<=plateau)return 1;double out=span-plateau;return out>0?(span-position)/out:1;}
HueBand HueBand::centered(double hue)const{double core=forward(rangeStart,rangeEnd),leading=forward(falloffStart,rangeStart),trailing=forward(rangeEnd,falloffEnd),start=wrap(hue-core/2);return {wrap(start-leading),start,wrap(start+core),wrap(start+core+trailing)};}
void HueBand::normalize(){falloffStart=wrap(falloffStart);rangeStart=wrap(rangeStart);rangeEnd=wrap(rangeEnd);falloffEnd=wrap(falloffEnd);if(forward(falloffStart,falloffEnd)>350)falloffEnd=wrap(falloffStart+350);}
void HueBand::include(double hue){finite(hue);if(weight(hue)>=1)return;double in=forward(falloffStart,rangeStart),out=forward(rangeEnd,falloffEnd);if(forward(hue,rangeStart)<=forward(rangeEnd,hue)){rangeStart=hue;falloffStart=hue-in;}else{rangeEnd=hue;falloffEnd=hue+out;}normalize();}
void HueBand::exclude(double hue){finite(hue);if(weight(hue)<=0)return;double in=forward(falloffStart,rangeStart),out=forward(rangeEnd,falloffEnd);if(forward(falloffStart,hue)<=forward(hue,falloffEnd)){falloffStart=hue+1;rangeStart=hue+1+in;}else{falloffEnd=hue-1;rangeEnd=hue-1-out;}normalize();}
void HueBand::setHandle(int i,double degrees){if(i<0||i>3)throw std::runtime_error("Invalid hue band handle");auto updated=*this;double value=wrap(degrees);if(i==0)updated.falloffStart=value;else if(i==1)updated.rangeStart=value;else if(i==2)updated.rangeEnd=value;else updated.falloffEnd=value;double span=forward(updated.falloffStart,updated.falloffEnd),start=forward(updated.falloffStart,updated.rangeStart),end=forward(updated.falloffStart,updated.rangeEnd);if(span>1&&span<=350&&start<=end&&end<=span)*this=updated;}
HueSettings::HueSettings(){for(int i=0;i<7;++i)bands[size_t(i)]=defaultHueBand(ColorRange(i));}
double HueSettings::weight(ColorRange r,double hue)const{int i=index(r);if(i==0)return 1;double w=bands[size_t(i)].weight(hue);return invertRange&&r==range?1-w:w;}
double HueSettings::shiftedHue(double hue)const{double shift=0;for(int i=0;i<7;++i)if(adjustments[size_t(i)].hue!=0)shift+=adjustments[size_t(i)].hue*weight(ColorRange(i),hue);return wrap(hue+shift);}
bool HueSettings::identity()const{return !colorize&&std::all_of(adjustments.begin(),adjustments.end(),[](auto a){return a==RangeAdjustment{};});}
HueSettings HueSettings::colorizeStart(){HueSettings result;result.colorize=true;result.adjustments[0].saturation=25;return result;}
std::optional<double> sampledHue(PaletteColor color){PickerHSB hsb;hsb.setRGB(color);if(hsb.saturation<=.02)return {};return hsb.hue;}
HueSettings sampleHueRange(const HueSettings& current,double hue,HueSample mode){validate(current);finite(hue);if(current.range==ColorRange::Master||current.colorize)return current;auto result=current;auto& band=result.bands[size_t(index(current.range))];if(mode==HueSample::Replace)band=band.centered(hue);else if(mode==HueSample::Add)band.include(hue);else if(mode==HueSample::Remove)band.exclude(hue);else throw std::runtime_error("Invalid hue sample mode");return result;}
std::optional<HueTargetDrag> beginHueTargeting(HueSettings& settings,PaletteColor color){validate(settings);if(settings.colorize)return {};auto hue=sampledHue(color);if(!hue)return {};ColorRange chosen=ColorRange::Reds;double best=settings.weight(chosen,*hue);for(int i=2;i<7;++i){double value=settings.weight(ColorRange(i),*hue);if(value>best){best=value;chosen=ColorRange(i);}}settings.range=chosen;auto a=settings.adjustments[size_t(index(chosen))];return HueTargetDrag{chosen,a.hue,a.saturation};}
HueSettings dragHueTargeting(const HueSettings& current,HueTargetDrag drag,double delta,bool adjustsHue){validate(current);finite(delta);auto result=current;auto& value=result.adjustments[size_t(index(drag.range))];if(adjustsHue)value.hue=std::clamp(drag.hue+delta/2,-180.,180.);else value.saturation=std::clamp(drag.saturation+delta/2,-100.,100.);return result;}
int nearestHueHandle(HueBand band,double degrees){finite(degrees);int nearest=0;double best=INFINITY;auto handles=band.handles();for(int i=0;i<4;++i){double gap=std::fmod(std::abs(handles[size_t(i)]-degrees),360.),distance=std::min(gap,360-gap);if(distance<best){best=distance;nearest=i;}}return nearest;}
HueSettings hueFromAdjustmentJson(std::string_view json){auto root=parse(json);HueSettings result;if(root["hsvSettings"].isUndefined()||root["hsvSettings"].isNull()){result.adjustments[0]={number(root["hue"]),number(root["saturation"]),number(root["lightness"])};result.colorize=boolean(root["colorize"]);validate(result);return result;}
    if(!root["hsvSettings"].isObject())throw std::runtime_error("Invalid hue settings");auto h=root["hsvSettings"].toObject();result.range=rangeName(h["range"]);result.colorize=boolean(h["colorize"]);result.invertRange=boolean(h["invertRange"]);
    auto dictionary=[&](const char* key,auto read){auto value=h[QLatin1String(key)];if(!value.isArray())throw std::runtime_error("Hue dictionary must use Swift enum-key array form");auto values=value.toArray();if(values.size()%2)throw std::runtime_error("Invalid hue dictionary");for(qsizetype i=0;i<values.size();i+=2){auto r=rangeName(values[i]);if(!values[i+1].isObject())throw std::runtime_error("Invalid hue dictionary value");read(index(r),values[i+1].toObject());}};
    dictionary("adjustments",[&](int i,QJsonObject a){result.adjustments[size_t(i)]={number(a["hue"]),number(a["saturation"]),number(a["lightness"])};});dictionary("bands",[&](int i,QJsonObject b){result.bands[size_t(i)]={number(b["falloffStart"]),number(b["rangeStart"]),number(b["rangeEnd"]),number(b["falloffEnd"])};});validate(result);return result;
}
std::string withHueSettings(std::string_view json,const HueSettings& s){validate(s);auto root=parse(json);QJsonArray adjustments,bands;for(int i=0;i<7;++i){const auto& a=s.adjustments[size_t(i)];const auto& b=s.bands[size_t(i)];adjustments.append(QLatin1String(names[i]));adjustments.append(QJsonObject{{"hue",a.hue},{"saturation",a.saturation},{"lightness",a.lightness}});bands.append(QLatin1String(names[i]));bands.append(QJsonObject{{"falloffStart",b.falloffStart},{"rangeStart",b.rangeStart},{"rangeEnd",b.rangeEnd},{"falloffEnd",b.falloffEnd}});}root["hsvSettings"]=QJsonObject{{"range",QLatin1String(names[index(s.range)])},{"colorize",s.colorize},{"invertRange",s.invertRange},{"adjustments",adjustments},{"bands",bands}};return QJsonDocument(root).toJson(QJsonDocument::Compact).toStdString();}
}
