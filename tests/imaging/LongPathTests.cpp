#include "imaging/wic_codec.h"
#include "imaging/stream_export.h"
#include "imaging/win32_file_path.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
using namespace compositor::imaging;
namespace fs=std::filesystem;
#define REQUIRE(...) do{if(!(__VA_ARGS__))throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+": " #__VA_ARGS__);}while(false)
template<class F>void rejects(F action){bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}REQUIRE(rejected);}
struct Fixture {
    QTemporaryDir root;fs::path directory;
    Fixture(){REQUIRE(root.isValid());directory=fs::path(root.path().toStdWString());for(int i=0;directory.native().size()<320;++i){directory/=std::wstring(55,wchar_t(L'a'+i))+L" 実証";fs::create_directories(win32FilePath(directory));}}
    fs::path path(const wchar_t* file)const{auto p=directory/file;REQUIRE(p.native().size()>300);return p;}
};
RgbaImage source(){RgbaImage out{11,7,48,std::vector<uint8_t>(48*7,0xDA)};for(unsigned y=0;y<7;++y)for(unsigned x=0;x<11;++x){auto*p=out.pixels.data()+48*y+x*4;auto a=uint8_t((x*19+y*37)%256);p[0]=uint8_t(a/4);p[1]=uint8_t(a/2);p[2]=a;p[3]=a;}return out;}
void same(const RgbaImage&a,const RgbaImage&b){REQUIRE(a.width==b.width&&a.height==b.height);for(unsigned y=0;y<a.height;++y)REQUIRE(std::equal(a.pixels.begin()+y*a.stride,a.pixels.begin()+y*a.stride+a.width*4,b.pixels.begin()+y*b.stride));}
std::vector<uint8_t> read(const fs::path& path){std::ifstream in(win32FilePath(path),std::ios::binary);REQUIRE(bool(in));return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};}
void rgba(){Fixture fixture;auto path=fixture.path(L"色-image.png");auto image=source();ExportOptions options;options.dpi=144;WicCodec::encode(path,image,options);auto decoded=WicCodec::decodeProjectRgbaPng(path);same(image,decoded);auto metadata=WicCodec::decode(path).metadata;REQUIRE(metadata.originalOrientation==1&&std::abs(metadata.dpiX-144)<.02);std::cout<<"color_path_length="<<path.native().size()<<" temporary_length="<<path.native().size()+43<<'\n';}
void gray(){Fixture fixture;auto path=fixture.path(L"灰-mask.png");GrayMask mask{11,7,14,std::vector<uint8_t>(14*7,0xDB)};for(unsigned y=0;y<7;++y)for(unsigned x=0;x<11;++x)mask.pixels[y*14+x]=uint8_t((x*23+y*11)%256);WicCodec::encodeProjectGrayPng(path,mask);auto decoded=WicCodec::decodeProjectGrayPng(path);REQUIRE(decoded.width==11&&decoded.height==7);for(unsigned y=0;y<7;++y)REQUIRE(std::equal(mask.pixels.begin()+y*14,mask.pixels.begin()+y*14+11,decoded.pixels.begin()+y*11));std::cout<<"gray_path_length="<<path.native().size()<<" temporary_length="<<path.native().size()+43<<'\n';}
void stream(){Fixture fixture;auto path=fixture.path(L"長-stream.png"),copy=fixture.path(L"長-copy.png");auto image=source();auto rows=[&](uint32_t y,uint32_t count,std::span<uint8_t> out,size_t stride){for(uint32_t row=0;row<count;++row)std::copy_n(image.pixels.data()+size_t(y+row)*image.stride,image.width*4,out.data()+row*stride);};encodeRowsAtomic(path,image.width,image.height,rows);same(image,WicCodec::decode(path).image);copyEncodedAtomic(path,copy);REQUIRE(read(path)==read(copy));auto before=read(copy);bool stop=true;rejects([&]{copyEncodedAtomic(path,copy,[&]{return stop;});});REQUIRE(read(copy)==before);}
void namespaces(){REQUIRE(win32FilePath(L"C:/folder/../images/a.png").native()==L"\\\\?\\C:\\images\\a.png");REQUIRE(win32FilePath(L"\\\\server\\share\\folder\\a.png").native()==L"\\\\?\\UNC\\server\\share\\folder\\a.png");REQUIRE(win32FilePath(L"\\\\?\\UNC\\server\\share\\a.png").native()==L"\\\\?\\UNC\\server\\share\\a.png");REQUIRE(win32FilePath(L"\\\\?\\C:\\images\\a.png").native()==L"\\\\?\\C:\\images\\a.png");REQUIRE(win32FilePath(L"relative-image.png").is_absolute());for(auto path:{L"\\\\.\\NUL",L"\\\\?\\GLOBALROOT\\Device\\HarddiskVolume1\\x",L"\\\\?\\Volume{00000000-0000-0000-0000-000000000000}\\x",L"\\??\\C:\\x",L"C:relative.png",L"C:\\NUL.png",L"C:\\COM1\\x.png",L"C:\\LPT².png",L"C:\\CONOUT$",L"C:\\a.png:stream",L"C:\\a. ",L"\\\\server",L"\\\\server\\share\\..\\escape.png"})rejects([&]{win32FilePath(path);});rejects([&]{win32FilePath(fs::path(std::wstring(L"C:\\a\0b.png",10)));});}
int main(int argc,char**argv){QCoreApplication app(argc,argv);try{REQUIRE(argc==2);std::map<std::string,void(*)()> tests{{"rgba_roundtrip",rgba},{"gray_roundtrip",gray},{"stream_copy_cancel",stream},{"namespace_rules",namespaces}};REQUIRE(tests.contains(argv[1]));tests.at(argv[1])();std::cout<<"PASS "<<argv[1]<<'\n';return 0;}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
