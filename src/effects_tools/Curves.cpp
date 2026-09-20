// Curves.swift and CurvesControls.swift at the pinned source; MIT notice in
// graphics/upstream/LICENSE, copyright (c) 2026 Wonder Assembly LLC.
#include "Curves.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
namespace compositor::effects_tools {
namespace {
constexpr const char* names[]{"RGB","Red","Green","Blue"};
size_t index(LevelsChannel c){if(int(c)<0||int(c)>3)throw std::runtime_error("Invalid curve channel");return size_t(c);}
Point clamped(Point p){if(!std::isfinite(p.x)||!std::isfinite(p.y))throw std::runtime_error("Invalid curve control point");return {std::clamp(p.x,0.,255.),std::clamp(p.y,0.,255.)};}
QJsonObject parse(std::string_view text){if(text.size()>4*1024*1024)throw std::runtime_error("Adjustment JSON exceeds budget");QJsonParseError error;auto doc=QJsonDocument::fromJson(QByteArray(text.data(),qsizetype(text.size())),&error);if(error.error!=QJsonParseError::NoError||!doc.isObject())throw std::runtime_error("Invalid adjustment JSON");return doc.object();}
}
CurvesSettings::CurvesSettings(){for(auto& points:channels)points={{0,0},{255,255}};}
bool CurvesSettings::valid()const{return int(channel)>=0&&int(channel)<=3&&std::all_of(channels.begin(),channels.end(),[](const auto& p){return validCurve(p);});}
std::array<Point,256> curveGraph(const CurvesSettings& s){if(!s.valid())throw std::runtime_error("Invalid curve settings");std::array<Point,256> graph;for(int x=0;x<256;++x)graph[size_t(x)]={double(x),curveValueUnchecked(s.channels[index(s.channel)],x)};return graph;}
void moveCurvePoint(CurvesSettings& s,size_t i,Point value){if(!s.valid())throw std::runtime_error("Invalid curve settings");value=clamped(value);auto& p=s.channels[index(s.channel)];if(i>=p.size())throw std::runtime_error("Invalid curve point index");auto next=p;next[i].y=value.y;if(i>0&&i+1<p.size())next[i].x=std::min(p[i+1].x-1,std::max(p[i-1].x+1,value.x));if(!validCurve(next))throw std::runtime_error("Curve handles are too close for this drag");p=std::move(next);}
std::optional<size_t> beginCurveDrag(CurvesSettings& s,Point value){if(!s.valid())throw std::runtime_error("Invalid curve settings");value=clamped(value);auto& p=s.channels[index(s.channel)];size_t nearest=0;double best=INFINITY;for(size_t i=0;i<p.size();++i){double distance=std::hypot(p[i].x-value.x,p[i].y-value.y);if(distance<best){best=distance;nearest=i;}}std::optional<size_t> selected;if(best<14)selected=nearest;else if(p.size()<32&&value.x>1&&value.x<254&&std::all_of(p.begin(),p.end(),[&](Point point){return std::abs(point.x-value.x)>1;})){p.push_back(value);std::sort(p.begin(),p.end(),[](Point a,Point b){return a.x<b.x;});selected=size_t(std::find(p.begin(),p.end(),value)-p.begin());}if(selected)moveCurvePoint(s,*selected,value);return selected;}
bool removeCurvePoint(CurvesSettings& s,size_t i){if(!s.valid())throw std::runtime_error("Invalid curve settings");auto& p=s.channels[index(s.channel)];if(i==0||i+1>=p.size())return false;p.erase(p.begin()+ptrdiff_t(i));return true;}
void resetCurve(CurvesSettings& s){s.channels[index(s.channel)]={{0,0},{255,255}};}
CurvesSettings curvesFromAdjustmentJson(std::string_view json){auto root=parse(json);if(!root["curves"].isObject())throw std::runtime_error("Missing Curves settings");auto object=root["curves"].toObject();CurvesSettings result;bool named=false;for(int i=0;i<4;++i)if(object["channel"].toString()==names[i]){result.channel=LevelsChannel(i);named=true;}if(!named||!object["channels"].isArray())throw std::runtime_error("Invalid Curves channels");auto channels=object["channels"].toArray();if(channels.size()!=4)throw std::runtime_error("Curves requires four channels");for(int c=0;c<4;++c){if(!channels[c].isArray())throw std::runtime_error("Invalid curve points");auto points=channels[c].toArray();result.channels[size_t(c)].clear();for(auto point:points){if(!point.isObject())throw std::runtime_error("Invalid curve point");auto o=point.toObject();if(!o["x"].isDouble()||!o["y"].isDouble())throw std::runtime_error("Missing curve coordinates");result.channels[size_t(c)].push_back({o["x"].toDouble(),o["y"].toDouble()});}}if(!result.valid())throw std::runtime_error("Invalid curve settings");return result;}
std::string withCurvesSettings(std::string_view json,const CurvesSettings& s){if(!s.valid())throw std::runtime_error("Invalid curve settings");auto root=parse(json);QJsonArray channels;for(const auto& points:s.channels){QJsonArray list;for(auto point:points)list.append(QJsonObject{{"x",point.x},{"y",point.y}});channels.append(list);}root["curves"]=QJsonObject{{"channel",QLatin1String(names[index(s.channel)])},{"channels",channels}};return QJsonDocument(root).toJson(QJsonDocument::Compact).toStdString();}
}
