#define main preserved_subregion_main
#include "SubregionContracts.cpp"
#undef main
namespace {
void boundedPrefetch(bool gray){
    ReducedSourceCache cache(1,2);auto reads=std::make_shared<Reads>();auto input=source(pattern(1051,131),reads,gray);
    auto actual=cache.resolve(input,2);reference::ReducedSourceCache oracle;auto expected=oracle.resolve(control(*input),2);
    const auto pixel=actual->pixel(100,20);require(pixel==expected->pixel(100,20),"Bounded prefetch pixel differs");
    record(*reads);report["generated_tiles"]=qint64(cache.generatedTiles());report["retained_tiles"]=qint64(cache.retainedTiles());
    require(cache.retainedTiles()<=1,"Prefetch expanded retained tile budget");
    require(cache.generatedTiles()<=4,"One output prefetch regenerated intersecting lower tiles");
    require(reads->rows<=450&&reads->pixels<=238500,"One output prefetch exceeds three bounded lower-tile reads");
}
void exterior(){
    for(bool gray:{false,true})for(auto size:std::array<std::pair<int,int>,3>{{{513,259},{1,1025},{1025,1}}})for(int level:{2,3,16}){
        ReducedSourceCache cache(1,2);auto input=source(pattern(size.first,size.second),std::make_shared<Reads>(),gray,-3,5);
        auto actual=cache.resolve(input,level);reference::ReducedSourceCache oracle;auto expected=oracle.resolve(control(*input),level);
        const auto g=actual->grid();for(auto point:std::array<Point,6>{{{-1,-1},{0,0},{double(g.width-1),0},{0,double(g.height-1)},{double(g.width-1),double(g.height-1)},{double(g.width),double(g.height)}}})require(actual->pixel(int(point.x),int(point.y))==expected->pixel(int(point.x),int(point.y)),"Tile-major gray/transparent exterior differs");
        require(cache.retainedTiles()<=1,"Exterior prefetch exceeded retained budget");
    }
}
void failure(){
    for(bool gray:{false,true}){ReducedSourceCache cache(1,2);auto reads=std::make_shared<Reads>();reads->throwRead=5;auto input=source(pattern(1051,131),reads,gray);auto handle=cache.resolve(input,2);bool threw=false;try{handle->pixel(100,20);}catch(const std::runtime_error& e){threw=std::string(e.what())=="Injected source read failure";}require(threw,"Prefetch callback error was swallowed");require(cache.generatedTiles()==0&&cache.retainedTiles()==0,"First child failure published a partial tile");reads->throwRead=0;reference::ReducedSourceCache oracle;require(handle->pixel(100,20)==oracle.resolve(control(*input),2)->pixel(100,20),"Prefetch callback recovery differs");require(cache.retainedTiles()<=1,"Recovery exceeded cache budget");}
}
void completeEvictionPixels(){
    for(bool gray:{false,true}){ReducedSourceCache cache(1,2);auto input=source(pattern(1051,131),std::make_shared<Reads>(),gray);exact(cache,input,2);require(cache.retainedTiles()<=1,"Complete output exceeded cache budget");}
}
}
int main(int argc,char** argv){
    const std::map<std::string,void(*)()> extra{{"prefetch_color_work",[]{boundedPrefetch(false);}},{"prefetch_gray_work",[]{boundedPrefetch(true);}},{"prefetch_exterior_alignment",exterior},{"prefetch_callback_recovery",failure},{"prefetch_complete_eviction_pixels",completeEvictionPixels}};
    if(argc!=3)return 2;auto found=extra.find(argv[1]);if(found==extra.end())return preserved_subregion_main(argc,argv);
    QCoreApplication app(argc,argv);directory=std::filesystem::path(argv[2]);if(std::filesystem::exists(directory))return 2;std::filesystem::create_directories(directory);
    bool passed=false;std::string error;try{found->second();passed=true;}catch(const std::exception& e){error=e.what();}
    report["schema"]="TILED_PREFETCH_CONTRACTS_V1";report["case"]=argv[1];report["passed"]=passed;report["failed_predicate"]=QString::fromStdString(error);report["comparisons"]=comparisons;report["mac_differential"]=false;
    QFile file(QString::fromStdWString((directory/L"results.json").wstring()));if(!file.open(QIODevice::WriteOnly)||file.write(QJsonDocument(report).toJson())<0)return 3;
    std::cout<<(passed?"PASS ":"FAIL ")<<argv[1]<<" "<<error<<'\n';return passed?0:1;
}
