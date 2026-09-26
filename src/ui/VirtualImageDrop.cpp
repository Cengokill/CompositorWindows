#include "VirtualImageDrop.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoopLocker>
#include <QFile>
#include <QFileInfo>
#include <QMimeData>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <QVariant>
#include <QWindowsMimeConverter>
#include <windows.h>
#include <ole2.h>
#include <shlobj.h>
#include <shldisp.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>
namespace compositor::ui {
namespace {
using Microsoft::WRL::ComPtr;
constexpr auto captureMime="application/x-compositor-native-virtual-file-capture";
const QString descriptorW="application/x-qt-windows-mime;value=\"FileGroupDescriptorW\"";
const QString descriptorA="application/x-qt-windows-mime;value=\"FileGroupDescriptor\"";
const QString contentsMime="application/x-qt-windows-mime;value=\"FileContents\"";
void check(bool good,const char* error){if(!good)throw std::runtime_error(error);}
void hr(HRESULT result,const char* error){check(SUCCEEDED(result),error);}
struct UnreadableItem:std::runtime_error {using std::runtime_error::runtime_error;};
void itemCheck(bool good,const char* error){if(!good)throw UnreadableItem(error);}
struct Medium {STGMEDIUM value{};~Medium(){if(value.tymed)ReleaseStgMedium(&value);}};
struct GlobalLockGuard {HGLOBAL memory;const void* data;explicit GlobalLockGuard(HGLOBAL value,bool perItem=false):memory(value),data(GlobalLock(value)){if(perItem)itemCheck(data!=nullptr,"Cannot lock virtual image contents");else check(data!=nullptr,"Cannot lock virtual file storage");}~GlobalLockGuard(){GlobalUnlock(memory);}};
struct Entry {QString name;std::optional<uint64_t> bytes;QString error;};
QString safeName(QString name){
    itemCheck(!name.isEmpty()&&name.size()<=255,"Virtual image filename length is invalid");
    itemCheck(name.trimmed()==name&&!name.endsWith('.')&&!name.endsWith(' '),"Virtual image filename has invalid trailing characters");
    for(const auto c:name)itemCheck(c.unicode()>=32&&!QStringLiteral("/\\:<>\"|?*").contains(c),"Virtual image filename contains a path or invalid character");
    const QString stem=name.section('.',0,0).toUpper();
    itemCheck(stem!="CON"&&stem!="PRN"&&stem!="AUX"&&stem!="NUL"&&!(stem.size()==4&&(stem.startsWith("COM")||stem.startsWith("LPT"))&&stem[3]>='1'&&stem[3]<='9'),"Virtual image filename is a reserved device name");
    const auto suffix=QFileInfo(name).suffix().toLower();
    itemCheck(QStringList{"png","jpg","jpeg","heic","heif","tif","tiff"}.contains(suffix),"Virtual image filename has an unsupported suffix");
    return QFileInfo(name).completeBaseName()+"-"+QUuid::createUuid().toString(QUuid::WithoutBraces)+"."+suffix;
}
void validateLimits(const VirtualDropLimits& value){
    check(value.maxItems>0&&value.maxItems<=256&&value.maxFileBytes>0&&value.maxFileBytes<=400000000&&value.maxTotalBytes>0&&value.maxTotalBytes<=1200000000&&value.chunkBytes>0&&value.chunkBytes<=65536&&value.softTimeoutMs>0,"Invalid virtual image transfer limits");
}
std::vector<Entry> descriptors(IDataObject* source,const VirtualDropLimits& limits){
    FORMATETC format{CLIPFORMAT(RegisterClipboardFormatW(CFSTR_FILEDESCRIPTORW)),nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};Medium medium;bool wide=true;
    if(FAILED(source->GetData(&format,&medium.value))){format.cfFormat=CLIPFORMAT(RegisterClipboardFormatW(L"FileGroupDescriptor"));wide=false;hr(source->GetData(&format,&medium.value),"Cannot read virtual file descriptors");}
    check(medium.value.tymed==TYMED_HGLOBAL&&medium.value.hGlobal,"Virtual file descriptors require global memory");GlobalLockGuard lock(medium.value.hGlobal);
    const auto available=GlobalSize(medium.value.hGlobal);check(available>=sizeof(UINT),"Truncated virtual file descriptor header");UINT count{};std::memcpy(&count,lock.data,sizeof(count));
    check(count>0&&count<=limits.maxItems,"Virtual file descriptor count exceeds the transfer limit");
    const size_t stride=wide?sizeof(FILEDESCRIPTORW):sizeof(FILEDESCRIPTORA);check(count<=(available-sizeof(UINT))/stride,"Truncated virtual file descriptors");
    std::vector<Entry> result;result.reserve(count);uint64_t total=0;
    for(UINT i=0;i<count;++i){DWORD flags{},attributes{},high{},low{};QString name;Entry entry;const auto* bytes=static_cast<const uint8_t*>(lock.data)+sizeof(UINT)+size_t(i)*stride;
        try{
        if(wide){FILEDESCRIPTORW value{};std::memcpy(&value,bytes,sizeof(value));const auto* end=std::find(std::begin(value.cFileName),std::end(value.cFileName),L'\0');itemCheck(end!=std::end(value.cFileName),"Unterminated virtual filename");name=QString::fromWCharArray(value.cFileName,int(end-value.cFileName));flags=value.dwFlags;attributes=value.dwFileAttributes;high=value.nFileSizeHigh;low=value.nFileSizeLow;}
        else{FILEDESCRIPTORA value{};std::memcpy(&value,bytes,sizeof(value));const auto* end=std::find(std::begin(value.cFileName),std::end(value.cFileName),'\0');itemCheck(end!=std::end(value.cFileName),"Unterminated virtual filename");name=QString::fromLocal8Bit(value.cFileName,int(end-value.cFileName));flags=value.dwFlags;attributes=value.dwFileAttributes;high=value.nFileSizeHigh;low=value.nFileSizeLow;}
        itemCheck(!(flags&FD_ATTRIBUTES)||!(attributes&FILE_ATTRIBUTE_DIRECTORY),"Virtual directories are unsupported");entry.name=safeName(name);
        if(flags&FD_FILESIZE){const uint64_t length=(uint64_t(high)<<32)|low;check(length<=limits.maxFileBytes&&length<=limits.maxTotalBytes-total,"Virtual image transfer exceeds its byte budget");itemCheck(length>0,"Virtual image is empty");entry.bytes=length;total+=length;}
        }catch(const UnreadableItem& error){entry.error=QString::fromUtf8(error.what());}
        result.push_back(std::move(entry));
    }
    return result;
}
struct Work {
    VirtualDropLimits limits;std::atomic<bool> cancel{};std::atomic<uint64_t> progress{};
    std::mutex mutex;std::optional<VirtualDropResult> result;
};
VirtualDropResult copyFiles(IDataObject* source,const std::shared_ptr<Work>& work){
    VirtualDropResult result;try{
        const auto entries=descriptors(source,work->limits);auto storage=std::make_shared<QTemporaryDir>();check(storage->isValid(),"Cannot create virtual image temporary storage");
        const auto cancelled=[&]{check(!work->cancel.load(),"Virtual image transfer cancelled");};cancelled();
        QStringList failures;
        for(size_t index=0;index<entries.size();++index){cancelled();const auto& entry=entries[index];if(!entry.error.isEmpty()){failures.append(QString("Item %1: %2").arg(index+1).arg(entry.error));continue;}const auto stagedPath=storage->filePath(entry.name);
            try{FORMATETC format{CLIPFORMAT(RegisterClipboardFormatW(L"FileContents")),nullptr,DVASPECT_CONTENT,LONG(index),TYMED_ISTREAM|TYMED_HGLOBAL};Medium medium;itemCheck(SUCCEEDED(source->GetData(&format,&medium.value)),"Cannot read virtual image contents");cancelled();
            QFile file(stagedPath);itemCheck(file.open(QIODevice::WriteOnly|QIODevice::NewOnly),"Cannot create copied virtual image");uint64_t fileBytes=0;
            const auto write=[&](const char* data,uint32_t size){cancelled();check(size<=work->limits.maxFileBytes-fileBytes&&size<=work->limits.maxTotalBytes-result.bytesCopied,"Virtual image transfer exceeds its byte budget");itemCheck(!entry.bytes||size<=*entry.bytes-fileBytes,"Virtual image contents exceed their declared size");itemCheck(file.write(data,size)==size,"Cannot write copied virtual image");fileBytes+=size;result.bytesCopied+=size;result.maximumChunk=std::max(result.maximumChunk,size);work->progress=result.bytesCopied;};
            if(medium.value.tymed==TYMED_ISTREAM&&medium.value.pstm){LARGE_INTEGER start{};(void)medium.value.pstm->Seek(start,STREAM_SEEK_SET,nullptr);std::vector<char> buffer(work->limits.chunkBytes);for(;;){cancelled();ULONG received=0;const auto status=medium.value.pstm->Read(buffer.data(),ULONG(buffer.size()),&received);itemCheck(SUCCEEDED(status),"Cannot read virtual image stream");itemCheck(received<=buffer.size(),"Virtual image stream returned an invalid byte count");if(received)write(buffer.data(),received);if(status==S_FALSE||!received)break;}}
            else if(medium.value.tymed==TYMED_HGLOBAL&&medium.value.hGlobal){GlobalLockGuard lock(medium.value.hGlobal,true);const uint64_t available=GlobalSize(medium.value.hGlobal);itemCheck(entry.bytes.has_value(),"Virtual global-memory contents require a declared size");itemCheck(*entry.bytes<=available,"Virtual global-memory contents are shorter than their declared size");for(uint64_t offset=0;offset<*entry.bytes;){const auto size=uint32_t(std::min<uint64_t>(work->limits.chunkBytes,*entry.bytes-offset));write(static_cast<const char*>(lock.data)+offset,size);offset+=size;}}
            else throw UnreadableItem("Virtual image storage medium is unsupported");
            itemCheck(fileBytes>0&&(!entry.bytes||fileBytes==*entry.bytes),"Virtual image contents do not match their declared size");file.close();result.request.paths.append(file.fileName());
            }catch(const UnreadableItem& error){QFile::remove(stagedPath);failures.append(QString("Item %1: %2").arg(index+1).arg(QString::fromUtf8(error.what())));}
        }
        cancelled();result.error=failures.join("\n\n");if(!result.request.paths.isEmpty())result.request.lifetime=std::move(storage);
    }catch(const std::exception& error){result.request.paths.clear();result.request.lifetime.reset();result.error=QString::fromUtf8(error.what());result.cancelled=work->cancel.load();}
    return result;
}
class CaptureConverter final:public QWindowsMimeConverter {
public:
    std::function<void(IDataObject*)> capture;
    bool canConvertFromMime(const FORMATETC&,const QMimeData*)const override{return false;}
    bool convertFromMime(const FORMATETC&,const QMimeData*,STGMEDIUM*)const override{return false;}
    QList<FORMATETC> formatsForMime(const QString&,const QMimeData*)const override{return {};}
    bool canConvertToMime(const QString& name,IDataObject*)const override{return name==QLatin1String(captureMime);}
    QVariant convertToMime(const QString&,IDataObject* source,QMetaType)const override{capture(source);return QByteArray("captured");}
    QString mimeForFormat(const FORMATETC&)const override{return {};}
};
}
struct VirtualDropJob::Impl {
    std::shared_ptr<Work> work=std::make_shared<Work>();QPointer<QObject> owner;Completed complete;Progress progress;
    WorkspaceDropRequest destination;ComPtr<IDataObjectAsyncCapability> async;bool started{},done{},timedOut{};
    QTimer poll;QElapsedTimer elapsed;QEventLoopLocker quitLock;
};
VirtualDropJob::VirtualDropJob(std::unique_ptr<Impl> impl):QObject(QCoreApplication::instance()),impl_(std::move(impl)){
    setObjectName("virtualImageDropJob");impl_->poll.setInterval(25);impl_->elapsed.start();
    connect(&impl_->poll,&QTimer::timeout,this,[this]{auto& p=*impl_;if(!p.timedOut&&p.elapsed.elapsed()>p.work->limits.softTimeoutMs){p.timedOut=true;p.work->cancel=true;}if(p.owner&&p.progress){try{p.progress(p.work->progress.load(),p.timedOut);}catch(...){qWarning("Virtual image progress callback threw");p.work->cancel=true;}}finish();});
    connect(impl_->owner,&QObject::destroyed,this,[this]{cancel();});
}
VirtualDropJob::~VirtualDropJob(){impl_->work->cancel=true;}
void VirtualDropJob::cancel(){impl_->work->cancel=true;}
bool VirtualDropJob::finished()const{return impl_->done;}
void VirtualDropJob::finish(){
    auto& p=*impl_;std::optional<VirtualDropResult> result;{std::lock_guard lock(p.work->mutex);if(!p.work->result)return;result=std::move(p.work->result);p.work->result.reset();}p.done=true;p.poll.stop();
    result->asynchronous=p.started;result->timedOut=p.timedOut;if(p.work->cancel){result->cancelled=true;result->request.paths.clear();result->request.lifetime.reset();if(result->error.isEmpty())result->error="Virtual image transfer cancelled";}
    if(p.started){const bool copied=!result->cancelled&&!result->request.paths.isEmpty();p.async->EndOperation(copied?S_OK:result->cancelled?E_ABORT:E_FAIL,nullptr,copied?DROPEFFECT_COPY:DROPEFFECT_NONE);p.started=false;p.async.Reset();}
    result->request.destination=p.destination.destination;result->request.hasDestination=p.destination.hasDestination;result->request.point=p.destination.point;result->request.completed=std::move(p.destination.completed);
    deleteLater();if(p.owner&&p.complete){try{p.complete(std::move(*result));}catch(...){qWarning("Virtual image completion callback threw");}}
}
VirtualDropJob* VirtualDropJob::startNative(IDataObject* source,WorkspaceDropRequest destination,QObject* owner,Completed complete,Progress progress,VirtualDropLimits limits){
    check(source&&owner&&QCoreApplication::instance()&&QThread::currentThread()==QCoreApplication::instance()->thread(),"Virtual image capture requires a live UI owner");validateLimits(limits);
    auto impl=std::make_unique<Impl>();impl->owner=owner;impl->destination=std::move(destination);impl->complete=std::move(complete);impl->progress=std::move(progress);impl->work->limits=limits;
    auto* job=new VirtualDropJob(std::move(impl));auto& p=*job->impl_;
    try{
        BOOL enabled=FALSE;if(SUCCEEDED(source->QueryInterface(IID_PPV_ARGS(&p.async)))&&SUCCEEDED(p.async->GetAsyncMode(&enabled))&&enabled){hr(p.async->StartOperation(nullptr),"Cannot start asynchronous virtual image extraction");p.started=true;}
        if(p.started){IStream* marshaled=nullptr;hr(CoMarshalInterThreadInterfaceInStream(IID_IDataObject,source,&marshaled),"Cannot marshal virtual image provider");auto work=p.work;
            try{std::thread([work,marshaled]{VirtualDropResult result;const auto initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(SUCCEEDED(initialized)){ComPtr<IDataObject> data;const auto status=CoGetInterfaceAndReleaseStream(marshaled,IID_PPV_ARGS(&data));if(SUCCEEDED(status))result=copyFiles(data.Get(),work);else result.error="Cannot unmarshal virtual image provider";data.Reset();CoUninitialize();}else{marshaled->Release();result.error="Cannot initialize virtual image worker apartment";}std::lock_guard lock(work->mutex);work->result=std::move(result);}).detach();}
            catch(...){CoReleaseMarshalData(marshaled);marshaled->Release();throw;}
        }else{auto result=copyFiles(source,p.work);std::lock_guard lock(p.work->mutex);p.work->result=std::move(result);}
        p.poll.start();if(!p.started)QTimer::singleShot(0,job,[job]{job->finish();});return job;
    }catch(...){if(p.started){p.async->EndOperation(E_FAIL,nullptr,DROPEFFECT_NONE);p.started=false;}delete job;throw;}
}
bool VirtualDropJob::hasVirtualFiles(const QMimeData& mime){const auto formats=mime.formats();return (formats.contains(descriptorW)||formats.contains(descriptorA))&&formats.contains(contentsMime);}
VirtualDropJob* VirtualDropJob::start(const QMimeData& mime,WorkspaceDropRequest destination,QObject* owner,Completed completed,Progress progress,VirtualDropLimits limits){
    check(hasVirtualFiles(mime),"The drop does not advertise virtual file contents");VirtualDropJob* job=nullptr;std::exception_ptr error;CaptureConverter converter;
    converter.capture=[&](IDataObject* source){try{job=startNative(source,std::move(destination),owner,std::move(completed),std::move(progress),limits);}catch(...){error=std::current_exception();}};
    (void)mime.data(QLatin1String(captureMime));if(error)std::rethrow_exception(error);check(job!=nullptr,"The drop is not backed by a native Windows provider");return job;
}
}
