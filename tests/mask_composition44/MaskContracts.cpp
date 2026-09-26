#define main preserved_composition_main
#include "CompositionContracts.cpp"
#undef main
namespace {
Layer masked(int w=513,int h=519){auto l=layer(2,w,h);l.transform={10.25,20.75,double(w),double(h)};l.mask=Mask{gray(w,h,true),true,false};return l;}
void maskWork(){run(masked(),settings(),{},true);report["mask_pixel_calls_actual"]=qint64(actualGrayCalls);report["mask_pixel_calls_reference"]=qint64(referenceGrayCalls);require(referenceGrayCalls>0,"Mask observer saw no reference dispatches");require(actualGrayCalls==0,"Same-grid mask composition still dispatches scalar gray reads");}
void rows(bool checkWork){
    auto l=masked();GrowingBrushSession a(l,settings(),1024,768,{}, {},true);reference::GrowingBrushSession b(l,settings(),1024,768,{}, {},true);a.begin({100,100});b.begin({100,100});
    auto ap=a.preview()->renderPreview();auto bp=b.preview()->renderPreview();require(ap&&bp&&ap->maskSource&&bp->maskSource,"Missing live mask source");
    uint64_t observedA=0,observedB=0;
    for(int y:std::array<int,8>{-1,0,79,255,256,400,518,519}){
        std::vector<Pixel> actual(520),control(520),diff(520);
        grayCalls=0;ap->maskSource->readRow(-3,y,520,actual.data());observedA+=grayCalls.exchange(0);bp->maskSource->readRow(-3,y,520,control.data());observedB+=grayCalls.exchange(0);
        bool equal=true;for(size_t i=0;i<actual.size();++i){auto* out=reinterpret_cast<uint8_t*>(&diff[i]);const auto* aa=reinterpret_cast<const uint8_t*>(&actual[i]);const auto* bb=reinterpret_cast<const uint8_t*>(&control[i]);for(int c=0;c<4;++c)out[c]=uint8_t(std::abs(int(aa[c])-int(bb[c])));equal&=actual[i]==control[i];}
        const auto prefix="row-"+std::to_string(y);saveBytes(prefix+"-actual.rgba",actual);saveBytes(prefix+"-reference.rgba",control);saveBytes(prefix+"-diff.rgba",diff);require(equal,"Live mask row/exterior bytes differ");
    }
    report["row_gray_calls_actual"]=qint64(observedA);report["row_gray_calls_reference"]=qint64(observedB);snapshots(a.preview(),b.preview());coverage(a,b);
    if(checkWork){require(observedB>0,"Row observer saw no reference reads");require(observedA==0,"Same-grid live row still dispatches scalar gray reads");}
}
void odd(){run(masked(),settings(),gray(1024,768,true),true);}
void placed(){auto l=masked(257,193);l.mask->placement=Transform{305.5,220.25,341,159,-17,true,true,Transform::Sampling::High};run(l,settings(),gray(1024,768,true),true);}
void uniform(){auto l=masked(257,193);l.mask->raster=gray(1,1);run(l,settings(),{},true);}
void different(){auto l=masked(257,193);l.mask->raster=gray(57,31,true);run(l,settings(),gray(1024,768,true),true);}
void sparse(){auto l=masked();l.mask->raster=GrayRaster::sampled(513,519,{0,0,513,519},[](int,int){return uint8_t(123);});bool aa=false,bb=false;try{GrowingBrushSession a(l,settings(),1024,768,{}, {},true);}catch(const std::invalid_argument&){aa=true;}try{reference::GrowingBrushSession b(l,settings(),1024,768,{}, {},true);}catch(const std::invalid_argument&){bb=true;}require(aa&&bb,"Sparse mask constructor precondition changed");}
void callbackStorage(){auto l=masked(257,193);auto g=std::make_shared<GrayRaster>(*GrayRaster::sampled(257,193,{0,0,257,193},[](int x,int y){return uint8_t((x+3*y)%256);}));g->pixels.assign(257*193,231);l.mask->raster=g;run(l,settings(),{},true);}
void smallBudget(){auto l=layer(0,1024,768);l.mask=Mask{gray(17,13,true),true,false};l.mask->placement=Transform{200,150,17,13};GrowingBrushSession a(l,settings(),1024,768,{}, {},true,221);reference::GrowingBrushSession b(l,settings(),1024,768,{}, {},true,221);a.begin({208,156});b.begin({208,156});snapshots(a.commit(),b.commit());sameLayer(a.preview()->materializeLayer(),b.preview()->materializeLayer());}
void customMask(){auto l=masked(257,193);auto s=settings();s.radius=120;bool fail=false;int ca=0,cb=0;auto compose=[&](int& calls){return [&,counter=&calls](const auto& input,std::span<Pixel> output){++*counter;if(fail&&*counter==2)throw std::runtime_error("Injected custom mask compositor failure");for(int y=0;y<input.rect.height;++y)for(int x=0;x<input.rect.width;++x){const auto at=size_t(y)*256+x;require(output[at]==input.original[at],"Custom mask was not based on original");if(input.rawCoverage[size_t(y)*input.rect.width+x]>127)output[at]={61,61,61,255};}};};GrowingBrushSession a(l,s,1024,768,{}, {},true,100000000,compose(ca));reference::GrowingBrushSession b(l,s,1024,768,{}, {},true,100000000,compose(cb));a.begin({90,90});b.begin({90,90});auto old=a.preview();auto referenceOld=b.preview();fail=true;ca=cb=0;bool aa=false,bb=false;try{a.append({263,150});}catch(const std::runtime_error&){aa=true;}try{b.append({263,150});}catch(const std::runtime_error&){bb=true;}require(aa&&bb&&old==a.preview()&&referenceOld==b.preview(),"Custom mask failure changed publication");snapshots(old,referenceOld);fail=false;ca=cb=0;a.append({263,150});b.append({263,150});require(ca==cb,"Custom mask callback count changed");snapshots(a.commit(),b.commit());sameLayer(a.preview()->materializeLayer(),b.preview()->materializeLayer());}
}
int main(int argc,char** argv){
    const std::map<std::string,void(*)()> extra{{"same_grid_mask_work",maskWork},{"same_grid_row_work",[]{rows(true);}},{"row_overlay_exterior",[]{rows(false);}},{"odd_mask_selection_phase",odd},{"placed_rotated_flipped_mask",placed},{"uniform_mask_fallback",uniform},{"different_grid_mask_fallback",different},{"sparse_mask_precondition",sparse},{"callback_storage_fallback",callbackStorage},{"small_placed_mask_budget",smallBudget},{"custom_mask_exception",customMask}};
    if(argc!=3)return 2;const auto it=extra.find(argv[1]);if(it==extra.end())return preserved_composition_main(argc,argv);
    QCoreApplication app(argc,argv);std::cout<<std::unitbuf;directory=std::filesystem::path(argv[2]);if(std::filesystem::exists(directory))return 2;std::filesystem::create_directories(directory);bool passed=false;std::string error;try{it->second();passed=true;}catch(const std::exception& e){error=e.what();}report["schema"]="MASK_COMPOSITION_CONTRACTS_V1";report["case"]=argv[1];report["passed"]=passed;report["failed_predicate"]=QString::fromStdString(error);report["comparisons"]=comparisons;report["mac_differential"]=false;QFile file(QString::fromStdWString((directory/L"results.json").wstring()));if(!file.open(QIODevice::WriteOnly)||file.write(QJsonDocument(report).toJson())<0)return 3;std::cout<<(passed?"PASS ":"FAIL ")<<argv[1]<<" "<<error<<'\n';return passed?0:1;
}
