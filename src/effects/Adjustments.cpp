// Formula translations from pinned Compositor, copyright Wonder Assembly LLC 2026 (MIT).
// See windows/LICENSE and docs/effects.md for exact source and reference limitations.
#include "Adjustments.h"
#include "effects_tools/CurveMath.h"
#include "filters/PixelFilters.h"
#include "graphics/PixelAlgorithms.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QString>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <vector>

namespace compositor::effects {
namespace {
using RGB=std::array<double,3>;
constexpr std::array<const char*,18> kinds{"Hue/Saturation","Levels","Curves","Exposure","Gradient Map","Grain","Invert","Black & White","Color Balance","Gaussian Blur","Motion Blur","Add Noise","Progressive Blur","Bloom / Glow","RGB Split","Vignette","Tonal Contrast","Lens Effects"};
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
 filters::VignetteLayerSettings vignette;
 filters::TonalLayerSettings tonal;
 filters::LensEffectSettings lens;
 size_t kind{};std::array<Level,4> levels{};std::array<Curve,4> curves;
 std::array<HSVChange,7> hsv{};std::array<Band,7> bands{{{0,0,360,360},{315,345,15,45},{15,45,75,105},{75,105,135,165},{135,165,195,225},{195,225,255,285},{255,285,315,345}}};
 size_t selectedRange{}; bool colorize{},invertRange{},reverse{};double exposure{},offset{},gamma{1};RGB shadows{0,0,0},highlights{1,1,1};double amount{25},size{1.5},roughness{50};uint32_t seed{};
 double bwReds{40},bwYellows{60},bwGreens{40},bwCyans{60},bwBlues{20},bwMagentas{80};
 double shadowCR{},shadowMG{},shadowYB{},midCR{},midMG{},midYB{},highCR{},highMG{},highYB{};
 double blurRadius{},blurAngle{},blurDistance{},noiseAmount{};bool noiseGaussian{},noiseMono{};
 double progressiveFrom{},progressiveTo{20};std::optional<Point> progressiveStart,progressiveEnd;bool progressiveRadial{},progressiveInverted{};
 double bloomAmount{40},bloomRadius{24};
 double rgbSplitDistance{10},rgbSplitBlur{},rgbSplitAngle{};bool rgbSplitRadial{};std::optional<Point> rgbSplitCenter;double rgbSplitReach{1000};
 bool identity()const{switch(kind){case 0:return !colorize&&std::all_of(hsv.begin(),hsv.end(),[](auto c){return c.zero();});case 1:return std::all_of(levels.begin(),levels.end(),[](auto l){return l.identity();});case 2:return std::all_of(curves.begin(),curves.end(),[](const auto&c){return c.identity();});case 3:return exposure==0&&offset==0&&gamma==1;case 5:return amount==0;case 8:return shadowCR==0&&shadowMG==0&&shadowYB==0&&midCR==0&&midMG==0&&midYB==0&&highCR==0&&highMG==0&&highYB==0;case 9:return blurRadius==0;case 10:return blurDistance==0;case 11:return noiseAmount==0;case 12:return progressiveFrom==0&&progressiveTo==0;case 13:return bloomAmount==0;case 14:return rgbSplitDistance==0&&rgbSplitBlur==0;case 15:return vignette.amount==0;case 16:return tonal.amount==0||(tonal.shadows==0&&tonal.midtones==0&&tonal.highlights==0);case 17:return lens.isIdentity();default:return false;}}
};
std::optional<Point> optionalPoint(const QJsonObject&o,const char*k){if(missing(o,k))return {};auto v=o[k];if(v.isArray()){auto a=array(v);if(a.size()!=2)bad();return Point{number(a[0]),number(a[1])};}auto p=object(v);return Point{number(p["x"]),number(p["y"])};}
Settings settings(const QJsonObject&o){Settings s;s.kind=named(o["kind"],kinds);s.hsv[0]=hsvChange(o);s.colorize=boolean(o["colorize"]);auto l=object(o["levels"]);channel(l["channel"]);auto la=array(l["ranges"]);if(la.size()!=4)bad();for(int i=0;i<4;++i)s.levels[i]=level(la[i]);auto c=object(o["curves"]);channel(c["channel"]);auto ca=array(c["channels"]);if(ca.size()!=4)bad();for(int i=0;i<4;++i)s.curves[i]=curve(ca[i]);
 if(!missing(o,"hsvSettings")){auto h=object(o["hsvSettings"]);s.selectedRange=named(h["range"],ranges);s.colorize=boolean(h["colorize"]);s.invertRange=boolean(h["invertRange"]);s.hsv={};auto values=array(h["adjustments"]);if(values.size()%2)bad();for(qsizetype i=0;i<values.size();i+=2)s.hsv[named(values[i],ranges)]=hsvChange(values[i+1]);auto bands=array(h["bands"]);if(bands.size()%2)bad();for(qsizetype i=0;i<bands.size();i+=2){auto b=object(bands[i+1]);s.bands[named(bands[i],ranges)]={number(b["falloffStart"]),number(b["rangeStart"]),number(b["rangeEnd"]),number(b["falloffEnd"])};}}
 if(!missing(o,"exposureSettings")){auto e=object(o["exposureSettings"]);s.exposure=number(e["exposure"],-20,20);s.offset=number(e["offset"],-.5,.5);s.gamma=number(e["gamma"],.01,9.99);}
 if(!missing(o,"gradientMapSettings")){auto g=object(o["gradientMapSettings"]);s.shadows=color(g["shadows"]);s.highlights=color(g["highlights"]);s.reverse=boolean(g["reversed"]);}
 if(!missing(o,"grainSettings")){auto g=object(o["grainSettings"]);s.amount=number(g["amount"],0,100);s.size=number(g["size"],.5,20);s.roughness=number(g["roughness"],0,100);auto seed=number(g["seed"],0,4294967295.);if(std::trunc(seed)!=seed)bad();s.seed=uint32_t(seed);}
 if(!missing(o,"blackWhiteSettings")){auto g=object(o["blackWhiteSettings"]);s.bwReds=number(g["reds"],-200,300);s.bwYellows=number(g["yellows"],-200,300);s.bwGreens=number(g["greens"],-200,300);s.bwCyans=number(g["cyans"],-200,300);s.bwBlues=number(g["blues"],-200,300);s.bwMagentas=number(g["magentas"],-200,300);}
 if(!missing(o,"colorBalanceSettings")){auto g=object(o["colorBalanceSettings"]);auto tone=[&](const char* name,double& cr,double& mg,double& yb){auto t=object(g[name]);cr=number(t["cyanRed"],-100,100);mg=number(t["magentaGreen"],-100,100);yb=number(t["yellowBlue"],-100,100);};tone("shadows",s.shadowCR,s.shadowMG,s.shadowYB);tone("midtones",s.midCR,s.midMG,s.midYB);tone("highlights",s.highCR,s.highMG,s.highYB);}
 if(!missing(o,"filterSettings")){auto g=object(o["filterSettings"]);s.blurRadius=number(g["radius"],0,250);s.blurAngle=number(g["angle"],-90,90);s.blurDistance=number(g["distance"],0,2000);s.noiseAmount=number(g["amount"],0,400);s.noiseGaussian=boolean(g["gaussian"]);s.noiseMono=boolean(g["monochromatic"]);if(!missing(g,"seed")){auto seed=number(g["seed"],0,4294967295.);if(std::trunc(seed)!=seed)bad();s.seed=uint32_t(seed);}}
 else if(s.kind==9)s.blurRadius=8;else if(s.kind==10)s.blurDistance=16;else if(s.kind==11)s.noiseAmount=20;
 if(!missing(o,"filterLayerSettings")){auto g=object(o["filterLayerSettings"]);if(!missing(g,"progressiveFrom"))s.progressiveFrom=number(g["progressiveFrom"],0,250);if(!missing(g,"progressiveTo"))s.progressiveTo=number(g["progressiveTo"],0,250);s.progressiveStart=optionalPoint(g,"progressiveStart");s.progressiveEnd=optionalPoint(g,"progressiveEnd");if(!missing(g,"progressiveRadial"))s.progressiveRadial=boolean(g["progressiveRadial"]);if(!missing(g,"progressiveInverted"))s.progressiveInverted=boolean(g["progressiveInverted"]);if(!missing(g,"bloomAmount"))s.bloomAmount=number(g["bloomAmount"],0,100);if(!missing(g,"bloomRadius"))s.bloomRadius=number(g["bloomRadius"],1,150);if(!missing(g,"rgbSplitDistance"))s.rgbSplitDistance=number(g["rgbSplitDistance"],0,200);if(!missing(g,"rgbSplitBlur"))s.rgbSplitBlur=number(g["rgbSplitBlur"],0,400);if(!missing(g,"rgbSplitAngle"))s.rgbSplitAngle=number(g["rgbSplitAngle"],-180,180);if(!missing(g,"rgbSplitRadial"))s.rgbSplitRadial=boolean(g["rgbSplitRadial"]);s.rgbSplitCenter=optionalPoint(g,"rgbSplitCenter");if(!missing(g,"rgbSplitReach"))s.rgbSplitReach=number(g["rgbSplitReach"],1,100000);}
 if(s.kind==16&&(missing(o,"filterLayerSettings")||missing(object(o["filterLayerSettings"]),"tonal")))bad();
 if(!missing(o,"filterLayerSettings")){auto g=object(o["filterLayerSettings"]);if(!missing(g,"tonal")){auto t=object(g["tonal"]);s.tonal.amount=number(t["amount"],0,100);s.tonal.radius=number(t["radius"],1,100);s.tonal.shadows=number(t["shadows"],-100,100);s.tonal.midtones=number(t["midtones"],-100,100);s.tonal.highlights=number(t["highlights"],-100,100);}}
 if(!missing(o,"filterLayerSettings")){auto g=object(o["filterLayerSettings"]);if(!missing(g,"vignette")){auto v=object(g["vignette"]);s.vignette.amount=number(v["amount"],0,100);s.vignette.color=color(v["color"]);s.vignette.midpoint=number(v["midpoint"],0,100);s.vignette.roundness=number(v["roundness"],-100,100);s.vignette.feather=number(v["feather"],0,100);s.vignette.highlights=number(v["highlights"],0,100);}}
 if(s.kind==17&&(missing(o,"filterLayerSettings")||missing(object(o["filterLayerSettings"]),"lens")))bad();
 if(!missing(o,"filterLayerSettings")){auto g=object(o["filterLayerSettings"]);if(!missing(g,"lens")){auto v=object(g["lens"]);s.lens.effect=filters::LensEffect(named(v["effect"],std::array<const char*,5>{"Anamorphic Flare","Starburst","Mist","Prism","Bokeh"}));s.lens.bokehShape=filters::BokehShape(named(v["bokehShape"],std::array<const char*,4>{"Round","Hexagon","Oval","Cat's Eye"}));auto read=[&](const char* key,double& target,double lo,double hi){target=number(v[key],lo,hi);};read("threshold",s.lens.threshold,50,100);read("flareAmount",s.lens.flareAmount,0,100);read("flareLength",s.lens.flareLength,1,100);read("flareBlue",s.lens.flareBlue,0,100);read("starAmount",s.lens.starAmount,0,100);read("starLength",s.lens.starLength,2,40);read("starAngle",s.lens.starAngle,-180,180);const double points=number(v["starPoints"],4,14);if(std::trunc(points)!=points)bad();s.lens.starPoints=int(points);if(s.lens.starPoints!=4&&s.lens.starPoints!=6&&s.lens.starPoints!=8&&s.lens.starPoints!=12&&s.lens.starPoints!=14)bad();read("mistStrength",s.lens.mistStrength,0,100);read("mistSize",s.lens.mistSize,0,100);read("prismAmount",s.lens.prismAmount,0,100);read("prismCopies",s.lens.prismCopies,1,8);if(std::trunc(s.lens.prismCopies)!=s.lens.prismCopies)bad();read("prismOffset",s.lens.prismOffset,0,50);read("prismAngle",s.lens.prismAngle,-180,180);read("prismBlur",s.lens.prismBlur,0,100);s.lens.prismBothWays=boolean(v["prismBothWays"]);read("bokehRadius",s.lens.bokehRadius,.5,6);read("bokehBrightness",s.lens.bokehBrightness,0,100);read("bokehRing",s.lens.bokehRing,0,100);}}
 return s;
}
RGB toHSL(RGB c){auto hi=*std::max_element(c.begin(),c.end()),lo=*std::min_element(c.begin(),c.end()),light=(hi+lo)/2,delta=hi-lo;if(!(delta>0))return {0,0,light};auto sat=delta/(1-std::abs(2*light-1));double hue;if(hi==c[0])hue=(c[1]-c[2])/delta;else if(hi==c[1])hue=(c[2]-c[0])/delta+2;else hue=(c[0]-c[1])/delta+4;hue*=60;if(hue<0)hue+=360;return {hue,std::min(1.,sat),light};}
RGB toRGB(RGB hsl){auto[h,s,l]=hsl;if(!(s>0))return {l,l,l};auto chroma=(1-std::abs(2*l-1))*s,sector=h/60,second=chroma*(1-std::abs(std::fmod(sector,2.)-1)),base=l-chroma/2;RGB c{};switch(int(sector)){case 0:c={chroma,second,0};break;case 1:c={second,chroma,0};break;case 2:c={0,chroma,second};break;case 3:c={0,second,chroma};break;case 4:c={second,0,chroma};break;default:c={chroma,0,second};}for(auto&v:c)v=std::clamp(v+base,0.,1.);return c;}
std::vector<std::array<float,3>> cube(const Settings&s){std::array<HSVChange,361> response{};for(int degree=0;degree<=360;++degree)for(size_t i=0;i<7;++i){auto weight=i?s.bands[i].weight(degree):1.;if(i&&s.invertRange&&i==s.selectedRange)weight=1-weight;if(weight>0){response[degree].hue+=s.hsv[i].hue*weight;response[degree].saturation+=s.hsv[i].saturation*weight;response[degree].lightness+=s.hsv[i].lightness*weight;}}
 std::vector<std::array<float,3>> values;values.reserve(33*33*33);for(int b=0;b<33;++b)for(int g=0;g<33;++g)for(int r=0;r<33;++r){auto[h,saturation,lightness]=toHSL({r/32.,g/32.,b/32.});double amount;if(s.colorize){h=std::fmod(s.hsv[s.selectedRange].hue,360.);saturation=std::clamp(s.hsv[s.selectedRange].saturation/100.,0.,1.);amount=s.hsv[s.selectedRange].lightness/100.;}else{auto sampled=response[size_t(std::clamp(std::lround(h),0L,360L))];amount=sampled.lightness/100;h=std::fmod(h+sampled.hue,360.);if(h<0)h+=360;auto satAmount=std::clamp(sampled.saturation/100,-1.,1.);if(satAmount<=0)saturation=std::max(0.,saturation*(1+satAmount));else if(satAmount>=1)saturation=saturation>0?1:0;else saturation=std::min(1.,saturation/(1-satAmount));}amount=std::clamp(amount,-1.,1.);lightness=amount>=0?lightness+(1-lightness)*amount:lightness*(1+amount);auto rgb=toRGB({h,saturation,std::clamp(lightness,0.,1.)});values.push_back({float(rgb[0]),float(rgb[1]),float(rgb[2])});}return values;
}
std::shared_ptr<const Raster> resizeNearest(const Raster& source,int width,int height,const std::function<bool()>& cancelled){
    auto cancel=[&]{if(cancelled&&cancelled())throw std::runtime_error("Adjustment cancelled");};
    if(width==source.width&&height==source.height){
        auto copy=std::make_shared<Raster>();copy->width=source.width;copy->height=source.height;copy->tiles=source.tiles;copy->samplingOriginX=source.samplingOriginX;copy->samplingOriginY=source.samplingOriginY;return copy;
    }
    if(width<1||height<1||width>30000||height>30000||uint64_t(width)*height>100000000)throw std::invalid_argument("Invalid adjustment preview size");
    std::vector<Pixel> pixels(size_t(width)*height);
    for(int y=0;y<height;++y){cancel();for(int x=0;x<width;++x)
        pixels[size_t(y)*width+x]=source.pixel(std::min(source.width-1,int((x+.5)*source.width/width)),std::min(source.height-1,int((y+.5)*source.height/height)));}
    return Raster::fromRgba(width,height,reinterpret_cast<const uint8_t*>(pixels.data()),size_t(width)*4,cancelled);
}
GrayRaster resizeGrayNearest(const GrayRaster& source,int width,int height,const std::function<bool()>& cancelled){
    auto cancel=[&]{if(cancelled&&cancelled())throw std::runtime_error("Adjustment cancelled");};
    GrayRaster out;out.width=width;out.height=height;out.pixels.resize(size_t(width)*height);
    for(int y=0;y<height;++y){cancel();for(int x=0;x<width;++x)
        out.pixels[size_t(y)*width+x]=source.pixel(std::min(source.width-1,int((x+.5)*source.width/width)),std::min(source.height-1,int((y+.5)*source.height/height)));}
    return out;
}
std::shared_ptr<const DeepImage> resizeDeepNearest(const DeepImage& source,int width,int height,const std::function<bool()>& cancelled){
    auto cancel=[&]{if(cancelled&&cancelled())throw std::runtime_error("HDR adjustment cancelled");};
    auto out=makeDeep(width,height,source.format);
    for(int y=0;y<height;++y){cancel();for(int x=0;x<width;++x)
        out->setPixel(x,y,source.pixel(std::min(source.width-1,int((x+.5)*source.width/width)),std::min(source.height-1,int((y+.5)*source.height/height))));}
    return out;
}
// Interpolate only the effect delta, retaining the source detail and coverage.
// Each reduced pixel is read once, even at high canvas zoom.
class LensPreviewDelta {
    struct Sample {int first,second;double fraction;};
    static std::vector<Sample> axis(int size,int reduced){
        std::vector<Sample> result(size);
        for(int i=0;i<size;++i){
            const double coordinate=std::clamp((i+.5)*reduced/size-.5,0.,double(reduced-1));
            const int first=int(coordinate);
            result[i]={first,std::min(first+1,reduced-1),coordinate-first};
        }
        return result;
    }
    int width_;
    std::vector<Sample> columns_,rows_;
    std::vector<RGB> pixels_;
public:
    template<class Read>
    LensPreviewDelta(int width,int height,int reducedWidth,int reducedHeight,Read read)
        :width_(reducedWidth),columns_(axis(width,reducedWidth)),rows_(axis(height,reducedHeight)),
         pixels_(size_t(reducedWidth)*reducedHeight){
        for(int y=0;y<reducedHeight;++y)for(int x=0;x<reducedWidth;++x)pixels_[size_t(y)*width_+x]=read(x,y);
    }
    RGB pixel(int x,int y)const{
        const auto [x0,x1,tx]=columns_[x];
        const auto [y0,y1,ty]=rows_[y];
        const auto* top=pixels_.data()+size_t(y0)*width_;
        const auto* bottom=pixels_.data()+size_t(y1)*width_;
        RGB result{};
        for(int k=0;k<3;++k)result[k]=(top[x0][k]*(1-tx)+top[x1][k]*tx)*(1-ty)+(bottom[x0][k]*(1-tx)+bottom[x1][k]*tx)*ty;
        return result;
    }
};
std::shared_ptr<const Raster> lensPreview(const Raster& source,const Raster& reduced,const Raster& filtered,const std::function<bool()>& cancelled){
    std::vector<Pixel> pixels(size_t(source.width)*source.height);
    auto delta=[&](int x,int y){auto a=reduced.pixel(x,y),b=filtered.pixel(x,y);return std::array<double,3>{double(b.r)-a.r,double(b.g)-a.g,double(b.b)-a.b};};
    const LensPreviewDelta grid(source.width,source.height,reduced.width,reduced.height,delta);
    for(int y=0;y<source.height;++y){if(cancelled&&cancelled())throw std::runtime_error("Adjustment cancelled");for(int x=0;x<source.width;++x){
        auto p=source.pixel(x,y);auto d=grid.pixel(x,y);
        auto channel=[&](int value,int k){return uint8_t(std::clamp(std::lround(value+d[k]),0L,long(p.a)));};
        pixels[size_t(y)*source.width+x]={channel(p.r,0),channel(p.g,1),channel(p.b,2),p.a};
    }}
    return Raster::fromRgba(source.width,source.height,reinterpret_cast<const uint8_t*>(pixels.data()),size_t(source.width)*4,cancelled);
}
std::shared_ptr<const DeepImage> lensPreview(const DeepImage& source,const DeepImage& reduced,const DeepImage& filtered,const std::function<bool()>& cancelled){
    auto out=makeDeep(source.width,source.height,source.format);
    auto delta=[&](int x,int y){auto a=reduced.pixel(x,y),b=filtered.pixel(x,y);return std::array<double,3>{double(b[0]-a[0]),double(b[1]-a[1]),double(b[2]-a[2])};};
    const LensPreviewDelta grid(source.width,source.height,reduced.width,reduced.height,delta);
    for(int y=0;y<source.height;++y){if(cancelled&&cancelled())throw std::runtime_error("Adjustment cancelled");for(int x=0;x<source.width;++x){
        auto p=source.pixel(x,y);auto d=grid.pixel(x,y);
        for(int k=0;k<3;++k)p[k]=float(std::clamp(double(p[k])+d[k],0.,double(p[3])));out->setPixel(x,y,p);
    }}return out;
}
uint8_t byte(double n){return uint8_t(std::clamp(std::lround(n),0L,255L));}
Pixel sampleCube(Pixel pixel,const std::vector<std::array<float,3>>&v){if(!pixel.a)return pixel;std::array<double,3> u{pixel.r*32./pixel.a,pixel.g*32./pixel.a,pixel.b*32./pixel.a};std::array<int,3> lower{};RGB f{};for(int c=0;c<3;++c){u[c]=std::clamp(u[c],0.,32.);lower[c]=std::min(31,int(u[c]));f[c]=u[c]-lower[c];}RGB out{};for(int b=0;b<2;++b)for(int g=0;g<2;++g)for(int r=0;r<2;++r){auto weight=(r?f[0]:1-f[0])*(g?f[1]:1-f[1])*(b?f[2]:1-f[2]);auto&entry=v[size_t((lower[2]+b)*33*33+(lower[1]+g)*33+lower[0]+r)];for(int c=0;c<3;++c)out[c]+=entry[c]*weight;}return {byte(out[0]*pixel.a),byte(out[1]*pixel.a),byte(out[2]*pixel.a),pixel.a};}
std::array<float,768> tables(const Settings&s){std::array<float,768> out{};for(int c=0;c<3;++c)for(int i=0;i<256;++i){double result;if(s.kind==1)result=s.levels[0].apply(s.levels[c+1].apply(i/255.));else if(s.kind==2)result=s.curves[0].value(s.curves[c+1].value(i))/255.;else{double encoded=i/255.,linear=encoded<=.04045?encoded/12.92:std::pow((encoded+.055)/1.055,2.4);linear=std::pow(std::max(0.,linear*std::pow(2.,s.exposure)+s.offset),1/s.gamma);result=linear<=.0031308?linear*12.92:1.055*std::pow(linear,1/2.4)-.055;}out[c*256+i]=float(std::clamp(result,0.,1.));}return out;}
std::array<uint8_t,768> gradient(const Settings&s){auto dark=s.reverse?s.highlights:s.shadows,light=s.reverse?s.shadows:s.highlights;std::array<uint8_t,768> out{};for(int i=0;i<256;++i)for(int c=0;c<3;++c)out[i*3+c]=byte((dark[c]+(light[c]-dark[c])*i/255.)*255);return out;}
}
std::string defaultAdjustmentJson(std::string_view kind){named(text(kind),kinds);QJsonObject range{{"black",0},{"white",255},{"gamma",1},{"outputBlack",0},{"outputWhite",255}};QJsonArray ls,cs;for(int i=0;i<4;++i){ls.append(range);cs.append(QJsonArray{QJsonObject{{"x",0},{"y",0}},QJsonObject{{"x",255},{"y",255}}});}QJsonObject o{{"kind",text(kind)},{"hue",0},{"saturation",0},{"lightness",0},{"colorize",false},{"levels",QJsonObject{{"channel","RGB"},{"ranges",ls}}},{"curves",QJsonObject{{"channel","RGB"},{"channels",cs}}}};if(kind=="Progressive Blur"||kind=="Bloom / Glow"||kind=="RGB Split")o["filterLayerSettings"]=QJsonObject{{"progressiveFrom",0},{"progressiveTo",20},{"progressiveRadial",false},{"progressiveInverted",false},{"bloomAmount",40},{"bloomRadius",24},{"rgbSplitDistance",10},{"rgbSplitBlur",0},{"rgbSplitAngle",0},{"rgbSplitRadial",false},{"rgbSplitReach",1000}};if(kind=="Vignette")o["filterLayerSettings"]=QJsonObject{{"vignette",QJsonObject{{"amount",35},{"color",QJsonObject{{"red",0},{"green",0},{"blue",0}}},{"midpoint",50},{"roundness",100},{"feather",60},{"highlights",25}}}};if(kind=="Tonal Contrast")o["filterLayerSettings"]=QJsonObject{{"tonal",QJsonObject{{"amount",50},{"radius",16},{"shadows",40},{"midtones",60},{"highlights",30}}}};if(kind=="Lens Effects")o["filterLayerSettings"]=QJsonObject{{"lens",QJsonObject{{"effect","Anamorphic Flare"},{"bokehShape","Round"},{"threshold",90},{"flareAmount",50},{"flareLength",60},{"flareBlue",80},{"starAmount",50},{"starLength",12},{"starAngle",15},{"starPoints",6},{"mistStrength",50},{"mistSize",30},{"prismAmount",80},{"prismCopies",5},{"prismOffset",6},{"prismAngle",180},{"prismBlur",0},{"prismBothWays",false},{"bokehRadius",2},{"bokehBrightness",50},{"bokehRing",100}}}};return QJsonDocument(o).toJson(QJsonDocument::Compact).toStdString();}
void validateAdjustmentJson(std::string_view json){settings(parse(json));}
bool hasLensAdjustment(const Document& document){
    for(const auto& layer:document.layers){
        if(layer.adjustmentJson.empty()||!layer.visible||layer.opacity==0||settings(parse(layer.adjustmentJson)).kind!=17)continue;
        auto parent=layer.parentId;
        while(!parent.empty()){
            auto ancestor=std::find_if(document.layers.begin(),document.layers.end(),[&](const Layer& candidate){return candidate.id==parent;});
            if(ancestor==document.layers.end()||!ancestor->visible||ancestor->opacity==0)break;
            parent=ancestor->parentId;
        }
        if(parent.empty())return true;
    }
    return false;
}
int adjustmentPadding(std::string_view json){const auto s=settings(parse(json));return s.kind==16?(s.identity()?0:int(std::ceil(s.tonal.radius*3+2))):s.kind==9?int(std::ceil(s.blurRadius*3+2)):s.kind==10?int(std::ceil(s.blurDistance+2)):s.kind==12?int(std::ceil(std::max(s.progressiveFrom,s.progressiveTo)*3+2)):s.kind==13?int(std::ceil(s.bloomRadius*3+2)):s.kind==14?int(std::ceil(s.rgbSplitDistance*5/3+s.rgbSplitBlur+2)):0;}
int adjustmentStackPadding(const Document& document){
    double margin=0;
    for(auto it=document.layers.rbegin();it!=document.layers.rend();++it){
        if(!it->visible||it->adjustmentJson.empty())continue;
        const auto s=settings(parse(it->adjustmentJson));
        if(s.kind==14&&s.rgbSplitRadial){
            const auto center=s.rgbSplitCenter.value_or(Point{document.width/2.,document.height/2.});
            const double extent=std::max({std::abs(center.x+margin),std::abs(document.width+margin-center.x),std::abs(center.y+margin),std::abs(document.height+margin-center.y)})/s.rgbSplitReach;
            margin+=std::ceil(extent*(s.rgbSplitDistance*5/3+s.rgbSplitBlur)+2);
        }else margin+=adjustmentPadding(it->adjustmentJson);
        if(!std::isfinite(margin)||margin>14000)throw std::runtime_error("Adjustment padding exceeds render budget");
    }
    return int(margin);
}
std::shared_ptr<const Raster> applyAdjustment(std::shared_ptr<const Raster> source,std::string_view json,const GrayRaster* selection,AdjustmentRegion region,const std::function<bool()>& cancelled){
 auto cancel=[&]{if(cancelled&&cancelled())throw std::runtime_error("Adjustment cancelled");};cancel();
 if(!source||source->width<1||source->height<1||source->width>30000||source->height>30000||uint64_t(source->width)*source->height>100000000)throw std::invalid_argument("Invalid source raster");
 auto expectedTiles=size_t((source->width+255)/256)*size_t((source->height+255)/256);if(source->tiles.size()!=expectedTiles||std::any_of(source->tiles.begin(),source->tiles.end(),[](const auto&t){return !t;}))throw std::invalid_argument("Invalid immutable tile storage");
 if(selection&&(selection->width!=source->width||selection->height!=source->height||selection->pixels.size()!=size_t(source->width)*source->height))throw std::invalid_argument("Selection must match source pixel grid");
 if(!std::isfinite(region.originX)||!std::isfinite(region.originY)||!std::isfinite(region.unitsPerPixel)||region.unitsPerPixel<=0)throw std::invalid_argument("Invalid adjustment region");auto s=settings(parse(json));
 if(s.identity()||(selection&&std::none_of(selection->pixels.begin(),selection->pixels.end(),[](auto v){return v!=0;})))return source;
 if(s.kind>=9){filters::Settings filter;filter.radius=s.blurRadius>0?s.blurRadius:1;filter.angle=s.blurAngle;filter.distance=s.blurDistance>0?s.blurDistance:1;filter.amount=s.noiseAmount>0?s.noiseAmount:.1;filter.gaussian=s.noiseGaussian;filter.monochromatic=s.noiseMono;
  filter.progressiveFrom=s.progressiveFrom;filter.progressiveTo=s.progressiveTo;filter.progressiveRadial=s.progressiveRadial;filter.progressiveInverted=s.progressiveInverted;
  filter.bloomGlow=s.kind==13;filter.bloomAmount=s.bloomAmount;filter.bloomRadius=s.bloomRadius;filter.bloom=s.bloomAmount;
  filter.rgbSplitDistance=s.rgbSplitDistance;filter.rgbSplitBlur=s.rgbSplitBlur;filter.rgbSplitAngle=s.rgbSplitAngle;filter.rgbSplitRadial=s.rgbSplitRadial;filter.rgbSplitReach=s.rgbSplitReach/region.unitsPerPixel;
  filter.rgbSplitOriginX=region.originX;filter.rgbSplitOriginY=region.originY;filter.rgbSplitUnitsPerPixel=region.unitsPerPixel;
  auto map=[&](Point p){return Point{(p.x-region.originX)/region.unitsPerPixel,(p.y-region.originY)/region.unitsPerPixel};};
  const double dw=region.documentWidth>0?region.documentWidth:source->width*region.unitsPerPixel,dh=region.documentHeight>0?region.documentHeight:source->height*region.unitsPerPixel;
  filter.progressiveStart=map(s.progressiveStart.value_or(Point{dw/2,0}));filter.progressiveEnd=map(s.progressiveEnd.value_or(Point{dw/2,dh}));
  filter.rgbSplitCenter=map(s.rgbSplitCenter.value_or(Point{dw/2,dh/2}));
  if(s.kind==15){filter.vignetteLayer=s.vignette;auto& v=*filter.vignetteLayer;v.frameX=-region.originX/region.unitsPerPixel;v.frameY=-region.originY/region.unitsPerPixel;v.frameWidth=dw/region.unitsPerPixel;v.frameHeight=dh/region.unitsPerPixel;}
  if(s.kind==16)filter.tonalLayer=s.tonal;if(s.kind==17)filter.lens=s.lens;
  auto kind=s.kind==17?filters::Kind::LensEffects:s.kind==16?filters::Kind::TonalContrast:s.kind==15?filters::Kind::Vignette:s.kind==9?filters::Kind::GaussianBlur:s.kind==10?filters::Kind::MotionBlur:s.kind==11?filters::Kind::AddNoise:s.kind==12?filters::Kind::ProgressiveBlur:s.kind==13?filters::Kind::Bloom:filters::Kind::RgbSplit;
  filters::Limits limits;limits.cancelled=cancelled;
  if(s.kind==17&&region.previewMaxEdge>0){
      const int longest=std::max(source->width,source->height);
      if(longest>region.previewMaxEdge){
          const int width=std::max(1,int(std::lround(double(source->width)*region.previewMaxEdge/longest)));
          const int height=std::max(1,int(std::lround(double(source->height)*region.previewMaxEdge/longest)));
          auto reduced=resizeNearest(*source,width,height,cancelled);
          std::optional<GrayRaster> reducedSelection;const GrayRaster* mask=selection;
          if(selection){reducedSelection=resizeGrayNearest(*selection,width,height,cancelled);mask=&*reducedSelection;}
          auto filtered=filters::runPixels(kind,*reduced,filter,1/region.unitsPerPixel,s.seed,mask,limits);
          return lensPreview(*source,*reduced,*filtered,cancelled);
      }
  }
  return filters::runPixels(kind,*source,filter,(s.kind>=12?1/region.unitsPerPixel:1),s.seed,selection,limits);}
 std::vector<std::array<float,3>> colors;if(s.kind==0)colors=cube(s);std::array<float,768> lut{};if(s.kind>=1&&s.kind<=3)lut=tables(s);std::array<uint8_t,768> gradientLut{};if(s.kind==4)gradientLut=gradient(s);
 cancel();auto result=std::make_shared<Raster>(*source);bool any=false;const int columns=(source->width+255)/256;
 for(int ty=0;ty<source->height;ty+=256)for(int tx=0;tx<source->width;tx+=256){cancel();int w=std::min(256,source->width-tx),h=std::min(256,source->height-ty);size_t index=size_t(ty/256)*columns+tx/256;auto original=source->tiles[index];
    if(selection){bool selected=false;for(int y=0;y<h&&!selected;++y)for(int x=0;x<w;++x)if(selection->pixels[size_t(ty+y)*source->width+tx+x]){selected=true;break;}if(!selected)continue;}
    auto tile=std::make_shared<Raster::Tile>(*original);auto bytes=std::span<uint8_t>(reinterpret_cast<uint8_t*>(tile->pixels.data()),sizeof(Raster::Tile));graphics::Rgba8View view{bytes,uint32_t(w),uint32_t(h),256*4};
    if(s.kind==0){for(int y=0;y<h;++y)for(int x=0;x<w;++x)tile->pixels[size_t(y)*256+x]=sampleCube(tile->pixels[size_t(y)*256+x],colors);}
    else if(s.kind<=3){
        graphics::applyLevels(view,lut);
    }else if(s.kind==4)graphics::gradientMap(view,gradientLut);
    else if(s.kind==5)graphics::grain(view,s.amount,s.size,s.roughness,s.seed,region.originX+tx*region.unitsPerPixel,region.originY+ty*region.unitsPerPixel,region.unitsPerPixel);
    else if(s.kind==6){for(int y=0;y<h;++y)for(int x=0;x<w;++x){auto& p=tile->pixels[size_t(y)*256+x];if(p.a)p={uint8_t(p.a-p.r),uint8_t(p.a-p.g),uint8_t(p.a-p.b),p.a};}}
    else if(s.kind==7||s.kind==8){auto rw=s.bwReds+s.bwYellows*.5+s.bwMagentas*.5,gw=s.bwGreens+s.bwYellows*.5+s.bwCyans*.5,bw=s.bwBlues+s.bwCyans*.5+s.bwMagentas*.5;double mix=rw+gw+bw;if(!(std::abs(mix)>1e-6))mix=1;for(int y=0;y<h;++y)for(int x=0;x<w;++x){auto& p=tile->pixels[size_t(y)*256+x];if(!p.a)continue;double r=p.r/double(p.a),g=p.g/double(p.a),b=p.b/double(p.a);
        if(s.kind==7){double gray=std::clamp((r*rw+g*gw+b*bw)/mix,0.,1.);p={byte(gray*p.a),byte(gray*p.a),byte(gray*p.a),p.a};}
        else{double l=.3*r+.59*g+.11*b;auto tone=[&](double shadow,double mid,double high){double sw=std::clamp(1-l*2,0.,1.),hw=std::clamp(l*2-1,0.,1.),mw=std::clamp(1-sw-hw,0.,1.);return (shadow*sw+mid*mw+high*hw)/100;};r=std::clamp(r+tone(s.shadowCR,s.midCR,s.highCR),0.,1.);g=std::clamp(g+tone(s.shadowMG,s.midMG,s.highMG),0.,1.);b=std::clamp(b-tone(s.shadowYB,s.midYB,s.highYB),0.,1.);p={byte(r*p.a),byte(g*p.a),byte(b*p.a),p.a};}}}
    else bad();
    if(selection)for(int y=0;y<h;++y)for(int x=0;x<w;++x){auto i=size_t(y)*256+x;auto mask=selection->pixels[size_t(ty+y)*source->width+tx+x];auto a=original->pixels[i],b=tile->pixels[i];auto mix=[&](uint8_t av,uint8_t bv){return uint8_t((int(av)*(255-mask)+int(bv)*mask+127)/255);};tile->pixels[i]={mix(a.r,b.r),mix(a.g,b.g),mix(a.b,b.b),mix(a.a,b.a)};}
    if(tile->pixels!=original->pixels){result->tiles[index]=tile;any=true;}
 }cancel();return any?std::shared_ptr<const Raster>(result):source;
}
std::shared_ptr<const DeepImage> applyAdjustment(std::shared_ptr<const DeepImage> source,std::string_view json,const GrayRaster* selection,AdjustmentRegion region,const std::function<bool()>& cancelled){
    auto cancel=[&]{if(cancelled&&cancelled())throw std::runtime_error("HDR adjustment cancelled");};cancel();
    if(!source||!source->valid())throw std::runtime_error("Invalid deep adjustment source");
    if(selection&&(selection->width!=source->width||selection->height!=source->height||!selection->validStorage()))throw std::runtime_error("HDR selection must match source grid");
    if(!std::isfinite(region.originX)||!std::isfinite(region.originY)||!std::isfinite(region.unitsPerPixel)||region.unitsPerPixel<=0)throw std::invalid_argument("Invalid adjustment region");
    auto s=settings(parse(json));if(s.identity())return source;
    if(s.kind>=9){filters::Settings filter;filter.radius=s.blurRadius;filter.angle=s.blurAngle;filter.distance=s.blurDistance;filter.amount=s.noiseAmount;filter.gaussian=s.noiseGaussian;filter.monochromatic=s.noiseMono;
        filter.progressiveFrom=s.progressiveFrom;filter.progressiveTo=s.progressiveTo;filter.progressiveRadial=s.progressiveRadial;filter.progressiveInverted=s.progressiveInverted;
        filter.bloomGlow=s.kind==13;filter.bloomAmount=s.bloomAmount;filter.bloomRadius=s.bloomRadius;
        filter.rgbSplitDistance=s.rgbSplitDistance;filter.rgbSplitBlur=s.rgbSplitBlur;filter.rgbSplitAngle=s.rgbSplitAngle;filter.rgbSplitRadial=s.rgbSplitRadial;filter.rgbSplitReach=s.rgbSplitReach/region.unitsPerPixel;
        filter.rgbSplitOriginX=region.originX;filter.rgbSplitOriginY=region.originY;filter.rgbSplitUnitsPerPixel=region.unitsPerPixel;
        auto map=[&](Point p){return Point{(p.x-region.originX)/region.unitsPerPixel,(p.y-region.originY)/region.unitsPerPixel};};
        const double dw=region.documentWidth>0?region.documentWidth:source->width*region.unitsPerPixel,dh=region.documentHeight>0?region.documentHeight:source->height*region.unitsPerPixel;
        filter.progressiveStart=map(s.progressiveStart.value_or(Point{dw/2,0}));filter.progressiveEnd=map(s.progressiveEnd.value_or(Point{dw/2,dh}));
        filter.rgbSplitCenter=map(s.rgbSplitCenter.value_or(Point{dw/2,dh/2}));
        if(s.kind==15){filter.vignetteLayer=s.vignette;auto& v=*filter.vignetteLayer;v.frameX=-region.originX/region.unitsPerPixel;v.frameY=-region.originY/region.unitsPerPixel;v.frameWidth=dw/region.unitsPerPixel;v.frameHeight=dh/region.unitsPerPixel;}
  if(s.kind==16)filter.tonalLayer=s.tonal;if(s.kind==17)filter.lens=s.lens;
  auto kind=s.kind==17?filters::Kind::LensEffects:s.kind==16?filters::Kind::TonalContrast:s.kind==15?filters::Kind::Vignette:s.kind==9?filters::Kind::GaussianBlur:s.kind==10?filters::Kind::MotionBlur:s.kind==11?filters::Kind::AddNoise:s.kind==12?filters::Kind::ProgressiveBlur:s.kind==13?filters::Kind::Bloom:filters::Kind::RgbSplit;
        if(!filters::supportsHdr(kind)&&source->isHdr())throw std::runtime_error("Use Layer > Convert HDR to 16-Bit before this spatial filter");
        filters::Limits limits;limits.cancelled=cancelled;
        if(s.kind==17&&region.previewMaxEdge>0){
            const int longest=std::max(source->width,source->height);
            if(longest>region.previewMaxEdge){
                const int width=std::max(1,int(std::lround(double(source->width)*region.previewMaxEdge/longest)));
                const int height=std::max(1,int(std::lround(double(source->height)*region.previewMaxEdge/longest)));
                auto reduced=resizeDeepNearest(*source,width,height,cancelled);
                std::optional<GrayRaster> reducedSelection;const GrayRaster* mask=selection;
                if(selection){reducedSelection=resizeGrayNearest(*selection,width,height,cancelled);mask=&*reducedSelection;}
                auto filtered=filters::runDeepPixels(kind,*reduced,filter,1/region.unitsPerPixel,s.seed,mask,limits);
                return lensPreview(*source,*reduced,*filtered,cancelled);
            }
        }
        return filters::runDeepPixels(kind,*source,filter,(s.kind>=12?1/region.unitsPerPixel:1),s.seed,selection,limits);}
    // HDR highlights stay on the normalized 8-bit operators. Unorm16 keeps the
    // retained samples and evaluates the same formulas in floating light.
    if(source->isHdr()&&s.kind!=1&&s.kind!=2&&s.kind!=3){
        auto normal=normalizeDeep(*source,cancelled);auto changed=applyAdjustment(normal.raster,json,nullptr,region,cancelled);
        if(changed==normal.raster)return source;
        auto result=restoreDeep(*changed,normal.scales,cancelled);
        if(!selection)return result;
        auto mixed=std::make_shared<DeepImage>(*result);for(int y=0;y<source->height;++y){cancel();for(int x=0;x<source->width;++x){const auto old=source->pixel(x,y);auto p=result->pixel(x,y);float amount=selection->pixel(x,y)/255.f;for(int c=0;c<4;++c)p[c]=old[c]+(p[c]-old[c])*amount;mixed->setPixel(x,y,p);}}return mixed;
    }
    std::vector<std::array<float,3>> colors;if(s.kind==0)colors=cube(s);
    auto output=std::make_shared<DeepImage>(*source);bool changed=false;
    const double rw=s.bwReds+s.bwYellows*.5+s.bwMagentas*.5,gw=s.bwGreens+s.bwYellows*.5+s.bwCyans*.5,bw=s.bwBlues+s.bwCyans*.5+s.bwMagentas*.5;
    double bwMix=rw+gw+bw;if(!(std::abs(bwMix)>1e-6))bwMix=1;
    for(int y=0;y<source->height;++y){cancel();for(int x=0;x<source->width;++x){auto p=source->pixel(x,y),out=p;if(!(p[3]>0))continue;const double amount=selection?selection->pixel(x,y)/255.:1.;if(amount==0)continue;
        const double peak=std::max({1.,double(p[0]/p[3]),double(p[1]/p[3]),double(p[2]/p[3])});
        RGB rgb{p[0]/p[3],p[1]/p[3],p[2]/p[3]};
        if(s.kind==0){
            std::array<double,3> u{std::clamp(rgb[0]*32,0.,32.),std::clamp(rgb[1]*32,0.,32.),std::clamp(rgb[2]*32,0.,32.)};
            std::array<int,3> lower{};RGB f{};for(int c=0;c<3;++c){lower[c]=std::min(31,int(u[c]));f[c]=u[c]-lower[c];}
            RGB mapped{};for(int b=0;b<2;++b)for(int g=0;g<2;++g)for(int r=0;r<2;++r){auto weight=(r?f[0]:1-f[0])*(g?f[1]:1-f[1])*(b?f[2]:1-f[2]);auto&entry=colors[size_t((lower[2]+b)*33*33+(lower[1]+g)*33+lower[0]+r)];for(int c=0;c<3;++c)mapped[c]+=entry[c]*weight;}
            rgb=mapped;
        }else if(s.kind==4){
            auto dark=s.reverse?s.highlights:s.shadows,light=s.reverse?s.shadows:s.highlights;
            const double yv=.2126*rgb[0]+.7152*rgb[1]+.0722*rgb[2];
            for(int c=0;c<3;++c)rgb[c]=dark[c]+(light[c]-dark[c])*yv;
        }else if(s.kind==5){
            const double px=region.originX+(x+.5)*region.unitsPerPixel,py=region.originY+(y+.5)*region.unitsPerPixel;
            const auto cell=[&](double axis){return std::llround(std::floor(axis/std::max(.5,s.size)));};
            uint32_t h=s.seed^uint32_t(cell(px)*747796405u)^uint32_t(cell(py)*2891336453u);
            h=h*1664525u+1013904223u;const double n=((h>>8)&255)/255.;
            const double grain=(n-.5)*s.amount/50.*(s.roughness/100.*.5+.5);
            for(int c=0;c<3;++c)rgb[c]=std::clamp(rgb[c]+grain,0.,1.);
        }else if(s.kind==7){
            const double gray=std::clamp((rgb[0]*rw+rgb[1]*gw+rgb[2]*bw)/bwMix,0.,1.);rgb={gray,gray,gray};
        }else if(s.kind==8){
            const double l=.3*rgb[0]+.59*rgb[1]+.11*rgb[2];
            auto tone=[&](double shadow,double mid,double high){double sw=std::clamp(1-l*2,0.,1.),hw=std::clamp(l*2-1,0.,1.),mw=std::clamp(1-sw-hw,0.,1.);return (shadow*sw+mid*mw+high*hw)/100;};
            rgb[0]=std::clamp(rgb[0]+tone(s.shadowCR,s.midCR,s.highCR),0.,1.);
            rgb[1]=std::clamp(rgb[1]+tone(s.shadowMG,s.midMG,s.highMG),0.,1.);
            rgb[2]=std::clamp(rgb[2]-tone(s.shadowYB,s.midYB,s.highYB),0.,1.);
        }else{
            for(int c=0;c<3;++c){const double straight=rgb[c];double value;
                if(s.kind==6)value=1-straight;
                else if(s.kind==3)value=linearToSrgb(std::pow(std::max(0.,srgbToLinear(straight)*std::pow(2.,s.exposure)+s.offset),1/s.gamma));
                else {value=std::clamp(straight/peak,0.,1.);value=s.kind==1?s.levels[0].apply(s.levels[c+1].apply(value)):s.curves[0].value(s.curves[c+1].value(value*255))/255.;value*=peak;}
                rgb[c]=value;
            }
        }
        for(int c=0;c<3;++c)out[c]=float(p[c]+(rgb[c]*p[3]-p[c])*amount);
        if(out!=p){output->setPixel(x,y,out);changed=true;}
    }}cancel();return changed?std::shared_ptr<const DeepImage>(output):source;
}

}
