#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "wic_codec.h"
#include "core/DeepImage.h"
#include "win32_file_path.h"
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
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
std::shared_ptr<const DeepImage> WicCodec::decodeProjectDeepPng(const std::filesystem::path& filename,const ImportOptions& options){
    const auto path=win32FilePath(filename);checkCancelled(options);
    const auto size=std::filesystem::file_size(path);if(size<45||size>512ULL*1024*1024)throw std::runtime_error("Project PNG file size invalid");
    std::ifstream stream(path,std::ios::binary);std::array<std::uint8_t,33> h{};stream.read(reinterpret_cast<char*>(h.data()),h.size());
    const std::uint8_t sig[]={137,80,78,71,13,10,26,10};
    if(!stream||memcmp(h.data(),sig,8)||big(h.data()+8)!=13||memcmp(h.data()+12,"IHDR",4)||h[24]!=16||h[25]!=6)throw std::runtime_error("Project 16-bit PNG must be 16-bit RGBA");
    const auto width=big(h.data()+16),height=big(h.data()+20);checkedBytes(width,height,4,options);
    Apartment apartment;ComPtr<IWICImagingFactory> factory;ok(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));
    ComPtr<IWICBitmapDecoder> decoder;ok(factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoder));
    UINT count=0;ok(decoder->GetFrameCount(&count));if(count!=1)throw std::runtime_error("Project PNG must have one frame");
    ComPtr<IWICBitmapFrameDecode> frame;ok(decoder->GetFrame(0,&frame));
    ComPtr<IWICFormatConverter> convert;ok(factory->CreateFormatConverter(&convert));
    ok(convert->Initialize(frame.Get(),GUID_WICPixelFormat64bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom));
    auto image=std::make_shared<DeepImage>();image->width=int(width);image->height=int(height);image->samples.resize(std::size_t(width)*height*4);
    ok(convert->CopyPixels(nullptr,width*8,UINT(image->samples.size()*sizeof(std::uint16_t)),reinterpret_cast<BYTE*>(image->samples.data())));
    for(std::uint32_t y=0;y<height;++y){checkCancelled(options);for(std::uint32_t x=0;x<width;++x){auto* pixel=image->at(int(x),int(y));const auto alpha=pixel[3];for(int c=0;c<3;++c)pixel[c]=alpha?std::uint16_t((std::uint32_t(pixel[c])*alpha+32767)/65535):0;}}
    return image;
}
void WicCodec::encodeProjectDeepPng(const std::filesystem::path& filename,const DeepImage& image){
    const auto path=win32FilePath(filename);if(!image.valid())throw std::runtime_error("Invalid 16-bit image");
    Apartment apartment;GUID id{};ok(CoCreateGuid(&id));wchar_t guid[40]{};StringFromGUID2(id,guid,40);auto temp=path;temp+=std::wstring(L".")+guid+L".tmp";
    struct Cleanup{std::filesystem::path p;~Cleanup(){std::error_code ec;std::filesystem::remove(p,ec);}} cleanup{temp};
    {ComPtr<IWICImagingFactory> factory;ok(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));ComPtr<IWICStream> stream;ok(factory->CreateStream(&stream));ok(stream->InitializeFromFilename(temp.c_str(),GENERIC_WRITE));
    ComPtr<IWICBitmapEncoder> encoder;ok(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder));ok(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache));
    ComPtr<IWICBitmapFrameEncode> frame;ComPtr<IPropertyBag2> props;ok(encoder->CreateNewFrame(&frame,&props));ok(frame->Initialize(props.Get()));ok(frame->SetSize(UINT(image.width),UINT(image.height)));
    GUID format=GUID_WICPixelFormat64bppRGBA;ok(frame->SetPixelFormat(&format));if(format!=GUID_WICPixelFormat64bppRGBA)throw std::runtime_error("Encoder cannot preserve 16-bit RGBA");
    std::vector<std::uint16_t> row(std::size_t(image.width)*4);
    for(int y=0;y<image.height;++y){for(int x=0;x<image.width;++x){const auto* pixel=image.at(x,y);const auto alpha=pixel[3];auto* out=row.data()+std::size_t(x)*4;out[3]=alpha;for(int c=0;c<3;++c)out[c]=alpha?std::uint16_t(std::min(65535u,(std::uint32_t(pixel[c])*65535+alpha/2)/alpha)):0;}ok(frame->WritePixels(1,UINT(image.width)*8,UINT(row.size()*sizeof(std::uint16_t)),reinterpret_cast<BYTE*>(row.data())));}
    ok(frame->Commit());ok(encoder->Commit());}
    if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Project image replacement failed");
}
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
