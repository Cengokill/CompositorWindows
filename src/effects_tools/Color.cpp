// ColorPalette.swift and SampleRingOverlay.swift at the pinned revision.
// Copyright (c) 2026 Wonder Assembly LLC; MIT notice in graphics/upstream/LICENSE.
#include "Color.h"
#include <QString>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
namespace compositor::effects_tools {
namespace {void check(PaletteColor color){for(double value:{color.red,color.green,color.blue})if(!std::isfinite(value)||value<0||value>1)throw std::runtime_error("Invalid palette RGB");}}
PaletteColor PaletteColor::quantized()const{check(*this);return {std::round(red*255)/255,std::round(green*255)/255,std::round(blue*255)/255};}
std::string PaletteColor::hex()const{auto c=quantized();char text[7];std::snprintf(text,sizeof(text),"%02X%02X%02X",unsigned(std::round(c.red*255)),unsigned(std::round(c.green*255)),unsigned(std::round(c.blue*255)));return text;}
std::optional<PaletteColor> PaletteColor::fromHex(std::string_view hex){if(hex.size()>4096)return {};auto text=QString::fromUtf8(hex.data(),qsizetype(hex.size()));auto space=[](QChar ch){return ch=='\t'||ch.category()==QChar::Separator_Space;};while(!text.isEmpty()&&space(text.front()))text.remove(0,1);while(!text.isEmpty()&&space(text.back()))text.chop(1);if(text.startsWith('#'))text.remove(0,1);if(text.size()==3){QString expanded;for(QChar c:text){expanded+=c;expanded+=c;}text=expanded;}if(text.size()!=6)return {};bool okay=false;auto value=text.toUInt(&okay,16);if(!okay)return {};return PaletteColor{double((value>>16)&255)/255,double((value>>8)&255)/255,double(value&255)/255};}
PaletteColor PickerHSB::rgb()const{if(!std::isfinite(hue)||!std::isfinite(saturation)||!std::isfinite(brightness)||saturation<0||saturation>1||brightness<0||brightness>1)throw std::runtime_error("Invalid picker HSB");double h=std::fmod(std::fmod(hue,360.)+360.,360.)/60,c=brightness*saturation,x=c*(1-std::abs(std::fmod(h,2.)-1)),m=brightness-c;PaletteColor color;switch(int(h)){case 0:color={c,x,0};break;case 1:color={x,c,0};break;case 2:color={0,c,x};break;case 3:color={0,x,c};break;case 4:color={x,0,c};break;default:color={c,0,x};}return {color.red+m,color.green+m,color.blue+m};}
void PickerHSB::setRGB(PaletteColor color){check(color);double high=std::max({color.red,color.green,color.blue}),low=std::min({color.red,color.green,color.blue}),delta=high-low;brightness=high;if(high>0)saturation=delta/high;if(delta<=0)return;double h=high==color.red?(color.green-color.blue)/delta:high==color.green?(color.blue-color.red)/delta+2:(color.red-color.green)/delta+4;h*=60;hue=h<0?h+360:h;}
std::optional<PaletteColor> sampleCompositeColor(const Document& doc,Point point,const IRasterBackend& backend){if(!std::isfinite(point.x)||!std::isfinite(point.y)||point.x<0||point.y<0||point.x>=doc.width||point.y>=doc.height)return {};auto raster=backend.render(doc,int(std::floor(point.x)),int(std::floor(point.y)),1,1);if(!raster||raster->width!=1||raster->height!=1)throw std::runtime_error("Invalid sampled composite");auto pixel=raster->pixel(0,0);if(!pixel.a)return {};auto c=[&](uint8_t value){return std::round(std::min(double(pixel.a),double(value))/pixel.a*255)/255;};return PaletteColor{c(pixel.r),c(pixel.g),c(pixel.b)};}
SampleRingGeometry sampleRingGeometry(Point p){if(!std::isfinite(p.x)||!std::isfinite(p.y))throw std::runtime_error("Invalid sample ring position");return {{p.x-58,p.y-58}};}
}
