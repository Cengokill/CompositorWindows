// Formula translations from pinned Compositor, copyright Wonder Assembly LLC 2026 (MIT).
// See windows/LICENSE and docs/effects.md for exact source and reference limitations.
#include "Adjustments.h"
#include "effects_tools/CurveMath.h"
#include "graphics/PixelAlgorithms.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QString>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace compositor::effects {
namespace {
using RGB=std::array<double,3>;
constexpr std::array<const char*,6> kinds{"Hue/Saturation","Levels","Curves","Exposure","Gradient Map","Grain"};
constexpr std::array<const char*,7> ranges{"Master","Reds","Yellows","Greens","Cyans","Blues","Magentas"};
[[noreturn]] void bad(){throw std::invalid_argument("Malformed or unsupported adjustment settings");}
QString text(std::string_view s){return QString::fromUtf8(s.data(),qsizetype(s.size()));}
QJsonObject object(QJsonValue v){if(!v.isObject())bad();return v.toObject();}
QJsonArray array(QJsonValue v){if(!v.isArray())bad();return v.toArray();}
double number(QJsonValue v,double lo=-std::numeric_limits<double>::max(),double hi=std::numeric_limits<double>::max()){if(!v.isDouble()||!std::isfinite(v.toDouble())||v.toDouble()<lo||v.toDouble()>hi)bad();return v.toDouble();}
bool boolean(QJsonValue v){if(!v.isBool())bad();return v.toBool();}
bool missing(const QJsonObject&o,const char*k){return !o.contains(k)||o[k].isNull();}
template<size_t N>size_t named(QJsonValue v,const std::array<const char*,N>&names){if(!v.isString())bad();auto i=std::find_if(names.begin(),names.end(),[&](auto n){return v.toString()==n;});if(i==names.end())bad();return size_t(i-names.begin());}
void channel(QJsonValue v){static constexpr std::array<const char*,4> channels{"RGB","Red","Green","Blue"};named(v,channels);}
QJsonObject parse(std::string_view json){if(json.size()>4*1024*1024)bad();QJsonParseError error;auto doc=QJsonDocument::fromJson(QByteArray(json.data(),qsizetype(json.size())),&error);if(error.error!=QJsonParseError::NoError||!doc.isObject())bad();return doc.object();}
struct Level {double black{},gamma{1},white{255},outputBlack{},outputWhite{255};double apply(double v)const{auto t=std::clamp((v*255-black)/(white-black),0.,1.);return (outputBlack+std::pow(t,1/gamma)*(outputWhite-outputBlack))/255;}bool identity()const{return black==0&&gamma==1&&white==255&&outputBlack==0&&outputWhite==255;}};
Level level(QJsonValue value){auto o=object(value);Level r;r.black=number(o["black"],0,254);r.white=number(o["white"],r.black+1,255);r.gamma=number(o["gamma"],.1,9.99);r.outputBlack=number(o["outputBlack"],0,255);r.outputWhite=number(o["outputWhite"],0,255);return r;}
struct Curve {std::vector<Point> points;bool identity()const{return std::all_of(points.begin(),points.end(),[](auto p){return p.x==p.y;});}double value(double x)const{return effects_tools::curveValueUnchecked(points,x);}};
Curve curve(QJsonValue v){auto a=array(v);if(a.size()<2||a.size()>32)bad();Curve c;double previous=-1;for(qsizetype i=0;i<a.size();++i){auto p=object(a[i]);auto x=number(p["x"],0,255),y=number(p["y"],0,255);if(x<=previous||(i==0&&x!=0)||(i==a.size()-1&&x!=255))bad();c.points.push_back({x,y});previous=x;}return c;}
RGB color(QJsonValue v){auto o=object(v);return {number(o["red"],0,1),number(o["green"],0,1),number(o["blue"],0,1)};}
struct HSVChange{double hue{},saturation{},lightness{};bool zero()const{return hue==0&&saturation==0&&lightness==0;}};
HSVChange hsvChange(QJsonValue v){auto o=object(v);return {number(o["hue"],-360,360),number(o["saturation"],-100,100),number(o["lightness"],-100,100)};}
struct Band {double falloffStart{},rangeStart{},rangeEnd{},falloffEnd{};static double forward(double from,double to){auto d=std::fmod(to-from,360.);return d<0?d+360:d;}double weight(double hue)const{auto span=forward(falloffStart,falloffEnd);if(!(span>0))return 1;auto p=forward(falloffStart,hue);if(p>span)return 0;auto ramp=forward(falloffStart,rangeStart),plateau=forward(falloffStart,rangeEnd);if(p<ramp)return ramp>0?p/ramp:1;if(p<=plateau)return 1;auto end=span-plateau;return end>0?(span-p)/end:1;}};
struct Settings {
 size_t kind{};std::array<Level,4> levels{};std::array<Curve,4> curves;
 std::array<HSVChange,7> hsv{};std::array<Band,7> bands{{{0,0,360,360},{315,345,15,45},{15,45,75,105},{75,105,135,165},{135,165,195,225},{195,225,255,285},{255,285,315,345}}};
 size_t selectedRange{};bool colorize{},invertRange{},reverse{};double exposure{},offset{},gamma{1};RGB shadows{0,0,0},highlights{1,1,1};double amount{25},size{1.5},roughness{50};uint32_t seed{};
 bool identity()const{switch(kind){case 0:return !colorize&&std::all_of(hsv.begin(),hsv.end(),[](auto c){return c.zero();});case 1:return std::all_of(levels.begin(),levels.end(),[](auto l){return l.identity();});case 2:return std::all_of(curves.begin(),curves.end(),[](const auto&c){return c.identity();});case 3:return exposure==0&&offset==0&&gamma==1;case 5:return amount==0;default:return false;}}
};
Settings settings(const QJsonObject&o){Settings s;s.kind=named(o["kind"],kinds);s.hsv[0]=hsvChange(o);s.colorize=boolean(o["colorize"]);auto l=object(o["levels"]);channel(l["channel"]);auto la=array(l["ranges"]);if(la.size()!=4)bad();for(int i=0;i<4;++i)s.levels[i]=level(la[i]);auto c=object(o["curves"]);channel(c["channel"]);auto ca=array(c["channels"]);if(ca.size()!=4)bad();for(int i=0;i<4;++i)s.curves[i]=curve(ca[i]);
 if(!missing(o,"hsvSettings")){auto h=object(o["hsvSettings"]);s.selectedRange=named(h["range"],ranges);s.colorize=boolean(h["colorize"]);s.invertRange=boolean(h["invertRange"]);s.hsv={};auto values=array(h["adjustments"]);if(values.size()%2)bad();for(qsizetype i=0;i<values.size();i+=2)s.hsv[named(values[i],ranges)]=hsvChange(values[i+1]);auto bands=array(h["bands"]);if(bands.size()%2)bad();for(qsizetype i=0;i<bands.size();i+=2){auto b=object(bands[i+1]);s.bands[named(bands[i],ranges)]={number(b["falloffStart"]),number(b["rangeStart"]),number(b["rangeEnd"]),number(b["falloffEnd"])};}}
 if(!missing(o,"exposureSettings")){auto e=object(o["exposureSettings"]);s.exposure=number(e["exposure"],-20,20);s.offset=number(e["offset"],-.5,.5);s.gamma=number(e["gamma"],.01,9.99);}
 if(!missing(o,"gradientMapSettings")){auto g=object(o["gradientMapSettings"]);s.shadows=color(g["shadows"]);s.highlights=color(g["highlights"]);s.reverse=boolean(g["reversed"]);}
 if(!missing(o,"grainSettings")){auto g=object(o["grainSettings"]);s.amount=number(g["amount"],0,100);s.size=number(g["size"],.5,20);s.roughness=number(g["roughness"],0,100);auto seed=number(g["seed"],0,4294967295.);if(std::trunc(seed)!=seed)bad();s.seed=uint32_t(seed);}return s;
}
RGB toHSL(RGB c){auto hi=*std::max_element(c.begin(),c.end()),lo=*std::min_element(c.begin(),c.end()),light=(hi+lo)/2,delta=hi-lo;if(!(delta>0))return {0,0,light};auto sat=delta/(1-std::abs(2*light-1));double hue;if(hi==c[0])hue=(c[1]-c[2])/delta;else if(hi==c[1])hue=(c[2]-c[0])/delta+2;else hue=(c[0]-c[1])/delta+4;hue*=60;if(hue<0)hue+=360;return {hue,std::min(1.,sat),light};}
RGB toRGB(RGB hsl){auto[h,s,l]=hsl;if(!(s>0))return {l,l,l};auto chroma=(1-std::abs(2*l-1))*s,sector=h/60,second=chroma*(1-std::abs(std::fmod(sector,2.)-1)),base=l-chroma/2;RGB c{};switch(int(sector)){case 0:c={chroma,second,0};break;case 1:c={second,chroma,0};break;case 2:c={0,chroma,second};break;case 3:c={0,second,chroma};break;case 4:c={second,0,chroma};break;default:c={chroma,0,second};}for(auto&v:c)v=std::clamp(v+base,0.,1.);return c;}
std::vector<std::array<float,3>> cube(const Settings&s){std::array<HSVChange,361> response{};for(int degree=0;degree<=360;++degree)for(size_t i=0;i<7;++i){auto weight=i?s.bands[i].weight(degree):1.;if(i&&s.invertRange&&i==s.selectedRange)weight=1-weight;if(weight>0){response[degree].hue+=s.hsv[i].hue*weight;response[degree].saturation+=s.hsv[i].saturation*weight;response[degree].lightness+=s.hsv[i].lightness*weight;}}
 std::vector<std::array<float,3>> values;values.reserve(33*33*33);for(int b=0;b<33;++b)for(int g=0;g<33;++g)for(int r=0;r<33;++r){auto[h,saturation,lightness]=toHSL({r/32.,g/32.,b/32.});double amount;if(s.colorize){h=std::fmod(s.hsv[s.selectedRange].hue,360.);saturation=std::clamp(s.hsv[s.selectedRange].saturation/100.,0.,1.);amount=s.hsv[s.selectedRange].lightness/100.;}else{auto sampled=response[size_t(std::clamp(std::lround(h),0L,360L))];amount=sampled.lightness/100;h=std::fmod(h+sampled.hue,360.);if(h<0)h+=360;saturation=std::clamp(saturation*(1+sampled.saturation/100),0.,1.);}amount=std::clamp(amount,-1.,1.);lightness=amount>=0?lightness+(1-lightness)*amount:lightness*(1+amount);auto rgb=toRGB({h,saturation,std::clamp(lightness,0.,1.)});values.push_back({float(rgb[0]),float(rgb[1]),float(rgb[2])});}return values;
}
uint8_t byte(double n){return uint8_t(std::clamp(std::lround(n),0L,255L));}
Pixel sampleCube(Pixel pixel,const std::vector<std::array<float,3>>&v){if(!pixel.a)return pixel;std::array<double,3> u{pixel.r*32./pixel.a,pixel.g*32./pixel.a,pixel.b*32./pixel.a};std::array<int,3> lower{};RGB f{};for(int c=0;c<3;++c){u[c]=std::clamp(u[c],0.,32.);lower[c]=std::min(31,int(u[c]));f[c]=u[c]-lower[c];}RGB out{};for(int b=0;b<2;++b)for(int g=0;g<2;++g)for(int r=0;r<2;++r){auto weight=(r?f[0]:1-f[0])*(g?f[1]:1-f[1])*(b?f[2]:1-f[2]);auto&entry=v[size_t((lower[2]+b)*33*33+(lower[1]+g)*33+lower[0]+r)];for(int c=0;c<3;++c)out[c]+=entry[c]*weight;}return {byte(out[0]*pixel.a),byte(out[1]*pixel.a),byte(out[2]*pixel.a),pixel.a};}
std::array<float,768> tables(const Settings&s){std::array<float,768> out{};for(int c=0;c<3;++c)for(int i=0;i<256;++i){double result;if(s.kind==1)result=s.levels[0].apply(s.levels[c+1].apply(i/255.));else if(s.kind==2)result=s.curves[0].value(s.curves[c+1].value(i))/255.;else{double encoded=i/255.,linear=encoded<=.04045?encoded/12.92:std::pow((encoded+.055)/1.055,2.4);linear=std::pow(std::max(0.,linear*std::pow(2.,s.exposure)+s.offset),1/s.gamma);result=linear<=.0031308?linear*12.92:1.055*std::pow(linear,1/2.4)-.055;}out[c*256+i]=float(std::clamp(result,0.,1.));}return out;}
std::array<uint8_t,768> gradient(const Settings&s){auto dark=s.reverse?s.highlights:s.shadows,light=s.reverse?s.shadows:s.highlights;std::array<uint8_t,768> out{};for(int i=0;i<256;++i)for(int c=0;c<3;++c)out[i*3+c]=byte((dark[c]+(light[c]-dark[c])*i/255.)*255);return out;}
}
std::string defaultAdjustmentJson(std::string_view kind){named(text(kind),kinds);QJsonObject range{{"black",0},{"white",255},{"gamma",1},{"outputBlack",0},{"outputWhite",255}};QJsonArray ls,cs;for(int i=0;i<4;++i){ls.append(range);cs.append(QJsonArray{QJsonObject{{"x",0},{"y",0}},QJsonObject{{"x",255},{"y",255}}});}return QJsonDocument(QJsonObject{{"kind",text(kind)},{"hue",0},{"saturation",0},{"lightness",0},{"colorize",false},{"levels",QJsonObject{{"channel","RGB"},{"ranges",ls}}},{"curves",QJsonObject{{"channel","RGB"},{"channels",cs}}}}).toJson(QJsonDocument::Compact).toStdString();}
void validateAdjustmentJson(std::string_view json){settings(parse(json));}
std::shared_ptr<const Raster> applyAdjustment(std::shared_ptr<const Raster> source,std::string_view json,const GrayRaster* selection,AdjustmentRegion region){
 if(!source||source->width<1||source->height<1||source->width>30000||source->height>30000||uint64_t(source->width)*source->height>100000000)throw std::invalid_argument("Invalid source raster");
 auto expectedTiles=size_t((source->width+255)/256)*size_t((source->height+255)/256);if(source->tiles.size()!=expectedTiles||std::any_of(source->tiles.begin(),source->tiles.end(),[](const auto&t){return !t;}))throw std::invalid_argument("Invalid immutable tile storage");
 if(selection&&(selection->width!=source->width||selection->height!=source->height||selection->pixels.size()!=size_t(source->width)*source->height))throw std::invalid_argument("Selection must match source pixel grid");
 if(!std::isfinite(region.originX)||!std::isfinite(region.originY)||!std::isfinite(region.unitsPerPixel)||region.unitsPerPixel<=0)throw std::invalid_argument("Invalid adjustment region");auto s=settings(parse(json));
 if(s.identity()||(selection&&std::none_of(selection->pixels.begin(),selection->pixels.end(),[](auto v){return v!=0;})))return source;
 std::vector<std::array<float,3>> colors;if(s.kind==0)colors=cube(s);std::array<float,768> lut{};if(s.kind>=1&&s.kind<=3)lut=tables(s);std::array<uint8_t,768> gradientLut{};if(s.kind==4)gradientLut=gradient(s);
 auto result=std::make_shared<Raster>(*source);bool any=false;const int columns=(source->width+255)/256;
 for(int ty=0;ty<source->height;ty+=256)for(int tx=0;tx<source->width;tx+=256){int w=std::min(256,source->width-tx),h=std::min(256,source->height-ty);size_t index=size_t(ty/256)*columns+tx/256;auto original=source->tiles[index];
    if(selection){bool selected=false;for(int y=0;y<h&&!selected;++y)for(int x=0;x<w;++x)if(selection->pixels[size_t(ty+y)*source->width+tx+x]){selected=true;break;}if(!selected)continue;}
    auto tile=std::make_shared<Raster::Tile>(*original);auto bytes=std::span<uint8_t>(reinterpret_cast<uint8_t*>(tile->pixels.data()),sizeof(Raster::Tile));graphics::Rgba8View view{bytes,uint32_t(w),uint32_t(h),256*4};
    if(s.kind==0){for(int y=0;y<h;++y)for(int x=0;x<w;++x)tile->pixels[size_t(y)*256+x]=sampleCube(tile->pixels[size_t(y)*256+x],colors);}
    else if(s.kind<=3){
        if(s.kind==1)for(int y=0;y<h;++y)for(int x=0;x<w;++x){auto&p=tile->pixels[size_t(y)*256+x];if(p.a&&p.a<255){p.r=uint8_t(std::min(255,(int(p.r)*255+p.a/2)/p.a));p.g=uint8_t(std::min(255,(int(p.g)*255+p.a/2)/p.a));p.b=uint8_t(std::min(255,(int(p.b)*255+p.a/2)/p.a));}}
        graphics::applyLevels(view,lut);
        if(s.kind==1)for(int y=0;y<h;++y)for(int x=0;x<w;++x){auto&p=tile->pixels[size_t(y)*256+x];if(p.a&&p.a<255){p.r=uint8_t((int(p.r)*p.a+127)/255);p.g=uint8_t((int(p.g)*p.a+127)/255);p.b=uint8_t((int(p.b)*p.a+127)/255);}}
    }else if(s.kind==4)graphics::gradientMap(view,gradientLut);
    else if(s.kind==5)graphics::grain(view,s.amount,s.size,s.roughness,s.seed,region.originX+tx*region.unitsPerPixel,region.originY+ty*region.unitsPerPixel,region.unitsPerPixel);
    else bad();
    if(selection)for(int y=0;y<h;++y)for(int x=0;x<w;++x){auto i=size_t(y)*256+x;auto mask=selection->pixels[size_t(ty+y)*source->width+tx+x];auto a=original->pixels[i],b=tile->pixels[i];auto mix=[&](uint8_t av,uint8_t bv){return uint8_t((int(av)*(255-mask)+int(bv)*mask+127)/255);};tile->pixels[i]={mix(a.r,b.r),mix(a.g,b.g),mix(a.b,b.b),mix(a.a,b.a)};}
    if(tile->pixels!=original->pixels){result->tiles[index]=tile;any=true;}
 }return any?std::shared_ptr<const Raster>(result):source;
}
}
