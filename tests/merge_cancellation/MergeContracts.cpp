#include "layers/LayerOperations.h"
#include "effects/Adjustments.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>
#include <stdexcept>
#include <string>
using namespace compositor;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Backend final:IRasterBackend {
    mutable size_t calls{},largest{};
    bool invalid{},fail{};
    std::shared_ptr<const Raster> render(const Document& document,int x,int y,int width,int height)const override{
        ++calls;largest=std::max(largest,size_t(width)*height);
        if(fail)throw std::runtime_error("Injected render failure");
        if(invalid)return Raster::filled(1,1);
        return SoftwareRenderer().render(document,x,y,width,height);
    }
};
Document fixture(bool trimmed=false,bool adjustment=false){
    Document d;d.id="merge-cancellation";d.width=519;d.height=263;
    Layer bottom;bottom.id="bottom";bottom.name="Bottom";bottom.transform=trimmed?Transform{7,9,507,249}:Transform{0,0,519,263};bottom.raster=Raster::filled(519,263,{30,50,70,200});
    Layer top;top.id="top";top.name="Top";top.transform={45.25,32.75,413,170,13.5};top.raster=Raster::filled(37,29,{70,20,90,120});top.blend=Blend::Overlay;
    if(adjustment){top.raster.reset();top.transform={0,0,519,263};auto object=QJsonDocument::fromJson(QByteArray::fromStdString(effects::defaultAdjustmentJson("Grain"))).object();object["grainSettings"]=QJsonObject{{"amount",70},{"size",1.5},{"roughness",50},{"seed",77}};top.adjustmentJson=QJsonDocument(object).toJson(QJsonDocument::Compact).toStdString();}
    d.layers={bottom,top};validateDocument(d);return d;
}
layers::SelectionState selection(){return {{"bottom","top"},"top"};}
template<class F>void fails(F operation,const char* expected){bool failed=false;try{operation();}catch(const std::runtime_error& error){failed=std::string(error.what())==expected;}require(failed,"Expected exact failure was not propagated");}
void run(const std::string& key){
    const auto d=fixture(key=="exact_trim",key=="exact_grain"),before=d;Backend backend;
    if(key=="bounded_render"){
        const auto result=layers::merge(d,selection(),backend);
        require(result.changed&&backend.calls==6&&backend.largest<=65536,"Merge must render six bounded tiles for519x263");
    }else if(key=="cancel_after_first"){
        layers::Limits limits;limits.cancelled=[&]{return backend.calls>=1;};
        fails([&]{layers::merge(d,selection(),backend,limits);},"Layer operation cancelled");
        require(backend.calls==1&&backend.largest<=65536,"Cancellation must stop after at most one256x256 render");
    }else if(key=="budget_before_render"){
        layers::Limits limits;limits.maxWorkingBytes=1;
        fails([&]{layers::merge(d,selection(),backend,limits);},"Layer operation exceeds working memory budget");require(backend.calls==0,"Budget rejection happened after render");
    }else if(key=="renderer_failure"){
        backend.fail=true;fails([&]{layers::merge(d,selection(),backend);},"Injected render failure");require(backend.calls==1,"Renderer failure was retried");
    }else if(key=="invalid_dimensions"){
        backend.invalid=true;fails([&]{layers::merge(d,selection(),backend);},"Merge renderer returned invalid dimensions");require(backend.calls==1,"Invalid render was used");
    }else if(key=="exact_opaque"||key=="exact_trim"||key=="exact_grain"){
        const auto expected=SoftwareRenderer().render(d,0,0,d.width,d.height);
        const auto result=layers::merge(d,selection(),backend);
        require(result.changed&&result.document.layers.size()==1&&result.action=="Merge Layers","Merge metadata changed");
        const auto actual=SoftwareRenderer().render(result.document,0,0,d.width,d.height);
        require(actual->rgba()==expected->rgba(),"Tiled merge differs from full composite");
        require(result.selection.ids.size()==1&&result.selection.primary==result.document.layers[0].id&&result.document.layers[0].id!="top"&&result.document.layers[0].id!="bottom","Merge identity or selection is invalid");
    }else throw std::runtime_error("Unknown case");
    require(d==before,"Merge or failure mutated canonical input");
    std::printf("PASS %s render_calls=%zu largest_region=%zu\n",key.c_str(),backend.calls,backend.largest);
}
}
int main(int argc,char** argv){try{if(argc!=2)return 2;run(argv[1]);return 0;}catch(const std::exception& error){std::fprintf(stderr,"FAIL %s\n",error.what());return 1;}}
