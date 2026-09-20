#include "Updater.h"
#include "TestTrust.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <bcrypt.h>
#include <winhttp.h>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <QUuid>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace compositor::update {
namespace {
namespace fs=std::filesystem;
constexpr const char* product="compositor-windows";
constexpr std::uint64_t metadataLimit=2*1024*1024;
[[noreturn]] void fail(const QString& message){throw std::runtime_error(message.toUtf8().constData());}
void require(bool value,const QString& message){if(!value)fail(message);}
void win(bool value,const char* operation){if(!value)fail(QString::fromLatin1(operation)+" (Windows error "+QString::number(GetLastError())+")");}
void cancelled(const Options& options){if(options.cancelled&&options.cancelled())fail("Update cancelled");}
QString qpath(const fs::path& path){return QString::fromStdWString(path.wstring());}
struct Handle {HANDLE value{INVALID_HANDLE_VALUE};Handle()=default;explicit Handle(HANDLE h):value(h){}~Handle(){if(value!=INVALID_HANDLE_VALUE&&value)CloseHandle(value);}Handle(const Handle&)=delete;Handle&operator=(const Handle&)=delete;Handle(Handle&& h)noexcept:value(h.value){h.value=INVALID_HANDLE_VALUE;}Handle&operator=(Handle&& h)noexcept{if(this!=&h){if(value!=INVALID_HANDLE_VALUE&&value)CloseHandle(value);value=h.value;h.value=INVALID_HANDLE_VALUE;}return *this;}};
struct Internet {HINTERNET value{};explicit Internet(HINTERNET h):value(h){win(h!=nullptr,"WinHTTP open");}~Internet(){if(value)WinHttpCloseHandle(value);}Internet(const Internet&)=delete;};
class Hash {
    BCRYPT_ALG_HANDLE algorithm_{};BCRYPT_HASH_HANDLE hash_{};std::vector<unsigned char> object_;
public:
    Hash(){if(BCryptOpenAlgorithmProvider(&algorithm_,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)fail("SHA256 provider unavailable");DWORD length=0,got=0;if(BCryptGetProperty(algorithm_,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&length),sizeof(length),&got,0)<0){BCryptCloseAlgorithmProvider(algorithm_,0);fail("SHA256 property failed");}object_.resize(length);if(BCryptCreateHash(algorithm_,&hash_,object_.data(),length,nullptr,0,0)<0){BCryptCloseAlgorithmProvider(algorithm_,0);fail("SHA256 initialization failed");}}
    ~Hash(){if(hash_)BCryptDestroyHash(hash_);if(algorithm_)BCryptCloseAlgorithmProvider(algorithm_,0);}
    void add(const char* bytes,DWORD length){if(BCryptHashData(hash_,reinterpret_cast<PUCHAR>(const_cast<char*>(bytes)),length,0)<0)fail("SHA256 update failed");}
    QByteArray finish(){QByteArray digest(32,0);if(BCryptFinishHash(hash_,reinterpret_cast<PUCHAR>(digest.data()),32,0)<0)fail("SHA256 finish failed");return digest;}
};
void keys(const QJsonObject& object,std::initializer_list<const char*> allowed){QSet<QString> names;for(auto key:allowed)names.insert(QString::fromLatin1(key));const auto present=object.keys();require(QSet<QString>(present.begin(),present.end())==names,"Unexpected or missing metadata fields");}
QJsonObject json(const QByteArray& bytes){QJsonParseError error;auto doc=QJsonDocument::fromJson(bytes,&error);require(error.error==QJsonParseError::NoError&&doc.isObject(),"Invalid JSON metadata");return doc.object();}
QString string(const QJsonObject& o,const char* key){require(o.value(key).isString(),"Metadata string required");return o.value(key).toString();}
std::uint64_t integer(const QJsonValue& v,std::uint64_t max){require(v.isDouble(),"Metadata integer required");double n=v.toDouble();require(std::isfinite(n)&&n>=0&&std::floor(n)==n&&n<=double(max),"Metadata integer outside limits");return std::uint64_t(n);}
QByteArray base64(const QString& text){auto raw=text.toLatin1();auto out=QByteArray::fromBase64(raw,QByteArray::AbortOnBase64DecodingErrors);require(!out.isEmpty()&&out.toBase64()==raw,"Invalid base64 metadata");return out;}
void relativePath(const QString& path){
    require(!path.isEmpty()&&path.size()<=240&&!path.startsWith('/')&&!path.contains('\\'),"Unsafe payload path");
    for(const auto& part:path.split('/')){
        require(!part.isEmpty()&&part!="."&&part!=".."&&!part.endsWith('.')&&!part.endsWith(' '),"Unsafe payload path component");
        for(auto ch:part)require(ch.unicode()>=32&&!QStringLiteral("<>:\"|?*").contains(ch),"Unsafe payload path character");
        const auto stem=part.section('.',0,0).toUpper();require(stem!="CON"&&stem!="PRN"&&stem!="AUX"&&stem!="NUL"&&!QRegularExpression("^(COM|LPT)[0-9]+$").match(stem).hasMatch(),"Reserved payload path");
    }
    require(path.compare("update-receipt.json",Qt::CaseInsensitive)!=0,"Reserved update receipt path");
}
class Directories {
    std::vector<Handle> handles_;
public:
    void hold(const fs::path& path){
        Handle h(CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));win(h.value!=INVALID_HANDLE_VALUE,"Open install directory");BY_HANDLE_FILE_INFORMATION info{};win(GetFileInformationByHandle(h.value,&info)!=0,"Inspect install directory");require((info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)&&!(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT),"Install paths must be ordinary directories, without reparse points");handles_.push_back(std::move(h));
    }
    void ensure(const fs::path& path){if(!CreateDirectoryW(path.c_str(),nullptr))require(GetLastError()==ERROR_ALREADY_EXISTS,"Cannot create update directory");hold(path);}
};
Handle readFile(const fs::path& path){Handle file(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN,nullptr));win(file.value!=INVALID_HANDLE_VALUE,"Open update file");BY_HANDLE_FILE_INFORMATION info{};win(GetFileInformationByHandle(file.value,&info)!=0,"Inspect update file");require(!(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)),"Update file cannot be a directory or reparse point");return file;}
QByteArray readSmall(const fs::path& path,std::uint64_t limit=metadataLimit){auto file=readFile(path);LARGE_INTEGER size{};win(GetFileSizeEx(file.value,&size)!=0,"Read update file size");require(size.QuadPart>=0&&std::uint64_t(size.QuadPart)<=limit,"Update metadata exceeds size limit");QByteArray out(int(size.QuadPart),0);DWORD done=0;win(ReadFile(file.value,out.data(),DWORD(out.size()),&done,nullptr)!=0,"Read update metadata");require(done==DWORD(out.size()),"Incomplete update metadata");return out;}
void writeAll(const fs::path& path,const QByteArray& bytes){Handle file(CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));win(file.value!=INVALID_HANDLE_VALUE,"Create update file");DWORD done=0;win(WriteFile(file.value,bytes.data(),DWORD(bytes.size()),&done,nullptr)!=0&&done==DWORD(bytes.size()),"Write update metadata");win(FlushFileBuffers(file.value)!=0,"Flush update metadata");}
void atomicJson(const fs::path& target,const QJsonObject& value){auto temporary=target.parent_path()/fs::path((QStringLiteral("pointer-")+QUuid::createUuid().toString(QUuid::WithoutBraces)+".tmp").toStdWString());writeAll(temporary,QJsonDocument(value).toJson(QJsonDocument::Compact));win(MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0,"Activate update pointer");}
QJsonObject stateJson(const ActiveState& state){return {{"schema",1},{"product",product},{"channel",state.channel},{"current",state.current.text()},{"previous",state.previous},{"pending",state.pending}};}
ActiveState readState(const fs::path& root){auto object=json(readSmall(root/L"state"/L"active.json",16384));keys(object,{"schema","product","channel","current","previous","pending"});require(integer(object["schema"],1)==1&&string(object,"product")==product&&object["pending"].isBool(),"Invalid install state");ActiveState s;s.current=Version::parse(string(object,"current"));s.channel=string(object,"channel");require(s.channel=="test","This development updater accepts only the test channel");s.previous=string(object,"previous");if(!s.previous.isEmpty())Version::parse(s.previous);s.pending=object["pending"].toBool();require(!s.pending||!s.previous.isEmpty(),"Pending update has no rollback version");return s;}
fs::path versionDirectory(const fs::path& root,const Version& v){return root/L"versions"/fs::path(v.text().toStdWString());}
struct Root {
    fs::path path;Directories anchors;
    explicit Root(const fs::path& supplied):path(fs::absolute(supplied).lexically_normal()){
        require(path.has_root_name()&&path.root_name().wstring().size()==2&&path!=path.root_path(),"Install root must be a local app directory");
        auto cursor=path.root_path();for(const auto& part:path.relative_path()){cursor/=part;anchors.hold(cursor);}
        auto marker=json(readSmall(path/L"install.json",16384));keys(marker,{"schema","product"});require(integer(marker["schema"],1)==1&&string(marker,"product")==product,"Root is not a Compositor versioned installation");anchors.hold(path/L"versions");anchors.hold(path/L"state");
    }
    Handle lock(){Handle h(CreateFileW((path/L"state"/L"update.lock").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));win(h.value!=INVALID_HANDLE_VALUE,"Lock update installation");BY_HANDLE_FILE_INFORMATION info{};win(GetFileInformationByHandle(h.value,&info)!=0,"Inspect update lock");require(!(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT),"Update lock cannot be a reparse point");return h;}
};
QByteArray envelope(const Manifest& manifest){return QJsonDocument(QJsonObject{{"keyId",testKeyId},{"payload",QString::fromLatin1(manifest.signedBytes.toBase64())},{"signature",QString::fromLatin1(manifest.signature.toBase64())}}).toJson(QJsonDocument::Compact);}
class Feed {
    QString base_;bool http_{};
public:
    explicit Feed(QString base):base_(std::move(base)){
        http_=base_.startsWith("http://",Qt::CaseInsensitive)||base_.startsWith("https://",Qt::CaseInsensitive);
        if(http_){QUrl url(base_);require(url.isValid()&&url.scheme()=="http"&&url.host()=="127.0.0.1"&&url.userInfo().isEmpty()&&!url.hasQuery()&&!url.hasFragment(),"Test HTTP feed must be a plain loopback URL without credentials or redirects");if(!base_.endsWith('/'))base_+='/';}
        else{if(QUrl(base_).isLocalFile())base_=QUrl(base_).toLocalFile();auto local=fs::path(base_.toStdWString());require(local.is_absolute()&&local.root_name().wstring().size()==2,"Test feed must be a local absolute directory or loopback HTTP URL");}
    }
    void read(const QString& relative,std::uint64_t maximum,const Options& options,const std::function<void(const char*,DWORD)>& consume)const{
        cancelled(options);std::uint64_t total=0;auto chunk=[&](const char* p,DWORD n){cancelled(options);require(n<=maximum-total,"Downloaded payload exceeds signed size");total+=n;consume(p,n);};std::array<char,65536> buffer{};
        if(!http_){auto file=readFile(fs::path(base_.toStdWString())/fs::path(relative.toStdWString()));for(;;){DWORD got=0;win(ReadFile(file.value,buffer.data(),DWORD(buffer.size()),&got,nullptr)!=0,"Read local update payload");if(!got)break;chunk(buffer.data(),got);}}
        else{
            QUrl url(base_);url.setPath(url.path()+relative);Internet session(WinHttpOpen(L"Compositor local update test/1",WINHTTP_ACCESS_TYPE_NO_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));WinHttpSetTimeouts(session.value,5000,5000,5000,5000);auto host=url.host().toStdWString();Internet connection(WinHttpConnect(session.value,host.c_str(),INTERNET_PORT(url.port(80)),0));auto path=url.path(QUrl::FullyEncoded).toStdWString();Internet request(WinHttpOpenRequest(connection.value,L"GET",path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,0));DWORD policy=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;win(WinHttpSetOption(request.value,WINHTTP_OPTION_REDIRECT_POLICY,&policy,sizeof(policy))!=0,"Disable update redirects");win(WinHttpSendRequest(request.value,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0)!=0&&WinHttpReceiveResponse(request.value,nullptr)!=0,"Fetch loopback test update");DWORD status=0,length=sizeof(status);win(WinHttpQueryHeaders(request.value,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&length,WINHTTP_NO_HEADER_INDEX)!=0,"Read update HTTP status");require(status==200,"Update HTTP response was not successful");for(;;){DWORD got=0;win(WinHttpReadData(request.value,buffer.data(),DWORD(buffer.size()),&got)!=0,"Download update payload");if(!got)break;chunk(buffer.data(),got);}
        }cancelled(options);
    }
    QByteArray metadata(const Options& options)const{QByteArray out;read("feed.json",metadataLimit,options,[&](const char* p,DWORD n){out.append(p,n);});return out;}
};
void verifyInstalled(const fs::path& directory,const Manifest& manifest,const Options& options){
    Directories held;held.hold(directory);QSet<QString> expected;for(const auto& file:manifest.files)expected.insert(file.path.toCaseFolded());
    for(const auto& entry:fs::recursive_directory_iterator(directory)){cancelled(options);auto relative=QString::fromStdWString(fs::relative(entry.path(),directory).generic_wstring());DWORD attributes=GetFileAttributesW(entry.path().c_str());win(attributes!=INVALID_FILE_ATTRIBUTES,"Inspect staged payload");require(!(attributes&FILE_ATTRIBUTE_REPARSE_POINT),"Version directory contains a reparse point");if(attributes&FILE_ATTRIBUTE_DIRECTORY){held.hold(entry.path());continue;}if(relative=="update-receipt.json")continue;require(expected.remove(relative.toCaseFolded()),"Version directory contains an unlisted payload");}
    require(expected.isEmpty(),"Version directory is missing signed payloads");
    for(const auto& item:manifest.files){auto file=readFile(directory/fs::path(item.path.toStdWString()));Hash hash;std::uint64_t size=0;std::array<char,65536> buffer{};for(;;){cancelled(options);DWORD got=0;win(ReadFile(file.value,buffer.data(),DWORD(buffer.size()),&got,nullptr)!=0,"Verify staged payload");if(!got)break;size+=got;require(size<=item.size,"Installed payload exceeds signed size");hash.add(buffer.data(),got);}require(size==item.size&&hash.finish()==item.sha256,"Installed payload signature/hash verification failed");}
}
int process(const fs::path& executable,const QStringList& arguments,bool wait,unsigned timeout,const Options* options=nullptr){
    QString command=quoteWindowsArgument(qpath(executable));for(const auto& arg:arguments)command+=' '+quoteWindowsArgument(arg);auto wide=command.toStdWString();require(wide.size()<32767,"Application command line exceeds Windows limit");STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION information{};auto directory=executable.parent_path();win(CreateProcessW(executable.c_str(),wide.data(),nullptr,nullptr,FALSE,wait?CREATE_NO_WINDOW:0,nullptr,directory.c_str(),&startup,&information)!=0,"Launch installed application");Handle thread(information.hThread),child(information.hProcess);if(!wait)return 0;auto start=GetTickCount64();for(;;){DWORD status=WaitForSingleObject(child.value,50);if(status==WAIT_OBJECT_0)break;win(status==WAIT_TIMEOUT,"Wait for update health check");bool stop=options&&options->cancelled&&options->cancelled();if(stop||(timeout!=INFINITE&&GetTickCount64()-start>=timeout)){TerminateProcess(child.value,124);WaitForSingleObject(child.value,5000);if(stop)fail("Update cancelled during health check");return 124;}}DWORD code=0;win(GetExitCodeProcess(child.value,&code)!=0,"Read update health result");return int(code);
}
bool removeVerifiedFile(const fs::path& path,std::uint64_t expectedSize,const QByteArray& expectedHash){
    Handle file(CreateFileW(path.c_str(),GENERIC_READ|DELETE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN,nullptr));if(file.value==INVALID_HANDLE_VALUE)return false;BY_HANDLE_FILE_INFORMATION info{};if(!GetFileInformationByHandle(file.value,&info)||(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)))return false;
    Hash hash;std::uint64_t size=0;std::array<char,65536> buffer{};for(;;){DWORD n=0;if(!ReadFile(file.value,buffer.data(),DWORD(buffer.size()),&n,nullptr))return false;if(!n)break;size+=n;if(size>expectedSize)return false;hash.add(buffer.data(),n);}if(size!=expectedSize||hash.finish()!=expectedHash)return false;FILE_DISPOSITION_INFO disposition{TRUE};return SetFileInformationByHandle(file.value,FileDispositionInfo,&disposition,sizeof(disposition))!=0;
}
ActiveState recoverLocked(Root& root){auto s=readState(root.path);if(s.pending){auto previous=Version::parse(s.previous);Directories held;auto directory=versionDirectory(root.path,previous);held.hold(directory);auto exe=readFile(directory/L"Compositor.exe");s.current=previous;s.previous.clear();s.pending=false;atomicJson(root.path/L"state"/L"active.json",stateJson(s));}return s;}
}

