#include "ProjectStore.h"
#include <QByteArray>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QString>
#include <QUuid>
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <exception>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace compositor {
namespace {
namespace fs=std::filesystem;
constexpr qint64 manifestLimit=4*1024*1024;
constexpr uintmax_t assetLimit=512ull*1024*1024;
[[noreturn]] void fail(const char* message){throw std::runtime_error(message);}
QString qs(const fs::path& p){return QString::fromStdWString(p.wstring());}
QString qstr(const std::string& s){return QString::fromUtf8(s.data(),qsizetype(s.size()));}
bool optional(const QJsonObject& o,const char* k){return !o.contains(k)||o.value(k).isNull();}
QJsonObject object(const QJsonValue& v){if(!v.isObject())fail("Required object missing or malformed");return v.toObject();}
QJsonArray array(const QJsonValue& v){if(!v.isArray())fail("Required array missing or malformed");return v.toArray();}
QString string(const QJsonValue& v){if(!v.isString())fail("Required string missing or malformed");return v.toString();}
bool boolean(const QJsonValue& v){if(!v.isBool())fail("Required Boolean missing or malformed");return v.toBool();}
double number(const QJsonValue& v){if(!v.isDouble()||!std::isfinite(v.toDouble()))fail("Required finite number missing or malformed");return v.toDouble();}
double bounded(const QJsonValue& v,double lo,double hi){auto n=number(v);if(n<lo||n>hi)fail("Numeric setting outside supported range");return n;}
int integer(const QJsonValue& v,int lo,int hi){auto n=bounded(v,lo,hi);if(std::trunc(n)!=n)fail("Integer expected");return int(n);}
QString uuid(const QJsonValue& value){auto s=string(value);QUuid id(s);if(s.size()!=36||s[8]!='-'||s[13]!='-'||s[18]!='-'||s[23]!='-')fail("Invalid UUID");auto normalized=id.toString(QUuid::WithoutBraces).toUpper();if(normalized.compare(s,Qt::CaseInsensitive)!=0)fail("Invalid UUID");return normalized;}
std::string optionalId(const QJsonObject&o,const char*k){return optional(o,k)?std::string{}:uuid(o[k]).toStdString();}
bool optionalBool(const QJsonObject&o,const char*k,bool fallback){return optional(o,k)?fallback:boolean(o[k]);}
double optionalNumber(const QJsonObject&o,const char*k,double fallback){return optional(o,k)?fallback:number(o[k]);}
void enumValue(const QJsonValue&v,std::initializer_list<const char*> values){auto s=string(v);if(std::none_of(values.begin(),values.end(),[&](const char*p){return s==p;}))fail("Unknown enum value");}
Transform transform(const QJsonValue&value){auto o=object(value);auto origin=array(o["origin"]),size=array(o["size"]);if(origin.size()!=2||size.size()!=2)fail("Foundation geometry requires two-number arrays");Transform t;
    t.x=number(origin[0]);t.y=number(origin[1]);t.width=number(size[0]);t.height=number(size[1]);t.rotation=number(o["rotation"]);t.flipX=boolean(o["flipX"]);t.flipY=boolean(o["flipY"]);
    auto sampling=string(o["sampling"]);if(sampling=="Nearest")t.sampling=Transform::Sampling::Nearest;else if(sampling=="Smooth")t.sampling=Transform::Sampling::Smooth;else if(sampling=="High quality")t.sampling=Transform::Sampling::High;else fail("Unknown sampling value");if(!t.valid())fail("Invalid layer transform");return t;}
QJsonObject encodeTransform(const Transform&t){return {{"origin",QJsonArray{t.x,t.y}},{"size",QJsonArray{t.width,t.height}},{"rotation",t.rotation},{"flipX",t.flipX},{"flipY",t.flipY},{"sampling",t.sampling==Transform::Sampling::Nearest?"Nearest":t.sampling==Transform::Sampling::Smooth?"Smooth":"High quality"}};}
void channel(const QJsonValue& v){enumValue(v,{"RGB","Red","Green","Blue"});}
void rangeName(const QJsonValue&v){enumValue(v,{"Master","Reds","Yellows","Greens","Cyans","Blues","Magentas"});}
void hsvTriplet(const QJsonObject&o){bounded(o["hue"],-360,360);bounded(o["saturation"],-100,100);bounded(o["lightness"],-100,100);}
void color(const QJsonValue&v){auto o=object(v);for(const char*k:{"red","green","blue"})bounded(o[k],0,1);}
void validateAdjustment(const QJsonObject&o){
    enumValue(o["kind"],{"Hue/Saturation","Levels","Curves","Exposure","Gradient Map","Grain"});hsvTriplet(o);boolean(o["colorize"]);
    auto levels=object(o["levels"]);channel(levels["channel"]);auto ranges=array(levels["ranges"]);if(ranges.size()!=4)fail("Levels needs four ranges");
    for(auto v:ranges){auto r=object(v);auto black=bounded(r["black"],0,254);bounded(r["white"],black+1,255);bounded(r["gamma"],.1,9.99);bounded(r["outputBlack"],0,255);bounded(r["outputWhite"],0,255);}
    auto curves=object(o["curves"]);channel(curves["channel"]);auto channels=array(curves["channels"]);if(channels.size()!=4)fail("Curves needs four channels");
    for(auto v:channels){auto points=array(v);if(points.size()<2||points.size()>32)fail("Curve point count invalid");double last=-1;for(qsizetype i=0;i<points.size();++i){auto p=object(points[i]);auto x=bounded(p["x"],0,255);bounded(p["y"],0,255);if(x<=last||(i==0&&x!=0)||(i==points.size()-1&&x!=255))fail("Curve abscissae invalid");last=x;}}
    if(!optional(o,"hsvSettings")){auto h=object(o["hsvSettings"]);rangeName(h["range"]);boolean(h["colorize"]);boolean(h["invertRange"]);
        for(const char*k:{"adjustments","bands"}){auto pairs=array(h[k]);if(pairs.size()%2)fail("Enum dictionary must contain key/value pairs");for(qsizetype i=0;i<pairs.size();i+=2){rangeName(pairs[i]);auto v=object(pairs[i+1]);if(std::string(k)=="adjustments")hsvTriplet(v);else for(const char*field:{"falloffStart","rangeStart","rangeEnd","falloffEnd"})number(v[field]);}}}
    if(!optional(o,"exposureSettings")){auto e=object(o["exposureSettings"]);bounded(e["exposure"],-20,20);bounded(e["offset"],-.5,.5);bounded(e["gamma"],.01,9.99);}
    if(!optional(o,"gradientMapSettings")){auto g=object(o["gradientMapSettings"]);color(g["shadows"]);color(g["highlights"]);boolean(g["reversed"]);}
    if(!optional(o,"grainSettings")){auto g=object(o["grainSettings"]);bounded(g["amount"],0,100);bounded(g["size"],.5,20);bounded(g["roughness"],0,100);auto seed=bounded(g["seed"],0,4294967295.);if(std::trunc(seed)!=seed)fail("Grain seed must be UInt32");}
}
void validateShape(const QJsonObject&o){enumValue(o["kind"],{"Rectangle","Ellipse"});for(const char*k:{"red","green","blue","cornerRadius"})number(o[k]);}
QJsonObject parseJson(const QByteArray&bytes){QJsonParseError error;auto json=QJsonDocument::fromJson(bytes,&error);if(error.error!=QJsonParseError::NoError||!json.isObject())fail("Invalid JSON object");return json.object();}
std::string jsonString(const QJsonObject&o){return QJsonDocument(o).toJson(QJsonDocument::Compact).toStdString();}
void noReparse(const fs::path&p){fs::path current;for(auto&part:fs::absolute(p).lexically_normal()){current/=part;if(current==current.root_name())continue;auto attributes=GetFileAttributesW(current.c_str());if(attributes==INVALID_FILE_ATTRIBUTES){auto error=GetLastError();if(error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND)continue;fail("Could not inspect project path");}if(attributes&FILE_ATTRIBUTE_REPARSE_POINT)fail("Project paths may not cross a reparse point");}}
void safeTree(const fs::path&p){noReparse(p);if(!fs::exists(p))return;if(!fs::is_directory(p))fail("Expected package directory");for(auto&e:fs::recursive_directory_iterator(p)){noReparse(e.path());if(!e.is_regular_file()&&!e.is_directory())fail("Unsupported package entry");}}
void removeTree(const fs::path&p){safeTree(p);if(fs::exists(p))fs::remove_all(p);}
QByteArray readFile(const fs::path&p,qint64 limit){noReparse(p);if(!fs::is_regular_file(p)||fs::file_size(p)>uintmax_t(limit))fail("Project file absent or oversized");QFile file(qs(p));if(!file.open(QIODevice::ReadOnly))fail("Cannot read project file");auto data=file.read(limit+1);if(data.size()>limit||file.error()!=QFileDevice::NoError)fail("Project file read failed");return data;}
void writeJson(const fs::path&p,const QJsonObject&o,qint64 limit=manifestLimit){auto bytes=QJsonDocument(o).toJson(QJsonDocument::Indented);if(bytes.size()>limit)fail("Project metadata exceeds size limit");noReparse(p);QSaveFile file(qs(p));file.setDirectWriteFallback(false);if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit())fail("Cannot commit project metadata");}
fs::path normalize(const fs::path&p){auto path=fs::absolute(p).lexically_normal();if(path.filename().empty()||path==path.root_path())fail("Invalid project destination");noReparse(path);if(!fs::is_directory(path.parent_path()))fail("Project parent directory does not exist");return path;}
fs::path suffix(const fs::path&p,const std::wstring&s){return p.parent_path()/(p.filename().wstring()+s);}
// A named mutex coordinates readers/writers without requiring write access merely to open
// a read-only package. Abandoned ownership is recoverable through the on-disk journal.
struct Lock {HANDLE handle{};explicit Lock(const fs::path&p){auto normalized=qs(p).toCaseFolded().toUtf8();uint64_t hash=14695981039346656037ull;for(char c:normalized){hash^=uint8_t(c);hash*=1099511628211ull;}auto name=L"Local\\CompositorProject-"+std::to_wstring(hash);handle=CreateMutexW(nullptr,FALSE,name.c_str());if(!handle)fail("Cannot create project coordination mutex");auto result=WaitForSingleObject(handle,0);if(result!=WAIT_OBJECT_0&&result!=WAIT_ABANDONED){CloseHandle(handle);handle=nullptr;fail("Project is busy in another process");}}~Lock(){if(handle){ReleaseMutex(handle);CloseHandle(handle);}}Lock(const Lock&)=delete;};
void dimension(int width,int height,uint64_t&used){if(width<1||height<1||width>30000||height>30000||uint64_t(width)*height>100000000-used)fail("Project image pixel budget exceeded");used+=uint64_t(width)*height;}
std::pair<int,int> pngDimensions(const fs::path&p,bool gray){QFile file(qs(p));if(!file.open(QIODevice::ReadOnly))fail("Cannot read embedded PNG header");auto h=file.read(33);const unsigned char signature[8]{137,80,78,71,13,10,26,10};if(h.size()!=33||!std::equal(std::begin(signature),std::end(signature),reinterpret_cast<const unsigned char*>(h.constData()))||h.mid(8,8)!=QByteArray("\0\0\0\rIHDR",8))fail("Embedded asset is not a PNG with IHDR");auto u=[&](int offset){auto*b=reinterpret_cast<const unsigned char*>(h.constData()+offset);return (uint32_t(b[0])<<24)|(uint32_t(b[1])<<16)|(uint32_t(b[2])<<8)|b[3];};auto w=u(16),height=u(20);if(!w||!height||w>30000||height>30000||uint8_t(h[24])>8||(gray&&(h[24]!=8||h[25]!=0)))fail("Unsupported embedded PNG dimensions or sample format");return {int(w),int(height)};}
void graph(const Document&d){std::unordered_map<std::string,const Layer*> by;for(auto&l:d.layers){if(!by.emplace(l.id,&l).second)fail("Duplicate layer UUID");if(l.group&&(l.raster||!l.adjustmentJson.empty()||l.opacity!=1||l.blend!=Blend::Normal))fail("Invalid folder properties");if(!l.adjustmentJson.empty()&&l.raster)fail("Adjustment has raster pixels");}
    for(auto&l:d.layers){std::unordered_set<std::string> seen{l.id};auto parent=l.parentId;while(!parent.empty()){if(seen.size()>64||!seen.insert(parent).second||!by.contains(parent)||!by.at(parent)->group)fail("Invalid folder hierarchy");parent=by.at(parent)->parentId;}if(l.group&&seen.size()>64)fail("Folder nesting exceeds limit");
        seen.clear();auto current=l.id;while(!current.empty()){if(seen.size()>=256||!seen.insert(current).second||!by.contains(current))fail("Invalid live-mask chain");auto&record=*by.at(current);if(!record.maskSourceId.empty()){if(record.group||!by.contains(record.maskSourceId)||by.at(record.maskSourceId)->group||!by.at(record.maskSourceId)->adjustmentJson.empty())fail("Invalid live-mask endpoint");}current=record.maskSourceId;}}
}
struct Manifest {OpenProject project;std::vector<std::string> images,masks;};
Manifest decodeManifest(const QJsonObject&o){Manifest m;if(string(o["format"])!="com.compositor.project")fail("Invalid project format");int version=integer(o["version"],1,7);if(string(o["colorSpace"])!="sRGB")fail("Unsupported project color space");auto&d=m.project.document;m.project.readVersion=version;d.id=uuid(o["documentID"]).toStdString();d.width=integer(o["width"],1,30000);d.height=integer(o["height"],1,30000);d.resolution=optionalNumber(o,"resolution",72);if(d.resolution<1||d.resolution>9600)fail("Invalid document resolution");m.project.activeLayer=optionalId(o,"activeLayerID");auto layers=array(o["layers"]);if(layers.size()>10000)fail("Layer count exceeds limit");
    for(auto value:layers){auto r=object(value);Layer l;l.id=uuid(r["id"]).toStdString();l.name=string(r["name"]).toUtf8().toStdString();if(qstr(l.name).trimmed().isEmpty()||l.name.size()>16384)fail("Invalid layer name");l.visible=boolean(r["isVisible"]);l.transform=transform(r["transform"]);l.parentId=optionalId(r,"parentID");l.group=optionalBool(r,"isGroup",false);l.opacity=optionalNumber(r,"opacity",1);if(l.opacity<0||l.opacity>1)fail("Invalid layer opacity");if(!optional(r,"blendMode")){auto blend=string(r["blendMode"]);auto it=std::find_if(blendNames.begin(),blendNames.end(),[&](const char*n){return blend==n;});if(it==blendNames.end())fail("Unknown blend mode");l.blend=Blend(it-blendNames.begin());}
        if(version<3&&(l.opacity!=1||l.blend!=Blend::Normal))fail("Appearance requires format v3");if(version==1&&(!l.parentId.empty()||l.group))fail("Groups require format v2");
        auto image=optional(r,"imageFile")?std::string{}:string(r["imageFile"]).toStdString();if(!image.empty()&&image!=l.id+".png")fail("Invalid image filename");if(!optional(r,"imageFile")&&image.empty())fail("Invalid image filename");if(l.group&&!image.empty())fail("Folder cannot carry image pixels");
        auto mask=optional(r,"maskFile")?std::string{}:string(r["maskFile"]).toStdString();if(!optional(r,"maskFile")&&(mask!=l.id+".mask.png"||version<(l.group?6:4)))fail("Invalid mask filename or version");if(!optional(r,"maskEnabled")&&mask.empty())fail("Mask enabled flag without mask");
        bool linked=optionalBool(r,"maskLinked",true);bool enabled=optionalBool(r,"maskEnabled",true);std::optional<Transform> placement;if(!optional(r,"maskPlacement")){placement=transform(r["maskPlacement"]);if(mask.empty())fail("Mask placement without mask");}if(!mask.empty())l.mask=Mask{nullptr,enabled,linked,placement};
        l.maskSourceId=optionalId(r,"maskSourceID");if(version<5&&!l.maskSourceId.empty())fail("Live masks require format v5");if(!optional(r,"adjustment")){auto a=object(r["adjustment"]);validateAdjustment(a);if(version<7||l.group||!image.empty())fail("Invalid adjustment layer");l.adjustmentJson=jsonString(a);}if(!optional(r,"shape")){auto s=object(r["shape"]);validateShape(s);if(!image.empty())l.shapeJson=jsonString(s);}
        m.images.push_back(image);m.masks.push_back(mask);d.layers.push_back(std::move(l));}
    graph(d);if(!m.project.activeLayer.empty()&&std::none_of(d.layers.begin(),d.layers.end(),[&](auto&l){return l.id==m.project.activeLayer;}))fail("Active layer does not exist");return m;
}
QJsonObject encodeManifest(const Document&d,const std::string&active){QJsonArray layers;for(auto&l:d.layers){if(!l.transform.valid())fail("Invalid transform");QJsonObject r{{"id",qstr(l.id)},{"name",qstr(l.name)},{"isVisible",l.visible},{"transform",encodeTransform(l.transform)},{"isGroup",l.group},{"opacity",l.opacity},{"blendMode",blendNames.at(size_t(l.blend))}};if(l.raster)r["imageFile"]=qstr(l.id+".png");if(!l.parentId.empty())r["parentID"]=qstr(l.parentId);if(!l.maskSourceId.empty())r["maskSourceID"]=qstr(l.maskSourceId);if(l.mask){if(!l.mask->raster)fail("Missing mask raster");r["maskFile"]=qstr(l.id+".mask.png");r["maskEnabled"]=l.mask->enabled;r["maskLinked"]=l.mask->linked;if(l.mask->placement)r["maskPlacement"]=encodeTransform(*l.mask->placement);}if(!l.adjustmentJson.empty())r["adjustment"]=parseJson(QByteArray::fromStdString(l.adjustmentJson));if(!l.shapeJson.empty()&&l.raster)r["shape"]=parseJson(QByteArray::fromStdString(l.shapeJson));layers.append(r);}
    QJsonObject result{{"format","com.compositor.project"},{"version",7},{"colorSpace","sRGB"},{"resolution",d.resolution},{"documentID",qstr(d.id)},{"width",d.width},{"height",d.height},{"layers",layers}};if(!active.empty())result["activeLayerID"]=qstr(active);decodeManifest(result);graph(d);return result;}
fs::path marker(const fs::path&p){return p/L".compositor-transaction";}
bool owns(const fs::path&p,const std::string&tx){return fs::exists(marker(p))&&readFile(marker(p),128).toStdString()==tx;}
struct Journal {std::string tx,phase;bool original{};fs::path stage,backup;};
Journal readJournal(const fs::path&p){auto o=parseJson(readFile(suffix(p,L".compositor-save.json"),65536));Journal j;j.tx=uuid(o["transaction"]).toStdString();j.phase=string(o["phase"]).toStdString();enumValue(o["phase"],{"prepared","backed_up","installed"});j.original=boolean(o["hadOriginal"]);if(string(o["target"])!=qs(p.filename()))fail("Journal target does not match");j.stage=suffix(p,L".stage-"+qstr(j.tx).toStdWString());j.backup=suffix(p,L".backup-"+qstr(j.tx).toStdWString());if(string(o["stage"])!=qs(j.stage.filename())||string(o["backup"])!=qs(j.backup.filename()))fail("Unsafe journal paths");safeTree(j.stage);safeTree(j.backup);return j;}
void writeJournal(const fs::path&p,const Journal&j){writeJson(suffix(p,L".compositor-save.json"),{{"transaction",qstr(j.tx)},{"phase",qstr(j.phase)},{"hadOriginal",j.original},{"target",qs(p.filename())},{"stage",qs(j.stage.filename())},{"backup",qs(j.backup.filename())}},65536);}
void finishJournal(const fs::path&p,const std::string&tx){noReparse(marker(p));if(owns(p,tx))fs::remove(marker(p));fs::remove(suffix(p,L".compositor-save.json"));}
}
ProjectStore::ProjectStore(ProjectAssetCodec codec,SaveFaultInjector fault):codec_(std::move(codec)),fault_(std::move(fault)){if(!codec_.readColor||!codec_.readGray||!codec_.writeColor||!codec_.writeGray)fail("Project codec callbacks are required");}
OpenProject ProjectStore::loadPackage(const fs::path&p)const{safeTree(p);if(!fs::is_directory(p))fail("Project is not a directory package");auto parsed=decodeManifest(parseJson(readFile(p/L"manifest.json",manifestLimit)));uint64_t images=0,masks=0;
    // Inspect all headers and cumulative budgets before any decoder allocates image pixels.
    for(size_t i=0;i<parsed.project.document.layers.size();++i)for(bool gray:{false,true}){auto name=gray?parsed.masks[i]:parsed.images[i];if(name.empty())continue;auto path=p/L"images"/fs::path(qstr(name).toStdWString());noReparse(path);if(!fs::is_regular_file(path)||fs::file_size(path)>assetLimit)fail("Embedded image is absent or oversized");auto[w,h]=pngDimensions(path,gray);dimension(w,h,gray?masks:images);}
    images=0;masks=0;for(size_t i=0;i<parsed.project.document.layers.size();++i){auto&l=parsed.project.document.layers[i];for(bool gray:{false,true}){auto name=gray?parsed.masks[i]:parsed.images[i];if(name.empty())continue;auto path=p/L"images"/fs::path(qstr(name).toStdWString());if(gray){auto raster=codec_.readGray(path);if(!raster)fail("Mask decode failed");dimension(raster->width,raster->height,masks);if(raster->pixels.size()!=size_t(raster->width)*raster->height)fail("Mask stride or data invalid");l.mask->raster=std::move(raster);}else{auto raster=codec_.readColor(path);if(!raster)fail("Image decode failed");dimension(raster->width,raster->height,images);l.raster=std::move(raster);}}}graph(parsed.project.document);return std::move(parsed.project);}
