// ShapeTool.swift at a19db9011282399785dc18efcfded904627bdcc2.
// Copyright (c) 2026 Wonder Assembly LLC; MIT notice: graphics/upstream/LICENSE.
#include "Shapes.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace compositor::editing {
namespace {
void validate(const ShapeStyle& s){if(s.kind!=ShapeKind::Rectangle&&s.kind!=ShapeKind::Ellipse&&s.kind!=ShapeKind::Line)throw std::runtime_error("Invalid shape kind");for(double c:{s.red,s.green,s.blue})if(!std::isfinite(c))throw std::runtime_error("Invalid shape color");if(!std::isfinite(s.cornerRadius))throw std::runtime_error("Invalid shape radius");if(s.kind==ShapeKind::Line&&(!(s.lineWidth>=.1)||s.lineWidth>500||s.startX<0||s.startX>1||s.startY<0||s.startY>1||s.endX<0||s.endX>1||s.endY<0||s.endY>1))throw std::runtime_error("Invalid line shape");}
uint8_t byte(double value){return uint8_t(std::clamp(std::lround(value),0L,255L));}
const char* kindName(ShapeKind kind){return kind==ShapeKind::Rectangle?"Rectangle":kind==ShapeKind::Ellipse?"Ellipse":"Line";}
Layer drawLayer(const Layer& source,const ShapeStyle& style,bool force){
    if(!source.raster||source.shapeJson.empty())throw std::runtime_error("Layer is no longer a live shape");if(!source.transform.valid())throw std::runtime_error("Invalid shape transform");
    int width=std::max(1,int(std::round(source.transform.width))),height=std::max(1,int(std::round(source.transform.height)));
    if(!force&&width==source.raster->width&&height==source.raster->height)return source;
    auto result=source;result.raster=shapeRaster(style,width,height);result.shapeJson=encodeShapeStyle(style);if(result.mask&&!result.mask->placement)result.mask->placement=source.transform;return result;
}
}
ShapeStyle decodeShapeStyle(std::string_view json){
    if(json.size()>65536)throw std::runtime_error("Shape style JSON exceeds budget");QJsonParseError error;auto doc=QJsonDocument::fromJson(QByteArray(json.data(),qsizetype(json.size())),&error);if(error.error!=QJsonParseError::NoError||!doc.isObject())throw std::runtime_error("Invalid shape style JSON");auto object=doc.object();auto kind=object.value("kind");if(!kind.isString()||(kind.toString()!="Rectangle"&&kind.toString()!="Ellipse"&&kind.toString()!="Line"))throw std::runtime_error("Invalid shape kind");
    auto number=[&](const char* name){auto value=object.value(QLatin1String(name));if(!value.isDouble()||!std::isfinite(value.toDouble()))throw std::runtime_error(std::string("Missing or invalid shape field: ")+name);return value.toDouble();};
    ShapeStyle style{kind.toString()=="Rectangle"?ShapeKind::Rectangle:kind.toString()=="Ellipse"?ShapeKind::Ellipse:ShapeKind::Line,number("red"),number("green"),number("blue"),number("cornerRadius")};
    if(style.kind==ShapeKind::Line){style.lineWidth=number("lineWidth");auto start=object.value("start").toObject(),end=object.value("end").toObject();auto axis=[&](const QJsonObject& p,const char* name){auto value=p.value(QLatin1String(name));if(!value.isDouble()||!std::isfinite(value.toDouble()))throw std::runtime_error("Invalid line endpoint");return value.toDouble();};style.startX=axis(start,"x");style.startY=axis(start,"y");style.endX=axis(end,"x");style.endY=axis(end,"y");}
    validate(style);return style;
}
std::string encodeShapeStyle(const ShapeStyle& style){validate(style);QJsonObject object{{"kind",QLatin1String(kindName(style.kind))},{"red",style.red},{"green",style.green},{"blue",style.blue},{"cornerRadius",style.cornerRadius}};if(style.kind==ShapeKind::Line){object["lineWidth"]=style.lineWidth;object["start"]=QJsonObject{{"x",style.startX},{"y",style.startY}};object["end"]=QJsonObject{{"x",style.endX},{"y",style.endY}};}return QJsonDocument(object).toJson(QJsonDocument::Compact).toStdString();}
std::shared_ptr<const Raster> shapeRaster(const ShapeStyle& style,int width,int height){
    validate(style);auto out=std::make_shared<Raster>(*Raster::filled(width,height));int columns=(width+255)/256;
    if(style.kind==ShapeKind::Line){double x0=style.startX*std::max(0,width-1),y0=style.startY*std::max(0,height-1),x1=style.endX*std::max(0,width-1),y1=style.endY*std::max(0,height-1),half=std::max(.5,style.lineWidth/2),len2=std::max(1e-6,(x1-x0)*(x1-x0)+(y1-y0)*(y1-y0));
        for(size_t key=0;key<out->tiles.size();++key){auto tile=std::make_shared<Raster::Tile>();int left=int(key%columns)*256,top=int(key/columns)*256;for(int y=0;y<std::min(256,height-top);++y)for(int x=0;x<std::min(256,width-left);++x){double px=left+x+.5,py=top+y+.5,t=std::clamp(((px-x0)*(x1-x0)+(py-y0)*(y1-y0))/len2,0.,1.),dx=px-(x0+t*(x1-x0)),dy=py-(y0+t*(y1-y0)),dist=std::hypot(dx,dy);auto alpha=uint8_t(std::clamp(std::lround((half+.5-dist)*255),0L,255L));tile->pixels[size_t(y)*256+x]={byte(std::clamp(style.red,0.,1.)*alpha),byte(std::clamp(style.green,0.,1.)*alpha),byte(std::clamp(style.blue,0.,1.)*alpha),alpha};}out->tiles[key]=tile;}return out;}
    Rect rect{0,0,double(width),double(height)};auto outline=style.kind==ShapeKind::Ellipse?SelectionOutline::ellipse(rect):SelectionOutline::roundedRectangle(rect,style.cornerRadius);auto coverage=outline.rasterize(width,height);
    for(size_t key=0;key<out->tiles.size();++key){auto tile=std::make_shared<Raster::Tile>();int left=int(key%columns)*256,top=int(key/columns)*256;for(int y=0;y<std::min(256,height-top);++y)for(int x=0;x<std::min(256,width-left);++x){auto alpha=coverage->pixel(left+x,top+y);tile->pixels[size_t(y)*256+x]={byte(std::clamp(style.red,0.,1.)*alpha),byte(std::clamp(style.green,0.,1.)*alpha),byte(std::clamp(style.blue,0.,1.)*alpha),alpha};}out->tiles[key]=tile;}return out;
}
std::optional<Layer> createShapeLayer(Rect rect,const ShapeStyle& style,std::string name){
    validate(style);for(double value:{rect.x,rect.y,rect.width,rect.height})if(!std::isfinite(value))throw std::runtime_error("Invalid shape rectangle");if(rect.width<1||rect.height<1)return {};if(rect.width>30000||rect.height>30000||uint64_t(rect.width)*uint64_t(rect.height)>100000000)throw std::runtime_error("Shape exceeds pixel budget");
    Layer layer;layer.id=newId();layer.name=std::move(name);layer.transform.x=rect.x;layer.transform.y=rect.y;layer.transform.width=rect.width;layer.transform.height=rect.height;if(!layer.transform.valid())throw std::runtime_error("Invalid shape placement");layer.raster=shapeRaster(style,int(rect.width),int(rect.height));layer.shapeJson=encodeShapeStyle(style);return layer;
}
std::string nextShapeName(const Document& doc,ShapeKind kind){for(size_t number=1;;++number){std::string name=std::string(kindName(kind))+" "+std::to_string(number);if(std::none_of(doc.layers.begin(),doc.layers.end(),[&](const Layer& layer){return layer.name==name;}))return name;}}
Layer redrawShape(const Layer& layer){if(layer.shapeJson.empty()||!layer.raster)return layer;return drawLayer(layer,decodeShapeStyle(layer.shapeJson),false);}
Layer restyleShape(const Layer& layer,const ShapeStyle& style){validate(style);if(layer.shapeJson.empty()||!layer.raster)throw std::runtime_error("Layer is no longer a live shape");if(decodeShapeStyle(layer.shapeJson)==style)return layer;return drawLayer(layer,style,true);}
std::shared_ptr<const Raster> shapeTransformPreview(const Layer& layer,const Transform& transform){
    if(layer.shapeJson.empty()||!layer.raster)return {};auto style=decodeShapeStyle(layer.shapeJson);if(style.kind!=ShapeKind::Rectangle||style.cornerRadius<=0)return {};if(!transform.valid())throw std::runtime_error("Invalid shape preview transform");if(std::abs(transform.width-layer.raster->width)<.5&&std::abs(transform.height-layer.raster->height)<.5)return {};
    double factor=std::min(1.,2048/std::max(transform.width,transform.height));int width=std::max(1,int(std::round(transform.width*factor))),height=std::max(1,int(std::round(transform.height*factor)));style.cornerRadius*=factor;return shapeRaster(style,width,height);
}
}
