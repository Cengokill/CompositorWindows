// Selection.swift and MagicWand.swift at a19db9011282399785dc18efcfded904627bdcc2.
// Copyright (c) 2026 Wonder Assembly LLC; MIT notice in graphics/upstream/LICENSE.
#include "Selection.h"
#include <windows.h>
#include <d2d1.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <cstdlib>
extern "C" {
#include "graphics/upstream/WandPixels.h"
}

namespace compositor::editing {
using Microsoft::WRL::ComPtr;
namespace {
void check(HRESULT hr) { if (FAILED(hr)) throw std::runtime_error("Direct2D selection operation failed: " + std::to_string(uint32_t(hr))); }
void sizeCheck(int w,int h) { if(w<1||h<1||w>30000||h>30000||uint64_t(w)*h>100000000) throw std::runtime_error("Selection raster exceeds pixel budget"); }
void grayCheck(const GrayRaster& g) { sizeCheck(g.width,g.height); if(g.pixels.size()!=size_t(g.width)*g.height) throw std::runtime_error("Invalid selection coverage"); }
void pointCheck(Point p) { if(!std::isfinite(p.x)||!std::isfinite(p.y)||std::abs(p.x)>10000000||std::abs(p.y)>10000000) throw std::runtime_error("Invalid selection coordinate"); }
void rectCheck(Rect r) { pointCheck({r.x,r.y}); pointCheck({r.x+r.width,r.y+r.height}); if(r.width<0||r.height<0)throw std::runtime_error("Invalid selection rectangle"); }
ComPtr<ID2D1Factory> factory() {
    static const auto value=[] { ComPtr<ID2D1Factory> f; check(D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED,f.GetAddressOf())); return f; }();
    return value;
}
struct Apartment {
    HRESULT result=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    Apartment(){if(FAILED(result)&&result!=RPC_E_CHANGED_MODE)check(result);}
    ~Apartment(){if(SUCCEEDED(result))CoUninitialize();}
};
ComPtr<ID2D1Geometry> emptyGeometry() {
    ComPtr<ID2D1PathGeometry> path;check(factory()->CreatePathGeometry(&path));ComPtr<ID2D1GeometrySink> sink;check(path->Open(&sink));check(sink->Close());return path;
}
uint8_t byte(double value){return uint8_t(std::clamp(std::lround(value),0L,255L));}
}
struct SelectionOutline::Impl { ComPtr<ID2D1Geometry> geometry; bool aa{true}; };
SelectionOutline::SelectionOutline():impl_(std::make_shared<Impl>(Impl{emptyGeometry(),true})){}
SelectionOutline::SelectionOutline(std::shared_ptr<const Impl> p):impl_(std::move(p)){}
SelectionOutline SelectionOutline::rectangle(Rect r,bool aa) {
    rectCheck(r);if(r.empty())return SelectionOutline(std::make_shared<Impl>(Impl{emptyGeometry(),aa}));
    ComPtr<ID2D1RectangleGeometry> g;check(factory()->CreateRectangleGeometry(D2D1::RectF(float(r.x),float(r.y),float(r.x+r.width),float(r.y+r.height)),&g));
    return SelectionOutline(std::make_shared<Impl>(Impl{g,aa}));
}
SelectionOutline SelectionOutline::ellipse(Rect r,bool aa) {
    rectCheck(r);if(r.empty())return SelectionOutline(std::make_shared<Impl>(Impl{emptyGeometry(),aa}));
    ComPtr<ID2D1EllipseGeometry> g;check(factory()->CreateEllipseGeometry(D2D1::Ellipse(D2D1::Point2F(float(r.x+r.width/2),float(r.y+r.height/2)),float(r.width/2),float(r.height/2)),&g));
    return SelectionOutline(std::make_shared<Impl>(Impl{g,aa}));
}
SelectionOutline SelectionOutline::roundedRectangle(Rect r,double radius,bool aa){
    rectCheck(r);if(!std::isfinite(radius))throw std::runtime_error("Invalid corner radius");radius=std::min({std::max(0.,radius),r.width/2,r.height/2});if(radius==0||r.empty())return rectangle(r,aa);
    ComPtr<ID2D1RoundedRectangleGeometry> g;auto rounded=D2D1::RoundedRect(D2D1::RectF(float(r.x),float(r.y),float(r.x+r.width),float(r.y+r.height)),float(radius),float(radius));check(factory()->CreateRoundedRectangleGeometry(rounded,&g));return SelectionOutline(std::make_shared<Impl>(Impl{g,aa}));
}
SelectionOutline SelectionOutline::fromCoverage(const GrayRaster& mask,bool aa){
    grayCheck(mask);int32_t* rawPoints=nullptr;int32_t* rawLoops=nullptr;size_t pointCount=0,loopCount=0;int status=wand_trace(mask.pixels.data(),size_t(mask.width),size_t(mask.height),&rawPoints,&pointCount,&rawLoops,&loopCount);
    std::unique_ptr<int32_t,decltype(&std::free)> points(rawPoints,&std::free),loops(rawLoops,&std::free);
    if(status==-2)throw std::runtime_error("Selection is too detailed to outline; reduce coverage detail");if(status!=0)throw std::runtime_error("Selection outline allocation failed");
    if(loopCount==0)return SelectionOutline(std::make_shared<Impl>(Impl{emptyGeometry(),aa}));if(!points||!loops)throw std::runtime_error("Invalid traced selection");
    ComPtr<ID2D1PathGeometry> path;check(factory()->CreatePathGeometry(&path));ComPtr<ID2D1GeometrySink> sink;check(path->Open(&sink));sink->SetFillMode(D2D1_FILL_MODE_WINDING);size_t index=0;
    for(size_t loop=0;loop<loopCount;++loop){int32_t length=rawLoops[loop];if(length<3||size_t(length)>pointCount-index)throw std::runtime_error("Invalid traced selection loop");sink->BeginFigure(D2D1::Point2F(float(rawPoints[index*2]),float(rawPoints[index*2+1])),D2D1_FIGURE_BEGIN_FILLED);for(size_t i=1;i<size_t(length);++i)sink->AddLine(D2D1::Point2F(float(rawPoints[(index+i)*2]),float(rawPoints[(index+i)*2+1])));sink->EndFigure(D2D1_FIGURE_END_CLOSED);index+=size_t(length);}
    check(sink->Close());return SelectionOutline(std::make_shared<Impl>(Impl{path,aa}));
}
SelectionOutline SelectionOutline::polygon(std::span<const Point> points,bool aa) {
    if(points.size()>1000000)throw std::runtime_error("Selection path exceeds point budget");
    for(auto p:points)pointCheck(p);
    if(points.size()<3)return SelectionOutline(std::make_shared<Impl>(Impl{emptyGeometry(),aa}));
    ComPtr<ID2D1PathGeometry> g;check(factory()->CreatePathGeometry(&g));ComPtr<ID2D1GeometrySink> sink;check(g->Open(&sink));sink->SetFillMode(D2D1_FILL_MODE_WINDING);
    sink->BeginFigure(D2D1::Point2F(float(points.front().x),float(points.front().y)),D2D1_FIGURE_BEGIN_FILLED);
    for(size_t i=1;i<points.size();++i)sink->AddLine(D2D1::Point2F(float(points[i].x),float(points[i].y)));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);check(sink->Close());return SelectionOutline(std::make_shared<Impl>(Impl{g,aa}));
}
Rect SelectionOutline::bounds()const {
    D2D1_RECT_F r{};check(impl_->geometry->GetBounds(nullptr,&r));
    if(r.right<=r.left||r.bottom<=r.top)return {};return {r.left,r.top,double(r.right)-r.left,double(r.bottom)-r.top};
}
bool SelectionOutline::empty()const{return bounds().empty();}
bool SelectionOutline::antialiased()const{return impl_->aa;}
bool SelectionOutline::contains(Point p)const {pointCheck(p);BOOL contained{};check(impl_->geometry->FillContainsPoint(D2D1::Point2F(float(p.x),float(p.y)),nullptr,&contained));return contained!=FALSE;}
SelectionOutline SelectionOutline::combined(const SelectionOutline& other,SelectionMode mode,bool aa)const {
    if(mode==SelectionMode::Replace)return SelectionOutline(std::make_shared<Impl>(Impl{other.impl_->geometry,aa}));
    const auto combine=mode==SelectionMode::Add?D2D1_COMBINE_MODE_UNION:mode==SelectionMode::Subtract?D2D1_COMBINE_MODE_EXCLUDE:D2D1_COMBINE_MODE_INTERSECT;
    ComPtr<ID2D1PathGeometry> g;check(factory()->CreatePathGeometry(&g));ComPtr<ID2D1GeometrySink> sink;check(g->Open(&sink));sink->SetFillMode(D2D1_FILL_MODE_WINDING);
    check(impl_->geometry->CombineWithGeometry(other.impl_->geometry.Get(),combine,nullptr,sink.Get()));check(sink->Close());
    return SelectionOutline(std::make_shared<Impl>(Impl{g,aa}));
}
SelectionOutline SelectionOutline::clipped(int w,int h)const {if(w<1||h<1||w>30000||h>30000)throw std::runtime_error("Invalid canvas size");return combined(rectangle({0,0,double(w),double(h)}),SelectionMode::Intersect,impl_->aa);}
SelectionOutline SelectionOutline::moved(Point offset)const {
    pointCheck(offset);offset={std::round(offset.x),std::round(offset.y)};
    auto b=bounds();if(!b.empty()){pointCheck({b.x+offset.x,b.y+offset.y});pointCheck({b.x+b.width+offset.x,b.y+b.height+offset.y});}
    ComPtr<ID2D1TransformedGeometry> g;check(factory()->CreateTransformedGeometry(impl_->geometry.Get(),D2D1::Matrix3x2F::Translation(float(offset.x),float(offset.y)),&g));
    return SelectionOutline(std::make_shared<Impl>(Impl{g,impl_->aa}));
}
SelectionOutline SelectionOutline::resized(double delta,int w,int h)const {
    if(!std::isfinite(delta))throw std::runtime_error("Invalid selection expansion");if(empty()||delta==0||std::abs(delta)>500)return *this;
    ComPtr<ID2D1StrokeStyle> style;auto properties=D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND,D2D1_CAP_STYLE_ROUND,D2D1_CAP_STYLE_ROUND,D2D1_LINE_JOIN_ROUND,10);check(factory()->CreateStrokeStyle(properties,nullptr,0,&style));
    ComPtr<ID2D1PathGeometry> band;check(factory()->CreatePathGeometry(&band));ComPtr<ID2D1GeometrySink> sink;check(band->Open(&sink));sink->SetFillMode(D2D1_FILL_MODE_WINDING);check(impl_->geometry->Widen(float(std::abs(delta)*2),style.Get(),nullptr,sink.Get()));check(sink->Close());
    SelectionOutline widened(std::make_shared<Impl>(Impl{band,impl_->aa}));auto result=combined(widened,delta>0?SelectionMode::Add:SelectionMode::Subtract,impl_->aa);return delta>0?result.clipped(w,h):result;
}
SelectionOutline SelectionOutline::mirrored(bool horizontally,double axis)const {
    if(!std::isfinite(axis)||std::abs(axis)>1000000)throw std::runtime_error("Invalid selection mirror axis");auto matrix=horizontally?D2D1::Matrix3x2F(-1,0,0,1,float(2*axis),0):D2D1::Matrix3x2F(1,0,0,-1,0,float(2*axis));ComPtr<ID2D1TransformedGeometry> geometry;check(factory()->CreateTransformedGeometry(impl_->geometry.Get(),matrix,&geometry));return SelectionOutline(std::make_shared<Impl>(Impl{geometry,impl_->aa}));
}
std::shared_ptr<const GrayRaster> SelectionOutline::rasterize(int w,int h)const {
    sizeCheck(w,h);auto out=std::make_shared<GrayRaster>();out->width=w;out->height=h;out->pixels.resize(size_t(w)*h);
    auto b=bounds();if(b.empty()||b.x>=w||b.y>=h||b.x+b.width<=0||b.y+b.height<=0)return out;
    Apartment apartment;ComPtr<IWICImagingFactory> wic;check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic)));
    constexpr int side=512;
    int left=std::max(0,int(std::floor(b.x))-1)/side*side,top=std::max(0,int(std::floor(b.y))-1)/side*side;
    int right=std::min(w,int(std::ceil(b.x+b.width))+1),bottom=std::min(h,int(std::ceil(b.y+b.height))+1);
    for(int y=top;y<bottom;y+=side)for(int x=left;x<right;x+=side){
        int tw=std::min(side,w-x),th=std::min(side,h-y);ComPtr<IWICBitmap> bitmap;check(wic->CreateBitmap(UINT(tw),UINT(th),GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,&bitmap));
        ComPtr<ID2D1RenderTarget> target;auto properties=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
        check(factory()->CreateWicBitmapRenderTarget(bitmap.Get(),properties,&target));ComPtr<ID2D1SolidColorBrush> white;check(target->CreateSolidColorBrush(D2D1::ColorF(1,1,1,1),&white));
        target->SetAntialiasMode(impl_->aa?D2D1_ANTIALIAS_MODE_PER_PRIMITIVE:D2D1_ANTIALIAS_MODE_ALIASED);
        target->BeginDraw();target->Clear(D2D1::ColorF(0,0,0,0));target->SetTransform(D2D1::Matrix3x2F::Translation(float(-x),float(-y)));target->FillGeometry(impl_->geometry.Get(),white.Get());check(target->EndDraw());
        WICRect rect{0,0,tw,th};ComPtr<IWICBitmapLock> lock;check(bitmap->Lock(&rect,WICBitmapLockRead,&lock));UINT stride{},length{};BYTE* bytes{};check(lock->GetStride(&stride));check(lock->GetDataPointer(&length,&bytes));
        if(uint64_t(stride)*(th-1)+uint64_t(tw)*4>length)throw std::runtime_error("Invalid WIC selection stride");
        for(int row=0;row<th;++row)for(int col=0;col<tw;++col)out->pixels[size_t(y+row)*w+x+col]=bytes[size_t(row)*stride+col*4+3];
    }
    return out;
}
Rect dragBox(Point anchor,Point point,bool square,bool fromCenter){pointCheck(anchor);pointCheck(point);double dx=std::round(point.x)-anchor.x,dy=std::round(point.y)-anchor.y;if(square){double side=std::max(std::abs(dx),std::abs(dy));dx=dx<0?-side:side;dy=dy<0?-side:side;}if(fromCenter)return {anchor.x-std::abs(dx),anchor.y-std::abs(dy),2*std::abs(dx),2*std::abs(dy)};return {std::min(anchor.x,anchor.x+dx),std::min(anchor.y,anchor.y+dy),std::abs(dx),std::abs(dy)};}
SelectionMode selectionMode(bool shift,bool option,SelectionMode choice){return option?SelectionMode::Subtract:shift?SelectionMode::Add:choice;}
std::optional<SelectionOutline> applySelection(const std::optional<SelectionOutline>& current,const SelectionOutline& shape,SelectionMode mode,int w,int h,bool aa){
    auto clipped=shape.clipped(w,h);
    if(!current&&(mode==SelectionMode::Subtract||mode==SelectionMode::Intersect))return {};
    return current?current->combined(clipped,mode,aa):clipped.combined(clipped,SelectionMode::Replace,aa);
}
std::optional<SelectionOutline> finishSelection(const std::optional<SelectionOutline>& current,const SelectionOutline& draft,SelectionMode mode,int w,int h,bool aa){if(draft.empty())return mode==SelectionMode::Replace?std::nullopt:current;return applySelection(current,draft,mode,w,h,aa);}
std::optional<SelectionOutline> inverseSelection(const std::optional<SelectionOutline>& current,int w,int h){if(!current)return {};return SelectionOutline::rectangle({0,0,double(w),double(h)}).combined(*current,SelectionMode::Subtract,current->antialiased());}
std::optional<Selection> rasterSelection(const std::optional<SelectionOutline>& outline,int w,int h){if(!outline)return {};return Selection{outline->rasterize(w,h),std::make_shared<SelectionOutline>(*outline)};}
bool appendLassoPoint(std::vector<Point>& points,Point p){if(!std::isfinite(p.x)||!std::isfinite(p.y))return false;pointCheck(p);if(!points.empty()&&std::hypot(p.x-points.back().x,p.y-points.back().y)<.25)return false;if(points.size()>=1000000)throw std::runtime_error("Selection path exceeds point budget");points.push_back(p);return true;}
Rect coverageBounds(const GrayRaster& g){grayCheck(g);int left=g.width,top=g.height,right=0,bottom=0;for(int y=0;y<g.height;++y)for(int x=0;x<g.width;++x)if(g.pixels[size_t(y)*g.width+x]){left=std::min(left,x);top=std::min(top,y);right=std::max(right,x+1);bottom=std::max(bottom,y+1);}return right<=left?Rect{}:Rect{double(left),double(top),double(right-left),double(bottom-top)};}
std::optional<Selection> moveSelectionCoverage(const std::optional<Selection>& original,Point offset){
    if(!original)return {};if(!original->coverage)throw std::runtime_error("Present selection has no coverage");grayCheck(*original->coverage);pointCheck(offset);int dx=int(std::round(offset.x)),dy=int(std::round(offset.y));if(dx==0&&dy==0)return original;
    const auto& g=*original->coverage;if(original->outline)return rasterSelection(original->outline->moved(offset),g.width,g.height);
    auto out=std::make_shared<GrayRaster>();out->width=g.width;out->height=g.height;out->pixels.resize(g.pixels.size());for(int y=0;y<g.height;++y)for(int x=0;x<g.width;++x)out->pixels[size_t(y)*g.width+x]=g.pixel(x-dx,y-dy);return Selection{out};
}
std::shared_ptr<const GrayRaster> mappedCoverage(const Document& doc,const Transform& transform,int w,int h,bool clipCanvas,CoverageSampling sampling){
    sizeCheck(w,h);if(!transform.valid()||doc.width<1||doc.height<1||doc.width>30000||doc.height>30000)throw std::runtime_error("Invalid selection mapping");
    if(!doc.selection&&!clipCanvas)return {};
    const GrayRaster* src=doc.selection?doc.selection->coverage.get():nullptr;if(doc.selection&&!src)throw std::runtime_error("Present selection has no coverage");
    if(src){grayCheck(*src);if(src->width!=doc.width||src->height!=doc.height)throw std::runtime_error("Selection must match document dimensions");}
    auto out=std::make_shared<GrayRaster>();out->width=w;out->height=h;out->pixels.resize(size_t(w)*h);
    auto origin=transform.fromUnit({0,0}),right=transform.fromUnit({1,0}),bottom=transform.fromUnit({0,1});
    Point stepX{(right.x-origin.x)/w,(right.y-origin.y)/w},stepY{(bottom.x-origin.x)/h,(bottom.y-origin.y)/h};
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){Point p{origin.x+(x+.5)*stepX.x+(y+.5)*stepY.x,origin.y+(x+.5)*stepX.y+(y+.5)*stepY.y};uint8_t value=0;
        if(p.x>=0&&p.y>=0&&p.x<doc.width&&p.y<doc.height){if(!src)value=255;else if(sampling==CoverageSampling::Nearest)value=src->pixel(int(std::floor(p.x)),int(std::floor(p.y)));else{double sx=p.x-.5,sy=p.y-.5;int ix=int(std::floor(sx)),iy=int(std::floor(sy));double fx=sx-ix,fy=sy-iy;value=byte((1-fy)*((1-fx)*src->pixel(ix,iy)+fx*src->pixel(ix+1,iy))+fy*((1-fx)*src->pixel(ix,iy+1)+fx*src->pixel(ix+1,iy+1)));}}
        out->pixels[size_t(y)*w+x]=value;
    }return out;
}
}
