#include "effects/Adjustments.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string_view>
#include <vector>
using namespace compositor;
namespace compositor::effects_reference {
std::shared_ptr<const Raster> applyAdjustment(std::shared_ptr<const Raster>,std::string_view,const GrayRaster*,effects::AdjustmentRegion);
}
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct CallbackFailure{};
std::vector<std::string> settings(){
    std::vector<std::string> result;
    for(const char* kind:{"Hue/Saturation","Levels","Curves","Exposure","Gradient Map","Grain"}){
        auto object=QJsonDocument::fromJson(QByteArray::fromStdString(effects::defaultAdjustmentJson(kind))).object();
        const QString name=kind;
        if(name=="Hue/Saturation")object["hue"]=120;
        else if(name=="Levels"){auto levels=object["levels"].toObject();auto ranges=levels["ranges"].toArray();auto range=ranges[0].toObject();range["gamma"]=1.5;ranges[0]=range;levels["ranges"]=ranges;object["levels"]=levels;}
        else if(name=="Curves"){auto curves=object["curves"].toObject();auto channels=curves["channels"].toArray();channels[0]=QJsonArray{QJsonObject{{"x",0},{"y",255}},QJsonObject{{"x",255},{"y",0}}};curves["channels"]=channels;object["curves"]=curves;}
        else if(name=="Exposure")object["exposureSettings"]=QJsonObject{{"exposure",1},{"offset",0},{"gamma",1}};
        else if(name=="Gradient Map")object["gradientMapSettings"]=QJsonObject{{"shadows",QJsonObject{{"red",.1},{"green",.2},{"blue",.3}}},{"highlights",QJsonObject{{"red",.9},{"green",.8},{"blue",.7}}},{"reversed",true}};
        else object["grainSettings"]=QJsonObject{{"amount",70},{"size",1.5},{"roughness",50},{"seed",77}};
        result.push_back(QJsonDocument(object).toJson(QJsonDocument::Compact).toStdString());
    }return result;
}
std::shared_ptr<const Raster> source(int width,int height){
    std::vector<Pixel> pixels(size_t(width)*height);uint32_t random=77;
    for(size_t i=0;i<pixels.size();++i){random=random*1664525u+1013904223u;const auto a=uint8_t(i%5==0?0:i%5==1?255:random>>24);pixels[i]={uint8_t(unsigned(uint8_t(random))*a/255),uint8_t(unsigned(uint8_t(random>>8))*a/255),uint8_t(unsigned(uint8_t(random>>16))*a/255),a};}
    return Raster::fromRgba(width,height,reinterpret_cast<const uint8_t*>(pixels.data()),width*4);
}
std::shared_ptr<const Raster> levelsGammaExpected(std::shared_ptr<const Raster> input,const GrayRaster* selection){
    std::array<float,256> gamma{};
    for(size_t i=0;i<gamma.size();++i)gamma[i]=float(std::pow(double(i)/255.,1/1.5));
    auto pixels=input->rgba();const auto before=pixels;
    for(size_t pixel=0;pixel<pixels.size()/4;++pixel){
        const auto alpha=pixels[pixel*4+3];if(!alpha)continue;
        const unsigned coverage=selection?selection->pixels[pixel]:255;
        for(size_t channel=0;channel<3;++channel){
            auto& value=pixels[pixel*4+channel];const auto original=value;
            const float coordinate=float(original)*255.f/float(alpha);
            const auto lower=size_t(coordinate),upper=std::min(lower+1,gamma.size()-1);
            const float mapped=gamma[lower]+(gamma[upper]-gamma[lower])*(coordinate-float(lower));
            const auto adjusted=unsigned(std::clamp(std::lround(mapped*alpha),0L,long(alpha)));
            value=uint8_t((unsigned(original)*(255-coverage)+adjusted*coverage+127)/255);
        }
    }
    if(pixels==before)return input;
    return Raster::fromRgba(input->width,input->height,pixels.data(),input->width*4);
}
}
int main(int argc,char** argv){try{
    const bool correctLevels=argc==2&&std::string_view(argv[1])=="--correct-levels-oracle";
    require(argc==1||correctLevels,"Unknown cancellation contract argument");
    int exact=0,stops=0,exceptions=0;const auto configurations=settings();
    for(auto dimensions:std::array<std::array<int,2>,3>{{{1,1},{17,9},{257,261}}}){
        const int width=dimensions[0],height=dimensions[1];const auto input=source(width,height);const auto before=input->rgba();
        GrayRaster coverage{width,height,std::vector<uint8_t>(size_t(width)*height)};
        for(int selection=0;selection<3;++selection){for(size_t i=0;i<coverage.pixels.size();++i)coverage.pixels[i]=selection==2?0:uint8_t(i%256);
            for(const auto& json:configurations){const auto mask=selection?&coverage:nullptr;const effects::AdjustmentRegion region{11.25,-17.5,.75};
                int polls=0;const auto actual=effects::applyAdjustment(input,json,mask,region,[&]{++polls;return false;});
                const bool levels=QJsonDocument::fromJson(QByteArray::fromStdString(json)).object()["kind"].toString()=="Levels";
                const auto expected=correctLevels&&levels?levelsGammaExpected(input,mask):effects_reference::applyAdjustment(input,json,mask,region);
                require(polls>0&&actual->rgba()==expected->rgba(),"Cancellation adapter changed exact adjustment pixels");
                require((actual==input)==(expected==input),"Identity/no-op sharing changed");
                for(size_t i=0;i<actual->tiles.size();++i)require(actual->tiles[i]->pixels==expected->tiles[i]->pixels,"Unchanged padding or tile bytes changed");
                require(input->rgba()==before,"Input source changed");++exact;
            }
        }
    }
    const auto input=source(257,261);const auto before=input->rgba();
    for(const auto& json:configurations)for(int checkpoint:{1,2,3,4,6,7}){
        int polls=0;bool stopped=false;try{effects::applyAdjustment(input,json,nullptr,{},[&]{return ++polls==checkpoint;});}catch(const std::runtime_error& e){stopped=std::string(e.what())=="Adjustment cancelled";}
        require(stopped&&polls==checkpoint,"Adjustment continued beyond requested checkpoint");require(input->rgba()==before,"Cancelled source mutated");++stops;
    }
    for(const auto& json:configurations){int polls=0;bool caught=false;try{effects::applyAdjustment(input,json,nullptr,{},[&]()->bool{if(++polls==3)throw CallbackFailure{};return false;});}catch(const CallbackFailure&){caught=true;}
        require(caught&&polls==3&&input->rgba()==before,"Callback exception lost or input mutated");++exceptions;
    }
    std::printf("PASS exact=%d checkpoint_stops=%d callback_exceptions=%d levels_oracle=%s\n",exact,stops,exceptions,correctLevels?"gamma":"frozen");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"FAIL %s\n",error.what());return 1;}}

