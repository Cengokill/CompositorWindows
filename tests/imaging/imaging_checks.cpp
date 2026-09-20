#include "wic_codec.h"
#include "subject_matte.h"
#include <cmath>
#include <iostream>
#include <fstream>
using namespace compositor::imaging;
void require(bool x,const char* m){if(!x)throw std::runtime_error(m);}
template<class F>void rejects(F f,const char* m){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}require(rejected,m);}
int main(int argc,char** argv){try{
    std::filesystem::path dir=argc>1?argv[1]:".";std::filesystem::create_directories(dir);
    // These are required acceptance cases. A missing corpus is a failing run.
    for(auto name:{"fixture-manifest.json","rgba16.png","transparent-gray.png","animated.png","orientation-6.jpg","adobe-rgb.png","adobe-rgb-srgb.png"})require(std::filesystem::is_regular_file(dir/name),"Required imaging fixture missing");
    for(int o=1;o<=8;++o){require(std::filesystem::is_regular_file(dir/("orientation-"+std::to_string(o)+".tif")),"Required orientation fixture missing");require(std::filesystem::is_regular_file(dir/("oriented-"+std::to_string(o)+".png")),"Required orientation reference missing");}
    RgbaImage src{7,5,36,std::vector<std::uint8_t>(180,0xEE)};
    for(unsigned y=0;y<5;++y)for(unsigned x=0;x<7;++x){auto* p=&src.pixels[y*src.stride+x*4];auto a=std::uint8_t(x*42);p[0]=a;p[1]=std::uint8_t((x+y)%2?a:0);p[2]=0;p[3]=a;}
    for(auto format:{ImageFormat::Png,ImageFormat::Tiff,ImageFormat::Jpeg}){ExportOptions opt;opt.format=format;opt.dpi=144;opt.jpegQuality=.95;auto path=dir/(format==ImageFormat::Png?"alpha.png":format==ImageFormat::Tiff?"alpha.tif":"alpha.jpg");WicCodec::encode(path,src,opt);auto decoded=WicCodec::decode(path);require(decoded.image.width==7&&decoded.image.height==5,"round trip dimensions");require(std::abs(decoded.metadata.dpiX-144)<.1,"DPI round trip");validate(decoded.image);
        if(format!=ImageFormat::Jpeg)for(unsigned y=0;y<5;++y)for(unsigned x=0;x<7;++x)for(int c=0;c<4;++c)require(src.pixels[y*src.stride+x*4+c]==decoded.image.pixels[(y*7+x)*4+c],"Lossless RGBA round trip");
        else for(unsigned y=0;y<5;++y)for(unsigned x=0;x<7;++x)require(decoded.image.pixels[(y*7+x)*4+3]==255,"JPEG opacity");
    }
    ImportOptions tiny;tiny.remainingPixels=34;rejects([&]{WicCodec::decode(dir/"alpha.png",tiny);},"pixel budget guard");tiny.remainingPixels=100000000;tiny.maxWorkingBytes=100;rejects([&]{WicCodec::decode(dir/"alpha.png",tiny);},"working budget guard");tiny={};tiny.cancelled=[] {return true;};rejects([&]{WicCodec::decode(dir/"alpha.png",tiny);},"cancellation");
    RgbaImage bad=src;bad.pixels[0]=255;rejects([&]{WicCodec::encode(dir/"bad.png",bad);},"premultiplied contract");
    RgbaImage transparent{16,16,64,std::vector<std::uint8_t>(1024)};ExportOptions matte;matte.format=ImageFormat::Jpeg;matte.matteR=23;matte.matteG=90;matte.matteB=180;matte.jpegQuality=.95;WicCodec::encode(dir/"custom-matte.jpg",transparent,matte);auto flattened=WicCodec::decode(dir/"custom-matte.jpg");const int wanted[]={23,90,180,255};for(std::size_t i=0;i<flattened.image.pixels.size();++i)require(std::abs(int(flattened.image.pixels[i])-wanted[i%4])<=2,"JPEG chosen matte within 2-byte codec tolerance");
    RgbaImage noise{64,64,256,std::vector<std::uint8_t>(16384)};std::uint32_t seed=7;for(std::size_t i=0;i<noise.pixels.size();i+=4){for(int c=0;c<3;++c){seed=seed*1664525U+1013904223U;noise.pixels[i+c]=std::uint8_t(seed>>24);}noise.pixels[i+3]=255;}matte.jpegQuality=.1;WicCodec::encode(dir/"quality-low.jpg",noise,matte);matte.jpegQuality=.95;WicCodec::encode(dir/"quality-high.jpg",noise,matte);require(std::filesystem::file_size(dir/"quality-high.jpg")>std::filesystem::file_size(dir/"quality-low.jpg"),"JPEG quality changes encoded output");
    auto original=WicCodec::decode(dir/"alpha.png").image.pixels;ExportOptions cancelledExport;cancelledExport.cancelled=[]{return true;};rejects([&]{WicCodec::encode(dir/"alpha.png",src,cancelledExport);},"Cancelled export rejected");require(WicCodec::decode(dir/"alpha.png").image.pixels==original,"Cancelled export preserves target");
    RgbaImage narrow{30000,1,120000,std::vector<std::uint8_t>(120000,255)};WicCodec::encode(dir/"narrow.png",narrow);require(WicCodec::decode(dir/"narrow.png").image.width==30000,"30000 pixel side");
    std::vector<float> sample{0,1,0,1,0,1};auto mean=boxMean(sample,3,2,1);
    for(int y=0;y<2;++y)for(int x=0;x<3;++x){float sum=0;for(int yy=-1;yy<=1;++yy)for(int xx=-1;xx<=1;++xx)sum+=sample[std::clamp(y+yy,0,1)*3+std::clamp(x+xx,0,2)];require(std::abs(mean[y*3+x]-sum/9)<1e-6,"box replicated boundary");}
    std::vector<float>constant(35,.25F),guide(35,.8F);auto filtered=guidedFilter(constant,guide,7,5,12);for(auto f:filtered)require(std::abs(f-.25F)<1e-5,"guided constant mask");
    GrayMask mask{7,5,7,std::vector<std::uint8_t>(35,128)},under{7,5,7,std::vector<std::uint8_t>(35,128)};auto basic=refineSubjectMask(mask,src,{},false,&under);for(auto p:basic.pixels)require(p==64,"existing mask multiplication");
    MatteSettings advanced;advanced.advanced=true;advanced.refineEdges=0;advanced.shiftEdge=0;advanced.contrast=100;auto hard=refineSubjectMask(mask,src,advanced);for(auto p:hard.pixels)require(p==153,"contrast source formula");
    auto applied=applySubjectMask(src,under);validate(applied);require(applied.pixels[4+3]==21,"preview alpha multiplication");
    GrayMask threshold{7,5,7,std::vector<std::uint8_t>(35,191)};advanced.shiftEdge=-2;auto ordered=refineSubjectMask(threshold,src,advanced);for(auto p:ordered.pixels)require(p==0,"Shift must precede contrast");
    WicCodec::encodeProjectGrayPng(dir/"mask.png",under);require(WicCodec::decodeProjectGrayPng(dir/"mask.png").pixels==under.pixels,"Gray project PNG exact roundtrip");
    require(WicCodec::decodeProjectRgbaPng(dir/"alpha.png").width==src.width,"Project PNG color decode");
    rejects([&]{WicCodec::decodeProjectGrayPng(dir/"alpha.png");},"Reject color mask PNG");rejects([&]{WicCodec::decodeProjectRgbaPng(dir/"alpha.jpg");},"Reject non-PNG project asset");
    rejects([&]{WicCodec::decodeProjectRgbaPng(dir/"rgba16.png");},"Reject 16-bit project PNG");rejects([&]{WicCodec::decodeProjectGrayPng(dir/"transparent-gray.png");},"Reject grayscale tRNS");rejects([&]{WicCodec::decodeProjectRgbaPng(dir/"animated.png");},"Reject animated project PNG");
    for(int orientation=1;orientation<=8;++orientation){auto path=dir/("orientation-"+std::to_string(orientation)+".tif");auto rotated=WicCodec::decode(path);require(rotated.metadata.originalOrientation==orientation,"TIFF orientation metadata");auto expected=WicCodec::decode(dir/("oriented-"+std::to_string(orientation)+".png"));require(rotated.image.width==expected.image.width&&rotated.image.height==expected.image.height&&rotated.image.pixels==expected.image.pixels,"All eight EXIF mappings");}
    {auto profile=WicCodec::decode(dir/"adobe-rgb.png");auto expected=WicCodec::decode(dir/"adobe-rgb-srgb.png");require(profile.metadata.profileStatus==ProfileStatus::ConvertedEmbeddedProfile,"Embedded ICC normalization status");for(std::size_t i=0;i<profile.image.pixels.size();++i)require(std::abs(int(profile.image.pixels[i])-int(expected.image.pixels[i]))<=2,"ICC normalization within frozen 2-byte CMS tolerance");}
    {auto rotated=WicCodec::decode(dir/"orientation-6.jpg");require(rotated.metadata.originalOrientation==6&&rotated.image.width==2&&rotated.image.height==3,"EXIF orientation 6");}
    std::ofstream(dir/"core-results.json")<<"{\"status\":\"passed\",\"checks\":[\"PNG/TIFF exact premultiplied roundtrip\",\"JPEG dimensions opacity DPI\",\"stride padding\",\"allocation limits\",\"cancellation\",\"30000-pixel side\",\"box reference\",\"guided constant\",\"mask multiplication\",\"contrast formula\",\"preview alpha\"]}";
    std::cout<<"Imaging codec and matte checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

