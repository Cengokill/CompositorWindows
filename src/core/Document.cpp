#include "Document.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <unordered_set>
#include <unordered_map>
#include <objbase.h>
#include <cstdio>
#include <atomic>
#include <set>

namespace compositor {
static std::atomic<uint64_t> materializations{0};
uint64_t Raster::materializationCount(){return materializations.load();}
void Raster::resetMaterializationCount(){materializations.store(0);}
namespace {
void sizeCheck(int w,int h){if(w<1||h<1||w>30000||h>30000||uint64_t(w)*uint64_t(h)>100000000)throw std::runtime_error("Raster dimensions exceed supported budget");}
uint8_t byte(double n){return uint8_t(std::clamp(std::lround(n),0L,255L));}
using Color=std::array<double,3>;
double lum(Color c){return .3*c[0]+.59*c[1]+.11*c[2];}
double sat(Color c){return *std::max_element(c.begin(),c.end())-*std::min_element(c.begin(),c.end());}
Color clip(Color c){double l=lum(c),n=*std::min_element(c.begin(),c.end()),x=*std::max_element(c.begin(),c.end());if(n<0)for(auto&v:c)v=l+(v-l)*l/(l-n);if(x>1)for(auto&v:c)v=l+(v-l)*(1-l)/(x-l);return c;}
Color setLum(Color c,double l){double d=l-lum(c);for(auto&v:c)v+=d;return clip(c);}
Color setSat(Color c,double s){std::array<int,3> i{0,1,2};std::sort(i.begin(),i.end(),[&](int a,int b){return c[a]<c[b];});double lo=c[i[0]],hi=c[i[2]];c[i[1]]=hi>lo?(c[i[1]]-lo)*s/(hi-lo):0;c[i[2]]=hi>lo?s:0;c[i[0]]=0;return c;}
double separable(double b,double s,Blend mode){switch(mode){case Blend::Multiply:return b*s;case Blend::Screen:return b+s-b*s;case Blend::Overlay:return b<=.5?2*b*s:1-2*(1-b)*(1-s);case Blend::Darken:return std::min(b,s);case Blend::Lighten:return std::max(b,s);case Blend::Difference:return std::abs(b-s);case Blend::ColorDodge:return b==0?0:s>=1?1:std::min(1.,b/(1-s));case Blend::ColorBurn:return b>=1?1:s==0?0:1-std::min(1.,(1-b)/s);default:return s;}}
Pixel scale(Pixel p,double a){return {byte(p.r*a),byte(p.g*a),byte(p.b*a),byte(p.a*a)};}
Pixel sample(const Raster& r,Point u,Transform::Sampling sampling){double x=u.x*r.width,y=u.y*r.height;if(x<0||y<0||x>=r.width||y>=r.height)return {};if(sampling==Transform::Sampling::Nearest)return r.pixel(int(x),int(y));x-=.5;y-=.5;int ix=int(std::floor(x)),iy=int(std::floor(y));double fx=x-ix,fy=y-iy;auto get=[&](int a,int b){return r.pixel(std::clamp(a,0,r.width-1),std::clamp(b,0,r.height-1));};auto a=get(ix,iy),b=get(ix+1,iy),c=get(ix,iy+1),d=get(ix+1,iy+1);auto mix=[&](uint8_t Pixel::*m){return byte((1-fy)*((1-fx)*(a.*m)+fx*(b.*m))+fy*((1-fx)*(c.*m)+fx*(d.*m)));};return {mix(&Pixel::r),mix(&Pixel::g),mix(&Pixel::b),mix(&Pixel::a)};}
double maskCoverage(const Layer& l,Point p){if(!l.mask||!l.mask->enabled||!l.mask->raster)return 1;const auto& r=*l.mask->raster;if(r.width==1&&r.height==1)return r.pixels[0]/255.;auto u=l.mask->placement.value_or(l.transform).toUnit(p);return r.pixel(int(std::floor(u.x*r.width)),int(std::floor(u.y*r.height)))/255.;}
}
bool Transform::valid() const {return std::isfinite(x)&&std::isfinite(y)&&std::isfinite(width)&&std::isfinite(height)&&std::isfinite(rotation)&&std::abs(x)<=1000000&&std::abs(y)<=1000000&&width>=1&&width<=300000&&height>=1&&height<=300000;}
Point Transform::fromUnit(Point p)const{double a=std::remainder(rotation,360.)*std::numbers::pi/180,c=std::cos(a),s=std::sin(a);double px=(p.x-.5)*width*(flipX?-1:1),py=(p.y-.5)*height*(flipY?-1:1);return {x+width/2+px*c-py*s,y+height/2+px*s+py*c};}
Point Transform::toUnit(Point p)const{double a=std::remainder(rotation,360.)*std::numbers::pi/180,c=std::cos(a),s=std::sin(a),px=p.x-x-width/2,py=p.y-y-height/2;return {.5+(px*c+py*s)/width*(flipX?-1:1),.5+(-px*s+py*c)/height*(flipY?-1:1)};}
std::string newId(){GUID g{};if(FAILED(CoCreateGuid(&g)))throw std::runtime_error("UUID generation failed");char b[37];std::snprintf(b,sizeof b,"%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",g.Data1,g.Data2,g.Data3,g.Data4[0],g.Data4[1],g.Data4[2],g.Data4[3],g.Data4[4],g.Data4[5],g.Data4[6],g.Data4[7]);return b;}
std::shared_ptr<const Raster> Raster::filled(int w,int h,Pixel value){sizeCheck(w,h);value.r=std::min(value.r,value.a);value.g=std::min(value.g,value.a);value.b=std::min(value.b,value.a);auto r=std::make_shared<Raster>();r->width=w;r->height=h;auto tile=std::make_shared<Tile>();tile->pixels.fill(value);r->tiles.resize(size_t((w+255)/256)*((h+255)/256),tile);return r;}
std::shared_ptr<const Raster> Raster::fromRgba(int w,int h,const uint8_t*data,size_t stride){sizeCheck(w,h);if(!data||stride<size_t(w)*4||stride%4!=0)throw std::runtime_error("Invalid RGBA buffer");auto r=filled(w,h);return r->replacing(0,0,w,h,reinterpret_cast<const Pixel*>(data),stride/4);}
Pixel Raster::pixel(int x,int y)const{if(x<0||y<0||x>=width||y>=height)return {};return tiles[size_t(y/256)*((width+255)/256)+x/256]->pixels[size_t(y%256)*256+x%256];}
std::shared_ptr<const Raster> Raster::replacing(int x,int y,int w,int h,const Pixel* src,size_t stride)const{if(x<0||y<0||w<0||h<0||w>width-x||h>height-y||stride<size_t(w)||(!src&&w*h))throw std::runtime_error("Invalid raster patch");auto out=std::make_shared<Raster>(*this);std::unordered_map<size_t,std::shared_ptr<Tile>> changed;for(int py=0;py<h;++py)for(int px=0;px<w;++px){int dx=x+px,dy=y+py;size_t idx=size_t(dy/256)*((width+255)/256)+dx/256;auto& tile=changed[idx];if(!tile){tile=std::make_shared<Tile>(*tiles[idx]);out->tiles[idx]=tile;}auto p=src[size_t(py)*stride+px];p.r=std::min(p.r,p.a);p.g=std::min(p.g,p.a);p.b=std::min(p.b,p.a);tile->pixels[size_t(dy%256)*256+dx%256]=p;}return out;}
std::vector<uint8_t> Raster::rgba()const{++materializations;std::vector<uint8_t> bytes(size_t(width)*height*4);for(int y=0;y<height;++y)for(int x=0;x<width;++x){auto p=pixel(x,y);auto i=(size_t(y)*width+x)*4;bytes[i]=p.r;bytes[i+1]=p.g;bytes[i+2]=p.b;bytes[i+3]=p.a;}return bytes;}
uint8_t GrayRaster::pixel(int x,int y,uint8_t exterior)const{return x<0||y<0||x>=width||y>=height?exterior:pixels[size_t(y)*width+x];}
void validateDocument(const Document& d) {
    if (d.width < 1 || d.height < 1 || d.width > 30000 || d.height > 30000 ||
        d.layers.size() > 10000 || !std::isfinite(d.resolution) || d.resolution < 1 || d.resolution > 9600)
        throw std::runtime_error("Invalid document dimensions or resolution");
    std::unordered_map<std::string, const Layer*> layers;
    uint64_t imagePixels = 0, maskPixels = 0;
    for (const auto& layer : d.layers) {
        if (layer.id.empty() || !layers.emplace(layer.id, &layer).second || layer.name.empty() ||
            layer.name.size() > 16384 || !layer.transform.valid() || !std::isfinite(layer.opacity) ||
            layer.opacity < 0 || layer.opacity > 1 || int(layer.blend) < 0 || int(layer.blend) > 12 ||
            (layer.group && (layer.opacity != 1 || layer.blend != Blend::Normal || layer.raster)) ||
            (!layer.adjustmentJson.empty() && (layer.group || layer.raster)))
            throw std::runtime_error("Invalid layer metadata");
        if (layer.raster) {
            sizeCheck(layer.raster->width, layer.raster->height);
            if(layer.raster->tiles.size()!=size_t((layer.raster->width+255)/256)*((layer.raster->height+255)/256) ||
               std::any_of(layer.raster->tiles.begin(),layer.raster->tiles.end(),[](const auto&tile){return !tile;}))
                throw std::runtime_error("Invalid raster tile storage");
            imagePixels += uint64_t(layer.raster->width) * layer.raster->height;
        }
        if (layer.mask) {
            if (!layer.mask->raster) throw std::runtime_error("Missing mask coverage");
            const auto& mask = *layer.mask->raster;
            sizeCheck(mask.width, mask.height);
            maskPixels += uint64_t(mask.width) * mask.height;
            if (mask.pixels.size() != size_t(mask.width) * mask.height ||
                (layer.mask->placement && !layer.mask->placement->valid()))
                throw std::runtime_error("Invalid mask");
        }
    }
    if (imagePixels > 100000000 || maskPixels > 100000000)
        throw std::runtime_error("Project pixel budget exceeded");
    if(d.selection && d.selection->coverage) {
        const auto& coverage=*d.selection->coverage;
        if(coverage.width!=d.width || coverage.height!=d.height || coverage.pixels.size()!=size_t(d.width)*d.height)
            throw std::runtime_error("Invalid selection coverage");
    }
    for (const auto& layer : d.layers) {
        std::unordered_set<std::string> seen{layer.id};
        const Layer* node = &layer;
        while (!node->parentId.empty()) {
            auto parent = layers.find(node->parentId);
            if (parent == layers.end() || !parent->second->group || seen.size() > 64 ||
                !seen.insert(node->parentId).second)
                throw std::runtime_error("Invalid folder hierarchy");
            node = parent->second;
        }
        if (layer.group && seen.size() > 64) throw std::runtime_error("Folder nesting too deep");
        seen.clear(); node = &layer;
        for (;;) {
            if (seen.size() >= 256 || !seen.insert(node->id).second)
                throw std::runtime_error("Invalid clipping graph");
            if (node->maskSourceId.empty()) break;
            auto source = layers.find(node->maskSourceId);
            if (node->group || source == layers.end() || source->second->group ||
                !source->second->adjustmentJson.empty())
                throw std::runtime_error("Invalid clipping source");
            node = source->second;
        }
    }
}
Pixel blendPixel(Pixel dst,Pixel src,Blend mode){double da=dst.a/255.,sa=src.a/255.;if(sa==0)return dst;if(da==0)return src;Color b{dst.r/(255.*da),dst.g/(255.*da),dst.b/(255.*da)},s{src.r/(255.*sa),src.g/(255.*sa),src.b/(255.*sa)},v{};switch(mode){case Blend::Hue:v=setLum(setSat(s,sat(b)),lum(b));break;case Blend::Saturation:v=setLum(setSat(b,sat(s)),lum(b));break;case Blend::Color:v=setLum(s,lum(b));break;case Blend::Luminosity:v=setLum(b,lum(s));break;default:for(int i=0;i<3;++i)v[i]=separable(b[i],s[i],mode);}Pixel out;out.a=byte((sa+da-sa*da)*255);out.r=std::min(out.a,byte(255*(sa*(1-da)*s[0]+da*(1-sa)*b[0]+sa*da*v[0])));out.g=std::min(out.a,byte(255*(sa*(1-da)*s[1]+da*(1-sa)*b[1]+sa*da*v[1])));out.b=std::min(out.a,byte(255*(sa*(1-da)*s[2]+da*(1-sa)*b[2]+sa*da*v[2])));return out;}

void History::begin(std::string name,const std::optional<Document>&d,const std::string&a){if(depth_++==0){pending_=Snapshot{d,a,revision_};pendingName_=std::move(name);}}
void History::end(const std::optional<Document>&d,const std::string&a){if(depth_==0||--depth_>0)return;auto before=std::move(*pending_);pending_.reset();if(before.document==d)return;revision_=nextRevision_++;past_.push_back({pendingName_,std::move(before),{d,a,revision_}});future_.clear();trim(d);}
std::optional<Snapshot> History::cancel(){auto s=pending_;pending_.reset();depth_=0;return s;}
std::optional<Snapshot> History::undo(){if(!canUndo())return {};auto e=std::move(past_.back());past_.pop_back();revision_=e.before.revision;auto s=e.before;future_.push_back(std::move(e));trim(s.document);return s;}
std::optional<Snapshot> History::redo(){if(!canRedo())return {};auto e=std::move(future_.back());future_.pop_back();revision_=e.after.revision;auto s=e.after;past_.push_back(std::move(e));trim(s.document);return s;}
void History::reset(){past_.clear();future_.clear();pending_.reset();depth_=0;revision_=nextRevision_++;savedRevision_=revision_;}
size_t History::retainedBytes(const std::optional<Document>& doc) const {
    std::unordered_set<const void*> seen;
    auto scan=[&](const std::optional<Document>& document,bool count){
        size_t bytes=0;
        if(!document)return bytes;
        for(const auto&layer:document->layers){
            if(layer.raster)for(const auto&tile:layer.raster->tiles)
                if(seen.insert(tile.get()).second&&count)bytes+=sizeof(Raster::Tile);
            if(layer.mask&&layer.mask->raster&&seen.insert(layer.mask->raster.get()).second&&count)
                bytes+=layer.mask->raster->pixels.size();
        }
        if(document->selection&&document->selection->coverage&&seen.insert(document->selection->coverage.get()).second&&count)
            bytes+=document->selection->coverage->pixels.size();
        return bytes;
    };
    scan(doc,false);size_t bytes=0;
    for(const auto&e:past_)bytes+=scan(e.before.document,true)+scan(e.after.document,true);
    for(const auto&e:future_)bytes+=scan(e.before.document,true)+scan(e.after.document,true);
    return bytes;
}
void History::trim(const std::optional<Document>&d){while(past_.size()+future_.size()>entryLimit||retainedBytes(d)>byteLimit){if(!past_.empty())past_.erase(past_.begin());else if(!future_.empty())future_.erase(future_.begin());else break;}}
}
