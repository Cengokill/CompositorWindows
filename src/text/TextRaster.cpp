#include "TextRaster.h"
#include "TextStyle.h"
#include <d2d1.h>
#include <dwrite.h>
#include <dwrite_1.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cwctype>
#include <stdexcept>
#include <string>
#include <vector>
namespace compositor::text {
namespace {
using Microsoft::WRL::ComPtr;
constexpr float kPad=12.f;
void check(HRESULT hr){if(FAILED(hr))throw std::runtime_error("Text rasterization failed");}
std::wstring wide(std::string_view utf8){if(utf8.empty())return {};int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,utf8.data(),int(utf8.size()),nullptr,0);if(count<=0)throw std::runtime_error("Text is not valid UTF-8");std::wstring out(count,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,utf8.data(),int(utf8.size()),out.data(),count);return out;}
std::wstring lower(std::wstring text){for(auto& c:text)c=towlower(c);return text;}
struct Face {std::wstring family{L"Segoe UI"};DWRITE_FONT_WEIGHT weight{DWRITE_FONT_WEIGHT_REGULAR};DWRITE_FONT_STYLE style{DWRITE_FONT_STYLE_NORMAL};DWRITE_FONT_STRETCH stretch{DWRITE_FONT_STRETCH_NORMAL};};
void applyStyleWords(Face& face,const std::wstring& name){
    const bool italic=name.find(L"italic")!=std::wstring::npos||name.find(L"oblique")!=std::wstring::npos||name.find(L"italique")!=std::wstring::npos;
    const bool bold=name.find(L"bold")!=std::wstring::npos||name.find(L"gras")!=std::wstring::npos;
    if(name.find(L"semibold")!=std::wstring::npos||name.find(L"semi bold")!=std::wstring::npos||name.find(L"demi")!=std::wstring::npos)face.weight=DWRITE_FONT_WEIGHT_SEMI_BOLD;
    else if(name.find(L"black")!=std::wstring::npos||name.find(L"heavy")!=std::wstring::npos)face.weight=DWRITE_FONT_WEIGHT_BLACK;
    else if(name.find(L"extralight")!=std::wstring::npos||name.find(L"ultra light")!=std::wstring::npos)face.weight=DWRITE_FONT_WEIGHT_EXTRA_LIGHT;
    else if(name.find(L"light")!=std::wstring::npos)face.weight=DWRITE_FONT_WEIGHT_LIGHT;
    else if(name.find(L"thin")!=std::wstring::npos)face.weight=DWRITE_FONT_WEIGHT_THIN;
    else if(bold)face.weight=DWRITE_FONT_WEIGHT_BOLD;
    if(name.find(L"oblique")!=std::wstring::npos)face.style=DWRITE_FONT_STYLE_OBLIQUE;
    else if(italic)face.style=DWRITE_FONT_STYLE_ITALIC;
    if(name.find(L"condensed")!=std::wstring::npos)face.stretch=DWRITE_FONT_STRETCH_CONDENSED;
    else if(name.find(L"expanded")!=std::wstring::npos)face.stretch=DWRITE_FONT_STRETCH_EXPANDED;
}
bool faceNameMatches(IDWriteFont* font,const std::wstring& wanted){
    ComPtr<IDWriteLocalizedStrings> names;if(FAILED(font->GetFaceNames(&names))||!names)return false;
    const UINT32 count=names->GetCount();
    for(UINT32 index=0;index<count;++index){
        UINT32 length=0;if(FAILED(names->GetStringLength(index,&length)))continue;
        std::wstring value(length+1,L'\0');if(FAILED(names->GetString(index,value.data(),length+1)))continue;
        value.resize(length);if(lower(value)==wanted)return true;
    }
    return false;
}
Face resolveFace(IDWriteFactory* factory,const std::string& family,const std::string& styleName){
    Face face;face.family=wide(family.empty()?"Segoe UI":family);
    const auto wanted=lower(wide(styleName.empty()?"Regular":styleName));
    applyStyleWords(face,wanted);
    ComPtr<IDWriteFontCollection> fonts;if(!factory||FAILED(factory->GetSystemFontCollection(&fonts))||!fonts)return face;
    UINT32 index=0;BOOL exists=FALSE;
    if(FAILED(fonts->FindFamilyName(face.family.c_str(),&index,&exists))||!exists){
        face.family=L"Segoe UI";fonts->FindFamilyName(face.family.c_str(),&index,&exists);if(!exists)return face;
    }
    ComPtr<IDWriteFontFamily> fontFamily;if(FAILED(fonts->GetFontFamily(index,&fontFamily))||!fontFamily)return face;
    const UINT32 count=fontFamily->GetFontCount();
    for(UINT32 i=0;i<count;++i){
        ComPtr<IDWriteFont> font;if(FAILED(fontFamily->GetFont(i,&font))||!font)continue;
        if(!faceNameMatches(font.Get(),wanted))continue;
        face.weight=font->GetWeight();face.style=font->GetStyle();face.stretch=font->GetStretch();return face;
    }
    ComPtr<IDWriteFont> matched;
    if(SUCCEEDED(fontFamily->GetFirstMatchingFont(face.weight,face.stretch,face.style,&matched))&&matched){
        face.weight=matched->GetWeight();face.style=matched->GetStyle();face.stretch=matched->GetStretch();
    }
    return face;
}
void applyFace(IDWriteTextLayout* layout,const Face& face,DWRITE_TEXT_RANGE range){
    layout->SetFontFamilyName(face.family.c_str(),range);
    layout->SetFontWeight(face.weight,range);
    layout->SetFontStyle(face.style,range);
    layout->SetFontStretch(face.stretch,range);
}
DWRITE_TEXT_ALIGNMENT alignmentOf(TextAlignment alignment){
    if(alignment==TextAlignment::Center)return DWRITE_TEXT_ALIGNMENT_CENTER;
    if(alignment==TextAlignment::Right)return DWRITE_TEXT_ALIGNMENT_TRAILING;
    return DWRITE_TEXT_ALIGNMENT_LEADING;
}
struct Built {
    ComPtr<IDWriteFactory> write;
    ComPtr<IDWriteTextLayout> layout;
    std::wstring characters;
    bool box{};
    int width{},height{};
};
Built build(const TextContent& text){
    if(!textRunsValid(text)||text.fontFamily.empty())throw std::runtime_error("Invalid text layer");
    Built built;built.characters=wide(text.value);
    if(built.characters.size()>100000)throw std::runtime_error("Text exceeds limit");
    check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(built.write.GetAddressOf())));
    const Face base=resolveFace(built.write.Get(),text.fontFamily,text.fontStyle);
    const float line=float(lineHeight(text));
    const bool box=hasTextBox(text);
    built.box=box;
    ComPtr<IDWriteTextFormat> format;
    check(built.write->CreateTextFormat(base.family.c_str(),nullptr,base.weight,base.style,base.stretch,float(text.fontSize),L"en-us",&format));
    format->SetWordWrapping(box?DWRITE_WORD_WRAPPING_WRAP:DWRITE_WORD_WRAPPING_NO_WRAP);
    format->SetTextAlignment(alignmentOf(text.alignment));
    const float wrap=box?std::max(1.f,float(*text.boxWidth)-kPad*2):100000.f;
    check(built.write->CreateTextLayout(built.characters.c_str(),UINT32(built.characters.size()),format.Get(),wrap,100000,&built.layout));
    DWRITE_TEXT_RANGE all{0,UINT32(std::max<size_t>(built.characters.size(),1))};
    applyFace(built.layout.Get(),base,all);
    for(const auto& run:text.fontRuns){
        if(!run.hasFont||run.location<0||run.length<=0||size_t(run.location)>=built.characters.size())continue;
        DWRITE_TEXT_RANGE range{UINT32(run.location),UINT32(std::min(size_t(run.length),built.characters.size()-size_t(run.location)))};
        auto face=resolveFace(built.write.Get(),run.fontFamily,run.fontStyle);
        applyFace(built.layout.Get(),face,range);
        if(run.fontSize>=1)built.layout->SetFontSize(float(run.fontSize),range);
    }
    built.layout->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM,line,std::min(line,line*0.8f));
    if(text.tracking!=0){
        ComPtr<IDWriteTextLayout1> spacing;check(built.layout.As(&spacing));
        check(spacing->SetCharacterSpacing(0,float(text.tracking),0,{0,UINT32(built.characters.size())}));
    }
    if(!box){
        DWRITE_TEXT_METRICS metrics{};check(built.layout->GetMetrics(&metrics));
        const float natural=std::max(1.f,metrics.widthIncludingTrailingWhitespace);
        built.layout->SetMaxWidth(natural);
        built.layout->SetTextAlignment(alignmentOf(text.alignment));
        check(built.layout->GetMetrics(&metrics));
        built.width=std::clamp(int(std::ceil(metrics.widthIncludingTrailingWhitespace+kPad*2+float(text.fontSize)*0.1f)),16,30000);
        built.height=std::clamp(int(std::ceil(std::max(metrics.height,line)+kPad*2)),16,30000);
    }else{
        built.width=std::clamp(int(std::lround(*text.boxWidth)),16,30000);
        built.height=std::clamp(int(std::lround(*text.boxHeight)),16,30000);
    }
    return built;
}
void collectCarets(const Built& built,const TextContent& text,TextLayout& laid){
    laid.carets.reserve(built.characters.size()+1);
    const float fallback=float(lineHeight(text));
    for(size_t index=0;index<=built.characters.size();++index){
        FLOAT x=0,y=0;DWRITE_HIT_TEST_METRICS metrics{};
        check(built.layout->HitTestTextPosition(UINT32(index),FALSE,&x,&y,&metrics));
        laid.carets.push_back({x+kPad,y+kPad,metrics.height>0?metrics.height:fallback,0});
    }
    std::vector<float> tops;
    for(auto& caret:laid.carets){
        auto found=std::find_if(tops.begin(),tops.end(),[&](float top){return std::abs(top-caret.top)<0.5f;});
        if(found==tops.end()){caret.line=int(tops.size());tops.push_back(caret.top);}else caret.line=int(found-tops.begin());
    }
}
RasterizedText draw(const Built& built,const TextContent& text){
    ComPtr<IWICImagingFactory> wic;check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic)));
    ComPtr<IWICBitmap> bitmap;check(wic->CreateBitmap(UINT(built.width),UINT(built.height),GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,&bitmap));
    ComPtr<ID2D1Factory> d2d;check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,d2d.GetAddressOf()));
    ComPtr<ID2D1RenderTarget> target;D2D1_RENDER_TARGET_PROPERTIES props=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));check(d2d->CreateWicBitmapRenderTarget(bitmap.Get(),props,&target));
    auto brushFor=[&](double r,double g,double b){ComPtr<ID2D1SolidColorBrush> brush;check(target->CreateSolidColorBrush(D2D1::ColorF(float(r),float(g),float(b),float(text.alpha)),&brush));return brush;};
    std::vector<ComPtr<ID2D1SolidColorBrush>> brushes;
    target->BeginDraw();target->Clear(D2D1::ColorF(0,0));
    auto base=brushFor(text.red,text.green,text.blue);
    for(const auto& run:text.colorRuns){
        if(!run.hasColor||run.location<0||run.length<=0||size_t(run.location)>=built.characters.size())continue;
        DWRITE_TEXT_RANGE range{UINT32(run.location),UINT32(std::min(size_t(run.length),built.characters.size()-size_t(run.location)))};
        brushes.push_back(brushFor(run.red,run.green,run.blue));
        built.layout->SetDrawingEffect(brushes.back().Get(),range);
    }
    target->DrawTextLayout(D2D1::Point2F(kPad,kPad),built.layout.Get(),base.Get());
    check(target->EndDraw());
    ComPtr<IWICBitmapLock> lock;WICRect rect{0,0,built.width,built.height};check(bitmap->Lock(&rect,WICBitmapLockRead,&lock));UINT stride=0,size=0;BYTE* data=nullptr;check(lock->GetStride(&stride));check(lock->GetDataPointer(&size,&data));
    std::vector<uint8_t> rgba(size_t(built.width)*built.height*4);
    for(int y=0;y<built.height;++y){auto row=data+y*stride;auto dest=rgba.data()+size_t(y)*built.width*4;for(int x=0;x<built.width;++x){dest[x*4]=row[x*4+2];dest[x*4+1]=row[x*4+1];dest[x*4+2]=row[x*4];dest[x*4+3]=row[x*4+3];}}
    return {Raster::fromRgba(built.width,built.height,rgba.data(),size_t(built.width)*4),built.width,built.height};
}
}
RasterizedText rasterize(const TextContent& text){
    if(text.value.empty())throw std::runtime_error("Invalid text layer");
    auto built=build(text);return draw(built,text);
}
TextLayout layoutText(const TextContent& text,bool includeRaster){
    TextLayout laid;
    if(text.value.empty()){
        if(!textRunsValid(text))throw std::runtime_error("Invalid text layer");
        const float line=float(lineHeight(text));
        if(hasTextBox(text)){laid.width=std::clamp(int(std::lround(*text.boxWidth)),16,30000);laid.height=std::clamp(int(std::lround(*text.boxHeight)),16,30000);}
        else{laid.width=std::clamp(int(std::ceil(kPad*2+float(text.fontSize)*0.1f)),16,30000);laid.height=std::clamp(int(std::ceil(line+kPad*2)),16,30000);}
        laid.carets.push_back({kPad,kPad,line,0});
        return laid;
    }
    auto built=build(text);
    if(includeRaster){
        auto drawn=draw(built,text);
        laid.raster=drawn.raster;laid.width=drawn.width;laid.height=drawn.height;
    }else{
        laid.width=built.width;laid.height=built.height;
    }
    collectCarets(built,text,laid);
    return laid;
}
}
