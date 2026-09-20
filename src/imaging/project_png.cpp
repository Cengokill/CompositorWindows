#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "wic_codec.h"
#include "win32_file_path.h"
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <array>
#include <cstring>
#include <fstream>
#include <sstream>
namespace compositor::imaging {
namespace {
using Microsoft::WRL::ComPtr;
void ok(HRESULT h,const char* operation="Project PNG operation",const std::filesystem::path* path=nullptr){if(FAILED(h)){std::ostringstream message;message<<operation<<" failed (HRESULT 0x"<<std::hex<<static_cast<unsigned long>(h)<<')';if(path){const auto utf8=path->generic_u8string();message<<" path_length="<<std::dec<<path->native().size()<<" path="<<std::string(utf8.begin(),utf8.end());}throw std::runtime_error(message.str());}}
struct Apartment{HRESULT h=CoInitializeEx(nullptr,COINIT_MULTITHREADED);Apartment(){if(FAILED(h)&&h!=RPC_E_CHANGED_MODE)ok(h);}~Apartment(){if(SUCCEEDED(h))CoUninitialize();}};
std::uint32_t big(const std::uint8_t* p){return std::uint32_t(p[0])<<24|std::uint32_t(p[1])<<16|std::uint32_t(p[2])<<8|p[3];}
void checkPng(const std::filesystem::path& filename,bool mask,const ImportOptions& options){
    const auto path=win32FilePath(filename);
    const auto size=std::filesystem::file_size(path);if(size<45||size>512ULL*1024*1024)throw std::runtime_error("Project PNG file size invalid");std::ifstream stream(path,std::ios::binary);std::array<std::uint8_t,33> h{};stream.read(reinterpret_cast<char*>(h.data()),h.size());
    const std::uint8_t sig[]={137,80,78,71,13,10,26,10};if(!stream||memcmp(h.data(),sig,8)||big(h.data()+8)!=13||memcmp(h.data()+12,"IHDR",4)||h[24]>8||h[24]==0||(mask&&h[25]!=0))throw std::runtime_error("Project assets require <=8-bit PNG; masks require grayscale without alpha");checkedBytes(big(h.data()+16),big(h.data()+20),mask?1:4,options);
    std::uint64_t offset=33;bool end=false;while(offset+12<=size){std::array<std::uint8_t,8> chunk{};stream.read(reinterpret_cast<char*>(chunk.data()),8);if(!stream)throw std::runtime_error("Truncated PNG chunk");auto length=big(chunk.data());if(std::uint64_t(length)+12>size-offset)throw std::runtime_error("PNG chunk exceeds file");if(memcmp(chunk.data()+4,"acTL",4)==0)throw std::runtime_error("Animated project PNG is unsupported");if(mask&&memcmp(chunk.data()+4,"tRNS",4)==0)throw std::runtime_error("Mask PNG has transparency");if(memcmp(chunk.data()+4,"IEND",4)==0){if(length)throw std::runtime_error("Malformed PNG end");end=true;break;}stream.seekg(std::streamoff(length)+4,std::ios::cur);offset+=std::uint64_t(length)+12;}
    if(!end)throw std::runtime_error("PNG end chunk missing");
}
}
RgbaImage WicCodec::decodeProjectRgbaPng(const std::filesystem::path& path,const ImportOptions& options){checkPng(path,false,options);auto project=options;project.applyExifOrientation=false;return decode(path,project).image;}
GrayMask WicCodec::decodeProjectGrayPng(const std::filesystem::path& filename,const ImportOptions& options){
    const auto path=win32FilePath(filename);
    checkPng(path,true,options);checkCancelled(options);Apartment apartment;ComPtr<IWICImagingFactory> factory;ok(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));ComPtr<IWICBitmapDecoder> decoder;ok(factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoder));UINT count;ok(decoder->GetFrameCount(&count));if(count!=1)throw std::runtime_error("Project PNG must have one frame");ComPtr<IWICBitmapFrameDecode> frame;ok(decoder->GetFrame(0,&frame));UINT w,h;ok(frame->GetSize(&w,&h));GrayMask out{w,h,w,std::vector<std::uint8_t>(checkedBytes(w,h,1,options))};ComPtr<IWICFormatConverter> convert;ok(factory->CreateFormatConverter(&convert));ok(convert->Initialize(frame.Get(),GUID_WICPixelFormat8bppGray,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom));ok(convert->CopyPixels(nullptr,w,UINT(out.pixels.size()),out.pixels.data()));checkCancelled(options);return out;
}
void WicCodec::encodeProjectGrayPng(const std::filesystem::path& filename,const GrayMask& mask){
    const auto path=win32FilePath(filename);
    validate(mask);Apartment apartment;GUID id{};ok(CoCreateGuid(&id));wchar_t guid[40]{};StringFromGUID2(id,guid,40);auto temp=path;temp+=std::wstring(L".")+guid+L".tmp";struct Cleanup{std::filesystem::path p;~Cleanup(){std::error_code ec;std::filesystem::remove(p,ec);}}cleanup{temp};
    {ComPtr<IWICImagingFactory> factory;ok(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));ComPtr<IWICStream> stream;ok(factory->CreateStream(&stream));ok(stream->InitializeFromFilename(temp.c_str(),GENERIC_WRITE),"Open gray PNG temporary export",&temp);ComPtr<IWICBitmapEncoder> encoder;ok(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder));ok(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache));ComPtr<IWICBitmapFrameEncode> frame;ComPtr<IPropertyBag2> props;ok(encoder->CreateNewFrame(&frame,&props));ok(frame->Initialize(props.Get()));ok(frame->SetSize(mask.width,mask.height));GUID format=GUID_WICPixelFormat8bppGray;ok(frame->SetPixelFormat(&format));if(format!=GUID_WICPixelFormat8bppGray)throw std::runtime_error("Encoder cannot preserve gray8");for(UINT y=0;y<mask.height;++y)ok(frame->WritePixels(1,mask.width,mask.width,const_cast<BYTE*>(mask.pixels.data()+std::size_t(y)*mask.stride)));ok(frame->Commit());ok(encoder->Commit());}
    if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Project mask replacement failed");
}
}
