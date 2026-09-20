#include "NativeCanvas.h"
#include <QEvent>
#include <QWindow>
#include <QScreen>
#include <d2d1effects.h>
#include <sstream>

namespace compositor {
namespace {
void presentationCheck(HRESULT result,const char* operation){
    if(FAILED(result)){std::ostringstream message;message<<operation<<" (HRESULT 0x"<<std::hex<<unsigned(result)<<')';throw std::runtime_error(message.str());}
}
}
void NativeCanvas::resetPresentationResources(){
    presentationEffect_.Reset();presentationScene_.Reset();presentationBytes_=0;presentationAttempted_=false;
}
void NativeCanvas::watchPresentationWindow(){
    if(!profileWatcher_)profileWatcher_=std::make_unique<platform::DisplayProfileWatcher>([this]{if(!displayProfileOverride_)refreshDisplayProfile();});
    auto* top=window();if(profileWindow_==top)return;
    if(profileWindow_)profileWindow_->removeEventFilter(this);
    QObject::disconnect(profileScreenConnection_);profileWindow_=top;
    if(top){top->installEventFilter(this);if(auto* handle=top->windowHandle())
        profileScreenConnection_=connect(handle,&QWindow::screenChanged,this,[this](QScreen*){refreshDisplayProfile();});}
}
void NativeCanvas::refreshDisplayProfile(){
    ++profileDiscoveryCount_;
    auto profile=displayProfileOverride_?platform::loadDisplayProfile(*displayProfileOverride_):platform::discoverDisplayProfile(reinterpret_cast<void*>(winId()));
    const bool changed=!profileKnown_||profile.kind!=displayProfile_.kind||profile.sha256!=displayProfile_.sha256;
    const bool failedPresentation=!changed&&presentationAttempted_&&!presentationEffect_&&displayProfile_.kind==platform::DisplayProfileKind::Icc;
    profileKnown_=true;displayProfile_=std::move(profile);
    if(!failedPresentation)presentationDiagnostic_=QString::fromStdString(displayProfile_.diagnostic);
    if(changed)resetPresentationResources();update();
}
void NativeCanvas::setDisplayProfileOverride(std::optional<std::filesystem::path> path){
    displayProfileOverride_=std::move(path);refreshDisplayProfile();
}
void NativeCanvas::setPresentationBudget(uint64_t bytes){
    if(bytes>128ULL*1024*1024)throw std::invalid_argument("Presentation budget cannot exceed 128 MiB");
    if(bytes!=presentationByteBudget_){presentationByteBudget_=bytes;resetPresentationResources();update();}
}
bool NativeCanvas::eventFilter(QObject* watched,QEvent* event){
    if(watched==profileWindow_){
        if(event->type()==QEvent::WindowActivate||event->type()==QEvent::Show){if(profileWatcher_)profileWatcher_->retry();refreshDisplayProfile();}
        else if(event->type()==QEvent::Move&&!displayProfileOverride_){
            MONITORINFOEXW info{};info.cbSize=sizeof(info);
            const auto monitor=MonitorFromWindow(reinterpret_cast<HWND>(winId()),MONITOR_DEFAULTTONEAREST);
            if(monitor&&GetMonitorInfoW(monitor,&info)&&displayProfile_.monitorDevice!=info.szDevice)refreshDisplayProfile();
        }
    }
    return QWidget::eventFilter(watched,event);
}
void NativeCanvas::preparePresentation(){
    if(!profileKnown_)refreshDisplayProfile();
    if(!presentationAttempted_&&displayProfile_.kind==platform::DisplayProfileKind::Icc){
        presentationAttempted_=true;
        try{
            const auto size=target_->GetPixelSize();const uint64_t bytes=uint64_t(size.width)*size.height*4;
            if(bytes>presentationByteBudget_)throw std::runtime_error("Monitor presentation exceeds its viewport memory budget");
            const auto properties=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_IGNORE),float(96*devicePixelRatioF()),float(96*devicePixelRatioF()));
            presentationCheck(context_->CreateBitmap(size,nullptr,0,&properties,&presentationScene_),"Create sRGB presentation surface");
            Microsoft::WRL::ComPtr<ID2D1ColorContext> srgb,destination;
            presentationCheck(context_->CreateColorContext(D2D1_COLOR_SPACE_SRGB,nullptr,0,&srgb),"Create presentation sRGB context");
            presentationCheck(context_->CreateColorContext(D2D1_COLOR_SPACE_CUSTOM,displayProfile_.icc.data(),UINT32(displayProfile_.icc.size()),&destination),"Create presentation monitor context");
            presentationCheck(context_->CreateEffect(CLSID_D2D1ColorManagement,&presentationEffect_),"Create presentation color effect");
            presentationCheck(presentationEffect_->SetValue(D2D1_COLORMANAGEMENT_PROP_SOURCE_COLOR_CONTEXT,srgb.Get()),"Set presentation source profile");
            presentationCheck(presentationEffect_->SetValue(D2D1_COLORMANAGEMENT_PROP_DESTINATION_COLOR_CONTEXT,destination.Get()),"Set presentation destination profile");
            presentationCheck(presentationEffect_->SetValue(D2D1_COLORMANAGEMENT_PROP_SOURCE_RENDERING_INTENT,D2D1_COLORMANAGEMENT_RENDERING_INTENT_RELATIVE_COLORIMETRIC),"Set presentation source intent");
            presentationCheck(presentationEffect_->SetValue(D2D1_COLORMANAGEMENT_PROP_DESTINATION_RENDERING_INTENT,D2D1_COLORMANAGEMENT_RENDERING_INTENT_RELATIVE_COLORIMETRIC),"Set presentation destination intent");
            presentationCheck(presentationEffect_->SetValue(D2D1_COLORMANAGEMENT_PROP_ALPHA_MODE,D2D1_COLORMANAGEMENT_ALPHA_MODE_PREMULTIPLIED),"Set presentation alpha mode");
            presentationEffect_->SetInput(0,presentationScene_.Get());presentationBytes_=bytes;presentationDiagnostic_=QString::fromStdString(displayProfile_.diagnostic);
        }catch(const std::exception& error){presentationEffect_.Reset();presentationScene_.Reset();presentationBytes_=0;presentationDiagnostic_=QString::fromUtf8(error.what())+"; using sRGB presentation";}
    }
    context_->SetTarget(presentationScene_?presentationScene_.Get():target_.Get());
}
HRESULT NativeCanvas::finishPresentation(){
    if(!presentationScene_)return S_OK;
    context_->SetTarget(target_.Get());context_->BeginDraw();
    context_->DrawImage(presentationEffect_.Get(),D2D1::Point2F(0,0),D2D1::RectF(0,0,float(width()),float(height())),D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR,D2D1_COMPOSITE_MODE_SOURCE_COPY);
    auto result=context_->EndDraw();
    if(FAILED(result)&&result!=D2DERR_RECREATE_TARGET){
        // The canonical scene is already complete. A failed transform can safely
        // present those pixels while retaining a diagnostic and disabling retries.
        presentationDiagnostic_=QString("Monitor presentation failed (HRESULT 0x%1); using sRGB presentation").arg(uint32_t(result),8,16,QChar('0'));
        context_->BeginDraw();context_->DrawBitmap(presentationScene_.Get(),D2D1::RectF(0,0,float(width()),float(height())),1,D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR);result=context_->EndDraw();
        presentationEffect_.Reset();presentationScene_.Reset();presentationBytes_=0;
    }
    return result;
}
}
