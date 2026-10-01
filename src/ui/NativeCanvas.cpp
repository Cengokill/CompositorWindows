#include "NativeCanvas.h"
#include "CanvasAccessibility.h"
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QTabletEvent>
#include <QNativeGestureEvent>
#include <QPaintEngine>
#include <QImage>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <d2d1_1helper.h>

namespace compositor {
using Microsoft::WRL::ComPtr;
static void check(HRESULT result,const char*message){if(FAILED(result))throw std::runtime_error(std::string(message)+" (HRESULT "+std::to_string(uint32_t(result))+")");}
NativeCanvas::NativeCanvas(bool warp,QWidget*parent):QWidget(parent),warp_(warp){ui::installCanvasAccessibility();setAttribute(Qt::WA_NativeWindow);setAttribute(Qt::WA_PaintOnScreen);setAttribute(Qt::WA_NoSystemBackground);setFocusPolicy(Qt::StrongFocus);setMouseTracking(true);setAccessibleName("Image canvas");setMinimumSize(160,120);selectionTimer_=new QTimer(this);selectionTimer_->setObjectName("selectionAntsTimer");selectionTimer_->setTimerType(Qt::PreciseTimer);selectionTimer_->setInterval(120);connect(selectionTimer_,&QTimer::timeout,this,[this]{selectionPhase_=(selectionPhase_+1)%8;update();});synchronizeViewport();}
NativeCanvas::~NativeCanvas(){selectionTimer_->stop();profileWatcher_.reset();if(profileWindow_)profileWindow_->removeEventFilter(this);QObject::disconnect(profileScreenConnection_);releaseDevice();}
void NativeCanvas::releaseDevice(){selectionGeometry_.Reset();selectionMatrix_.reset();brushPreviewBitmap_.Reset();brushPreviewSource_.reset();textBitmap_.Reset();textRasterSource_.reset();resetPresentationResources();displayTiles_.clear();image_.Reset();target_.Reset();if(context_)context_->SetTarget(nullptr);context_.Reset();d2device_.Reset();factory_.Reset();swap_.Reset();immediate_.Reset();device_.Reset();}
void NativeCanvas::createDevice(){UINT flags=D3D11_CREATE_DEVICE_BGRA_SUPPORT;D3D_FEATURE_LEVEL level{};auto hr=D3D11CreateDevice(nullptr,warp_?D3D_DRIVER_TYPE_WARP:D3D_DRIVER_TYPE_HARDWARE,nullptr,flags,nullptr,0,D3D11_SDK_VERSION,&device_,&level,&immediate_);if(FAILED(hr)&&!warp_)hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,flags,nullptr,0,D3D11_SDK_VERSION,&device_,&level,&immediate_);check(hr,"D3D11 device");ComPtr<IDXGIDevice> dxgi;check(device_.As(&dxgi),"DXGI device");ComPtr<IDXGIAdapter> adapter;check(dxgi->GetAdapter(&adapter),"DXGI adapter");ComPtr<IDXGIFactory2> dxgiFactory;check(adapter->GetParent(IID_PPV_ARGS(&dxgiFactory)),"DXGI factory");DXGI_SWAP_CHAIN_DESC1 desc{};desc.Width=std::max(1,int(width()*devicePixelRatioF()));desc.Height=std::max(1,int(height()*devicePixelRatioF()));desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=2;desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;desc.Scaling=DXGI_SCALING_STRETCH;desc.AlphaMode=DXGI_ALPHA_MODE_IGNORE;check(dxgiFactory->CreateSwapChainForHwnd(device_.Get(),reinterpret_cast<HWND>(winId()),&desc,nullptr,nullptr,&swap_),"Canvas swap chain");dxgiFactory->MakeWindowAssociation(reinterpret_cast<HWND>(winId()),DXGI_MWA_NO_ALT_ENTER);check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,IID_PPV_ARGS(&factory_)),"D2D factory");check(factory_->CreateDevice(dxgi.Get(),&d2device_),"D2D device");check(d2device_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,&context_),"D2D context");createTarget();upload();}
void NativeCanvas::createTarget(){resetPresentationResources();ComPtr<IDXGISurface> surface;check(swap_->GetBuffer(0,IID_PPV_ARGS(&surface)),"Canvas surface");float dpi=float(96*devicePixelRatioF());auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_IGNORE),dpi,dpi);check(context_->CreateBitmapFromDxgiSurface(surface.Get(),&props,&target_),"Canvas render target");context_->SetTarget(target_.Get());context_->SetDpi(dpi,dpi);}
void NativeCanvas::upload(){
    image_.Reset();
    if(!raster_){displayTiles_.clear();return;}
    std::unordered_map<const Raster::Tile*,bool> live;
    for(const auto&tile:raster_->tiles)live[tile.get()]=true;
    std::erase_if(displayTiles_,[&](const auto&entry){return !live.contains(entry.first);});
}
void NativeCanvas::draw(bool present){const double scale=pointsPerPixel();if(viewportProvider&&documentWidth_>0&&documentHeight_>0){auto origin=documentPoint({0,0});const bool crisp=zoom>=2;const double requestX=crisp?std::floor(origin.x()):origin.x(),requestY=crisp?std::floor(origin.y()):origin.y();const double requestWidth=crisp?std::ceil(origin.x()+width()/scale)-requestX:width()/scale,requestHeight=crisp?std::ceil(origin.y()+height()/scale)-requestY:height()/scale;auto patch=viewportProvider(requestX,requestY,requestWidth,requestHeight,crisp?1:1/zoom);raster_=std::move(patch.raster);rasterX_=patch.documentX;rasterY_=patch.documentY;rasterUnits_=patch.unitsPerPixel;upload();}if(!context_)createDevice();preparePresentation();context_->BeginDraw();context_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);context_->Clear(D2D1::ColorF(.115f,.122f,.137f));if(raster_){float left=float((width()-documentWidth_*scale)/2+pan.x()),top=float((height()-documentHeight_*scale)/2+pan.y());auto bounds=D2D1::RectF(left,top,left+float(documentWidth_*scale),top+float(documentHeight_*scale));auto renderBounds=bounds;if(cropOverlay_){const auto& crop=*cropOverlay_;renderBounds=D2D1::RectF(left+float(std::min(0.,crop.x)*scale),top+float(std::min(0.,crop.y)*scale),left+float(std::max(double(documentWidth_),crop.x+crop.width)*scale),top+float(std::max(double(documentHeight_),crop.y+crop.height)*scale));}context_->PushAxisAlignedClip(renderBounds,D2D1_ANTIALIAS_MODE_ALIASED);ComPtr<ID2D1SolidColorBrush> light,dark;check(context_->CreateSolidColorBrush(D2D1::ColorF(.77f,.78f,.79f),&light),"Checker brush");check(context_->CreateSolidColorBrush(D2D1::ColorF(.91f,.92f,.93f),&dark),"Checker brush");for(int y=0;y<height();y+=12)for(int x=0;x<width();x+=12)context_->FillRectangle(D2D1::RectF(float(x),float(y),float(x+12),float(y+12)),((x/12+y/12)%2?light:dark).Get());if(image_)context_->DrawBitmap(image_.Get(),bounds,1,D2D1_INTERPOLATION_MODE_LINEAR);else{
// Upload bounded tiles so a valid 30,000-pixel narrow document remains displayable.
for(int ty=0;ty<raster_->height;ty+=256)for(int tx=0;tx<raster_->width;tx+=256){int tw=std::min(256,raster_->width-tx),th=std::min(256,raster_->height-ty);auto rect=D2D1::RectF(left+float((rasterX_+tx*rasterUnits_)*scale),top+float((rasterY_+ty*rasterUnits_)*scale),left+float((rasterX_+(tx+tw)*rasterUnits_)*scale),top+float((rasterY_+(ty+th)*rasterUnits_)*scale));if(rect.right<0||rect.bottom<0||rect.left>width()||rect.top>height())continue;auto tile=raster_->tiles[size_t(ty/256)*((raster_->width+255)/256)+tx/256];auto found=displayTiles_.find(tile.get());
if(found==displayTiles_.end()){
    if(displayTiles_.size()>=1024)displayTiles_.erase(displayTiles_.begin());
    DisplayTile display;display.source=tile;
    auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE,D2D1::PixelFormat(DXGI_FORMAT_R8G8B8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));
    check(context_->CreateBitmap(D2D1::SizeU(256,256),tile->pixels.data(),256*4,&props,&display.bitmap),"Canvas tile upload");
    found=displayTiles_.emplace(tile.get(),std::move(display)).first;
}
auto sourceRect=D2D1::RectF(0,0,float(tw),float(th));
context_->DrawBitmap(found->second.bitmap.Get(),rect,1,D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR,sourceRect);}}
if(showGuides){ComPtr<ID2D1SolidColorBrush> guide;check(context_->CreateSolidColorBrush(D2D1::ColorF(.2f,.75f,.95f,.85f),&guide),"Guide brush");for(auto [horizontal,position]:guides){if(horizontal){float y=top+float(position*scale);context_->DrawLine(D2D1::Point2F(0,y),D2D1::Point2F(float(width()),y),guide.Get(),1);}else{float x=left+float(position*scale);context_->DrawLine(D2D1::Point2F(x,0),D2D1::Point2F(x,float(height())),guide.Get(),1);}}}
if(showPixelGrid&&zoom>=8){
    const auto area=D2D1::RectF(std::max(0.f,left),std::max(0.f,top),std::min(float(width()),bounds.right),std::min(float(height()),bounds.bottom));
    if(area.right>area.left&&area.bottom>area.top){
        // EditorCanvas.swift 896-919 fills one winding path so crossings have
        // the same coverage as each one-physical-pixel hairline.
        const float halfHairline=float(.5/backingScale_);
        ComPtr<ID2D1PathGeometry> path;check(factory_->CreatePathGeometry(&path),"Grid path");
        ComPtr<ID2D1GeometrySink> sink;check(path->Open(&sink),"Grid path sink");sink->SetFillMode(D2D1_FILL_MODE_WINDING);
        auto rectangle=[&](D2D1_RECT_F rect){sink->BeginFigure(D2D1::Point2F(rect.left,rect.top),D2D1_FIGURE_BEGIN_FILLED);sink->AddLine(D2D1::Point2F(rect.right,rect.top));sink->AddLine(D2D1::Point2F(rect.right,rect.bottom));sink->AddLine(D2D1::Point2F(rect.left,rect.bottom));sink->EndFigure(D2D1_FIGURE_END_CLOSED);};
        const int firstX=std::max(0,int(std::ceil((area.left-left)/scale))),lastX=std::min(documentWidth_,int(std::floor((area.right-left)/scale)));
        const int firstY=std::max(0,int(std::ceil((area.top-top)/scale))),lastY=std::min(documentHeight_,int(std::floor((area.bottom-top)/scale)));
        for(int x=firstX;x<=lastX;++x){const float px=left+float(x*scale);rectangle(D2D1::RectF(px-halfHairline,area.top,px+halfHairline,area.bottom));}
        for(int y=firstY;y<=lastY;++y){const float py=top+float(y*scale);rectangle(D2D1::RectF(area.left,py-halfHairline,area.right,py+halfHairline));}
        check(sink->Close(),"Grid path close");ComPtr<ID2D1SolidColorBrush> grid;
        check(context_->CreateSolidColorBrush(D2D1::ColorF(.55f,.55f,.55f,.45f),&grid),"Grid brush");
        context_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);context_->FillGeometry(path.Get(),grid.Get());context_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    }
}
context_->PopAxisAlignedClip();}
drawSelection();
if(selectionDraft_&&!selectionDraft_->points.empty()&&documentWidth_>0&&documentHeight_>0){
    auto points=selectionDraft_->points;
    if(selectionDraft_->kind==editing::LassoKind::Polygonal&&selectionDraft_->cursor)points.push_back(*selectionDraft_->cursor);
    const auto mapping=viewMapping();for(auto& point:points)point=mapping.toView(point);
    ComPtr<ID2D1PathGeometry> path;check(factory_->CreatePathGeometry(&path),"Selection draft path");
    ComPtr<ID2D1GeometrySink> sink;check(path->Open(&sink),"Selection draft sink");
    auto point=[](Point p){return D2D1::Point2F(float(p.x),float(p.y));};
    sink->BeginFigure(point(points.front()),D2D1_FIGURE_BEGIN_HOLLOW);
    for(size_t i=1;i<points.size();++i)sink->AddLine(point(points[i]));
    sink->EndFigure(selectionDraft_->kind==editing::LassoKind::Rectangle?D2D1_FIGURE_END_CLOSED:D2D1_FIGURE_END_OPEN);check(sink->Close(),"Selection draft path close");
    ComPtr<ID2D1SolidColorBrush> black,white;
    check(context_->CreateSolidColorBrush(D2D1::ColorF(0,0,0,.8f),&black),"Selection draft black");
    check(context_->CreateSolidColorBrush(D2D1::ColorF(1,1,1),&white),"Selection draft white");
    context_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    if(selectionDraft_->kind==editing::LassoKind::Ellipse&&points.size()==4){
        const auto a=points.front(),b=points[2];const auto ellipse=D2D1::Ellipse(D2D1::Point2F(float((a.x+b.x)/2),float((a.y+b.y)/2)),float(std::abs(b.x-a.x)/2),float(std::abs(b.y-a.y)/2));
        context_->DrawEllipse(ellipse,black.Get(),2);context_->DrawEllipse(ellipse,white.Get(),1);
    }else{context_->DrawGeometry(path.Get(),black.Get(),2);context_->DrawGeometry(path.Get(),white.Get(),1);}
    if(selectionDraft_->kind==editing::LassoKind::Polygonal){const auto first=points.front();const auto handle=D2D1::RectF(float(first.x-4),float(first.y-4),float(first.x+4),float(first.y+4));context_->FillRectangle(handle,white.Get());context_->DrawRectangle(handle,black.Get(),1);}
    context_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
}
if(shapeDraft_&&documentWidth_>0&&documentHeight_>0){
    // TransformOverlay.drawShapeDraft: a transient document-space fill with a
    // one-DIP black outline. No layer pixels or history exist before release.
    const auto& draft=*shapeDraft_;const auto mapping=viewMapping();
    const auto a=mapping.toView({draft.rect.x,draft.rect.y}),b=mapping.toView({draft.rect.x+draft.rect.width,draft.rect.y+draft.rect.height});
    const auto bounds=D2D1::RectF(float(a.x),float(a.y),float(b.x),float(b.y));
    ComPtr<ID2D1SolidColorBrush> fill,outline;
    check(context_->CreateSolidColorBrush(D2D1::ColorF(draft.fill.r/255.f,draft.fill.g/255.f,draft.fill.b/255.f,draft.fill.a/255.f),&fill),"Shape draft fill");
    check(context_->CreateSolidColorBrush(D2D1::ColorF(0,0,0,.6f),&outline),"Shape draft outline");
    context_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    if(draft.kind==editing::ShapeKind::Line){context_->DrawLine(D2D1::Point2F(bounds.left,bounds.top),D2D1::Point2F(bounds.right,bounds.bottom),fill.Get(),std::max(1.f,float(draft.lineWidth*pointsPerPixel())));context_->DrawLine(D2D1::Point2F(bounds.left,bounds.top),D2D1::Point2F(bounds.right,bounds.bottom),outline.Get(),1);}
    else if(draft.kind==editing::ShapeKind::Ellipse){const auto ellipse=D2D1::Ellipse(D2D1::Point2F((bounds.left+bounds.right)/2,(bounds.top+bounds.bottom)/2),(bounds.right-bounds.left)/2,(bounds.bottom-bounds.top)/2);context_->FillEllipse(ellipse,fill.Get());context_->DrawEllipse(ellipse,outline.Get(),1);}
    else{const float radius=float(std::clamp(draft.cornerRadius,0.,std::min(draft.rect.width,draft.rect.height)/2)*pointsPerPixel());const auto rounded=D2D1::RoundedRect(bounds,radius,radius);context_->FillRoundedRectangle(rounded,fill.Get());context_->DrawRoundedRectangle(rounded,outline.Get(),1);}
    context_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
}
if(cropOverlay_&&documentWidth_>0&&documentHeight_>0){
    const auto& crop=*cropOverlay_;const auto mapping=viewMapping();
    const auto a=mapping.toView({crop.x,crop.y}),b=mapping.toView({crop.x+crop.width,crop.y+crop.height});
    const auto box=D2D1::RectF(float(a.x),float(a.y),float(b.x),float(b.y));
    ComPtr<ID2D1SolidColorBrush> dim,white,thirds,black;
    check(context_->CreateSolidColorBrush(D2D1::ColorF(0,0,0,.6f),&dim),"Crop shade");
    check(context_->CreateSolidColorBrush(D2D1::ColorF(1,1,1),&white),"Crop outline");
    check(context_->CreateSolidColorBrush(D2D1::ColorF(1,1,1,.4f),&thirds),"Crop thirds");
    check(context_->CreateSolidColorBrush(D2D1::ColorF(0,0,0),&black),"Crop handles");
    ComPtr<ID2D1PathGeometry> shade;ComPtr<ID2D1GeometrySink> sink;
    check(factory_->CreatePathGeometry(&shade),"Crop shade geometry");check(shade->Open(&sink),"Crop shade sink");sink->SetFillMode(D2D1_FILL_MODE_ALTERNATE);
    for(const auto bounds:{D2D1::RectF(0,0,float(width()),float(height())),box}){
        sink->BeginFigure(D2D1::Point2F(bounds.left,bounds.top),D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine(D2D1::Point2F(bounds.right,bounds.top));sink->AddLine(D2D1::Point2F(bounds.right,bounds.bottom));sink->AddLine(D2D1::Point2F(bounds.left,bounds.bottom));sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    }check(sink->Close(),"Crop shade close");context_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    context_->FillGeometry(shade.Get(),dim.Get());context_->DrawRectangle(box,white.Get(),1);
    for(int index=1;index<=2;++index){const float fraction=float(index)/3;const float x=box.left+(box.right-box.left)*fraction,y=box.top+(box.bottom-box.top)*fraction;
        context_->DrawLine(D2D1::Point2F(x,box.top),D2D1::Point2F(x,box.bottom),thirds.Get(),1);
        context_->DrawLine(D2D1::Point2F(box.left,y),D2D1::Point2F(box.right,y),thirds.Get(),1);}
    const auto geometry=editing_transform::OverlayGeometry::fromTransform({crop.x,crop.y,crop.width,crop.height},mapping);
    for(const auto handle:geometry.handles){const auto bounds=D2D1::RectF(float(handle.x-4),float(handle.y-4),float(handle.x+4),float(handle.y+4));context_->FillRectangle(bounds,white.Get());context_->DrawRectangle(bounds,black.Get(),1);}
    context_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
}
if(gradientLine_&&documentWidth_>0&&documentHeight_>0){
    const auto a=viewMapping().toView(gradientLine_->first),b=viewMapping().toView(gradientLine_->second);
    const auto start=D2D1::Point2F(float(a.x),float(a.y)),end=D2D1::Point2F(float(b.x),float(b.y));
    ComPtr<ID2D1SolidColorBrush> black,white;
    check(context_->CreateSolidColorBrush(D2D1::ColorF(0,0,0,.7f),&black),"Gradient outline");
    check(context_->CreateSolidColorBrush(D2D1::ColorF(1,1,1),&white),"Gradient line");
    context_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    context_->DrawLine(start,end,black.Get(),3);context_->DrawLine(start,end,white.Get(),1);
    black->SetColor(D2D1::ColorF(0,0,0));
    for(auto point:{start,end}){const auto handle=D2D1::Ellipse(point,6,6);context_->FillEllipse(handle,white.Get());context_->DrawEllipse(handle,black.Get(),1);}
    context_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
}
if((transformOverlay_||distortionOverlay_)&&documentWidth_>0&&documentHeight_>0){auto overlay=distortionOverlay_?editing_transform::OverlayGeometry::fromCorners(*distortionOverlay_,viewMapping()):editing_transform::OverlayGeometry::fromTransform(*transformOverlay_,viewMapping());ComPtr<ID2D1SolidColorBrush> line,fill;check(context_->CreateSolidColorBrush(D2D1::ColorF(.3f,.7f,1),&line),"Transform outline");check(context_->CreateSolidColorBrush(D2D1::ColorF(1,1,1),&fill),"Transform handle");auto point=[](Point p){return D2D1::Point2F(float(p.x),float(p.y));};for(int i=0;i<4;++i)context_->DrawLine(point(overlay.outline[i]),point(overlay.outline[(i+1)%4]),line.Get(),1);for(auto h:overlay.handles){auto box=D2D1::RectF(float(h.x-3),float(h.y-3),float(h.x+3),float(h.y+3));context_->FillRectangle(box,fill.Get());context_->DrawRectangle(box,line.Get(),1);}if(overlay.showsRotation){context_->DrawLine(point(overlay.handles[1]),point(overlay.rotationHandle),line.Get(),1);context_->FillEllipse(D2D1::Ellipse(point(overlay.rotationHandle),4,4),fill.Get());context_->DrawEllipse(D2D1::Ellipse(point(overlay.rotationHandle),4,4),line.Get(),1);}}
if(brushCursor_){
    const auto& cursor=*brushCursor_;const auto center=D2D1::Point2F(float(cursor.center.x),float(cursor.center.y));const float radius=float(cursor.diameter/2);
    ComPtr<ID2D1SolidColorBrush> black,white;check(context_->CreateSolidColorBrush(D2D1::ColorF(0,0,0),&black),"Brush cursor black");check(context_->CreateSolidColorBrush(D2D1::ColorF(1,1,1),&white),"Brush cursor white");
    context_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    const auto circle=D2D1::Ellipse(center,radius,radius);
    if(cursor.preview&&cursor.opacity>0){
        const auto& image=*cursor.preview;
        if(image.width<1||image.height<1||image.width>1024||image.height>1024)throw std::invalid_argument("Invalid clone cursor bitmap extent");
        if(brushPreviewSource_!=cursor.preview||!brushPreviewBitmap_){
            std::vector<Pixel> pixels(size_t(image.width)*image.height);
            for(int y=0;y<image.height;++y)for(int x=0;x<image.width;++x)pixels[size_t(y)*image.width+x]=image.pixel(x,y);
            auto properties=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE,D2D1::PixelFormat(DXGI_FORMAT_R8G8B8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));
            brushPreviewBitmap_.Reset();check(context_->CreateBitmap(D2D1::SizeU(UINT32(image.width),UINT32(image.height)),pixels.data(),UINT32(image.width*4),&properties,&brushPreviewBitmap_),"Clone cursor bitmap");brushPreviewSource_=cursor.preview;
        }
        ComPtr<ID2D1EllipseGeometry> mask;check(factory_->CreateEllipseGeometry(circle,&mask),"Clone cursor clip");
        context_->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(),mask.Get()),nullptr);
        context_->DrawBitmap(brushPreviewBitmap_.Get(),D2D1::RectF(center.x-radius,center.y-radius,center.x+radius,center.y+radius),float(std::clamp(cursor.opacity,0.,1.)),D2D1_INTERPOLATION_MODE_LINEAR);
        context_->PopLayer();
    }
    context_->DrawEllipse(circle,white.Get(),2.5f);context_->DrawEllipse(circle,black.Get(),1);
    if(cursor.hardness&&*cursor.hardness>0){
        auto style=D2D1::StrokeStyleProperties();style.dashStyle=D2D1_DASH_STYLE_CUSTOM;
        const float whiteDashes[]{4/2.5f,3/2.5f},blackDashes[]{4,3};ComPtr<ID2D1StrokeStyle> whiteDash,blackDash;
        check(factory_->CreateStrokeStyle(style,whiteDashes,2,&whiteDash),"Hardness white dash");check(factory_->CreateStrokeStyle(style,blackDashes,2,&blackDash),"Hardness black dash");
        const float inner=radius*float(*cursor.hardness);const auto ring=D2D1::Ellipse(center,inner,inner);context_->DrawEllipse(ring,white.Get(),2.5f,whiteDash.Get());context_->DrawEllipse(ring,black.Get(),1,blackDash.Get());
    }
    if(cursor.marker){const float x=float(cursor.marker->x),y=float(cursor.marker->y);for(const bool horizontal:{true,false}){const auto a=D2D1::Point2F(x-(horizontal?7:0),y-(horizontal?0:7)),b=D2D1::Point2F(x+(horizontal?7:0),y+(horizontal?0:7));context_->DrawLine(a,b,white.Get(),3);context_->DrawLine(a,b,black.Get(),1);}}
}
if(sampleRing_){auto center=viewMapping().toView(sampleRing_->position);auto ring=D2D1::Ellipse(D2D1::Point2F(float(center.x),float(center.y)),43,43);ComPtr<ID2D1SolidColorBrush> brush;check(context_->CreateSolidColorBrush(D2D1::ColorF(.45f,.45f,.45f),&brush),"Sample ring");context_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);context_->DrawEllipse(ring,brush.Get(),24);for(int half=0;half<2;++half){auto color=half?sampleRing_->original:sampleRing_->sampled;brush->SetColor(D2D1::ColorF(color.r/255.f,color.g/255.f,color.b/255.f));context_->PushAxisAlignedClip(D2D1::RectF(float(center.x-58),float(center.y+(half?0:-58)),float(center.x+58),float(center.y+(half?58:0))),D2D1_ANTIALIAS_MODE_ALIASED);context_->DrawEllipse(ring,brush.Get(),16);context_->PopAxisAlignedClip();}}
if((snapGuideX_||snapGuideY_)&&documentWidth_>0&&documentHeight_>0){
    const auto color=palette().color(QPalette::Highlight);ComPtr<ID2D1SolidColorBrush> accent;
    check(context_->CreateSolidColorBrush(D2D1::ColorF(float(color.redF()),float(color.greenF()),float(color.blueF())),&accent),"Snap guide");
    const auto mapping=viewMapping();const auto point=[&](Point p){auto view=mapping.toView(p);return D2D1::Point2F(float(view.x),float(view.y));};
    context_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    if(snapGuideX_)context_->DrawLine(point({*snapGuideX_,0}),point({*snapGuideX_,double(documentHeight_)}),accent.Get(),1);
    if(snapGuideY_)context_->DrawLine(point({0,*snapGuideY_}),point({double(documentWidth_),*snapGuideY_}),accent.Get(),1);
}
if(textOverlay_)drawTextOverlay();
auto hr=context_->EndDraw();if(hr==D2DERR_RECREATE_TARGET){releaseDevice();update();return;}check(hr,"Canvas draw");hr=finishPresentation();if(hr==D2DERR_RECREATE_TARGET){releaseDevice();update();return;}check(hr,"Canvas presentation");if(!present)return;hr=swap_->Present(1,0);if(hr==DXGI_ERROR_DEVICE_REMOVED||hr==DXGI_ERROR_DEVICE_RESET){releaseDevice();update();return;}check(hr,"Canvas present");}
std::optional<int> NativeCanvas::cropResizeHandle(editing::Rect crop,Point point,const editing_transform::ViewMapping& mapping){
    const auto geometry=editing_transform::OverlayGeometry::fromTransform({crop.x,crop.y,crop.width,crop.height},mapping);
    const auto contains=[&](double x,double y,double w,double h){return point.x>=x&&point.y>=y&&point.x<x+w&&point.y<y+h;};
    for(const int index:{0,2,4,6}){const auto p=geometry.handles[size_t(index)];if(contains(p.x-10,p.y-10,20,20))return index;}
    const auto a=geometry.handles[0],b=geometry.handles[4];
    for(const int index:{1,5})if(contains(a.x+10,geometry.handles[size_t(index)].y-10,std::max(0.,b.x-a.x-20),20))return index;
    for(const int index:{3,7})if(contains(geometry.handles[size_t(index)].x-10,a.y+10,20,std::max(0.,b.y-a.y-20)))return index;
    return {};
}
void NativeCanvas::setSelection(std::shared_ptr<const GrayRaster> value,std::shared_ptr<const editing::SelectionOutline> outline){
    if(selection_==value&&selectionInputOutline_==outline)return;
    auto path=outline;
    if(!path&&value&&value->source)path=value->source->vectorOutline();
    if(!path&&value)path=std::make_shared<editing::SelectionOutline>(editing::SelectionOutline::fromCoverage(*value));
    selection_=std::move(value);selectionInputOutline_=std::move(outline);selectionOutline_=std::move(path);
    selectionGeometry_.Reset();selectionMatrix_.reset();updateSelectionAnimation();update();
}
void NativeCanvas::updateSelectionAnimation(){
    if(selectionTimer_){const bool active=selectionOutline_&&!selectionOutline_->empty()&&documentWidth_>0&&documentHeight_>0&&isVisible();
        if(active&&!selectionTimer_->isActive())selectionTimer_->start();else if(!active)selectionTimer_->stop();}
}
void NativeCanvas::drawSelection(){
    if(!selectionOutline_||selectionOutline_->empty()||documentWidth_<=0||documentHeight_<=0)return;
    const auto mapping=viewMapping();const std::array<float,6> matrix{float(mapping.pointsPerPixel),0,0,float(mapping.pointsPerPixel),float(mapping.origin.x),float(mapping.origin.y)};
    if(!selectionGeometry_||selectionMatrix_!=matrix){
        ComPtr<ID2D1PathGeometry> path;check(factory_->CreatePathGeometry(&path),"Selection path");
        ComPtr<ID2D1GeometrySink> sink;check(path->Open(&sink),"Selection path sink");
        selectionOutline_->writeGeometry(sink.Get(),matrix);check(sink->Close(),"Selection path close");
        selectionGeometry_=std::move(path);selectionMatrix_=matrix;
    }
    ComPtr<ID2D1SolidColorBrush> black,white;
    check(context_->CreateSolidColorBrush(D2D1::ColorF(0,0,0,1),&black),"Selection black");
    check(context_->CreateSolidColorBrush(D2D1::ColorF(1,1,1,1),&white),"Selection white");
    auto style=D2D1::StrokeStyleProperties();style.dashStyle=D2D1_DASH_STYLE_CUSTOM;style.dashOffset=float(selectionPhase_);
    const float lengths[]{4,4};ComPtr<ID2D1StrokeStyle> dashed;check(factory_->CreateStrokeStyle(style,lengths,2,&dashed),"Selection dash");
    context_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    context_->DrawGeometry(selectionGeometry_.Get(),white.Get(),1);
    context_->DrawGeometry(selectionGeometry_.Get(),black.Get(),1,dashed.Get());
    context_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
}
void NativeCanvas::setRaster(std::shared_ptr<const Raster> r){raster_=std::move(r);setDocumentSize(raster_?raster_->width:0,raster_?raster_->height:0);rasterX_=rasterY_=0;rasterUnits_=1;try{upload();}catch(const std::exception&e){error_=e.what();releaseDevice();}update();}
graphics::CanvasViewport NativeCanvas::viewportState()const{return {{double(width()),double(height())},{pan.x(),pan.y()},backingScale_,zoom,followsFit_};}
void NativeCanvas::installViewport(const graphics::CanvasViewport& state){zoom=state.zoom;pan={state.pan.x,state.pan.y};backingScale_=state.backingScale;followsFit_=state.followsFit;update();}
void NativeCanvas::synchronizeViewport(){auto state=viewportState();std::optional<Point> document;if(documentWidth_>0&&documentHeight_>0)document=Point{double(documentWidth_),double(documentHeight_)};state.resize({double(width()),double(height())},devicePixelRatioF(),document);installViewport(state);}
void NativeCanvas::setDocumentSize(int w,int h){const bool changed=documentWidth_!=w||documentHeight_!=h;documentWidth_=w;documentHeight_=h;if(w<=0||h<=0){documentWidth_=documentHeight_=0;raster_.reset();selection_.reset();selectionInputOutline_.reset();selectionOutline_.reset();selectionGeometry_.Reset();selectionMatrix_.reset();selectionDraft_.reset();transformOverlay_.reset();distortionOverlay_.reset();gradientLine_.reset();shapeDraft_.reset();cropOverlay_.reset();snapGuideX_.reset();snapGuideY_.reset();sampleRing_.reset();displayTiles_.clear();image_.Reset();}updateSelectionAnimation();if(changed)synchronizeViewport();else update();}
void NativeCanvas::fit(){if(documentWidth_>0&&documentHeight_>0){auto state=viewportState();state.fit({double(documentWidth_),double(documentHeight_)});installViewport(state);}}
editing_transform::ViewMapping NativeCanvas::viewMapping()const{if(documentWidth_<=0)return {};const auto state=viewportState();return {state.pointsPerPixel(),state.origin({double(documentWidth_),double(documentHeight_)})};}
QPointF NativeCanvas::documentPoint(QPointF p)const{if(documentWidth_<=0)return p;const auto point=viewportState().documentPoint({p.x(),p.y()},{double(documentWidth_),double(documentHeight_)});return {point.x,point.y};}
void NativeCanvas::zoomAt(double value,QPointF anchor){if(documentWidth_<=0||documentHeight_<=0)return;auto state=viewportState();state.setZoom(value,{anchor.x(),anchor.y()},{double(documentWidth_),double(documentHeight_)});installViewport(state);}
void NativeCanvas::panBy(QPointF delta){auto state=viewportState();state.translate({delta.x(),delta.y()});installViewport(state);}
QString NativeCanvas::adapterName()const{
    if(!device_)return {};
    ComPtr<IDXGIDevice> device;ComPtr<IDXGIAdapter> adapter;DXGI_ADAPTER_DESC description{};
    if(FAILED(device_.As(&device))||FAILED(device->GetAdapter(&adapter))||FAILED(adapter->GetDesc(&description)))return {};
    return QString::fromWCharArray(description.Description);
}
void NativeCanvas::recreateDevice(){releaseDevice();error_.clear();update();}
QImage NativeCanvas::captureRendered(){
    // Read the freshly rendered back buffer before Present advances the flip chain.
    draw(false);
    if(!device_||!swap_)throw std::runtime_error("No canvas graphics device");
    ComPtr<ID3D11Texture2D> source;
    check(swap_->GetBuffer(0,IID_PPV_ARGS(&source)),"Canvas capture buffer");
    D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);
    desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;
    ComPtr<ID3D11Texture2D> staging;check(device_->CreateTexture2D(&desc,nullptr,&staging),"Canvas staging capture");
    immediate_->CopyResource(staging.Get(),source.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};check(immediate_->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"Canvas capture readback");
    QImage image(static_cast<const uchar*>(mapped.pData),int(desc.Width),int(desc.Height),mapped.RowPitch,QImage::Format_ARGB32);
    auto copy=image.copy();immediate_->Unmap(staging.Get(),0);check(swap_->Present(1,0),"Present captured canvas");return copy;
}
void NativeCanvas::paintEvent(QPaintEvent*){try{draw();error_.clear();}catch(const std::exception&e){error_=e.what();releaseDevice();}}
void NativeCanvas::resizeEvent(QResizeEvent*){synchronizeViewport();if(swap_){context_->SetTarget(nullptr);target_.Reset();auto hr=swap_->ResizeBuffers(0,std::max(1,int(width()*devicePixelRatioF())),std::max(1,int(height()*devicePixelRatioF())),DXGI_FORMAT_UNKNOWN,0);try{check(hr,"Canvas resize");createTarget();}catch(const std::exception&e){error_=e.what();releaseDevice();}}update();}
void NativeCanvas::mousePressEvent(QMouseEvent*e){
    if(tabletActive_){e->accept();return;}
    setFocus();last_=e->position();if(pointerHover)pointerHover(e->position(),e->modifiers());
    if(e->button()==Qt::RightButton&&rightPointerDown&&rightPointerDown(e->position(),e->modifiers())){rightDragging_=true;grabMouse();e->accept();return;}
    if(e->button()==Qt::MiddleButton){panning_=true;grabMouse();e->accept();return;}
    if(e->button()==Qt::LeftButton&&!rightDragging_&&!panning_){dragging_=true;grabMouse();if(pointerDown)pointerDown(documentPoint(e->position()),e->modifiers());}
}
void NativeCanvas::mouseMoveEvent(QMouseEvent*e){
    if(tabletActive_){e->accept();return;}
    if(pointerHover)pointerHover(e->position(),e->modifiers());
    if(rightDragging_){if(rightPointerMove)rightPointerMove(e->position(),e->modifiers(),false);last_=e->position();return;}
    if(!dragging_&&cropOverlay_){
        const auto hit=cropResizeHandle(*cropOverlay_,{e->position().x(),e->position().y()},viewMapping());
        if(hit){constexpr Qt::CursorShape cursors[]{Qt::SizeFDiagCursor,Qt::SizeVerCursor,Qt::SizeBDiagCursor,Qt::SizeHorCursor,Qt::SizeFDiagCursor,Qt::SizeVerCursor,Qt::SizeBDiagCursor,Qt::SizeHorCursor};setCursor(cursors[*hit]);}
        else unsetCursor();
    }
    if(!dragging_&&(transformOverlay_||distortionOverlay_)){auto geometry=distortionOverlay_?editing_transform::OverlayGeometry::fromCorners(*distortionOverlay_,viewMapping()):editing_transform::OverlayGeometry::fromTransform(*transformOverlay_,viewMapping());auto mode=geometry.hit({e->position().x(),e->position().y()});auto cursor=mode?geometry.cursor(*mode,{e->modifiers().testFlag(Qt::ShiftModifier),e->modifiers().testFlag(Qt::AltModifier),e->modifiers().testFlag(Qt::ControlModifier),false},distortionOverlay_.has_value()):editing_transform::Cursor::Move;Qt::CursorShape shape=Qt::SizeAllCursor;switch(cursor){case editing_transform::Cursor::Horizontal:shape=Qt::SizeHorCursor;break;case editing_transform::Cursor::Vertical:shape=Qt::SizeVerCursor;break;case editing_transform::Cursor::DiagonalDown:shape=Qt::SizeFDiagCursor;break;case editing_transform::Cursor::DiagonalUp:shape=Qt::SizeBDiagCursor;break;case editing_transform::Cursor::Rotate:case editing_transform::Cursor::Distort:shape=Qt::CrossCursor;break;default:break;}setCursor(shape);}
    if(panning_&&(!navigationAllowed||navigationAllowed()))panBy(e->position()-last_);else if(dragging_&&pointerMove)pointerMove(documentPoint(e->position()),e->modifiers());last_=e->position();
}
void NativeCanvas::mouseDoubleClickEvent(QMouseEvent* e){
    if(tabletActive_||e->button()!=Qt::LeftButton||rightDragging_){e->accept();return;}
    setFocus();last_=e->position();if(pointerHover)pointerHover(e->position(),e->modifiers());
    dragging_=true;grabMouse();if(pointerDoubleClick)pointerDoubleClick(documentPoint(e->position()),e->modifiers());else if(pointerDown)pointerDown(documentPoint(e->position()),e->modifiers());
}
void NativeCanvas::mouseReleaseEvent(QMouseEvent*e){
    if(tabletActive_){e->accept();return;}
    if(panning_&&e->button()==Qt::MiddleButton){panning_=false;releaseMouse();return;}
    if(rightDragging_&&e->button()==Qt::RightButton){rightDragging_=false;releaseMouse();if(rightPointerMove)rightPointerMove(e->position(),e->modifiers(),true);return;}
    if(dragging_&&e->button()==Qt::LeftButton){dragging_=false;releaseMouse();if(pointerUp)pointerUp(documentPoint(e->position()),e->modifiers());}
}
void NativeCanvas::wheelEvent(QWheelEvent*e){
    e->accept();if(documentWidth_<=0||documentHeight_<=0||(navigationAllowed&&!navigationAllowed()))return;
    // Qt reports discrete wheel angle in eighths of degrees (120 per detent).
    // One normalized detent is one source non-precise scrolling unit.
    const bool precise=!e->pixelDelta().isNull();const QPointF delta=precise?QPointF(e->pixelDelta()):QPointF(e->angleDelta())/120;
    if(e->modifiers()&(Qt::ControlModifier|Qt::AltModifier))zoomAt(zoom*std::exp(-delta.y()*.015),e->position());else panBy(delta*(precise?1:12));
    if(pointerHover)pointerHover(e->position(),e->modifiers());
}
void NativeCanvas::setTextOverlay(std::optional<TextCaretOverlay> value){textOverlay_=std::move(value);setAttribute(Qt::WA_InputMethodEnabled,textOverlay_.has_value());if(!textOverlay_){textBitmap_.Reset();textRasterSource_.reset();}update();}
void NativeCanvas::drawTextOverlay(){
    if(!textOverlay_||!context_||documentWidth_<=0)return;
    const auto& overlay=*textOverlay_;const auto mapping=viewMapping();
    auto viewOf=[&](double x,double y){auto point=mapping.toView({x,y});return D2D1::Point2F(float(point.x),float(point.y));};
    if(overlay.raster&&overlay.raster->width>0&&overlay.raster->height>0&&overlay.raster->width<=30000&&overlay.raster->height<=30000){
        if(textRasterSource_!=overlay.raster||!textBitmap_){
            const auto& image=*overlay.raster;std::vector<Pixel> pixels(size_t(image.width)*image.height);
            for(int y=0;y<image.height;++y)for(int x=0;x<image.width;++x)pixels[size_t(y)*image.width+x]=image.pixel(x,y);
            auto properties=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE,D2D1::PixelFormat(DXGI_FORMAT_R8G8B8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));
            textBitmap_.Reset();check(context_->CreateBitmap(D2D1::SizeU(UINT32(image.width),UINT32(image.height)),pixels.data(),UINT32(image.width*4),&properties,&textBitmap_),"Text overlay bitmap");textRasterSource_=overlay.raster;
        }
        const auto origin=viewOf(overlay.originX,overlay.originY),opposite=viewOf(overlay.originX+overlay.width,overlay.originY+overlay.height);
        context_->DrawBitmap(textBitmap_.Get(),D2D1::RectF(origin.x,origin.y,opposite.x,opposite.y),1,D2D1_INTERPOLATION_MODE_LINEAR);
    }
    ComPtr<ID2D1SolidColorBrush> fill,caret;check(context_->CreateSolidColorBrush(D2D1::ColorF(0.2f,0.55f,1.f,0.35f),&fill),"Text selection");check(context_->CreateSolidColorBrush(D2D1::ColorF(0.15f,0.45f,1.f),&caret),"Text caret");
    context_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    for(const auto& box:overlay.selection){const auto a=viewOf(box.x,box.y),b=viewOf(box.x+box.width,box.y+box.height);context_->FillRectangle(D2D1::RectF(a.x,a.y,b.x,b.y),fill.Get());}
    if(overlay.caret>=0&&overlay.caret<int(overlay.carets.size())){const auto& line=overlay.carets[size_t(overlay.caret)];context_->DrawLine(viewOf(line.x0,line.y0),viewOf(line.x1,line.y1),caret.Get(),std::max(1.f,float(pointsPerPixel())));}
    context_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
}
void NativeCanvas::keyPressEvent(QKeyEvent*e){if(textKey&&textKey(e)){e->accept();return;}if(e->key()==Qt::Key_Escape&&(dragging_||rightDragging_)){dragging_=rightDragging_=tabletActive_=false;releaseMouse();if(pointerCancel)pointerCancel();e->accept();}else QWidget::keyPressEvent(e);}
void NativeCanvas::inputMethodEvent(QInputMethodEvent*e){if(textInput){textInput(e);e->accept();return;}QWidget::inputMethodEvent(e);}
QVariant NativeCanvas::inputMethodQuery(Qt::InputMethodQuery query) const{
    if(query==Qt::ImEnabled)return textOverlay_.has_value();
    if(textOverlay_&&query==Qt::ImCursorRectangle&&textOverlay_->caret>=0&&textOverlay_->caret<int(textOverlay_->carets.size())){
        const auto& line=textOverlay_->carets[size_t(textOverlay_->caret)];const auto mapping=viewMapping();
        const auto a=mapping.toView({line.x0,line.y0}),b=mapping.toView({line.x1,line.y1});
        return QRect(QPoint(int(std::min(a.x,b.x)),int(std::min(a.y,b.y))),QPoint(int(std::max(a.x,b.x))+1,int(std::max(a.y,b.y))+1));
    }
    return QWidget::inputMethodQuery(query);
}
void NativeCanvas::tabletEvent(QTabletEvent*e){
    e->accept();if(pointerHover)pointerHover(e->position(),e->modifiers());
    if(e->type()==QEvent::TabletPress){setFocus();if(dragging_||rightDragging_)return;tabletActive_=dragging_=true;grabMouse();if(pointerDown)pointerDown(documentPoint(e->position()),e->modifiers());}
    else if(e->type()==QEvent::TabletMove){if(tabletActive_&&pointerMove)pointerMove(documentPoint(e->position()),e->modifiers());}
    else if(e->type()==QEvent::TabletRelease&&tabletActive_){tabletActive_=dragging_=false;releaseMouse();if(pointerUp)pointerUp(documentPoint(e->position()),e->modifiers());}
    // The pinned source has no pressure mapping. Accept prevents Qt's duplicate synthesized mouse stroke.
}
void NativeCanvas::leaveEvent(QEvent*e){if(pointerLeave)pointerLeave();if(!dragging_&&!rightDragging_)unsetCursor();QWidget::leaveEvent(e);}
bool NativeCanvas::event(QEvent*e){if(e->type()==QEvent::Show){watchPresentationWindow();refreshDisplayProfile();updateSelectionAnimation();}
    if(e->type()==QEvent::Hide&&selectionTimer_)selectionTimer_->stop();
    const bool focusLost=e->type()==QEvent::WindowDeactivate||e->type()==QEvent::FocusOut;
    if(focusLost||(e->type()==QEvent::UngrabMouse&&(dragging_||rightDragging_))){setSnapGuides();dragging_=rightDragging_=tabletActive_=false;if(QWidget::mouseGrabber()==this)releaseMouse();if(pointerInterrupted)pointerInterrupted();else if(pointerCancel)pointerCancel();}
    if(e->type()==QEvent::DevicePixelRatioChange){releaseDevice();synchronizeViewport();}
    if(e->type()==QEvent::NativeGesture){auto*gesture=static_cast<QNativeGestureEvent*>(e);if(gesture->gestureType()==Qt::ZoomNativeGesture){if(documentWidth_>0&&(!navigationAllowed||navigationAllowed()))zoomAt(zoom*(1+gesture->value()),gesture->position());gesture->accept();return true;}}
    return QWidget::event(e);
}
}



