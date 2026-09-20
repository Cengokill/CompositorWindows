#include "BrushCoverage.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace compositor::graphics {
using Microsoft::WRL::ComPtr;
namespace {
void checked(HRESULT hr,const char* operation) {
    if(FAILED(hr)){std::ostringstream message;message<<operation<<" failed (HRESULT 0x"<<std::hex<<uint32_t(hr)<<")";
        throw std::runtime_error(message.str());}
}
ComPtr<ID3D11Buffer> makeBuffer(ID3D11Device* device,UINT bytes,UINT bind,UINT stride,const void* initial=nullptr) {
    D3D11_BUFFER_DESC desc{};desc.ByteWidth=bytes;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=bind;
    if(stride){desc.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;desc.StructureByteStride=stride;}
    D3D11_SUBRESOURCE_DATA data{};data.pSysMem=initial;ComPtr<ID3D11Buffer> result;
    checked(device->CreateBuffer(&desc,initial?&data:nullptr,&result),"CreateBuffer");return result;
}
ComPtr<ID3D11Buffer> stagingBuffer(ID3D11Device* device,UINT bytes) {
    D3D11_BUFFER_DESC desc{};desc.ByteWidth=bytes;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Buffer> result;checked(device->CreateBuffer(&desc,nullptr,&result),"Create staging buffer");return result;
}
ComPtr<ID3D11UnorderedAccessView> uav(ID3D11Device* device,ID3D11Buffer* buffer,UINT count) {
    D3D11_UNORDERED_ACCESS_VIEW_DESC desc{};desc.Format=DXGI_FORMAT_UNKNOWN;desc.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;
    desc.Buffer.NumElements=count;ComPtr<ID3D11UnorderedAccessView> result;
    checked(device->CreateUnorderedAccessView(buffer,&desc,&result),"CreateUnorderedAccessView");return result;
}
void readBuffer(ID3D11DeviceContext* context,ID3D11Buffer* buffer,void* destination,size_t bytes) {
    D3D11_MAPPED_SUBRESOURCE mapping{};checked(context->Map(buffer,0,D3D11_MAP_READ,0,&mapping),"Map staging buffer");
    std::memcpy(destination,mapping.pData,bytes);context->Unmap(buffer,0);
}
}
struct D3D11BrushCoverage::Impl {
    struct Slot {
        ComPtr<ID3D11Buffer> permanent,preview,constants,permanentRead,previewRead;
        ComPtr<ID3D11UnorderedAccessView> permanentView,previewView;
    };
    struct Readback {std::vector<float> permanent;std::vector<uint32_t> preview;};
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;ComPtr<ID3D11ComputeShader> shader;
    std::string name;
    std::vector<Slot> pool;
    std::vector<std::unique_ptr<Readback>> readbacks;
    Slot& slot(size_t index) {
        constexpr UINT pixels=brushTileSize*brushTileSize,bytes=pixels*4;
        while(pool.size()<=index){Slot next;
            next.permanent=makeBuffer(device.Get(),bytes,D3D11_BIND_UNORDERED_ACCESS,4);
            next.preview=makeBuffer(device.Get(),bytes,D3D11_BIND_UNORDERED_ACCESS,4);
            next.constants=makeBuffer(device.Get(),sizeof(BrushUniforms),D3D11_BIND_CONSTANT_BUFFER,0);
            next.permanentView=uav(device.Get(),next.permanent.Get(),pixels);next.previewView=uav(device.Get(),next.preview.Get(),pixels);
            next.permanentRead=stagingBuffer(device.Get(),bytes);next.previewRead=stagingBuffer(device.Get(),bytes);
            pool.push_back(std::move(next));}
        return pool[index];
    }
};
D3D11BrushCoverage::D3D11BrushCoverage(const std::filesystem::path& path,bool preferWarp):impl_(std::make_unique<Impl>()) {
    const D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_0};D3D_FEATURE_LEVEL obtained{};
    auto create=[&](D3D_DRIVER_TYPE type){return D3D11CreateDevice(nullptr,type,nullptr,0,levels,1,D3D11_SDK_VERSION,
        &impl_->device,&obtained,&impl_->context);};
    auto hr=create(preferWarp?D3D_DRIVER_TYPE_WARP:D3D_DRIVER_TYPE_HARDWARE);
    if(FAILED(hr) && !preferWarp){impl_->device.Reset();impl_->context.Reset();hr=create(D3D_DRIVER_TYPE_WARP);}
    checked(hr,"D3D11CreateDevice");
    ComPtr<IDXGIDevice> dxgiDevice;ComPtr<IDXGIAdapter> adapter;DXGI_ADAPTER_DESC desc{};
    checked(impl_->device.As(&dxgiDevice),"Query DXGI device");checked(dxgiDevice->GetAdapter(&adapter),"Get adapter");
    checked(adapter->GetDesc(&desc),"Get adapter description");
    char name[512]{};WideCharToMultiByte(CP_UTF8,0,desc.Description,-1,name,sizeof(name),nullptr,nullptr);impl_->name=name;
    ComPtr<ID3DBlob> code,errors;
    hr=D3DCompileFromFile(path.c_str(),nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,"continuousBrush","cs_5_0",
        D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
    if(FAILED(hr) && errors)throw std::runtime_error(std::string(static_cast<const char*>(errors->GetBufferPointer()),errors->GetBufferSize()));
    checked(hr,"D3DCompileFromFile");checked(impl_->device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&impl_->shader),"CreateComputeShader");
}
D3D11BrushCoverage::~D3D11BrushCoverage()=default;
D3D11BrushCoverage::D3D11BrushCoverage(D3D11BrushCoverage&&) noexcept=default;
D3D11BrushCoverage& D3D11BrushCoverage::operator=(D3D11BrushCoverage&&) noexcept=default;
std::string D3D11BrushCoverage::adapterName() const {return impl_->name;}
void D3D11BrushCoverage::render(BrushTile& tile,BrushUniforms uniforms,std::span<const BrushSegment> settled,std::span<const BrushSegment> tail) {
    const BrushTileRender request{&tile,uniforms};renderBatch({&request,1},settled,tail);
}
void D3D11BrushCoverage::renderBatch(std::span<const BrushTileRender> requests,std::span<const BrushSegment> settled,std::span<const BrushSegment> tail) {
    if(requests.empty())return;
    if(requests.size()>4096)throw std::length_error("Brush batch exceeds bounded tile count");
    std::unordered_set<const BrushTile*> unique;
    struct Pending {BrushTile* tile;BrushUniforms uniforms;Impl::Readback* readback;};
    std::vector<Pending> pending;pending.reserve(requests.size());
    std::vector<std::unique_ptr<Impl::Readback>> overflow;
    for(auto request:requests){if(!request.tile||!unique.insert(request.tile).second)throw std::invalid_argument("Missing or duplicate brush batch tile");
        validateBrush(*request.tile,request.uniforms,settled,tail);
        Impl::Readback* result;
        if(pending.size()<64){while(impl_->readbacks.size()<=pending.size())impl_->readbacks.push_back(std::make_unique<Impl::Readback>());result=impl_->readbacks[pending.size()].get();}
        else{overflow.push_back(std::make_unique<Impl::Readback>());result=overflow.back().get();}
        // Reuse staging storage; do not copy authoritative permanent floats into
        // a readback buffer that is about to be entirely overwritten by Map.
        result->permanent.resize(request.tile->permanent.size());result->preview.resize(request.tile->preview.size());
        pending.push_back({request.tile,request.uniforms,result});}
    std::vector<BrushSegment> segments;segments.reserve(std::max<size_t>(1,settled.size()+tail.size()));
    segments.insert(segments.end(),settled.begin(),settled.end());segments.insert(segments.end(),tail.begin(),tail.end());
    const auto count=static_cast<uint32_t>(segments.size());if(segments.empty())segments.push_back({});
    auto geometry=makeBuffer(impl_->device.Get(),static_cast<UINT>(segments.size()*sizeof(BrushSegment)),D3D11_BIND_SHADER_RESOURCE,16,segments.data());
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};srvDesc.Format=DXGI_FORMAT_UNKNOWN;srvDesc.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.NumElements=static_cast<UINT>(segments.size());ComPtr<ID3D11ShaderResourceView> segmentView;
    checked(impl_->device->CreateShaderResourceView(geometry.Get(),&srvDesc,&segmentView),"CreateShaderResourceView");
    ID3D11ShaderResourceView* input=segmentView.Get();
    auto* context=impl_->context.Get();context->CSSetShader(impl_->shader.Get(),nullptr,0);
    context->CSSetShaderResources(0,1,&input);
    // Sixteen reusable slots bound GPU/staging storage to approximately 16 MiB.
    // Dispatch a chunk before mapping any tile, avoiding per-tile CPU/GPU stalls.
    constexpr size_t batchSize=16;
    for(size_t first=0;first<pending.size();first+=batchSize){size_t number=std::min(batchSize,pending.size()-first);
        impl_->slot(number-1);
        for(size_t j=0;j<number;++j){auto& job=pending[first+j];auto& slot=impl_->pool[j];auto& tile=*job.tile;
            job.uniforms.counts={tile.width,tile.height,static_cast<uint32_t>(settled.size()),count};
            D3D11_BOX box{0,0,0,static_cast<UINT>(tile.permanent.size()*sizeof(float)),1,1};
            context->UpdateSubresource(slot.permanent.Get(),0,&box,tile.permanent.data(),0,0);
            context->UpdateSubresource(slot.constants.Get(),0,nullptr,&job.uniforms,0,0);
            ID3D11UnorderedAccessView* outputs[]={slot.permanentView.Get(),slot.previewView.Get()};ID3D11Buffer* cb=slot.constants.Get();
            context->CSSetConstantBuffers(0,1,&cb);context->CSSetUnorderedAccessViews(0,2,outputs,nullptr);
            context->Dispatch((tile.width+15)/16,(tile.height+15)/16,1);
        }
        ID3D11UnorderedAccessView* nullOutputs[2]{};context->CSSetUnorderedAccessViews(0,2,nullOutputs,nullptr);
        for(size_t j=0;j<number;++j){auto& slot=impl_->pool[j];context->CopyResource(slot.permanentRead.Get(),slot.permanent.Get());context->CopyResource(slot.previewRead.Get(),slot.preview.Get());}
        for(size_t j=0;j<number;++j){auto& job=pending[first+j];auto& slot=impl_->pool[j];auto bytes=job.readback->permanent.size()*sizeof(float);
            readBuffer(context,slot.permanentRead.Get(),job.readback->permanent.data(),bytes);readBuffer(context,slot.previewRead.Get(),job.readback->preview.data(),bytes);}
    }
    ID3D11ShaderResourceView* nullInput=nullptr;ID3D11Buffer* nullBuffer=nullptr;
    context->CSSetShaderResources(0,1,&nullInput);context->CSSetConstantBuffers(0,1,&nullBuffer);
    checked(impl_->device->GetDeviceRemovedReason(),"D3D11 device status");
    for(auto& job:pending){job.tile->permanent.swap(job.readback->permanent);for(size_t i=0;i<job.readback->preview.size();++i)job.tile->preview[i]=static_cast<uint8_t>(job.readback->preview[i]);}
}
} // namespace compositor::graphics
