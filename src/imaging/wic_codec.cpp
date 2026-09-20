#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "wic_codec.h"
#include "win32_file_path.h"
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <propvarutil.h>
#include <cmath>
#include <sstream>

namespace compositor::imaging {
using Microsoft::WRL::ComPtr;
namespace {
void ok(HRESULT hr,const char* what) { if(FAILED(hr)) { std::ostringstream s; s<<what<<" (HRESULT 0x"<<std::hex<<static_cast<unsigned long>(hr)<<")"; throw std::runtime_error(s.str()); } }
struct Apartment { HRESULT hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED); Apartment(){if(FAILED(hr)&&hr!=RPC_E_CHANGED_MODE)ok(hr,"Initialize COM");} ~Apartment(){if(SUCCEEDED(hr))CoUninitialize();} };
ComPtr<IWICImagingFactory> factory(){ComPtr<IWICImagingFactory> f;ok(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&f)),"Create WIC factory");return f;}
std::uint16_t orientation(IWICBitmapFrameDecode* frame){
    ComPtr<IWICMetadataQueryReader> r;if(FAILED(frame->GetMetadataQueryReader(&r)))return 1;
    for(auto query:{L"/app1/ifd/{ushort=274}",L"/ifd/{ushort=274}"}){PROPVARIANT v;PropVariantInit(&v);if(SUCCEEDED(r->GetMetadataByName(query,&v))){auto n=v.vt==VT_UI2?v.uiVal:1;PropVariantClear(&v);if(n>=1&&n<=8)return static_cast<std::uint16_t>(n);}else PropVariantClear(&v);}return 1;
}
ComPtr<IWICColorContext> srgb(IWICImagingFactory* f){ComPtr<IWICColorContext> c;ok(f->CreateColorContext(&c),"Create sRGB context");ok(c->InitializeFromExifColorSpace(1),"Initialize sRGB context");return c;}
ComPtr<IWICBitmapSource> convert(IWICImagingFactory* f,IWICBitmapSource* source,REFWICPixelFormatGUID format){ComPtr<IWICFormatConverter> c;ok(f->CreateFormatConverter(&c),"Create converter");ok(c->Initialize(source,format,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom),"Convert pixel format");ComPtr<IWICBitmapSource> out;ok(c.As(&out),"Query bitmap source");return out;}
void premultiply(std::vector<std::uint8_t>& p){for(std::size_t i=0;i<p.size();i+=4)for(int c=0;c<3;++c)p[i+c]=std::uint8_t((unsigned(p[i+c])*p[i+3]+127)/255);}
}
DecodedImage WicCodec::decode(const std::filesystem::path& filename,const ImportOptions& options){
    const auto path=win32FilePath(filename);
    checkCancelled(options);Apartment apartment;auto f=factory();ComPtr<IWICBitmapDecoder> decoder;
    ok(f->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoder),"Open image");
    GUID container{};ok(decoder->GetContainerFormat(&container),"Read image type");
    if(container!=GUID_ContainerFormatPng&&container!=GUID_ContainerFormatJpeg&&container!=GUID_ContainerFormatTiff)throw std::runtime_error("Choose PNG, JPEG, TIFF or bundled HEIC");
    ComPtr<IWICBitmapFrameDecode> frame;ok(decoder->GetFrame(0,&frame),"Decode first frame");UINT w=0,h=0;ok(frame->GetSize(&w,&h),"Read dimensions");
    auto bytes=checkedBytes(w,h,4,options);if(bytes>options.maxWorkingBytes/3)throw std::runtime_error("Image exceeds transient allocation budget");
    DecodedImage out;out.metadata.originalOrientation=orientation(frame.Get());out.metadata.decoder="Windows Imaging Component";
    if(FAILED(frame->GetResolution(&out.metadata.dpiX,&out.metadata.dpiY))){out.metadata.dpiX=out.metadata.dpiY=72;}
    ComPtr<IWICBitmapSource> source;ok(frame.As(&source),"Get frame source");UINT count=0;HRESULT colorHr=frame->GetColorContexts(0,nullptr,&count);
    if(FAILED(colorHr)&&colorHr!=WINCODEC_ERR_UNSUPPORTEDOPERATION)ok(colorHr,"Read color context count");
    if(count>16)throw std::runtime_error("Too many embedded color contexts");
    if(count){
        std::vector<ComPtr<IWICColorContext>> contexts(count);std::vector<IWICColorContext*> raw(count);
        for(UINT i=0;i<count;++i){ok(f->CreateColorContext(&contexts[i]),"Create embedded color context");raw[i]=contexts[i].Get();}
        ok(frame->GetColorContexts(count,raw.data(),&count),"Read embedded color contexts");
        auto dest=srgb(f.Get());ComPtr<IWICColorTransform> transform;ok(f->CreateColorTransformer(&transform),"Create color transform");
        ok(transform->Initialize(source.Get(),contexts[0].Get(),dest.Get(),GUID_WICPixelFormat32bppRGBA),"Normalize embedded profile to sRGB");
        ok(transform.As(&source),"Get normalized source");out.metadata.profileStatus=ProfileStatus::ConvertedEmbeddedProfile;
    }else{
        GUID format{};ok(frame->GetPixelFormat(&format),"Read pixel format");
        if(format==GUID_WICPixelFormat32bppCMYK || format==GUID_WICPixelFormat64bppCMYK)throw std::runtime_error("Unprofiled CMYK cannot be assumed sRGB");
        source=convert(f.Get(),source.Get(),GUID_WICPixelFormat32bppRGBA);
        out.metadata.profileStatus=ProfileStatus::AssumedSrgbNoProfile;
    }
    checkCancelled(options);std::vector<std::uint8_t> rgba(bytes);ok(source->CopyPixels(nullptr,w*4,static_cast<UINT>(bytes),rgba.data()),"Read RGBA pixels");
    const auto o=options.applyExifOrientation?out.metadata.originalOrientation:1;const bool transpose=o>=5;out.image.width=transpose?h:w;out.image.height=transpose?w:h;out.image.stride=std::size_t(out.image.width)*4;out.image.pixels.resize(bytes);
    for(UINT y=0;y<h;++y){checkCancelled(options);for(UINT x=0;x<w;++x){UINT dx=x,dy=y;
        switch(o){case 2:dx=w-1-x;break;case 3:dx=w-1-x;dy=h-1-y;break;case 4:dy=h-1-y;break;case 5:dx=y;dy=x;break;case 6:dx=h-1-y;dy=x;break;case 7:dx=h-1-y;dy=w-1-x;break;case 8:dx=y;dy=w-1-x;break;default:break;}
        std::copy_n(rgba.data()+(std::size_t(y)*w+x)*4,4,out.image.pixels.data()+std::size_t(dy)*out.image.stride+dx*4);
    }}
    if(transpose)std::swap(out.metadata.dpiX,out.metadata.dpiY);premultiply(out.image.pixels);return out;
}
void WicCodec::normalizeStraightRgba(std::span<std::uint8_t> rgba,std::uint32_t w,std::uint32_t h,std::span<const std::uint8_t> icc){
    if(rgba.size()!=checkedBytes(w,h,4)||icc.empty()||icc.size()>16*1024*1024)throw std::runtime_error("Invalid ICC conversion input");
    Apartment apartment;auto f=factory();ComPtr<IWICBitmap> bitmap;ok(f->CreateBitmapFromMemory(w,h,GUID_WICPixelFormat32bppRGBA,w*4,static_cast<UINT>(rgba.size()),rgba.data(),&bitmap),"Create ICC source bitmap");
    ComPtr<IWICColorContext> context;ok(f->CreateColorContext(&context),"Create ICC context");ok(context->InitializeFromMemory(const_cast<BYTE*>(icc.data()),static_cast<UINT>(icc.size())),"Read ICC profile");
    auto dest=srgb(f.Get());ComPtr<IWICColorTransform> transform;ok(f->CreateColorTransformer(&transform),"Create ICC transform");ok(transform->Initialize(bitmap.Get(),context.Get(),dest.Get(),GUID_WICPixelFormat32bppRGBA),"Normalize HEIC ICC to sRGB");
    std::vector<std::uint8_t> copy(rgba.size());ok(transform->CopyPixels(nullptr,w*4,static_cast<UINT>(copy.size()),copy.data()),"Read ICC transformed pixels");std::copy(copy.begin(),copy.end(),rgba.begin());
}
void WicCodec::encode(const std::filesystem::path& filename,const RgbaImage& image,const ExportOptions& options){
    const auto path=win32FilePath(filename);
    validate(image);if(!std::isfinite(options.dpi)||options.dpi<=0||!std::isfinite(options.jpegQuality))throw std::runtime_error("Invalid export settings");
    auto cancelled=[&]{if(options.cancelled&&options.cancelled())throw std::runtime_error("Image export cancelled");};cancelled();Apartment apartment;auto f=factory();
    GUID id{};ok(CoCreateGuid(&id),"Create output identifier");wchar_t guid[40]{};StringFromGUID2(id,guid,40);auto temp=path;temp+=std::wstring(L".")+guid+L".tmp";
    struct Cleanup{std::filesystem::path path;~Cleanup(){std::error_code ec;std::filesystem::remove(path,ec);}}cleanup{temp};
    {
    ComPtr<IWICStream> stream;ok(f->CreateStream(&stream),"Create export stream");ok(stream->InitializeFromFilename(temp.c_str(),GENERIC_WRITE),"Open temporary export");
    const GUID& container=options.format==ImageFormat::Jpeg?GUID_ContainerFormatJpeg:options.format==ImageFormat::Tiff?GUID_ContainerFormatTiff:GUID_ContainerFormatPng;
    ComPtr<IWICBitmapEncoder> encoder;ok(f->CreateEncoder(container,nullptr,&encoder),"Create encoder");ok(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache),"Initialize encoder");
    ComPtr<IWICBitmapFrameEncode> frame;ComPtr<IPropertyBag2> props;ok(encoder->CreateNewFrame(&frame,&props),"Create export frame");
    if(options.format==ImageFormat::Jpeg){PROPBAG2 p{};p.pstrName=const_cast<LPOLESTR>(L"ImageQuality");VARIANT v;VariantInit(&v);v.vt=VT_R4;v.fltVal=static_cast<float>(std::clamp(options.jpegQuality,0.0,1.0));ok(props->Write(1,&p,&v),"Set JPEG quality");}
    ok(frame->Initialize(props.Get()),"Initialize frame");ok(frame->SetSize(image.width,image.height),"Set export dimensions");ok(frame->SetResolution(options.dpi,options.dpi),"Set DPI");
    if(options.format!=ImageFormat::Png){
        wchar_t system[MAX_PATH]{};if(!GetSystemDirectoryW(system,MAX_PATH))throw std::runtime_error("Cannot locate Windows color profiles");
        auto profile=std::filesystem::path(system)/L"spool/drivers/color/sRGB Color Space Profile.icm";
        ComPtr<IWICColorContext> color;ok(f->CreateColorContext(&color),"Create export profile");ok(color->InitializeFromFilename(profile.c_str()),"Load Windows sRGB profile");IWICColorContext* colors[]={color.Get()};ok(frame->SetColorContexts(1,colors),"Set export sRGB profile");
    }
    if(options.format==ImageFormat::Png){ComPtr<IWICMetadataQueryWriter> metadata;ok(frame->GetMetadataQueryWriter(&metadata),"Get PNG metadata");PROPVARIANT v;PropVariantInit(&v);v.vt=VT_UI1;v.bVal=0;ok(metadata->SetMetadataByName(L"/sRGB/RenderingIntent",&v),"Write PNG sRGB declaration");}
    const auto bytes=checkedBytes(image.width,image.height,4);std::vector<std::uint8_t> straight(bytes);
    const std::uint8_t matte[]={options.matteR,options.matteG,options.matteB};
    for(std::uint32_t y=0;y<image.height;++y){cancelled();for(std::uint32_t x=0;x<image.width;++x){const auto* p=&image.pixels[y*image.stride+x*4];auto* q=&straight[(std::size_t(y)*image.width+x)*4];
        for(int c=0;c<3;++c)q[c]=options.format==ImageFormat::Jpeg?std::uint8_t(std::min(255U,unsigned(p[c])+(unsigned(matte[c])*(255-p[3])+127)/255)):p[3]?std::uint8_t(std::min(255U,(unsigned(p[c])*255+p[3]/2)/p[3])):0;
        q[3]=options.format==ImageFormat::Jpeg?255:p[3];
    }}
    ComPtr<IWICBitmap> bitmap;ok(f->CreateBitmapFromMemory(image.width,image.height,GUID_WICPixelFormat32bppRGBA,image.width*4,static_cast<UINT>(bytes),straight.data(),&bitmap),"Create export bitmap");
    GUID format=options.format==ImageFormat::Jpeg?GUID_WICPixelFormat24bppBGR:GUID_WICPixelFormat32bppBGRA;ok(frame->SetPixelFormat(&format),"Negotiate encoded pixel format");auto source=convert(f.Get(),bitmap.Get(),format);ok(frame->WriteSource(source.Get(),nullptr),"Encode pixels");cancelled();ok(frame->Commit(),"Commit export frame");ok(encoder->Commit(),"Commit export container");
    }cancelled();if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Atomic export replacement failed");
}
}
