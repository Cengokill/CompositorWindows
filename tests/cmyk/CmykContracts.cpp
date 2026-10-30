#include "Fixtures.h"
#include "ProfileFixture.h"
#include "core/DeepImage.h"
#include "core/DocumentExport.h"
#include "imaging/cmyk.h"
#include "imaging/psd/PsdDocument.h"
#include "imaging/wic_codec.h"
#include "persistence/ProjectStore.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
using namespace compositor;
namespace {
void require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
template<class F> void rejects(F&& run){bool rejected=false;try{run();}catch(const std::exception&){rejected=true;}require(rejected,"Malformed input or cancelled operation was accepted");}
CmykProfilePtr profile(){const auto bytes=cmyk_test::profileBytes();return imaging::makeCmykProfile(std::span(reinterpret_cast<const uint8_t*>(bytes.data()),size_t(bytes.size())),"Synthetic CMYK");}
std::vector<uint8_t> read(const std::filesystem::path& path){std::ifstream f(path,std::ios::binary);require(bool(f),"Read output");return {(std::istreambuf_iterator<char>(f)),{}};}
void write(const std::filesystem::path& path,const std::vector<uint8_t>& bytes){std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));require(bool(f),"Write fixture");}
Document document(){Document d;d.id=newId();d.width=4;d.height=1;d.resolution=150;Layer layer;layer.id=newId();layer.name="Ink";layer.transform={0,0,4,1};const uint8_t pixels[]{255,255,255,255,0,0,0,255,0,128,128,128,0,0,0,0};layer.raster=Raster::fromRgba(4,1,pixels,16);d.layers.push_back(layer);return d;}
void profileContract(){
    const auto p=profile();require(p->sha256.size()==64,"Missing profile checksum");
    auto bytes=p->bytes;bytes[16]='R';rejects([&]{imaging::makeCmykProfile(bytes,"wrong space");});
    bytes=p->bytes;bytes.pop_back();rejects([&]{imaging::makeCmykProfile(bytes,"truncated");});
    bytes=p->bytes;std::fill(bytes.begin()+136,bytes.begin()+140,255);rejects([&]{imaging::makeCmykProfile(bytes,"tag outside file");});
    imaging::CmykTransform transform(p);const std::array<uint16_t,12> ink{0,0,0,0,0,0,0,65535,65535,0,0,0};std::array<uint16_t,9> rgb{};transform.toRgb(ink,rgb);
    require(rgb[0]>62000&&rgb[1]>62000&&rgb[2]>62000,"Zero ink is not paper white");
    require(rgb[3]<3000&&rgb[4]<3000&&rgb[5]<3000,"Black ink is not black");
    require(rgb[6]<16000&&rgb[7]>45000&&rgb[8]>45000,"CMYK channel order or ICC conversion is wrong");
    const auto lut=imaging::cmykProofLookup(p);require(lut.size()==33*33*33*4,"Wrong bounded proof lookup size");
    require(std::all_of(lut.begin(),lut.end(),[](float v){return std::isfinite(v)&&v>=0&&v<=1;}),"Invalid proof values");
    rejects([&]{imaging::cmykProofLookup(p,[]{return true;});});
}
void psdContract(){
    const auto p=profile();QTemporaryDir temporary;const auto path=std::filesystem::path(temporary.path().toStdWString())/L"CMYK-画像.psd";
    for(bool psb:{false,true})for(int depth:{8,16})for(bool rle:{false,true})for(bool layered:{false,true}){
        const auto bytes=cmyk_test::psd(p->bytes,psb,depth,rle,layered);write(path,bytes);
        require(imaging::photoshopCmykProfile(path)->sha256==p->sha256,"Metadata preflight lost embedded profile");
        const auto imported=imaging::readPsd(path);require(imported.document.cmykProfile->bytes==p->bytes&&imported.document.layers.size()==1,"PSD profile/layer lost");
        const auto& layer=imported.document.layers[0];const auto white=layer.raster->pixel(0,0),black=layer.raster->pixel(1,0),cyan=layer.raster->pixel(2,0);
        require(white.r>240&&white.g>240&&white.b>240&&white.a==255,"PSD paper color/inversion");
        require(black.r<12&&black.g<12&&black.b<12&&black.a==255,"PSD black treated as alpha");
        require(cyan.a==(layered?128:255)&&cyan.g>cyan.r+40&&cyan.b>cyan.r+40,"PSD alpha or channel order");
        require(bool(layer.deep)==(depth==16),"PSD deep channel precision lost");
        if(layered)require(layer.mask&&layer.mask->raster->pixel(0,0)==255&&layer.mask->raster->pixel(3,0)==0,"PSD mask changed by color conversion");
    }
    auto oversized=cmyk_test::psd(p->bytes,false,16,false,false);oversized[14]=0;oversized[15]=0;oversized[16]=7;oversized[17]=208;oversized[18]=0;oversized[19]=0;oversized[20]=117;oversized[21]=48;
    bool budget=false;try{imaging::readPsd(oversized.data(),oversized.size());}catch(const std::exception& error){budget=std::string(error.what()).find("memory budget")!=std::string::npos;}require(budget,"Oversized CMYK planes reached allocation/decoding before budget check");
    auto spot=cmyk_test::psd(p->bytes,false,8,false,false);spot[13]=5;spot.insert(spot.end(),4,0);const auto withSpot=imaging::readPsd(spot.data(),spot.size());require(withSpot.document.layers[0].raster->pixel(0,0).a==255,"Extra merged spot/alpha channel became transparency");
    auto missing=cmyk_test::psd();write(path,missing);rejects([&]{imaging::photoshopCmykProfile(path);});rejects([&]{imaging::readPsd(path);});
    require(imaging::readPsd(path,p).document.cmykProfile==p,"Explicit fallback not used");
    auto incomplete=cmyk_test::psd(p->bytes,false,8,false,true,true);rejects([&]{imaging::readPsd(incomplete.data(),incomplete.size());});
    auto truncated=cmyk_test::psd(p->bytes,false,16,false,false);truncated.pop_back();rejects([&]{imaging::readPsd(truncated.data(),truncated.size());});
}
void persistenceContract(){
    QTemporaryDir temporary;const auto path=std::filesystem::path(temporary.path().toStdWString())/"ink.comp";auto d=document();const auto p=profile();
    ProjectStore store(makeWicProjectCodec());store.save(path,d,d.layers[0].id);require(store.load(path).readVersion==7,"RGB format version changed");
    std::optional<Document> current=d;History history;history.begin("CMYK Profile",current,d.layers[0].id);current->cmykProfile=p;history.end(current,d.layers[0].id);
    require(history.undoCount()==1,"Profile edit did not create exactly one undo");current=history.undo()->document;require(!current->cmykProfile,"Undo retained profile");current=history.redo()->document;require(current->cmykProfile==p,"Redo lost profile ownership");
    history.begin("No change",current,d.layers[0].id);history.end(current,d.layers[0].id);require(history.undoCount()==1,"Profile no-op changed history");
    store.save(path,*current,d.layers[0].id);auto reopened=store.load(path);require(reopened.readVersion==15&&reopened.document.cmykProfile->bytes==p->bytes&&reopened.document.cmykProfile->sha256==p->sha256,"Profile did not survive save/reopen");
    require(reopened.document.layers[0].raster->rgba()==d.layers[0].raster->rgba(),"Save/reopen changed RGB working pixels");
    ProjectStore failing(makeWicProjectCodec(),[](SaveFaultPoint point){if(point==SaveFaultPoint::AfterStage)throw std::runtime_error("injected failure");});rejects([&]{failing.save(path,d,d.layers[0].id);});require(store.load(path).document.cmykProfile->sha256==p->sha256,"Failed replacement lost original profile");
    auto tampered=p->bytes;tampered.back()^=1;write(path/"cmyk.icc",tampered);rejects([&]{store.load(path);});write(path/"cmyk.icc",p->bytes);
    std::filesystem::remove(path/"cmyk.icc");rejects([&]{store.load(path);});write(path/"cmyk.icc",p->bytes);
    QFile manifest(QString::fromStdWString((path/"manifest.json").wstring()));require(manifest.open(QIODevice::ReadWrite),"Read manifest");auto json=QJsonDocument::fromJson(manifest.readAll()).object();json["version"]=14;manifest.resize(0);manifest.write(QJsonDocument(json).toJson());manifest.close();rejects([&]{store.load(path);});
}
// Recover the PDF Flate stream as a temporary RGBA PNG to inspect all four raw
// ink samples with WIC. Do not use WicCodec: it would premultiply K as PNG alpha.
std::vector<uint8_t> pdfInks(const std::filesystem::path& pdfFile,const std::filesystem::path& pngFile){
    const auto bytes=read(pdfFile);const std::string pdf(bytes.begin(),bytes.end());const auto image=pdf.find("4 0 obj\n");const auto lengthAt=pdf.find("/Length ",image);const auto payload=pdf.find("stream\n",lengthAt)+7;
    require(image!=std::string::npos&&lengthAt!=std::string::npos&&payload>=7,"PDF image stream missing");const auto length=std::stoull(pdf.substr(lengthAt+8));require(payload<=bytes.size()&&length<=bytes.size()-payload,"PDF image stream bounds");
    cmyk_test::Bytes png{137,80,78,71,13,10,26,10};
    const auto chunk=[&](const char* type,const cmyk_test::Bytes& data){cmyk_test::integer(png,data.size(),4);const auto first=png.size();cmyk_test::ascii(png,std::string_view(type,4));cmyk_test::append(png,data);uint32_t crc=0xffffffffu;for(size_t i=first;i<png.size();++i){crc^=png[i];for(int bit=0;bit<8;++bit)crc=crc&1?(crc>>1)^0xedb88320u:crc>>1;}cmyk_test::integer(png,crc^0xffffffffu,4);};
    cmyk_test::Bytes header;cmyk_test::integer(header,4,4);cmyk_test::integer(header,1,4);header.insert(header.end(),{8,6,0,0,0});chunk("IHDR",header);chunk("IDAT",{bytes.begin()+std::ptrdiff_t(payload),bytes.begin()+std::ptrdiff_t(payload+length)});chunk("IEND",{});write(pngFile,png);
    const auto hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);require(SUCCEEDED(hr)||hr==RPC_E_CHANGED_MODE,"Initialize WIC test");struct Com{HRESULT hr;~Com(){if(SUCCEEDED(hr))CoUninitialize();}} apartment{hr};
    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;require(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory))),"Create WIC test factory");Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;require(SUCCEEDED(factory->CreateDecoderFromFilename(pngFile.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoder)),"Decode PDF carrier");Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;require(SUCCEEDED(decoder->GetFrame(0,&frame)),"PDF carrier frame");Microsoft::WRL::ComPtr<IWICFormatConverter> converted;require(SUCCEEDED(factory->CreateFormatConverter(&converted)),"PDF carrier converter");require(SUCCEEDED(converted->Initialize(frame.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom)),"Read straight PDF carrier bytes");std::vector<uint8_t> inks(16);require(SUCCEEDED(converted->CopyPixels(nullptr,16,16,inks.data())),"Copy PDF ink samples");return inks;
}
void exportContract(){
    const auto p=profile();QTemporaryDir temporary;const auto root=std::filesystem::path(temporary.path().toStdWString());const auto d=document();
    for(auto format:{imaging::ImageFormat::Jpeg,imaging::ImageFormat::Tiff,imaging::ImageFormat::Pdf}){
        const auto file=root/(format==imaging::ImageFormat::Jpeg?"ink.jpg":format==imaging::ImageFormat::Tiff?"ink.tif":"ink.pdf");
        imaging::ExportOptions options;options.format=format;options.cmykProfile=p;options.jpegQuality=1;
        const auto result=exportDocumentAtomic(d,file,options);require(result.preview.width==4&&result.preview.height==1&&result.encodedBytes==std::filesystem::file_size(file),"CMYK encoded preview dimensions");
        if(format==imaging::ImageFormat::Pdf){const auto bytes=read(file);const std::string pdf(bytes.begin(),bytes.end());require(pdf.find("/N 4 /Alternate /DeviceCMYK")!=std::string::npos&&pdf.find("/Colors 4")!=std::string::npos&&std::search(bytes.begin(),bytes.end(),p->bytes.begin(),p->bytes.end())!=bytes.end(),"PDF is not ICC CMYK");
            imaging::CmykTransform transform(p);std::vector<uint8_t> expected(16);const auto rgba=d.layers[0].raster->rgba();transform.encode(rgba,expected,{255,255,255});require(pdfInks(file,root/"carrier.png")==expected,"PDF reordered, premultiplied or lost CMYK samples");}
        else {
            const auto decoded=imaging::WicCodec::decode(file);require(decoded.metadata.cmykProfile&&decoded.metadata.cmykProfile->bytes==p->bytes,"Export is RGB or has no exact CMYK profile");
            require(decoded.image.width==4&&decoded.image.height==1&&std::abs(decoded.metadata.dpiX-150)<1,"CMYK dimensions/DPI lost");
            if(format==imaging::ImageFormat::Tiff){require(decoded.image.pixels[0]>240&&decoded.image.pixels[4]<12&&decoded.image.pixels[12]>240,"TIFF black/paper/matte changed");}
        }
        const auto original=read(file);options.cancelled=[]{return true;};rejects([&]{exportDocumentAtomic(d,file,options);});require(read(file)==original,"Cancelled CMYK export replaced destination");
        bool stop=false;options.cancelled=[&]{return stop;};rejects([&]{exportDocumentAtomic(d,file,options,{},[&](uint32_t,uint32_t){stop=true;});});require(read(file)==original,"Cancellation after encoding replaced destination");
    }
    // Strip only ICC APP2 segments from the produced JPEG; keep its CMYK samples.
    const auto jpeg=read(root/"ink.jpg");std::vector<uint8_t> unprofiled{0xff,0xd8};size_t at=2;int removed=0;
    while(at<jpeg.size()){require(at+4<=jpeg.size()&&jpeg[at]==0xff,"JPEG marker bounds");const auto marker=jpeg[at+1];if(marker==0xda){unprofiled.insert(unprofiled.end(),jpeg.begin()+std::ptrdiff_t(at),jpeg.end());break;}const size_t count=size_t(jpeg[at+2])*256+jpeg[at+3];require(count>=2&&count+2<=jpeg.size()-at,"JPEG segment bounds");const bool icc=marker==0xe2&&count>=14&&std::equal(jpeg.begin()+std::ptrdiff_t(at+4),jpeg.begin()+std::ptrdiff_t(at+15),"ICC_PROFILE");if(icc)++removed;else unprofiled.insert(unprofiled.end(),jpeg.begin()+std::ptrdiff_t(at),jpeg.begin()+std::ptrdiff_t(at+count+2));at+=count+2;}
    require(removed>0,"JPEG ICC segments missing");write(root/"unprofiled.jpg",unprofiled);rejects([&]{imaging::WicCodec::sourceCmykProfile(root/"unprofiled.jpg");});rejects([&]{imaging::WicCodec::decode(root/"unprofiled.jpg");});imaging::ImportOptions fallback;fallback.cmykFallback=p;require(imaging::WicCodec::decode(root/"unprofiled.jpg",fallback).metadata.cmykProfile==p,"WIC explicit CMYK fallback not used");
    imaging::ExportOptions png;png.cmykProfile=p;rejects([&]{exportDocumentAtomic(d,root/"invalid.png",png);});require(!std::filesystem::exists(root/"invalid.png"),"Rejected CMYK PNG created output");
    const auto deepBytes=cmyk_test::psd(p->bytes,false,16);const auto deep=imaging::readPsd(deepBytes.data(),deepBytes.size());rejects([&]{exportDocumentAtomic(deep.document,root/"invalid-deep.png",png);});require(!std::filesystem::exists(root/"invalid-deep.png"),"Deep PNG bypassed CMYK rejection");
    auto tagged=d;tagged.cmykProfile=p;png.cmykProfile.reset();exportDocumentAtomic(d,root/"before.png",png);exportDocumentAtomic(tagged,root/"after.png",png);
    require(imaging::WicCodec::decode(root/"before.png").image.pixels==imaging::WicCodec::decode(root/"after.png").image.pixels,"Document proof profile changed RGB export");
}
}
void cmykContract(const std::string& name){
    if(name=="profile")profileContract();else if(name=="psd")psdContract();else if(name=="persistence")persistenceContract();else if(name=="export")exportContract();else throw std::runtime_error("Unknown CMYK contract");
}
