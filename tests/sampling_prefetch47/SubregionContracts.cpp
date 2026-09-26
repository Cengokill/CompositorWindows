#include "core/Document.h"
#include "graphics/SamplingSource.h"
#define graphics reference_graphics
#include "reference42/SamplingSource.h"
#undef graphics
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <map>
#include <thread>
using namespace compositor;
using namespace compositor::graphics;
namespace reference=compositor::reference_graphics;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Input {
    int width,height;
    std::vector<Pixel> pixels;
    std::vector<std::shared_ptr<const void>> tokens;
    std::shared_ptr<const void> domain;
};
struct Reads {
    uint64_t pixels{},rows{},dependencies{};
    int minX{30001},minY{30001},maxX{-1},maxY{-1};
    uint64_t throwRead{},throwDependency{};
    std::thread::id owner=std::this_thread::get_id();
};
std::filesystem::path directory;
QJsonObject report;
QJsonArray comparisons;
std::shared_ptr<const Input> pattern(int width=512,int height=512){
    auto out=std::make_shared<Input>(Input{width,height,{}, {},std::make_shared<int>(1)});
    out->pixels.resize(size_t(width)*height);uint32_t state=29;
    const auto next=[&](){state=state*1664525u+1013904223u;return state;};
    for(auto& p:out->pixels){p.a=uint8_t(next()>>24);p.r=uint8_t(next()%(uint32_t(p.a)+1));p.g=uint8_t(next()%(uint32_t(p.a)+1));p.b=uint8_t(next()%(uint32_t(p.a)+1));}
    for(int i=0;i<((width+63)/64)*((height+63)/64);++i)out->tokens.push_back(std::make_shared<int>(i));
    return out;
}
std::shared_ptr<const Input> changed(std::shared_ptr<const Input> before,std::vector<std::pair<int,int>> points){
    auto out=std::make_shared<Input>(*before);
    for(auto [x,y]:points){require(x>=0&&y>=0&&x<out->width&&y<out->height,"Invalid fixture change");auto& p=out->pixels[size_t(y)*out->width+x];p={uint8_t(p.r^255),uint8_t(p.g^255),uint8_t(p.b^255),255};out->tokens[size_t(y/64)*((out->width+63)/64)+x/64]=std::make_shared<int>(-1);}
    return out;
}
std::shared_ptr<SamplingSource> source(std::shared_ptr<const Input> input,std::shared_ptr<Reads> reads=std::make_shared<Reads>(),bool gray=false,int ax=0,int ay=0){
    auto out=std::make_shared<SamplingSource>();out->width=input->width;out->height=input->height;out->alignmentX=ax;out->alignmentY=ay;out->identity=input;out->reuseDomain=input->domain;
    if(gray)out->gray=[input](int x,int y){return input->pixels[size_t(y)*input->width+x].r;};
    else out->rgba=[input](int x,int y){return input->pixels[size_t(y)*input->width+x];};
    out->readRow=[input,reads,gray](int x,int y,int count,Pixel* result){
        require(std::this_thread::get_id()==reads->owner,"Source read escaped owning thread");require(x>=0&&y>=0&&count>0&&x+count<=input->width&&y<input->height,"Source read outside bounds");
        ++reads->rows;if(reads->throwRead&&reads->rows==reads->throwRead)throw std::runtime_error("Injected source read failure");reads->pixels+=uint64_t(count);reads->minX=std::min(reads->minX,x);reads->minY=std::min(reads->minY,y);reads->maxX=std::max(reads->maxX,x+count-1);reads->maxY=std::max(reads->maxY,y);
        for(int i=0;i<count;++i){const auto p=input->pixels[size_t(y)*input->width+x+i];result[i]=gray?Pixel{p.r,p.r,p.r,255}:p;}
    };
    out->dependencies=[input,reads](int x,int y,int width,int height){
        require(std::this_thread::get_id()==reads->owner,"Dependency read escaped owning thread");require(x>=0&&y>=0&&width>0&&height>0&&x+width<=input->width&&y+height<=input->height,"Dependency read outside bounds");
        ++reads->dependencies;if(reads->throwDependency&&reads->dependencies==reads->throwDependency)throw std::runtime_error("Injected dependency failure");
        std::vector<std::shared_ptr<const void>> result;const int columns=(input->width+63)/64;
        for(int ty=y/64;ty<=(y+height-1)/64;++ty)for(int tx=x/64;tx<=(x+width-1)/64;++tx)result.push_back(input->tokens[size_t(ty)*columns+tx]);return result;
    };
    return out;
}
std::shared_ptr<reference::SamplingSource> control(const SamplingSource& input){
    auto result=std::make_shared<reference::SamplingSource>();result->width=input.width;result->height=input.height;result->alignmentX=input.alignmentX;result->alignmentY=input.alignmentY;result->identity=input.identity;result->rgba=input.rgba;result->gray=input.gray;
    // No row/token callback: the frozen scalar source readers form an independent
    // full-generation oracle and cannot change actual work counters.
    return result;
}
void bytes(const std::string& name,const std::vector<Pixel>& pixels){QFile file(QString::fromStdWString((directory/std::filesystem::path(name)).wstring()));const auto count=qint64(pixels.size()*sizeof(Pixel));require(file.open(QIODevice::WriteOnly)&&file.write(reinterpret_cast<const char*>(pixels.data()),count)==count,"Cannot preserve pixels");}
std::vector<Pixel> exact(ReducedSourceCache& cache,std::shared_ptr<const SamplingSource> input,int level){
    auto actual=cache.resolve(input,level);reference::ReducedSourceCache oracle;auto expected=oracle.resolve(control(*input),level);const auto grid=actual->grid();const auto before=expected->grid();
    require(grid.x==before.x&&grid.y==before.y&&grid.width==before.width&&grid.height==before.height&&grid.step==before.step,"Reduction grid differs");
    std::vector<Pixel> a(size_t(grid.width)*grid.height),b(a.size()),diff(a.size());int maximum=0;uint64_t different=0;
    for(int y=0;y<grid.height;++y)for(int x=0;x<grid.width;++x){const auto i=size_t(y)*grid.width+x;a[i]=actual->pixel(x,y);b[i]=expected->pixel(x,y);const auto* aa=reinterpret_cast<const uint8_t*>(&a[i]);const auto* bb=reinterpret_cast<const uint8_t*>(&b[i]);auto* dd=reinterpret_cast<uint8_t*>(&diff[i]);for(int c=0;c<4;++c){dd[c]=uint8_t(std::abs(int(aa[c])-int(bb[c])));maximum=std::max(maximum,int(dd[c]));different+=dd[c]!=0;}}
    const auto prefix=std::to_string(comparisons.size());bytes(prefix+"-actual.rgba",a);bytes(prefix+"-reference.rgba",b);bytes(prefix+"-diff.rgba",diff);
    comparisons.append(QJsonObject{{"prefix",QString::fromStdString(prefix)},{"width",grid.width},{"height",grid.height},{"level",level},{"max_difference",maximum},{"different_bytes",qint64(different)}});
    require(maximum==0&&different==0,"Subregion pixels differ from full frozen reference");return a;
}
void record(const Reads& reads){report["source_pixels_read"]=qint64(reads.pixels);report["source_rows_read"]=qint64(reads.rows);report["dependency_calls"]=qint64(reads.dependencies);report["read_bounds"]=QJsonArray{reads.minX,reads.minY,reads.maxX,reads.maxY};}
void work(){ReducedSourceCache cache;auto first=pattern();exact(cache,source(first),1);auto reads=std::make_shared<Reads>();exact(cache,source(changed(first,{{208,208}}),reads),1);record(*reads);require(reads->pixels<=44100,"Dirty subregion exceeded frozen44100 source-pixel work bound");require(reads->minX>=119&&reads->minY>=119&&reads->maxX<=328&&reads->maxY<=328,"Dirty subregion fetched outside exact conservative halo");}
void latest(){ReducedSourceCache cache;auto first=pattern();exact(cache,source(first),1);auto second=changed(first,{{208,208}});exact(cache,source(second),1);auto reads=std::make_shared<Reads>();exact(cache,source(changed(second,{{416,208}}),reads),1);record(*reads);require(reads->pixels<=44100,"Newest candidate was not used for bounded dirty work");}
void edges(){for(bool gray:{false,true})for(int level:{1,2,4,16}){ReducedSourceCache cache;auto first=pattern(513,259);exact(cache,source(first,std::make_shared<Reads>(),gray,-3,5),level);auto next=changed(first,{{0,0},{512,258},{255,31}});exact(cache,source(next,std::make_shared<Reads>(),gray,-3,5),level);}}
void thin(){for(auto [w,h]:std::array<std::pair<int,int>,2>{{{1,1025},{1025,1}}})for(bool gray:{false,true})for(int level:{1,3,16}){ReducedSourceCache cache;auto first=pattern(w,h);exact(cache,source(first,std::make_shared<Reads>(),gray,7,-11),level);exact(cache,source(changed(first,{{w-1,h-1},{w/2,h/2}}),std::make_shared<Reads>(),gray,7,-11),level);}}
void halo(){for(bool gray:{false,true})for(int level:{1,2,3}){ReducedSourceCache cache;auto first=pattern(777,321);exact(cache,source(first,std::make_shared<Reads>(),gray,-3,5),level);for(auto point:std::array<std::pair<int,int>,4>{{{119,119},{200,200},{511,63},{63,255}}}){first=changed(first,{point});exact(cache,source(first,std::make_shared<Reads>(),gray,-3,5),level);}}}
void multiple(){for(bool gray:{false,true}){ReducedSourceCache cache;auto first=pattern();exact(cache,source(first,std::make_shared<Reads>(),gray),1);exact(cache,source(changed(first,{{80,400},{400,80},{208,208}}),std::make_shared<Reads>(),gray),1);}}
void isolation(){for(int variant=0;variant<6;++variant){ReducedSourceCache cache;auto first=pattern();exact(cache,source(first),1);auto next=changed(first,{{208,208}});auto reads=std::make_shared<Reads>();auto input=source(next,reads);if(variant==0)input->reuseDomain=std::make_shared<int>(2);if(variant==1)input->alignmentX=-3;if(variant==2)input=source(next,reads,true);if(variant==3)input=source(pattern(513,512),reads);if(variant==4)input->dependencies={};if(variant==5)input->reuseDomain.reset();exact(cache,input,1);require(reads->pixels>44100,"Ineligible candidate supplied partial output");}}
void unchanged(){ReducedSourceCache cache;auto first=pattern();exact(cache,source(first),1);const auto generated=cache.generatedTiles();auto copy=std::make_shared<Input>(*first);auto reads=std::make_shared<Reads>();exact(cache,source(copy,reads),1);record(*reads);require(reads->pixels==0&&cache.generatedTiles()==generated&&cache.reusedTiles()>0,"Unchanged candidate lost whole-tile reuse");}
void eviction(){for(size_t budget:{size_t(1),size_t(3),size_t(8)}){ReducedSourceCache cache(budget,2);auto image=pattern(1051,131);for(int i=0;i<4;++i){exact(cache,source(image),2);require(cache.retainedTiles()<=budget&&cache.retainedSources()<=2,"Cache budget exceeded");image=changed(image,{{100+i*250,60}});}cache.clear();require(!cache.retainedTiles()&&!cache.retainedSources(),"Cache clear retained entries");}}
void lifetime(){ReducedSourceCache cache;std::weak_ptr<const Input> weak;std::weak_ptr<const void> domain;{
    auto first=pattern();weak=first;domain=first->domain;auto input=source(first);auto held=cache.resolve(input,1);const auto old=exact(cache,input,1);exact(cache,source(changed(first,{{208,208}})),1);for(int y=0;y<held->grid().height;++y)for(int x=0;x<held->grid().width;++x)require(held->pixel(x,y)==old[size_t(y)*held->grid().width+x],"New generation mutated old immutable output");
}require(!weak.expired()&&!domain.expired(),"Cached candidate source lifetime ended early");cache.clear();require(weak.expired()&&domain.expired(),"Clear leaked candidate source/domain");}
void exceptions(){for(bool dependency:{false,true}){ReducedSourceCache cache;auto first=pattern();auto old=cache.resolve(source(first),1);const auto before=exact(cache,source(first),1);auto reads=std::make_shared<Reads>();auto input=source(changed(first,{{208,208}}),reads);if(dependency)reads->throwDependency=1;else reads->throwRead=5;const auto generated=cache.generatedTiles();bool threw=false;try{cache.resolve(input,1)->pixel(100,100);}catch(const std::runtime_error&){threw=true;}require(threw,"Injected callback exception was swallowed");require(cache.generatedTiles()==generated,"Failed output was installed");require(old->pixel(100,100)==before[size_t(100)*256+100],"Failed generation changed old output");reads->throwRead=reads->throwDependency=0;exact(cache,input,1);}}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);std::cout<<std::unitbuf;if(argc!=3)return 2;directory=std::filesystem::path(argv[2]);if(std::filesystem::exists(directory))return 2;std::filesystem::create_directories(directory);const std::string name=argv[1];const std::map<std::string,void(*)()> cases{{"dirty_block_work",work},{"latest_candidate",latest},{"odd_gray_alignment",edges},{"thin_high_levels",thin},{"recursive_halo",halo},{"multiple_dirty_cells",multiple},{"candidate_isolation",isolation},{"unchanged_identity",unchanged},{"cache_eviction",eviction},{"immutable_lifetime",lifetime},{"callback_exception_recovery",exceptions}};bool passed=false;std::string error;try{auto it=cases.find(name);require(it!=cases.end(),"Unknown case");it->second();passed=true;}catch(const std::exception& e){error=e.what();}report["schema"]="REDUCTION_SUBREGION_CONTRACTS_V1";report["case"]=QString::fromStdString(name);report["passed"]=passed;report["failed_predicate"]=QString::fromStdString(error);report["comparisons"]=comparisons;report["mac_differential"]=false;QFile file(QString::fromStdWString((directory/L"results.json").wstring()));if(!file.open(QIODevice::WriteOnly)||file.write(QJsonDocument(report).toJson())<0)return 3;std::cout<<(passed?"PASS ":"FAIL ")<<name<<" "<<error<<'\n';return passed?0:1;}
