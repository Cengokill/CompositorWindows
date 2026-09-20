// Levels.swift, LevelsAutomatic.swift and LevelsSheet.swift at the pinned source.
// Copyright (c) 2026 Wonder Assembly LLC; MIT notice in graphics/upstream/LICENSE.
#include "Levels.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <algorithm>
#include <cmath>
#include <stdexcept>
extern "C" {
#include "graphics/upstream/LevelsPixels.h"
}
namespace compositor::effects_tools {
namespace {
constexpr const char* channelNames[]{"RGB","Red","Green","Blue"};
int channelIndex(LevelsChannel channel){int index=int(channel);if(index<0||index>3)throw std::runtime_error("Invalid Levels channel");return index;}
double clamp(double value,double lo,double hi,double fallback){return std::isfinite(value)?std::clamp(value,lo,hi):fallback;}
QJsonObject parse(std::string_view text){if(text.size()>4*1024*1024)throw std::runtime_error("Adjustment JSON exceeds budget");QJsonParseError error;auto doc=QJsonDocument::fromJson(QByteArray(text.data(),qsizetype(text.size())),&error);if(error.error!=QJsonParseError::NoError||!doc.isObject())throw std::runtime_error("Invalid adjustment JSON");return doc.object();}
void rasterCheck(const Raster& raster){if(raster.width<1||raster.height<1||raster.width>30000||raster.height>30000||uint64_t(raster.width)*raster.height>100000000||raster.tiles.size()!=size_t((raster.width+255)/256)*size_t((raster.height+255)/256)||std::any_of(raster.tiles.begin(),raster.tiles.end(),[](const auto& tile){return !tile;}))throw std::runtime_error("Invalid Levels source raster");}
}
LevelRange LevelRange::normalized()const{auto result=*this;result.black=clamp(black,0,254,0);result.white=clamp(white,result.black+1,255,255);result.gamma=clamp(gamma,.1,9.99,1);result.outputBlack=clamp(outputBlack,0,255,0);result.outputWhite=clamp(outputWhite,0,255,255);return result;}
double LevelRange::apply(double value)const{auto s=normalized();double input=std::clamp((value*255-s.black)/(s.white-s.black),0.,1.);return (s.outputBlack+std::pow(input,1/s.gamma)*(s.outputWhite-s.outputBlack))/255;}
bool LevelsSettings::identity()const{return std::all_of(ranges.begin(),ranges.end(),[](auto range){return range.normalized()==LevelRange{};});}
double LevelsSettings::apply(double value,LevelsChannel c)const{return ranges[0].apply(ranges[size_t(channelIndex(c))].apply(value));}
LevelsHistogram levelsHistogram(const Raster& raster,const GrayRaster* selection){
    rasterCheck(raster);if(selection&&(selection->width!=raster.width||selection->height!=raster.height||selection->pixels.size()!=size_t(raster.width)*raster.height))throw std::runtime_error("Levels histogram selection must match source grid");
    LevelsHistogram result{};static_assert(sizeof(LevelsHistogram)==1024*sizeof(double));int columns=(raster.width+255)/256;
    for(size_t tile=0;tile<raster.tiles.size();++tile){int x=int(tile%columns)*256,y=int(tile/columns)*256,w=std::min(256,raster.width-x),h=std::min(256,raster.height-y);for(int row=0;row<h;++row){auto pixels=reinterpret_cast<const uint8_t*>(raster.tiles[tile]->pixels.data()+size_t(row)*256);const uint8_t* coverage=selection?selection->pixels.data()+size_t(y+row)*raster.width+x:nullptr;levels_histogram(pixels,coverage,size_t(w),result[0].data());}}
    return result;
}
double histogramDisplayScale(std::span<const double> bins){double peak=0;for(double value:bins)if(std::isfinite(value)&&value>0)peak=std::max(peak,value);if(peak<=0)return 0;std::vector<double> interior;if(bins.size()>2)for(size_t i=1;i+1<bins.size();++i)if(std::isfinite(bins[i])&&bins[i]>0)interior.push_back(bins[i]);if(interior.empty())return peak;std::sort(interior.begin(),interior.end());return std::min(peak,interior[size_t(double(interior.size()-1)*.95)]*4);}
LevelsSettings autoLevels(const LevelsHistogram& histogram,LevelsAuto mode){
    if(mode!=LevelsAuto::Contrast&&mode!=LevelsAuto::Color&&mode!=LevelsAuto::Neutral)throw std::runtime_error("Invalid Auto Levels mode");for(const auto& bins:histogram)for(double value:bins)if(!std::isfinite(value)||value<0)throw std::runtime_error("Invalid histogram weight");
    auto endpoints=[](const std::array<double,256>& bins)->std::optional<std::array<double,2>>{double total=0;for(double value:bins)total+=value;if(total<=0)return {};double sum=0;int low=0,high=255;for(int i=0;i<256;++i){sum+=bins[size_t(i)];if(sum>total*.001){low=i;break;}}sum=0;for(int i=255;i>=0;--i){sum+=bins[size_t(i)];if(sum>total*.001){high=i;break;}}if(low>=high)return {};return std::array<double,2>{double(low),double(high)};};
    LevelsSettings result;if(mode==LevelsAuto::Contrast){double low=255,high=0;for(int c=1;c<4;++c)if(auto limits=endpoints(histogram[size_t(c)])){low=std::min(low,(*limits)[0]);high=std::max(high,(*limits)[1]);}if(low<high){result.ranges[0].black=low;result.ranges[0].white=high;}}
    else for(int c=1;c<4;++c){const auto& bins=histogram[size_t(c)];auto limits=endpoints(bins);if(!limits)continue;auto& range=result.ranges[size_t(c)];range.black=(*limits)[0];range.white=(*limits)[1];if(mode==LevelsAuto::Neutral){double total=0,mean=0;for(int i=0;i<256;++i){total+=bins[size_t(i)];mean+=range.apply(double(i)/255)*bins[size_t(i)];}mean/=total;if(mean>0&&mean<1)range.gamma=std::clamp(std::log(mean)/std::log(.5),.1,9.99);}}
    return result;
}
LevelsSettings sampleLevels(const LevelsSettings& current,std::array<double,3> rgb,LevelsSample mode){
    channelIndex(current.channel);if(mode!=LevelsSample::Black&&mode!=LevelsSample::Gray&&mode!=LevelsSample::White)throw std::runtime_error("Invalid Levels sample mode");for(double c:rgb)if(!std::isfinite(c)||c<0||c>1)throw std::runtime_error("Invalid straight RGB sample");auto result=current;result.ranges[0]={};
    for(int c=1;c<4;++c){auto range=result.ranges[size_t(c)];double value=rgb[size_t(c-1)]*255;if(mode==LevelsSample::Black)range.black=std::min(range.white-1,std::max(0.,value));else if(mode==LevelsSample::White)range.white=std::max(range.black+1,std::min(255.,value));else{double fraction=(value-range.black)/(range.white-range.black);if(fraction<=0||fraction>=1)continue;range.gamma=std::log(fraction)/std::log(.5);}range.outputBlack=0;range.outputWhite=255;result.ranges[size_t(c)]=range.normalized();}return result;
}
std::optional<std::array<double,3>> sampleOriginalRGB(const Raster& raster,const Transform& transform,Point point,int w,int h){
    rasterCheck(raster);if(!transform.valid()||w<1||h<1)throw std::runtime_error("Invalid Levels sampling geometry");if(!std::isfinite(point.x)||!std::isfinite(point.y)||point.x<0||point.y<0||point.x>=w||point.y>=h)return {};auto unit=transform.toUnit(point);if(unit.x<0||unit.y<0||unit.x>=1||unit.y>=1)return {};auto pixel=raster.pixel(int(std::floor(unit.x*raster.width)),int(std::floor(unit.y*raster.height)));if(!pixel.a)return {};return std::array<double,3>{std::min(1.,double(pixel.r)/pixel.a),std::min(1.,double(pixel.g)/pixel.a),std::min(1.,double(pixel.b)/pixel.a)};
}
std::array<double,3> levelsInputHandles(LevelRange range){range=range.normalized();return {range.black,range.black+(range.white-range.black)*std::pow(.5,range.gamma),range.white};}
LevelRange moveLevelsHandle(LevelRange range,int index,bool output,double position){if(!std::isfinite(position)||index<0||index>(output?1:2))throw std::runtime_error("Invalid Levels handle");double x=std::clamp(position,0.,255.);if(output){if(index==0)range.outputBlack=std::round(x);else range.outputWhite=std::round(x);}else if(index==0)range.black=std::min(range.white-1,std::round(x));else if(index==2)range.white=std::max(range.black+1,std::round(x));else{double fraction=std::clamp((x-range.black)/(range.white-range.black),.001,.999);range.gamma=std::log(fraction)/std::log(.5);}return range.normalized();}
LevelsSettings levelsFromAdjustmentJson(std::string_view json){auto root=parse(json);if(!root["levels"].isObject())throw std::runtime_error("Missing Levels settings");auto levels=root["levels"].toObject();LevelsSettings result;bool found=false;for(int c=0;c<4;++c)if(levels["channel"].toString()==channelNames[c]){result.channel=LevelsChannel(c);found=true;}if(!found||!levels["ranges"].isArray())throw std::runtime_error("Invalid Levels settings");auto ranges=levels["ranges"].toArray();if(ranges.size()!=4)throw std::runtime_error("Levels requires four channels");for(int c=0;c<4;++c){if(!ranges[c].isObject())throw std::runtime_error("Invalid Levels range");auto object=ranges[c].toObject();auto number=[&](const char* key){auto value=object[QLatin1String(key)];if(!value.isDouble()||!std::isfinite(value.toDouble()))throw std::runtime_error("Missing or invalid Levels value");return value.toDouble();};result.ranges[size_t(c)]={number("black"),number("gamma"),number("white"),number("outputBlack"),number("outputWhite")};}return result;}
std::string withLevelsSettings(std::string_view json,const LevelsSettings& settings){auto root=parse(json);QJsonArray ranges;for(auto range:settings.ranges){range=range.normalized();ranges.append(QJsonObject{{"black",range.black},{"gamma",range.gamma},{"white",range.white},{"outputBlack",range.outputBlack},{"outputWhite",range.outputWhite}});}root["levels"]=QJsonObject{{"channel",QLatin1String(channelNames[channelIndex(settings.channel)])},{"ranges",ranges}};return QJsonDocument(root).toJson(QJsonDocument::Compact).toStdString();}
}
