#include "TextRaster.h"
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>
namespace compositor::text {
namespace {
using Microsoft::WRL::ComPtr;
void check(HRESULT hr){if(FAILED(hr))throw std::runtime_error("Text rasterization failed");}
std::wstring wide(std::string_view utf8){if(utf8.empty())return {};int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,utf8.data(),int(utf8.size()),nullptr,0);if(count<=0)throw std::runtime_error("Text is not valid UTF-8");std::wstring out(count,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,utf8.data(),int(utf8.size()),out.data(),count);return out;}
}
RasterizedText rasterize(const TextContent& text){
    if(text.value.empty()||text.fontFamily.empty()||!(text.fontSize>=1)||text.fontSize>1000)throw std::runtime_error("Invalid text layer");
    auto characters=wide(text.value);if(characters.size()>100000)throw std::runtime_error("Text exceeds limit");
    ComPtr<IDWriteFactory> write;check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(write.GetAddressOf())));
    ComPtr<IDWriteTextFormat> format;auto family=wide(text.fontFamily);check(write->CreateTextFormat(family.c_str(),nullptr,DWRITE_FONT_WEIGHT_REGULAR,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,float(text.fontSize),L"en-us",&format));
    ComPtr<IDWriteTextLayout> layout;check(write->CreateTextLayout(characters.c_str(),UINT32(characters.size()),format.Get(),100000,100000,&layout));
    for(const auto& run:text.fontRuns){auto name=wide(run.fontFamily);if(name.empty())continue;DWRITE_TEXT_RANGE range{UINT32(std::max(0,run.location)),UINT32(std::max(0,run.length))};if(range.startPosition>=characters.size())continue;range.length=std::min(range.length,UINT32(characters.size()-range.startPosition));layout->SetFontFamilyName(name.c_str(),range);if(run.fontSize>=1)layout->SetFontSize(float(run.fontSize),range);}
    DWRITE_TEXT_METRICS metrics{};check(layout->GetMetrics(&metrics));int width=std::clamp(int(std::ceil(metrics.widthIncludingTrailingWhitespace))+2,1,30000),height=std::clamp(int(std::ceil(metrics.height))+2,1,30000);
    ComPtr<IWICImagingFactory> wic;check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic)));
    ComPtr<IWICBitmap> bitmap;check(wic->CreateBitmap(width,height,GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,&bitmap));
    ComPtr<ID2D1Factory> d2d;check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,d2d.GetAddressOf()));
    ComPtr<ID2D1RenderTarget> target;D2D1_RENDER_TARGET_PROPERTIES props=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));check(d2d->CreateWicBitmapRenderTarget(bitmap.Get(),props,&target));
    auto brushFor=[&](double r,double g,double b){ComPtr<ID2D1SolidColorBrush> brush;check(target->CreateSolidColorBrush(D2D1::ColorF(float(r),float(g),float(b),float(text.alpha)),&brush));return brush;};
    target->BeginDraw();target->Clear(D2D1::ColorF(0,0));target->DrawTextLayout(D2D1::Point2F(1,1),layout.Get(),brushFor(text.red,text.green,text.blue).Get());
    for(const auto& run:text.colorRuns){if(run.location<0||run.length<=0||size_t(run.location)>=characters.size())continue;UINT32 length=UINT32(std::min(size_t(run.length),characters.size()-size_t(run.location)));ComPtr<IDWriteTextLayout> prefix;float origin=1;if(run.location>0){check(write->CreateTextLayout(characters.c_str(),UINT32(run.location),format.Get(),100000,100000,&prefix));for(const auto& font:text.fontRuns){if(font.location>=run.location)continue;DWRITE_TEXT_RANGE range{UINT32(font.location),UINT32(std::min(font.length,run.location-font.location))};auto name=wide(font.fontFamily);if(!name.empty())prefix->SetFontFamilyName(name.c_str(),range);if(font.fontSize>=1)prefix->SetFontSize(float(font.fontSize),range);}DWRITE_TEXT_METRICS prefixMetrics{};prefix->GetMetrics(&prefixMetrics);origin+=prefixMetrics.widthIncludingTrailingWhitespace;}
        ComPtr<IDWriteTextLayout> slice;check(write->CreateTextLayout(characters.c_str()+run.location,length,format.Get(),100000,100000,&slice));for(const auto& font:text.fontRuns){int start=std::max(font.location,run.location),end=std::min(font.location+font.length,run.location+int(length));if(end<=start)continue;DWRITE_TEXT_RANGE range{UINT32(start-run.location),UINT32(end-start)};auto name=wide(font.fontFamily);if(!name.empty())slice->SetFontFamilyName(name.c_str(),range);if(font.fontSize>=1)slice->SetFontSize(float(font.fontSize),range);}
        target->DrawTextLayout(D2D1::Point2F(origin,1),slice.Get(),brushFor(run.red,run.green,run.blue).Get());}
    check(target->EndDraw());
    ComPtr<IWICBitmapLock> lock;WICRect rect{0,0,width,height};check(bitmap->Lock(&rect,WICBitmapLockRead,&lock));UINT stride=0,size=0;BYTE* data=nullptr;check(lock->GetStride(&stride));check(lock->GetDataPointer(&size,&data));
    std::vector<uint8_t> rgba(size_t(width)*height*4);
    for(int y=0;y<height;++y){auto row=data+y*stride;auto dest=rgba.data()+size_t(y)*width*4;for(int x=0;x<width;++x){dest[x*4]=row[x*4+2];dest[x*4+1]=row[x*4+1];dest[x*4+2]=row[x*4];dest[x*4+3]=row[x*4+3];}}
    return {Raster::fromRgba(width,height,rgba.data(),size_t(width)*4),width,height};
}
TextLayout layoutText(const TextContent& text){
    TextLayout laid;
    if(text.value.empty()){
        const float size=float(std::clamp(text.fontSize,1.,1000.));
        laid.carets.push_back({1,1,size,0});
        laid.width=2;laid.height=int(std::ceil(size))+2;
        return laid;
    }
    auto drawn=rasterize(text);
    laid.raster=drawn.raster;laid.width=drawn.width;laid.height=drawn.height;
    auto characters=wide(text.value);
    ComPtr<IDWriteFactory> write;check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(write.GetAddressOf())));
    ComPtr<IDWriteTextFormat> format;auto family=wide(text.fontFamily);check(write->CreateTextFormat(family.c_str(),nullptr,DWRITE_FONT_WEIGHT_REGULAR,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,float(text.fontSize),L"en-us",&format));
    ComPtr<IDWriteTextLayout> layout;check(write->CreateTextLayout(characters.c_str(),UINT32(characters.size()),format.Get(),100000,100000,&layout));
    for(const auto& run:text.fontRuns){auto name=wide(run.fontFamily);if(name.empty())continue;DWRITE_TEXT_RANGE range{UINT32(std::max(0,run.location)),UINT32(std::max(0,run.length))};if(range.startPosition>=characters.size())continue;range.length=std::min(range.length,UINT32(characters.size()-range.startPosition));layout->SetFontFamilyName(name.c_str(),range);if(run.fontSize>=1)layout->SetFontSize(float(run.fontSize),range);}
    laid.carets.reserve(characters.size()+1);
    for(size_t index=0;index<=characters.size();++index){
        FLOAT x=0,y=0;DWRITE_HIT_TEST_METRICS metrics{};
        check(layout->HitTestTextPosition(UINT32(index),FALSE,&x,&y,&metrics));
        laid.carets.push_back({x+1,y+1,metrics.height>0?metrics.height:float(text.fontSize),0});
    }
    std::vector<float> tops;
    for(auto& caret:laid.carets){
        auto found=std::find_if(tops.begin(),tops.end(),[&](float top){return std::abs(top-caret.top)<0.5f;});
        if(found==tops.end()){caret.line=int(tops.size());tops.push_back(caret.top);}else caret.line=int(found-tops.begin());
    }
    return laid;
}
}
