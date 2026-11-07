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
void validate(const ShapeStyle& s){if(s.kind!=ShapeKind::Rectangle&&s.kind!=ShapeKind::Ellipse&&s.kind!=ShapeKind::Line)throw std::runtime_error("Invalid shape kind");for(double c:{s.red,s.green,s.blue})if(!std::isfinite(c))throw std::runtime_error("Invalid shape color");if(!std::isfinite(s.cornerRadius))throw std::runtime_error("Invalid shape radius");if(s.smoothing&&s.kind!=ShapeKind::Rectangle)throw std::runtime_error("Only rectangles can have smooth corners");if(s.kind==ShapeKind::Line&&(!(s.lineWidth>=.1)||s.lineWidth>500||s.startX<0||s.startX>1||s.startY<0||s.startY>1||s.endX<0||s.endX>1||s.endY<0||s.endY>1))throw std::runtime_error("Invalid line shape");if(s.strokeEnabled&&(s.kind==ShapeKind::Line||!(s.strokeWidth>=.1)||s.strokeWidth>500||!std::isfinite(s.strokeRed)||!std::isfinite(s.strokeGreen)||!std::isfinite(s.strokeBlue)))throw std::runtime_error("Invalid shape stroke");}
constexpr const char* lineStyles[]{"Solid","Dashed","Long Dash","Dash Dot","Long Dash Dot","Dotted"};
constexpr const char* lineCaps[]{"Round","Square"};
template<size_t N> int lineOption(const QJsonValue& value,const char* const (&names)[N],const char* error){
    if(value.isUndefined()||value.isNull())return 0;
    if(!value.isString())throw std::runtime_error(error);
    for(size_t i=0;i<N;++i)if(value.toString()==QLatin1String(names[i]))return int(i);
    throw std::runtime_error(error);
}
uint8_t byte(double value){return uint8_t(std::clamp(std::lround(value),0L,255L));}
const char* kindName(ShapeKind kind){return kind==ShapeKind::Rectangle?"Rectangle":kind==ShapeKind::Ellipse?"Ellipse":"Line";}
Layer drawLayer(const Layer& source,const ShapeStyle& style,bool force){
    if(!source.raster||source.shapeJson.empty())throw std::runtime_error("Layer is no longer a live shape");if(!source.transform.valid())throw std::runtime_error("Invalid shape transform");
    int width=std::max(1,int(std::round(source.transform.width))),height=std::max(1,int(std::round(source.transform.height)));
    if(!force&&width==source.raster->width&&height==source.raster->height)return source;
    auto result=source;result.raster=shapeRaster(style,width,height);result.deep.reset();result.shapeJson=encodeShapeStyle(style);if(result.mask&&!result.mask->placement)result.mask->placement=source.transform;return result;
}
// Premultiplied pixel with the stroke composited over the fill. Both coverages are 0-255.
Pixel strokedPixel(const ShapeStyle& style,int fillAlpha,int strokeAlpha){
    const double fill=fillAlpha/255.,stroke=strokeAlpha/255.,alpha=stroke+fill*(1-stroke);
    auto channel=[&](double fillColor,double strokeColor){return byte(255*(strokeColor*stroke+fillColor*fill*(1-stroke)));};
    return {channel(std::clamp(style.red,0.,1.),std::clamp(style.strokeRed,0.,1.)),channel(std::clamp(style.green,0.,1.),std::clamp(style.strokeGreen,0.,1.)),channel(std::clamp(style.blue,0.,1.),std::clamp(style.strokeBlue,0.,1.)),byte(255*alpha)};
}
}
std::vector<double> shapeLinePattern(ShapeLineStyle style){
    switch(style){
    case ShapeLineStyle::Solid:return {};
    case ShapeLineStyle::Dashed:return {3,2};
    case ShapeLineStyle::LongDash:return {7,2};
    case ShapeLineStyle::DashDot:return {3,1.5,1,1.5};
    case ShapeLineStyle::LongDashDot:return {6,2,1,2};
    case ShapeLineStyle::Dotted:return {1,1};
    }
    throw std::runtime_error("Invalid line style");
}
ShapeStyle decodeShapeStyle(std::string_view json){
    if(json.size()>65536)throw std::runtime_error("Shape style JSON exceeds budget");QJsonParseError error;auto doc=QJsonDocument::fromJson(QByteArray(json.data(),qsizetype(json.size())),&error);if(error.error!=QJsonParseError::NoError||!doc.isObject())throw std::runtime_error("Invalid shape style JSON");auto object=doc.object();auto kind=object.value("kind");if(!kind.isString()||(kind.toString()!="Rectangle"&&kind.toString()!="Ellipse"&&kind.toString()!="Line"))throw std::runtime_error("Invalid shape kind");
    auto number=[&](const char* name){auto value=object.value(QLatin1String(name));if(!value.isDouble()||!std::isfinite(value.toDouble()))throw std::runtime_error(std::string("Missing or invalid shape field: ")+name);return value.toDouble();};
    ShapeStyle style{kind.toString()=="Rectangle"?ShapeKind::Rectangle:kind.toString()=="Ellipse"?ShapeKind::Ellipse:ShapeKind::Line,number("red"),number("green"),number("blue"),number("cornerRadius")};
    const auto smoothing=object.value("smoothing");
    if(!smoothing.isUndefined()&&!smoothing.isNull()&&!smoothing.isBool())throw std::runtime_error("Invalid shape smoothing");
    style.smoothing=smoothing.toBool();
    style.lineStyle=ShapeLineStyle(lineOption(object.value("lineStyle"),lineStyles,"Invalid line style"));
    style.lineCap=ShapeLineCap(lineOption(object.value("lineCap"),lineCaps,"Invalid line cap"));
    if(style.kind!=ShapeKind::Line&&(style.lineStyle!=ShapeLineStyle::Solid||style.lineCap!=ShapeLineCap::Round))throw std::runtime_error("Only lines have a line style");
    if(style.kind==ShapeKind::Line){style.lineWidth=number("lineWidth");auto start=object.value("start").toObject(),end=object.value("end").toObject();auto axis=[&](const QJsonObject& p,const char* name){auto value=p.value(QLatin1String(name));if(!value.isDouble()||!std::isfinite(value.toDouble()))throw std::runtime_error("Invalid line endpoint");return value.toDouble();};style.startX=axis(start,"x");style.startY=axis(start,"y");style.endX=axis(end,"x");style.endY=axis(end,"y");}
    if(object.contains("stroke")){auto stroke=object.value("stroke").toObject();auto field=[&](const char* name){auto value=stroke.value(QLatin1String(name));if(!value.isDouble()||!std::isfinite(value.toDouble()))throw std::runtime_error(std::string("Invalid shape stroke field: ")+name);return value.toDouble();};style.strokeEnabled=true;style.strokeWidth=field("width");style.strokeRed=field("red");style.strokeGreen=field("green");style.strokeBlue=field("blue");}
    validate(style);return style;
}
std::string encodeShapeStyle(const ShapeStyle& style){validate(style);const int lineStyle=int(style.lineStyle),lineCap=int(style.lineCap);if(lineStyle<0||lineStyle>=6||lineCap<0||lineCap>=2)throw std::runtime_error("Invalid line style or cap");QJsonObject object{{"kind",QLatin1String(kindName(style.kind))},{"red",style.red},{"green",style.green},{"blue",style.blue},{"cornerRadius",style.cornerRadius}};if(style.kind==ShapeKind::Line){object["lineWidth"]=style.lineWidth;object["start"]=QJsonObject{{"x",style.startX},{"y",style.startY}};object["end"]=QJsonObject{{"x",style.endX},{"y",style.endY}};if(lineStyle)object["lineStyle"]=QLatin1String(lineStyles[lineStyle]);if(lineCap)object["lineCap"]=QLatin1String(lineCaps[lineCap]);}if(style.smoothing)object["smoothing"]=true;if(style.strokeEnabled)object["stroke"]=QJsonObject{{"width",style.strokeWidth},{"red",style.strokeRed},{"green",style.strokeGreen},{"blue",style.strokeBlue}};return QJsonDocument(object).toJson(QJsonDocument::Compact).toStdString();}
SelectionOutline rectangleShapeOutline(Rect rect,const ShapeStyle& style,double inset){
    if(inset>0){
        if(rect.width<=2*inset||rect.height<=2*inset)return {};
        rect={rect.x+inset,rect.y+inset,rect.width-2*inset,rect.height-2*inset};
    }
    const double radius=style.cornerRadius-inset;
    return style.smoothing?SelectionOutline::smoothedRectangle(rect,radius):SelectionOutline::roundedRectangle(rect,radius);
}
static std::shared_ptr<const Raster> shapeRasterImpl(const ShapeStyle& style,int width,int height,double lineCoverage){
    validate(style);auto out=std::make_shared<Raster>(*Raster::filled(width,height));int columns=(width+255)/256;
    if(style.kind==ShapeKind::Line){
        const double x0=style.startX*std::max(0,width-1),y0=style.startY*std::max(0,height-1),x1=style.endX*std::max(0,width-1),y1=style.endY*std::max(0,height-1);
        const double length=std::hypot(x1-x0,y1-y0),ux=length>1e-6?(x1-x0)/length:1,uy=length>1e-6?(y1-y0)/length:0,half=std::max(.5,style.lineWidth/2);
        auto pattern=shapeLinePattern(style.lineStyle);double period=0;for(auto& value:pattern){value*=2*half;period+=value;}
        for(size_t key=0;key<out->tiles.size();++key){auto tile=std::make_shared<Raster::Tile>();int left=int(key%columns)*256,top=int(key/columns)*256;for(int y=0;y<std::min(256,height-top);++y)for(int x=0;x<std::min(256,width-left);++x){
            const double dx=left+x+.5-x0,dy=top+y+.5-y0,along=dx*ux+dy*uy,across=std::abs(-dx*uy+dy*ux);double distance=1e30;
            if(across>half+.5||along< -half-.5||along>length+half+.5)continue;
            auto segment=[&](double a,double b){a=std::max(0.,a);b=std::min(length,b);if(b<a)return;const double outside=std::max({a-along,along-b,0.});const double d=style.lineCap==ShapeLineCap::Round?std::hypot(outside,across)-half:std::max(outside-half,across-half);distance=std::min(distance,d);};
            if(pattern.empty())segment(0,length);else{const double cycle=std::floor(along/period);for(int offset=-1;offset<=1;++offset){double at=(cycle+offset)*period;for(size_t i=0;i<pattern.size();i+=2){segment(at,at+std::max(.001,pattern[i]-2*half));at+=pattern[i]+pattern[i+1];}}}
            auto alpha=uint8_t(std::clamp(std::lround((.5-distance)*255*lineCoverage),0L,255L));tile->pixels[size_t(y)*256+x]={byte(std::clamp(style.red,0.,1.)*alpha),byte(std::clamp(style.green,0.,1.)*alpha),byte(std::clamp(style.blue,0.,1.)*alpha),alpha};}out->tiles[key]=tile;}return out;}
    Rect rect{0,0,double(width),double(height)};auto outline=style.kind==ShapeKind::Ellipse?SelectionOutline::ellipse(rect):rectangleShapeOutline(rect,style);auto coverage=outline.rasterize(width,height);
    // The stroke is the band between the outer outline and the same outline inset by the stroke width. A stroke wider than the shape leaves no hole.
    std::shared_ptr<const GrayRaster> hole;
    if(style.strokeEnabled){const double inset=style.strokeWidth;const Rect band{inset,inset,width-2*inset,height-2*inset};if(band.width>0&&band.height>0){auto inner=style.kind==ShapeKind::Ellipse?SelectionOutline::ellipse(band):rectangleShapeOutline(rect,style,inset);hole=inner.rasterize(width,height);}}
    for(size_t key=0;key<out->tiles.size();++key){auto tile=std::make_shared<Raster::Tile>();int left=int(key%columns)*256,top=int(key/columns)*256;for(int y=0;y<std::min(256,height-top);++y)for(int x=0;x<std::min(256,width-left);++x){auto alpha=coverage->pixel(left+x,top+y);if(style.strokeEnabled){const int inside=hole?hole->pixel(left+x,top+y):0;tile->pixels[size_t(y)*256+x]=strokedPixel(style,alpha,std::max(0,int(alpha)-inside));continue;}tile->pixels[size_t(y)*256+x]={byte(std::clamp(style.red,0.,1.)*alpha),byte(std::clamp(style.green,0.,1.)*alpha),byte(std::clamp(style.blue,0.,1.)*alpha),alpha};}out->tiles[key]=tile;}return out;
}
std::shared_ptr<const Raster> shapeRaster(const ShapeStyle& style,int width,int height){return shapeRasterImpl(style,width,height,1);}
std::optional<Layer> createShapeLayer(Rect rect,const ShapeStyle& style,std::string name){
    validate(style);for(double value:{rect.x,rect.y,rect.width,rect.height})if(!std::isfinite(value))throw std::runtime_error("Invalid shape rectangle");if(rect.width<1||rect.height<1)return {};if(rect.width>30000||rect.height>30000||uint64_t(rect.width)*uint64_t(rect.height)>100000000)throw std::runtime_error("Shape exceeds pixel budget");
    Layer layer;layer.id=newId();layer.name=std::move(name);layer.transform.x=rect.x;layer.transform.y=rect.y;layer.transform.width=rect.width;layer.transform.height=rect.height;if(!layer.transform.valid())throw std::runtime_error("Invalid shape placement");layer.raster=shapeRaster(style,int(rect.width),int(rect.height));layer.shapeJson=encodeShapeStyle(style);return layer;
}
std::string nextShapeName(const Document& doc,ShapeKind kind){for(size_t number=1;;++number){std::string name=std::string(kindName(kind))+" "+std::to_string(number);if(std::none_of(doc.layers.begin(),doc.layers.end(),[&](const Layer& layer){return layer.name==name;}))return name;}}
Layer redrawShape(const Layer& layer){if(layer.shapeJson.empty()||!layer.raster)return layer;return drawLayer(layer,decodeShapeStyle(layer.shapeJson),false);}
Layer restyleShape(const Layer& layer,const ShapeStyle& style){
    validate(style);if(layer.shapeJson.empty()||!layer.raster)throw std::runtime_error("Layer is no longer a live shape");if(decodeShapeStyle(layer.shapeJson)==style)return layer;
    if(style.kind!=ShapeKind::Line)return drawLayer(layer,style,true);
    auto expanded=layer;auto adjusted=style;const auto& t=layer.transform;
    const double reach=std::ceil(std::max(1.,style.lineWidth)/std::sqrt(2.)+1);
    const double margin=std::min({style.startX*t.width,(1-style.startX)*t.width,style.endX*t.width,(1-style.endX)*t.width,style.startY*t.height,(1-style.startY)*t.height,style.endY*t.height,(1-style.endY)*t.height});
    const double padding=std::max(0.,std::ceil(reach-margin));
    if(padding>0){
        const double width=t.width+2*padding,height=t.height+2*padding;
        if(width>30000||height>30000||width*height>100000000)throw std::runtime_error("Shape exceeds pixel budget");
        if(expanded.mask&&!expanded.mask->placement)expanded.mask->placement=t;
        expanded.transform.x-=padding;expanded.transform.y-=padding;expanded.transform.width=width;expanded.transform.height=height;
        auto remap=[&](double value,double oldSize,int oldPixels,double newSize){const int newPixels=std::max(1,int(std::round(newSize)));return (value*(oldPixels-1)/oldPixels*oldSize+padding)/newSize*newPixels/(newPixels-1);};
        adjusted.startX=remap(style.startX,t.width,layer.raster->width,width);adjusted.endX=remap(style.endX,t.width,layer.raster->width,width);
        adjusted.startY=remap(style.startY,t.height,layer.raster->height,height);adjusted.endY=remap(style.endY,t.height,layer.raster->height,height);
    }
    return drawLayer(expanded,adjusted,true);
}
std::shared_ptr<const Raster> shapeTransformPreview(const Layer& layer,const Transform& transform){
    if(layer.shapeJson.empty()||!layer.raster)return {};auto style=decodeShapeStyle(layer.shapeJson);if(style.kind!=ShapeKind::Line&&(style.kind!=ShapeKind::Rectangle||style.cornerRadius<=0))return {};if(!transform.valid())throw std::runtime_error("Invalid shape preview transform");if(std::abs(transform.width-layer.raster->width)<.5&&std::abs(transform.height-layer.raster->height)<.5)return {};
    double factor=std::min(1.,2048/std::max(transform.width,transform.height));int width=std::max(1,int(std::round(transform.width*factor))),height=std::max(1,int(std::round(transform.height*factor)));style.cornerRadius*=factor;const double lineCoverage=std::min(1.,style.lineWidth*factor);style.lineWidth=std::max(.1,style.lineWidth*factor);if(style.strokeEnabled)style.strokeWidth=std::max(.1,style.strokeWidth*factor);return shapeRasterImpl(style,width,height,lineCoverage);
}
}