Version Version::parse(const QString& text){static const QRegularExpression pattern("^(0|[1-9][0-9]{0,4})\\.(0|[1-9][0-9]{0,4})\\.(0|[1-9][0-9]{0,4})$");auto m=pattern.match(text);require(m.hasMatch(),"Version must be strict major.minor.patch");Version v{m.captured(1).toUInt(),m.captured(2).toUInt(),m.captured(3).toUInt()};require(v.major<=65535&&v.minor<=65535&&v.patch<=65535,"Version component exceeds limit");return v;}
QString Version::text()const{return QString::number(major)+'.'+QString::number(minor)+'.'+QString::number(patch);}
QByteArray sha256(const QByteArray& bytes){Hash hash;hash.add(bytes.data(),DWORD(bytes.size()));return hash.finish();}
Manifest verifyManifest(const QByteArray& bytes,const Options& options){
    require(options.allowTestKey,"Development update signing key requires explicit opt-in");require(bytes.size()>0&&std::uint64_t(bytes.size())<=metadataLimit,"Invalid update envelope size");auto outer=json(bytes);keys(outer,{"keyId","payload","signature"});require(string(outer,"keyId")==testKeyId,"Untrusted update signing key");auto payload=base64(string(outer,"payload")),signature=base64(string(outer,"signature"));require(signature.size()==384,"Invalid RSA3072 update signature size");
    QByteArray modulus=QByteArray::fromHex(testModulusHex),blob;BCRYPT_RSAKEY_BLOB header{BCRYPT_RSAPUBLIC_MAGIC,3072,3,384,0,0};blob.append(reinterpret_cast<const char*>(&header),sizeof(header));blob.append("\x01\x00\x01",3);blob.append(modulus);BCRYPT_ALG_HANDLE algorithm{};BCRYPT_KEY_HANDLE key{};require(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_RSA_ALGORITHM,nullptr,0)>=0,"RSA provider unavailable");auto status=BCryptImportKeyPair(algorithm,nullptr,BCRYPT_RSAPUBLIC_BLOB,&key,reinterpret_cast<PUCHAR>(blob.data()),ULONG(blob.size()),0);if(status<0){BCryptCloseAlgorithmProvider(algorithm,0);fail("RSA public key import failed");}auto hash=sha256(payload);BCRYPT_PKCS1_PADDING_INFO padding{BCRYPT_SHA256_ALGORITHM};status=BCryptVerifySignature(key,&padding,reinterpret_cast<PUCHAR>(hash.data()),ULONG(hash.size()),reinterpret_cast<PUCHAR>(signature.data()),ULONG(signature.size()),BCRYPT_PAD_PKCS1);BCryptDestroyKey(key);BCryptCloseAlgorithmProvider(algorithm,0);require(status>=0,"Update metadata signature rejected");
    auto object=json(payload);keys(object,{"schema","product","channel","version","files"});require(integer(object["schema"],1)==1&&string(object,"product")==product,"Update product/schema mismatch");Manifest manifest;manifest.version=Version::parse(string(object,"version"));manifest.channel=string(object,"channel");require(manifest.channel=="test","Update channel mismatch");require(object["files"].isArray(),"Update files must be an array");auto files=object["files"].toArray();require(!files.empty()&&files.size()<=8192,"Update file count exceeds limit");QSet<QString> paths;std::uint64_t total=0;bool application=false;
    for(const auto& value:files){require(value.isObject(),"Invalid payload entry");auto entry=value.toObject();keys(entry,{"path","size","sha256"});Payload item;item.path=string(entry,"path");relativePath(item.path);require(!paths.contains(item.path.toCaseFolded()),"Duplicate payload path");paths.insert(item.path.toCaseFolded());item.size=integer(entry["size"],options.maxPayloadBytes);require(item.size<=options.maxPayloadBytes-total,"Update payload exceeds total size budget");total+=item.size;auto hex=string(entry,"sha256");require(QRegularExpression("^[0-9a-f]{64}$").match(hex).hasMatch(),"Invalid payload SHA256");item.sha256=QByteArray::fromHex(hex.toLatin1());application|=item.path=="Compositor.exe";manifest.files.push_back(std::move(item));}
    require(application,"Update has no Compositor.exe");manifest.signedBytes=std::move(payload);manifest.signature=std::move(signature);return manifest;
}
Updater::Updater(fs::path root):root_(fs::absolute(std::move(root)).lexically_normal()){Root checked(root_);}
ActiveState Updater::state()const{Root root(root_);return readState(root.path);}
CheckResult Updater::check(const QString& testFeed,const Options& options)const{require(options.allowTestKey,"Development update signing key requires explicit opt-in");Root root(root_);auto active=readState(root.path);require(!active.pending,"Recover interrupted activation before checking updates");auto manifest=verifyManifest(Feed(testFeed).metadata(options),options);require(manifest.channel==active.channel,"Update channel differs from installation");require(manifest.version>=active.current,"Update downgrade rejected");bool available=manifest.version>active.current;return {std::move(manifest),available};}
InstallResult Updater::install(const QString& testFeed,const Options& options){
    require(options.allowTestKey,"Development update signing key requires explicit opt-in");Root root(root_);auto lock=root.lock();auto active=recoverLocked(root);Feed feed(testFeed);auto manifest=verifyManifest(feed.metadata(options),options);require(manifest.channel==active.channel&&manifest.version>=active.current,"Update channel mismatch or downgrade rejected");if(manifest.version==active.current)return {active.current.text(),false,false};cancelled(options);
    const auto final=versionDirectory(root.path,manifest.version);bool pending=false;auto before=active;try{
        if(!fs::exists(final)){
            auto staging=root.path/L"versions"/fs::path((QStringLiteral("stage-")+QUuid::createUuid().toString(QUuid::WithoutBraces)).toStdWString());
            {
                Directories directories;directories.ensure(staging);std::uint64_t completed=0,total=0;for(const auto& file:manifest.files)total+=file.size;
                for(const auto& item:manifest.files){cancelled(options);auto relative=fs::path(item.path.toStdWString());auto cursor=staging;for(const auto& part:relative.parent_path()){cursor/=part;directories.ensure(cursor);}auto target=staging/relative;Handle file(CreateFileW(target.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));win(file.value!=INVALID_HANDLE_VALUE,"Create staged payload");Hash hash;std::uint64_t size=0;feed.read("payload/"+item.path,item.size,options,[&](const char* p,DWORD n){DWORD done=0;win(WriteFile(file.value,p,n,&done,nullptr)!=0&&done==n,"Write staged payload");hash.add(p,n);size+=n;completed+=n;if(options.progress)options.progress(completed,total);});require(size==item.size&&hash.finish()==item.sha256,"Downloaded payload hash/signature rejected");win(FlushFileBuffers(file.value)!=0,"Flush staged payload");}
                writeAll(staging/L"update-receipt.json",envelope(manifest));
            }
            verifyInstalled(staging,manifest,options);cancelled(options);win(MoveFileExW(staging.c_str(),final.c_str(),MOVEFILE_WRITE_THROUGH)!=0,"Publish verified version directory");
        }else{auto receipt=verifyManifest(readSmall(final/L"update-receipt.json"),options);require(receipt.signedBytes==manifest.signedBytes,"Existing version differs from signed release");verifyInstalled(final,manifest,options);}
        if(options.fault==Options::Fault::AfterVersionRename)fail("Injected install failure after version rename");
        active.previous=before.current.text();active.current=manifest.version;active.pending=true;atomicJson(root.path/L"state"/L"active.json",stateJson(active));pending=true;
        if(options.fault==Options::Fault::CrashAfterPendingActivation)ExitProcess(86);
        if(options.fault==Options::Fault::AfterPendingActivation)fail("Injected install failure after activation");cancelled(options);require(process(final/L"Compositor.exe",{"--update-health-check"},true,options.healthTimeoutMs,&options)==0,"Installed application health check failed");
        if(options.fault==Options::Fault::BeforeFinalize)fail("Injected install failure before finalization");cancelled(options);active.pending=false;atomicJson(root.path/L"state"/L"active.json",stateJson(active));return {active.current.text(),true,false};
    }catch(...){if(pending){try{atomicJson(root.path/L"state"/L"active.json",stateJson(before));}catch(...){fail("Update failed and automatic rollback could not complete; launcher recovery is required");}}throw;}
}
ActiveState Updater::recover(){Root root(root_);auto lock=root.lock();return recoverLocked(root);}
UninstallResult Updater::uninstallPayloads(const Options& options){
    require(options.allowTestKey,"Development update signing key requires explicit opt-in");Root root(root_);auto lock=root.lock();UninstallResult result;
    for(const auto& entry:fs::directory_iterator(root.path/L"versions")){
        cancelled(options);auto directory=entry.path();try{
            auto version=Version::parse(qpath(directory.filename()));QByteArray receiptBytes;Manifest manifest;std::vector<fs::path> folders{directory};bool complete=true;
            {
                Directories held;held.hold(directory);receiptBytes=readSmall(directory/L"update-receipt.json");manifest=verifyManifest(receiptBytes,options);require(manifest.version==version,"Receipt version mismatch");
                // Validate and lock every parent before deleting any manifest file.
                for(const auto& item:manifest.files){auto cursor=directory;for(const auto& part:fs::path(item.path.toStdWString()).parent_path()){cursor/=part;held.hold(cursor);folders.push_back(cursor);}}
                for(const auto& item:manifest.files){cancelled(options);bool project=false;for(const auto& component:item.path.split('/'))project|=component.endsWith(".comp",Qt::CaseInsensitive);auto path=directory/fs::path(item.path.toStdWString());if(!project&&removeVerifiedFile(path,item.size,item.sha256))++result.removedFiles;else{complete=false;result.retainedPaths.push_back(qpath(path));}}
                if(complete&&removeVerifiedFile(directory/L"update-receipt.json",std::uint64_t(receiptBytes.size()),sha256(receiptBytes)))++result.removedFiles;
            }
            std::sort(folders.begin(),folders.end(),[](const auto& a,const auto& b){return a.native().size()>b.native().size();});folders.erase(std::unique(folders.begin(),folders.end()),folders.end());
            // RemoveDirectory removes only an empty directory and never recurses.
            for(const auto& folder:folders)RemoveDirectoryW(folder.c_str());
            if(fs::exists(directory))result.retainedPaths.push_back(qpath(directory));
        }catch(const std::exception&){result.retainedPaths.push_back(qpath(directory));}
    }return result;
}
QString quoteWindowsArgument(const QString& argument){require(!argument.contains(QChar(0)),"Application argument contains a NUL");QString out="\"";int slashes=0;for(auto c:argument){if(c=='\\'){++slashes;continue;}if(c=='\"'){out+=QString(slashes*2+1,'\\');out+=c;}else{out+=QString(slashes,'\\');out+=c;}slashes=0;}out+=QString(slashes*2,'\\');out+='\"';return out;}
int launchCurrent(const fs::path& rootPath,const QStringList& arguments,bool waitForExit){Root root(rootPath);auto lock=root.lock();auto state=recoverLocked(root);auto directory=versionDirectory(root.path,state.current);Directories held;held.hold(directory);auto executable=readFile(directory/L"Compositor.exe");return process(directory/L"Compositor.exe",arguments,waitForExit,INFINITE);}
}


