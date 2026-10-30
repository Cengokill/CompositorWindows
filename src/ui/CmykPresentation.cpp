#include "NativeCanvas.h"
#include "imaging/cmyk.h"
#include <QtConcurrent/QtConcurrentRun>
#include <d2d1_3.h>
#include <d2d1effects_2.h>
#include <stdexcept>

namespace compositor {
namespace {
void proofCheck(HRESULT hr,const char* text){if(FAILED(hr))throw std::runtime_error(text);}
}
void NativeCanvas::setCmykProof(CmykProfilePtr profile,bool enabled){
    const auto hash=profile?profile->sha256:std::string{};
    if(proofEnabled_==enabled&&(proofProfile_?proofProfile_->sha256:std::string{})==hash)return;
    if(proofCancelled_)proofCancelled_->store(true);
    proofProfile_=std::move(profile);proofEnabled_=enabled&&bool(proofProfile_);proofFailure_.clear();
    resetCmykResources();
    if(!proofEnabled_)setToolTip({});
    if(proofLutHash_!=hash){proofLut_.clear();proofLutHash_.clear();}
    if(!proofConnected_){proofConnected_=true;connect(&proofWatcher_,&QFutureWatcher<ProofResult>::finished,this,[this]{
        const auto result=proofWatcher_.result();
        if(proofEnabled_&&proofProfile_&&result.hash==proofProfile_->sha256&&!result.cancelled){
            if(!result.error.isEmpty()){proofFailure_=result.error;setToolTip("CMYK proof unavailable: "+result.error);}
            else {proofLut_=result.lookup;proofLutHash_=result.hash;setToolTip({});}
            resetCmykResources();update();
        }
        if(proofEnabled_&&proofLut_.empty()&&proofFailure_.isEmpty())startCmykLookup();
    });}
    if(proofEnabled_&&proofLut_.empty()&&!proofWatcher_.isRunning())startCmykLookup();
    update();
}
void NativeCanvas::startCmykLookup(){
    if(!proofEnabled_||!proofProfile_||proofWatcher_.isRunning())return;
    auto profile=proofProfile_;proofCancelled_=std::make_shared<std::atomic_bool>(false);auto cancel=proofCancelled_;
    setToolTip("Preparing CMYK proof…");
    proofWatcher_.setFuture(QtConcurrent::run([profile,cancel]{ProofResult result;result.hash=profile->sha256;
        try{result.lookup=imaging::cmykProofLookup(profile,[cancel]{return cancel->load();});}
        catch(const std::exception& error){result.error=QString::fromUtf8(error.what());}
        result.cancelled=cancel->load();return result;
    }));
}
void NativeCanvas::shutdownCmykProof(){if(proofCancelled_)proofCancelled_->store(true);disconnect(&proofWatcher_,nullptr,this,nullptr);proofWatcher_.waitForFinished();}
void NativeCanvas::resetCmykResources(){proofEffect_.Reset();proofScene_.Reset();proofDestination_.Reset();proofApplied_=false;}
bool NativeCanvas::beginCmykProof(){
    proofApplied_=false;
    if(!proofEnabled_||proofLut_.empty()||!proofFailure_.isEmpty())return false;
    try{
        if(!proofScene_){
            const auto size=target_->GetPixelSize();const uint64_t bytes=uint64_t(size.width)*size.height*4;
            if(bytes>presentationByteBudget_||presentationBytes_>presentationByteBudget_-bytes)throw std::runtime_error("CMYK proof exceeds the presentation memory budget");
            Microsoft::WRL::ComPtr<ID2D1DeviceContext2> context;proofCheck(context_.As(&context),"Direct2D CMYK lookup support is unavailable");
            const UINT32 extents[]{33,33,33},strides[]{33*4*sizeof(float),33*33*4*sizeof(float)};
            Microsoft::WRL::ComPtr<ID2D1LookupTable3D> lookup;
            proofCheck(context->CreateLookupTable3D(D2D1_BUFFER_PRECISION_32BPC_FLOAT,extents,reinterpret_cast<const BYTE*>(proofLut_.data()),UINT32(proofLut_.size()*sizeof(float)),strides,&lookup),"Cannot upload CMYK proof lookup");
            proofCheck(context_->CreateEffect(CLSID_D2D1LookupTable3D,&proofEffect_),"Cannot create CMYK proof effect");
            proofCheck(proofEffect_->SetValue(D2D1_LOOKUPTABLE3D_PROP_LUT,lookup.Get()),"Cannot set CMYK proof lookup");
            proofCheck(proofEffect_->SetValue(D2D1_LOOKUPTABLE3D_PROP_ALPHA_MODE,D2D1_ALPHA_MODE_PREMULTIPLIED),"Cannot set CMYK proof alpha mode");
            const auto properties=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_IGNORE),float(96*devicePixelRatioF()),float(96*devicePixelRatioF()));
            proofCheck(context_->CreateBitmap(size,nullptr,0,&properties,&proofScene_),"Cannot allocate CMYK proof viewport");proofEffect_->SetInput(0,proofScene_.Get());
        }
        context_->GetTarget(&proofDestination_);context_->SetTarget(proofScene_.Get());context_->Clear(D2D1::ColorF(1,1,1));proofApplied_=true;return true;
    }catch(const std::exception& error){proofFailure_=QString::fromUtf8(error.what());setToolTip("CMYK proof unavailable: "+proofFailure_);resetCmykResources();return false;}
}
void NativeCanvas::finishCmykProof(D2D1_RECT_F bounds){
    if(!proofApplied_)return;
    context_->SetTarget(proofDestination_.Get());proofDestination_.Reset();
    context_->PushAxisAlignedClip(bounds,D2D1_ANTIALIAS_MODE_ALIASED);
    context_->DrawImage(proofEffect_.Get());context_->PopAxisAlignedClip();
}
}
