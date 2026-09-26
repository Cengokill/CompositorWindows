// Owned native OLE provider. No QMimeData or synthetic Qt drop substitutes.
#include "ui/MainWindow.h"
#include "ui/ImportActions.h"
#include "ui/WorkspaceDropQueue.h"
#include <QApplication>
#include <QBuffer>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QThread>
#include <QUrl>
#include <windows.h>
#include <ole2.h>
#include <shlobj.h>
#include <atomic>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <memory>
#include <thread>

using namespace compositor;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void pump(int ms=25){QElapsedTimer elapsed;elapsed.start();while(elapsed.elapsed()<ms){QApplication::processEvents();QThread::msleep(1);}}
struct Stats{QJsonArray gets,queries,reads;int providerDestroyed{},streamDestroyed{};};
struct Item{QString name;QByteArray bytes;QSize size;QColor color;};
Item item(QString name,QSize size,QColor color){QImage image(size,QImage::Format_RGBA8888);image.fill(color);QByteArray bytes;QBuffer buffer(&bytes);require(buffer.open(QIODevice::WriteOnly)&&image.save(&buffer,"PNG"),"create owned PNG payload");return{std::move(name),std::move(bytes),size,color};}
class Stream final:public IStream {
    std::atomic<ULONG> refs_{1};IStream* inner_;std::shared_ptr<Stats> stats_;int index_;
public:
    Stream(IStream* inner,std::shared_ptr<Stats> stats,int index):inner_(inner),stats_(std::move(stats)),index_(index){}
    ~Stream(){inner_->Release();++stats_->streamDestroyed;}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(id==IID_IUnknown||id==IID_ISequentialStream||id==IID_IStream){*out=static_cast<IStream*>(this);AddRef();return S_OK;}return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs_;}
    ULONG STDMETHODCALLTYPE Release()override{auto count=--refs_;if(!count)delete this;return count;}
    HRESULT STDMETHODCALLTYPE Read(void* destination,ULONG count,ULONG* read)override{ULONG actual=0;const auto hr=inner_->Read(destination,count,&actual);if(read)*read=actual;stats_->reads.append(QJsonObject{{"index",index_},{"requested",qint64(count)},{"actual",qint64(actual)},{"hr",qint64(hr)}});return hr;}
    HRESULT STDMETHODCALLTYPE Write(const void*,ULONG,ULONG*)override{return STG_E_ACCESSDENIED;}
    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER move,DWORD origin,ULARGE_INTEGER* position)override{return inner_->Seek(move,origin,position);}
    HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER)override{return STG_E_ACCESSDENIED;}
    HRESULT STDMETHODCALLTYPE CopyTo(IStream* destination,ULARGE_INTEGER count,ULARGE_INTEGER* read,ULARGE_INTEGER* written)override{return inner_->CopyTo(destination,count,read,written);}
    HRESULT STDMETHODCALLTYPE Commit(DWORD flags)override{return inner_->Commit(flags);}
    HRESULT STDMETHODCALLTYPE Revert()override{return STG_E_REVERTED;}
    HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER,ULARGE_INTEGER,DWORD)override{return STG_E_INVALIDFUNCTION;}
    HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER,ULARGE_INTEGER,DWORD)override{return STG_E_INVALIDFUNCTION;}
    HRESULT STDMETHODCALLTYPE Stat(STATSTG* stat,DWORD flags)override{return inner_->Stat(stat,flags);}
    HRESULT STDMETHODCALLTYPE Clone(IStream** stream)override{if(!stream)return E_POINTER;*stream=nullptr;IStream* clone=nullptr;const auto hr=inner_->Clone(&clone);if(FAILED(hr))return hr;*stream=new Stream(clone,stats_,index_);return S_OK;}
};
HGLOBAL globalCopy(const void* bytes,size_t count){const auto memory=GlobalAlloc(GMEM_MOVEABLE,count);if(!memory)return nullptr;auto* target=GlobalLock(memory);if(!target){GlobalFree(memory);return nullptr;}memcpy(target,bytes,count);GlobalUnlock(memory);return memory;}
class Provider final:public IDataObject {
    std::atomic<ULONG> refs_{1};std::vector<Item> items_;QStringList paths_;bool stream_;
    std::shared_ptr<Stats> stats_;CLIPFORMAT descriptor_=CLIPFORMAT(RegisterClipboardFormatW(CFSTR_FILEDESCRIPTORW)),contents_=CLIPFORMAT(RegisterClipboardFormatW(L"FileContents"));
    QString format(CLIPFORMAT type)const{if(type==CF_HDROP)return "CF_HDROP";if(type==descriptor_)return "FileGroupDescriptorW";if(type==contents_)return "FileContents";wchar_t text[256]{};const int length=GetClipboardFormatNameW(type,text,256);return length?QString::fromWCharArray(text,length):QString::number(type);}
    HRESULT supported(FORMATETC* value)const{
        if(!value)return E_POINTER;if(value->dwAspect!=DVASPECT_CONTENT)return DV_E_DVASPECT;
        if(!paths_.empty())return value->cfFormat==CF_HDROP&&(value->tymed&TYMED_HGLOBAL)?S_OK:DV_E_FORMATETC;
        if(value->cfFormat==descriptor_)return value->tymed&TYMED_HGLOBAL?S_OK:DV_E_TYMED;
        if(value->cfFormat==contents_){if(value->lindex<0||size_t(value->lindex)>=items_.size())return DV_E_LINDEX;return value->tymed&(stream_?TYMED_ISTREAM:TYMED_HGLOBAL)?S_OK:DV_E_TYMED;}
        return DV_E_FORMATETC;
    }
public:
    Provider(std::vector<Item> items,bool stream,QStringList paths,std::shared_ptr<Stats> stats):items_(std::move(items)),paths_(std::move(paths)),stream_(stream),stats_(std::move(stats)){}
    ~Provider(){++stats_->providerDestroyed;}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(id==IID_IUnknown||id==IID_IDataObject){*out=static_cast<IDataObject*>(this);AddRef();return S_OK;}return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs_;}
    ULONG STDMETHODCALLTYPE Release()override{auto count=--refs_;if(!count)delete this;return count;}
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* value)override{FORMATETC capability{};auto* query=value;if(value&&value->cfFormat==contents_&&value->lindex==-1){capability=*value;capability.lindex=0;query=&capability;}const auto hr=supported(query);if(value&&stats_->queries.size()<2048)stats_->queries.append(QJsonObject{{"format",format(value->cfFormat)},{"index",int(value->lindex)},{"tymed",int(value->tymed)},{"hr",qint64(hr)}});return hr;}
    HRESULT STDMETHODCALLTYPE GetData(FORMATETC* value,STGMEDIUM* medium)override{
        if(!medium)return E_POINTER;*medium={};const auto hr=supported(value);if(FAILED(hr))return hr;
        stats_->gets.append(QJsonObject{{"format",format(value->cfFormat)},{"index",int(value->lindex)},{"requested_tymed",int(value->tymed)}});
        if(value->cfFormat==CF_HDROP){
            std::wstring names;for(const auto& path:paths_){names+=path.toStdWString();names.push_back(0);}names.push_back(0);
            std::vector<uint8_t> bytes(sizeof(DROPFILES)+names.size()*sizeof(wchar_t));auto* header=reinterpret_cast<DROPFILES*>(bytes.data());header->pFiles=sizeof(DROPFILES);header->fWide=TRUE;memcpy(bytes.data()+sizeof(DROPFILES),names.data(),names.size()*sizeof(wchar_t));
            medium->tymed=TYMED_HGLOBAL;medium->hGlobal=globalCopy(bytes.data(),bytes.size());return medium->hGlobal?S_OK:E_OUTOFMEMORY;
        }
        if(value->cfFormat==descriptor_){
            const size_t count=offsetof(FILEGROUPDESCRIPTORW,fgd)+items_.size()*sizeof(FILEDESCRIPTORW);std::vector<uint8_t> bytes(count);auto* group=reinterpret_cast<FILEGROUPDESCRIPTORW*>(bytes.data());group->cItems=UINT(items_.size());
            for(size_t i=0;i<items_.size();++i){auto& entry=group->fgd[i];entry.dwFlags=DWORD(FD_FILESIZE|FD_ATTRIBUTES|FD_UNICODE);entry.dwFileAttributes=FILE_ATTRIBUTE_NORMAL;entry.nFileSizeLow=DWORD(items_[i].bytes.size());const auto name=items_[i].name.toStdWString();wcscpy_s(entry.cFileName,name.c_str());}
            medium->tymed=TYMED_HGLOBAL;medium->hGlobal=globalCopy(bytes.data(),bytes.size());return medium->hGlobal?S_OK:E_OUTOFMEMORY;
        }
        // Contents are provided only when requested, after a bounded25ms delay.
        Sleep(25);const auto& bytes=items_[size_t(value->lindex)].bytes;HGLOBAL memory=globalCopy(bytes.data(),size_t(bytes.size()));if(!memory)return E_OUTOFMEMORY;
        if(!stream_){medium->tymed=TYMED_HGLOBAL;medium->hGlobal=memory;return S_OK;}
        IStream* inner=nullptr;const auto result=CreateStreamOnHGlobal(memory,TRUE,&inner);if(FAILED(result)){GlobalFree(memory);return result;}
        ULARGE_INTEGER length{};length.QuadPart=ULONGLONG(bytes.size());const auto resized=inner->SetSize(length);if(FAILED(resized)){inner->Release();return resized;}
        medium->tymed=TYMED_ISTREAM;medium->pstm=new Stream(inner,stats_,int(value->lindex));return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*,STGMEDIUM*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*,FORMATETC* value)override{if(value)value->ptd=nullptr;return DATA_S_SAMEFORMATETC;}
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC*,STGMEDIUM*,BOOL)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD direction,IEnumFORMATETC** enumerator)override{
        if(direction!=DATADIR_GET)return E_NOTIMPL;
        if(!paths_.empty()){FORMATETC value{CF_HDROP,nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};return SHCreateStdEnumFmtEtc(1,&value,enumerator);}
        FORMATETC values[2]{{descriptor_,nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL},{contents_,nullptr,DVASPECT_CONTENT,0,DWORD(stream_?TYMED_ISTREAM:TYMED_HGLOBAL)}};return SHCreateStdEnumFmtEtc(2,values,enumerator);
    }
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*,DWORD,IAdviseSink*,DWORD*)override{return OLE_E_ADVISENOTSUPPORTED;}
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD)override{return OLE_E_ADVISENOTSUPPORTED;}
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**)override{return OLE_E_ADVISENOTSUPPORTED;}
};
class DropSource final:public IDropSource {
    std::atomic<ULONG> refs_{1};DWORD thread_=GetCurrentThreadId();ULONGLONG started_=GetTickCount64();LPARAM point_;
public:
    explicit DropSource(LPARAM point):point_(point){}
    int feedback{};DWORD lastEffect{};
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(id==IID_IUnknown||id==IID_IDropSource){*out=static_cast<IDropSource*>(this);AddRef();return S_OK;}return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs_;}
    ULONG STDMETHODCALLTYPE Release()override{auto count=--refs_;if(!count)delete this;return count;}
    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escape,DWORD)override{if(escape||GetTickCount64()-started_>3000)return DRAGDROP_S_CANCEL;return feedback?DRAGDROP_S_DROP:S_OK;}
    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD effect)override{++feedback;lastEffect=effect;PostThreadMessageW(thread_,WM_LBUTTONUP,0,point_);return DRAGDROP_S_USEDEFAULTCURSORS;}
};
struct CursorRestore{POINT point{};CursorRestore(){GetCursorPos(&point);}~CursorRestore(){SetCursorPos(point.x,point.y);}};
QJsonObject performDrop(QWidget& target,QPoint local,Provider* provider){
    target.show();target.raise();target.activateWindow();pump(70);CursorRestore restore;
    const QPoint rootLocal=target.mapTo(target.window(),local);const double dpr=target.devicePixelRatioF();POINT global{LONG(rootLocal.x()*dpr),LONG(rootLocal.y()*dpr)};
    require(ClientToScreen(reinterpret_cast<HWND>(target.window()->winId()),&global)!=FALSE,"map owned window point to physical screen");require(SetCursorPos(global.x,global.y)!=FALSE,"position pointer inside owned window");
    const HWND root=reinterpret_cast<HWND>(target.window()->winId());POINT physical{};GetCursorPos(&physical);require(GetAncestor(WindowFromPoint(physical),GA_ROOT)==root,"owned drop window is under pointer");
    const DWORD thread=GetCurrentThreadId();
    // Native OLE owns the loop and calls the real Qt IDropTarget. Only this
    // process's thread queue is pulsed; no OS key/button input is synthesized.
    const LPARAM point=MAKELPARAM(global.x,global.y);
    std::jthread pulse([thread,point](std::stop_token stop){while(!stop.stop_requested()){Sleep(20);PostThreadMessageW(thread,WM_MOUSEMOVE,0,point);PostThreadMessageW(thread,WM_LBUTTONUP,0,point);}});
    auto* source=new DropSource(point);DWORD effect=0;const auto hr=DoDragDrop(provider,source,DROPEFFECT_COPY,&effect);pulse.request_stop();pulse.join();
    QJsonObject result{{"hr",qint64(hr)},{"effect",qint64(effect)},{"feedback_calls",source->feedback},{"last_feedback_effect",qint64(source->lastEffect)}};source->Release();return result;
}
class Probe final:public QWidget {
    const std::vector<Item>& items_;
public:
    QJsonArray formats;bool entered{},dropped{},bytesExact{},imageExact{};
    explicit Probe(const std::vector<Item>& items):items_(items){setAcceptDrops(true);resize(500,300);setWindowTitle("Owned virtual file provider probe");}
protected:
    void dragEnterEvent(QDragEnterEvent* event)override{entered=true;for(const auto& format:event->mimeData()->formats())formats.append(format);event->acceptProposedAction();}
    void dropEvent(QDropEvent* event)override{
        dropped=true;bytesExact=imageExact=true;
        for(size_t i=0;i<items_.size();++i){const auto bytes=event->mimeData()->data(QString("application/x-qt-windows-mime;value=\"FileContents\";index=%1").arg(i));bytesExact=bytesExact&&bytes==items_[i].bytes;const auto image=QImage::fromData(bytes);imageExact=imageExact&&!image.isNull()&&image.size()==items_[i].size&&image.pixelColor(0,0)==items_[i].color;}
        event->acceptProposedAction();
    }
};
class Observe final:public QObject {
public:QJsonArray formats;bool entered{},dropped{};
protected:bool eventFilter(QObject*,QEvent* event)override{if(event->type()==QEvent::DragEnter){entered=true;const auto* drag=static_cast<QDragEnterEvent*>(event);for(const auto& format:drag->mimeData()->formats())formats.append(format);}if(event->type()==QEvent::Drop)dropped=true;return false;}
};
Document document(){Document d;d.id=newId();d.width=64;d.height=48;Layer l;l.id=newId();l.name="Original";l.transform={0,0,64,48};l.raster=Raster::filled(64,48,{20,30,40,255});d.layers={l};return d;}
}
int main(int argc,char** argv){
    QApplication app(argc,argv);std::cout<<std::unitbuf;if(argc!=3)return 2;const std::string key=argv[1];const std::filesystem::path output(argv[2]);if(std::filesystem::exists(output))return 2;std::filesystem::create_directories(output);
    const std::vector<std::string> keys{"hdrop_control","virtual_stream_probe","virtual_hglobal_probe","virtual_stream_single","virtual_hglobal_single","virtual_stream_order","virtual_busy_rejected"};if(std::find(keys.begin(),keys.end(),key)==keys.end())return 2;
    const auto ole=OleInitialize(nullptr);if(FAILED(ole))return 3;
    auto stats=std::make_shared<Stats>();QJsonObject report;bool passed=false;std::string error;Provider* provider=nullptr;
    try{
        const bool probe=key.ends_with("probe"),multiple=probe||key=="virtual_stream_order",stream=key.find("hglobal")==std::string::npos;
        std::vector<Item> items{item("First.png",{7,5},Qt::red)};if(multiple)items.push_back(item("Second.png",{9,6},Qt::blue));QStringList paths;
        if(key=="hdrop_control")for(const auto& value:items){const auto path=QString::fromStdWString((output/value.name.toStdWString()).wstring());QFile file(path);require(file.open(QIODevice::WriteOnly)&&file.write(value.bytes)==value.bytes.size(),"write owned hdrop fixture");file.close();paths.append(path);}
        provider=new Provider(items,stream,paths,stats);require(stats->gets.empty(),"provider contents not eagerly requested");
        if(probe){Probe target(items);report["ole"]=performDrop(target,target.rect().center(),provider);report["qt_formats"]=target.formats;report["qt_drag_enter"]=target.entered;report["qt_drop"]=target.dropped;report["bytes_exact"]=target.bytesExact;report["images_exact"]=target.imageExact;require(target.entered&&target.dropped&&target.bytesExact&&target.imageExact,"real Qt Windows MIME bridge reads every indexed provider payload exactly");}
        else{
            MainWindow window(true);auto& project=window.addProject(document());const auto before=project.document;window.resize(1180,880);window.show();pump(40);Observe observed;window.installEventFilter(&observed);
            const bool busy=key=="virtual_busy_rejected";project.projectBusy=busy;const auto point=project.canvas->mapTo(&window,project.canvas->rect().center());report["ole"]=performDrop(window,point,provider);report["qt_formats"]=observed.formats;report["qt_drag_enter"]=observed.entered;report["qt_drop"]=observed.dropped;
            QElapsedTimer elapsed;elapsed.start();while(elapsed.elapsed()<5000){pump(5);const auto* imports=ui::ImportQueue::find(&window);const auto* drops=ui::WorkspaceDropQueue::find(&window);if((!imports||imports->idle())&&(!drops||drops->idle()))break;}
            report["layer_count"]=int(project.document->layers.size());report["undo_count"]=int(project.history.undoCount());
            if(busy){const bool fetchedContents=std::any_of(stats->gets.begin(),stats->gets.end(),[](const QJsonValue& call){return call.toObject()["format"]=="FileContents";});require(!observed.dropped&&project.document==before&&project.history.undoCount()==0&&!fetchedContents,"prebusy actual OLE drop rejects without fetching contents or mutating");project.projectBusy=false;}
            else{require(observed.entered&&observed.dropped,"actual MainWindow accepts the provider drop");require(project.document->layers.size()==items.size()+1&&project.history.undoCount()==items.size(),"provider images import in source order with separate undo entries");for(size_t i=0;i<items.size();++i){const auto& layer=project.document->layers[i+1];require(layer.name.starts_with(items[i].name.chopped(4).toStdString())&&layer.raster&&layer.raster->width==items[i].size.width()&&layer.raster->height==items[i].size.height(),"provider name prefix, order and source dimensions survive ingestion");const auto pixel=layer.raster->pixel(0,0);require(pixel.r==items[i].color.red()&&pixel.g==items[i].color.green()&&pixel.b==items[i].color.blue()&&pixel.a==255,"provider pixel bytes survive import");}}
        }
        passed=true;
    }catch(const std::exception& value){error=value.what();}
    if(provider)provider->Release();pump();report["get_data"]=stats->gets;report["query_get_data"]=stats->queries;report["stream_reads"]=stats->reads;report["provider_destroyed"]=stats->providerDestroyed;report["streams_destroyed"]=stats->streamDestroyed;report["schema"]="NATIVE_VIRTUAL_DROP_V1";report["case"]=QString::fromStdString(key);report["passed"]=passed;report["error"]=QString::fromStdString(error);report["mac_differential"]=false;
    QFile file(QString::fromStdWString((output/L"results.json").wstring()));if(!file.open(QIODevice::WriteOnly))return 3;file.write(QJsonDocument(report).toJson());std::cout<<(passed?"PASS ":"FAIL ")<<key<<" "<<error<<'\n';OleUninitialize();return passed?0:1;
}