void ProjectStore::recoverLocked(const fs::path&p){auto file=suffix(p,L".compositor-save.json");if(!fs::exists(file))return;auto j=readJournal(p);safeTree(p);if(fs::exists(j.stage)&&!owns(j.stage,j.tx))fail("Recovery stage ownership is unknown; preserving packages");if(fs::exists(j.backup)){if(fs::exists(p)){if(!owns(p,j.tx))fail("Recovery found an unrelated target; preserving all packages");try{loadPackage(p);}catch(...){if(fs::exists(j.stage))fail("Recovery stage collision; preserving all packages");fs::rename(p,j.stage);fs::rename(j.backup,p);removeTree(j.stage);finishJournal(p,j.tx);return;}removeTree(j.backup);}else{fs::rename(j.backup,p);}removeTree(j.stage);finishJournal(p,j.tx);return;}
    if(fs::exists(p)){if(owns(p,j.tx)){loadPackage(p);}else if(!j.original)fail("Recovery target ownership is unknown");removeTree(j.stage);finishJournal(p,j.tx);return;}
    if(j.original)fail("Original package is missing during recovery; preserving stage and journal");removeTree(j.stage);fs::remove(file);
}
void ProjectStore::recover(const fs::path&path){auto p=normalize(path);Lock lock(p);recoverLocked(p);}
OpenProject ProjectStore::load(const fs::path&path){auto p=normalize(path);Lock lock(p);recoverLocked(p);return loadPackage(p);}
void ProjectStore::save(const fs::path&path,const Document&document,const std::string&active){auto p=normalize(path);Lock lock(p);recoverLocked(p);auto manifest=encodeManifest(document,active);Journal j;j.tx=newId();j.phase="prepared";j.original=fs::exists(p);if(j.original)safeTree(p);j.stage=suffix(p,L".stage-"+qstr(j.tx).toStdWString());j.backup=suffix(p,L".backup-"+qstr(j.tx).toStdWString());if(fs::exists(j.stage)||fs::exists(j.backup))fail("Save transaction name collision");
    auto inject=[&](SaveFaultPoint point){if(fault_)fault_(point);};
    try{fs::create_directory(j.stage);fs::create_directory(j.stage/L"images");QFile mark(qs(marker(j.stage)));if(!mark.open(QIODevice::WriteOnly|QIODevice::NewOnly)||mark.write(QByteArray::fromStdString(j.tx))!=qsizetype(j.tx.size())||!mark.flush())fail("Cannot write save transaction marker");mark.close();uint64_t images=0,masks=0;
        for(auto&l:document.layers){if(l.raster){dimension(l.raster->width,l.raster->height,images);auto asset=j.stage/L"images"/fs::path(qstr(l.id+".png").toStdWString());codec_.writeColor(asset,*l.raster);if(!fs::is_regular_file(asset)||fs::file_size(asset)>assetLimit)fail("Encoded image missing or too large");}if(l.mask){auto&r=*l.mask->raster;dimension(r.width,r.height,masks);if(r.pixels.size()!=size_t(r.width)*r.height)fail("Invalid mask pixels");auto asset=j.stage/L"images"/fs::path(qstr(l.id+".mask.png").toStdWString());codec_.writeGray(asset,r);if(!fs::is_regular_file(asset)||fs::file_size(asset)>assetLimit)fail("Encoded mask missing or too large");}}
        writeJson(j.stage/L"manifest.json",manifest);loadPackage(j.stage);inject(SaveFaultPoint::AfterStage);writeJournal(p,j);inject(SaveFaultPoint::AfterJournal);
        if(j.original)fs::rename(p,j.backup);j.phase="backed_up";writeJournal(p,j);inject(SaveFaultPoint::AfterBackup);
        fs::rename(j.stage,p);j.phase="installed";writeJournal(p,j);inject(SaveFaultPoint::AfterInstall);inject(SaveFaultPoint::BeforeCleanup);
    }catch(...){auto error=std::current_exception();try{if(fs::exists(j.backup)){if(fs::exists(p)){if(!owns(p,j.tx))fail("Cannot roll back unrelated package");removeTree(p);}fs::rename(j.backup,p);}else if(!j.original&&fs::exists(p)&&owns(p,j.tx))removeTree(p);removeTree(j.stage);if(fs::exists(suffix(p,L".compositor-save.json")))fs::remove(suffix(p,L".compositor-save.json"));}catch(...){throw std::runtime_error("Save failed and rollback needs recovery; all remaining transaction paths are preserved");}std::rethrow_exception(error);}
    // Installation is committed. Cleanup failure leaves a recoverable journal and a valid new package.
    try{removeTree(j.backup);finishJournal(p,j.tx);}catch(...){return;}
}
}
