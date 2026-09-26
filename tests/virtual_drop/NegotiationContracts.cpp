// Frozen before first execution of pending42. Reuses only the owned provider
// and native loop controller from the immutable baseline fixture.
#define main preservedVirtualDropMain
#include "NativeVirtualDropWitness.cpp"
#undef main
#include "ui/VirtualImageDrop.h"
#include <QFileInfo>
#include <shldisp.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
namespace {
struct Negotiation {std::atomic<int> starts{},ends{},contents{},workerContents{};DWORD owner=GetCurrentThreadId();HRESULT ending=E_UNEXPECTED;bool inOperation{};};
class NegotiatedProvider final:public IDataObject,public IDataObjectAsyncCapability {
    std::atomic<ULONG> refs_{1};ComPtr<IDataObject> inner_;ComPtr<IUnknown> marshaler_;std::shared_ptr<Negotiation> log_;bool enabled_;std::string fault_;
public:
    NegotiatedProvider(Provider* inner,std::shared_ptr<Negotiation> log,bool enabled,bool agile,std::string fault):inner_(inner),log_(std::move(log)),enabled_(enabled),fault_(std::move(fault)){if(agile)require(SUCCEEDED(CoCreateFreeThreadedMarshaler(static_cast<IDataObject*>(this),&marshaler_)),"create owned agile provider marshaler");}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(id==IID_IUnknown||id==IID_IDataObject)*out=static_cast<IDataObject*>(this);else if(id==IID_IDataObjectAsyncCapability)*out=static_cast<IDataObjectAsyncCapability*>(this);else if(id==IID_IMarshal&&marshaler_)return marshaler_->QueryInterface(id,out);else return E_NOINTERFACE;AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs_;}ULONG STDMETHODCALLTYPE Release()override{auto n=--refs_;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE GetData(FORMATETC* format,STGMEDIUM* medium)override{
        const auto contents=RegisterClipboardFormatW(L"FileContents"),descriptor=RegisterClipboardFormatW(CFSTR_FILEDESCRIPTORW);
        if(format&&format->cfFormat==contents){++log_->contents;if(GetCurrentThreadId()!=log_->owner)++log_->workerContents;if(fault_=="slow")Sleep(150);if(fault_=="unreadable"&&format->lindex==0)return E_FAIL;}
        auto status=inner_->GetData(format,medium);if(SUCCEEDED(status)&&format->cfFormat==descriptor&&medium->tymed==TYMED_HGLOBAL){
            if(fault_=="truncated"){ReleaseStgMedium(medium);UINT count=2;medium->tymed=TYMED_HGLOBAL;medium->hGlobal=globalCopy(&count,sizeof(count));medium->pUnkForRelease=nullptr;}
            else if(fault_=="short_size"){auto* group=static_cast<FILEGROUPDESCRIPTORW*>(GlobalLock(medium->hGlobal));group->fgd[0].nFileSizeLow=1;GlobalUnlock(medium->hGlobal);}
        }return status;
    }
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC* a,STGMEDIUM* b)override{return inner_->GetDataHere(a,b);}
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* a)override{return inner_->QueryGetData(a);}
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC* a,FORMATETC* b)override{return inner_->GetCanonicalFormatEtc(a,b);}
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC* a,STGMEDIUM* b,BOOL c)override{return inner_->SetData(a,b,c);}
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD a,IEnumFORMATETC** b)override{return inner_->EnumFormatEtc(a,b);}
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC* a,DWORD b,IAdviseSink* c,DWORD* d)override{return inner_->DAdvise(a,b,c,d);}
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD a)override{return inner_->DUnadvise(a);}
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA** a)override{return inner_->EnumDAdvise(a);}
    HRESULT STDMETHODCALLTYPE SetAsyncMode(BOOL value)override{enabled_=value!=FALSE;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetAsyncMode(BOOL* value)override{if(!value)return E_POINTER;*value=enabled_;return S_OK;}
    HRESULT STDMETHODCALLTYPE StartOperation(IBindCtx*)override{++log_->starts;log_->inOperation=true;return S_OK;}
    HRESULT STDMETHODCALLTYPE InOperation(BOOL* value)override{if(!value)return E_POINTER;*value=log_->inOperation;return S_OK;}
    HRESULT STDMETHODCALLTYPE EndOperation(HRESULT result,IBindCtx*,DWORD)override{++log_->ends;log_->ending=result;log_->inOperation=false;return S_OK;}
};
QJsonObject nativeDrop(QWidget& target,IDataObject* provider){
    target.show();target.raise();target.activateWindow();pump(70);CursorRestore restore;const double dpr=target.devicePixelRatioF();POINT global{LONG(target.width()*dpr/2),LONG(target.height()*dpr/2)};const auto window=reinterpret_cast<HWND>(target.winId());require(ClientToScreen(window,&global)!=FALSE&&SetCursorPos(global.x,global.y)!=FALSE,"owned native drop pointer mapping");const auto under=GetAncestor(WindowFromPoint(global),GA_ROOT);POINT actual{};GetCursorPos(&actual);DWORD underPid=0;GetWindowThreadProcessId(under,&underPid);std::cout<<"native_mapping own_visible="<<IsWindowVisible(window)<<" own="<<window<<" own_root="<<GetAncestor(window,GA_ROOT)<<" under="<<under<<" under_pid="<<underPid<<" own_pid="<<GetCurrentProcessId()<<" point="<<global.x<<','<<global.y<<" actual="<<actual.x<<','<<actual.y<<" dpr="<<dpr<<'\n';require(under==window,"owned native drop target visible");const auto thread=GetCurrentThreadId();const LPARAM point=MAKELPARAM(global.x,global.y);
    std::jthread pulse([thread,point](std::stop_token stop){while(!stop.stop_requested()){Sleep(20);PostThreadMessageW(thread,WM_MOUSEMOVE,0,point);PostThreadMessageW(thread,WM_LBUTTONUP,0,point);}});auto* source=new DropSource(point);DWORD effect=0;const auto status=DoDragDrop(provider,source,DROPEFFECT_COPY,&effect);pulse.request_stop();pulse.join();source->Release();return{{"hr",qint64(status)},{"effect",qint64(effect)}};
}
class TransferTarget final:public QWidget {
public:
    ui::VirtualDropLimits limits;QObject* owner;QPointer<ui::VirtualDropJob> job;std::optional<ui::VirtualDropResult> result;QString captureError;bool entered{},dropped{};
    explicit TransferTarget(QObject* scope):owner(scope){setWindowFlag(Qt::WindowStaysOnTopHint);resize(480,260);move(100,100);setAcceptDrops(true);setWindowTitle("Owned virtual image negotiation contract");}
protected:
    void dragEnterEvent(QDragEnterEvent* event)override{entered=true;if(ui::VirtualDropJob::hasVirtualFiles(*event->mimeData()))event->acceptProposedAction();}
    void dropEvent(QDropEvent* event)override{dropped=true;try{ui::WorkspaceDropRequest destination;destination.destination=owner;destination.hasDestination=true;destination.point=Point{12.5,31.25};job=ui::VirtualDropJob::start(*event->mimeData(),destination,owner,[this](ui::VirtualDropResult value){result=std::move(value);},{},limits);event->acceptProposedAction();}catch(const std::exception& error){captureError=QString::fromUtf8(error.what());}}
};
}
#ifndef VIRTUAL_DROP_PROVIDER_ONLY
int main(int argc,char** argv){
    QApplication app(argc,argv);std::cout<<std::unitbuf;if(argc!=3)return 2;const std::string key=argv[1];const std::filesystem::path output(argv[2]);if(std::filesystem::exists(output))return 2;std::filesystem::create_directories(output);require(SUCCEEDED(OleInitialize(nullptr)),"initialize owned OLE apartment");
    const std::vector<std::string> keys{"sync_stream_order","sync_hglobal_order","async_sta_apartment","async_agile_apartment","owner_destroyed","cancel_pending","soft_timeout_waits_for_cleanup","item_count_limit","file_byte_limit","total_byte_limit","exact_byte_boundary","unsafe_path","reserved_name","truncated_descriptors","declared_size_mismatch","unreadable_item_continues"};if(std::find(keys.begin(),keys.end(),key)==keys.end())return 2;
    bool pass=false;std::string error;QJsonObject report;auto stats=std::make_shared<Stats>();auto negotiation=std::make_shared<Negotiation>();
    try{
        auto owner=std::make_unique<QObject>();TransferTarget target(owner.get());target.limits.chunkBytes=16;std::vector<Item> items{item("First.png",{7,5},Qt::red),item(QString::fromUtf8("猫.png"),{9,6},Qt::blue)};
        const uint64_t bytes=uint64_t(items[0].bytes.size()+items[1].bytes.size());const bool asynchronous=key.starts_with("async_")||key=="owner_destroyed"||key=="cancel_pending"||key=="soft_timeout_waits_for_cleanup"||key=="unreadable_item_continues";const bool agile=asynchronous&&key!="async_sta_apartment";
        if(key=="item_count_limit")target.limits.maxItems=1;if(key=="file_byte_limit")target.limits.maxFileBytes=uint64_t(items[0].bytes.size())-1;if(key=="total_byte_limit")target.limits.maxTotalBytes=bytes-1;if(key=="exact_byte_boundary")target.limits.maxTotalBytes=bytes;if(key=="unsafe_path")items[0].name="../First.png";if(key=="reserved_name")items[0].name="CON.png";if(key=="soft_timeout_waits_for_cleanup")target.limits.softTimeoutMs=25;
        const auto fault=key=="truncated_descriptors"?"truncated":key=="declared_size_mismatch"?"short_size":key=="unreadable_item_continues"?"unreadable":key=="owner_destroyed"||key=="cancel_pending"||key=="soft_timeout_waits_for_cleanup"?"slow":"";
        auto* raw=new Provider(items,key!="sync_hglobal_order",{},stats);auto* native=new NegotiatedProvider(raw,negotiation,asynchronous,agile,fault);raw->Release();report["ole"]=nativeDrop(target,native);native->Release();require(target.entered&&target.dropped&&target.captureError.isEmpty()&&target.job,"real native provider capture succeeds");
        if(key=="owner_destroyed")owner.reset();if(key=="cancel_pending")target.job->cancel();QElapsedTimer elapsed;elapsed.start();while(target.job&&elapsed.elapsed()<6000)pump(5);require(!target.job,"provider copy and operation cleanup complete");
        report["callback_received"]=target.result.has_value();report["starts"]=negotiation->starts.load();report["ends"]=negotiation->ends.load();report["ending_hr"]=qint64(negotiation->ending);report["contents_calls"]=negotiation->contents.load();report["worker_contents_calls"]=negotiation->workerContents.load();
        require(negotiation->starts==(asynchronous?1:0)&&negotiation->ends==(asynchronous?1:0)&&!negotiation->inOperation,"async operation starts and ends exactly once on every path");
        if(key=="owner_destroyed"){require(!target.result&&negotiation->ending==E_ABORT,"destroyed owner cancels without invoking stale callback");}
        else{
            require(target.result.has_value(),"live owner receives result");auto& result=*target.result;report["error"]=result.error;report["cancelled"]=result.cancelled;report["timed_out"]=result.timedOut;report["copied_bytes"]=qint64(result.bytesCopied);report["maximum_chunk"]=int(result.maximumChunk);report["path_count"]=int(result.request.paths.size());
            require(result.request.hasDestination&&result.request.destination==owner.get()&&result.request.point==Point{12.5,31.25},"captured destination and point remain exact");
            const bool bad=key=="item_count_limit"||key=="file_byte_limit"||key=="total_byte_limit"||key=="truncated_descriptors";
            if(bad){require(!result.error.isEmpty()&&result.request.paths.isEmpty()&&!result.request.lifetime,"invalid input fails with no partial imported paths or retained temporary files");if(key!="declared_size_mismatch")require(negotiation->contents==0,"invalid metadata fails before any contents read");}
            else if(key=="unsafe_path"||key=="reserved_name"||key=="declared_size_mismatch"||key=="unreadable_item_continues"){
                require(result.error.startsWith("Item 1:")&&result.request.paths.size()==1&&result.request.lifetime&&!result.cancelled,"one unreadable item reports its index and preserves the readable sibling");
                const auto path=result.request.paths.front();QFile copied(path);require(copied.open(QIODevice::ReadOnly)&&copied.readAll()==items[1].bytes,"readable sibling retains every PNG byte in descriptor order");copied.close();
                require(QFileInfo(path).fileName().startsWith(QFileInfo(items[1].name).completeBaseName()+"-"),"partial success preserves the readable Unicode filename");
                require(result.maximumChunk<=16,"partial success retains the frozen chunk bound");
                if(key=="unsafe_path"||key=="reserved_name")require(negotiation->contents==1,"unsafe names are skipped before reading their contents");
                if(asynchronous)require(negotiation->ending==S_OK,"partial asynchronous success ends with successful copy status");
                result.request.lifetime.reset();require(!QFileInfo::exists(path),"partial-success temporary storage is removed after lifetime release");
            }
            else if(key=="cancel_pending"||key=="soft_timeout_waits_for_cleanup"){require(result.cancelled&&result.request.paths.isEmpty()&&!result.request.lifetime&&negotiation->ending==E_ABORT,"cancellation waits for safe provider cleanup and discards files");if(key=="soft_timeout_waits_for_cleanup")require(result.timedOut,"soft timeout is explicitly reported");}
            else{
                require(result.error.isEmpty()&&result.request.paths.size()==2&&result.bytesCopied==bytes&&result.maximumChunk<=16,"exact byte budget, bounded chunk size and descriptor order");const auto paths=result.request.paths;for(int index=0;index<2;++index){QFile file(paths[index]);require(file.open(QIODevice::ReadOnly)&&file.readAll()==items[size_t(index)].bytes,"copied native contents equal every original PNG byte");require(QFileInfo(paths[index]).fileName().startsWith(QFileInfo(items[size_t(index)].name).completeBaseName()+"-"),"safe Unicode descriptor names retain their stem");}result.request.lifetime.reset();for(const auto& path:paths)require(!QFileInfo::exists(path),"temporary copies removed when queue lifetime releases");
                if(key=="async_sta_apartment")require(negotiation->workerContents==0,"STA provider calls marshal back to the originating apartment");if(key=="async_agile_apartment")require(negotiation->workerContents==2,"agile provider reads execute in the worker apartment");
            }
        }
        require(stats->providerDestroyed==1,"owned provider released after transfer settles");pass=true;
    }catch(const std::exception& value){error=value.what();}
    report["schema"]="VIRTUAL_DROP_NEGOTIATION_V1";report["case"]=QString::fromStdString(key);report["passed"]=pass;report["failed_predicate"]=QString::fromStdString(error);report["provider_destroyed"]=stats->providerDestroyed;report["get_data"]=stats->gets;report["stream_reads"]=stats->reads;report["mac_differential"]=false;QFile file(QString::fromStdWString((output/L"results.json").wstring()));require(file.open(QIODevice::WriteOnly),"write negotiation result");file.write(QJsonDocument(report).toJson());std::cout<<(pass?"PASS ":"FAIL ")<<key<<" "<<error<<'\n';OleUninitialize();return pass?0:1;
}
#endif
