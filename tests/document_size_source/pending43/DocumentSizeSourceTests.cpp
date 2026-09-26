#include "core/Document.h"
#include "core/DocumentExport.h"
#include "editing/DocumentGeometry.h"
#include "editing_transform/TransformSessionState.h"
#include "imaging/wic_codec.h"
#include "persistence/ProjectStore.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <map>
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

using namespace compositor;
using Microsoft::WRL::ComPtr;
namespace {
constexpr const char* canvasSource="CompositorTests/CanvasSizeTests.swift";
constexpr const char* imageSource="CompositorTests/ImageSizeTests.swift";
constexpr const char* fixtureSource="CompositorTests/ImageImportTests.swift";
const char* currentSource=canvasSource;
QJsonArray checks;
int failures{};
struct SourceRequirementFailure:std::runtime_error {using std::runtime_error::runtime_error;};
void guard(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void checkAt(const char* source,int line,bool value,const char* kind){
    checks.append(QJsonObject{{"source_file",source},{"source_line",line},{"kind",kind},{"passed",value}});
    if(!value){++failures;std::fprintf(stderr,"FAIL %s %s line%d\n",source,kind,line);}
}
void expect(int line,bool value){checkAt(currentSource,line,value,"expect");}
void required(int line,bool value){checkAt(currentSource,line,value,"require");if(!value)throw SourceRequirementFailure("Original source requirement failed");}
void helperRequire(int line,bool value){checkAt(fixtureSource,line,value,"require");if(!value)throw SourceRequirementFailure("Original fixture requirement failed");}
std::filesystem::path path(const QString& text){return std::filesystem::path(text.toStdWString());}

struct Files {
    QTemporaryDir directory;
    Files(){guard(directory.isValid(),"Owned temporary fixture directory exists");}
    std::filesystem::path at(const char* name)const{return path(directory.filePath(QString::fromUtf8(name)));}
};

// The original helper creates an sRGB premultiplied64x32 context, paints only
// its left32 columns opaque red, writes one PNG frame and imports that file.
// WIC objects below retain each original allocation/destination precondition.
std::filesystem::path importFixture(Files& files){
    ComPtr<IWICImagingFactory> factory;
    guard(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory))),"WIC factory exists");
    std::array<uint8_t,64*32*4> bytes{};
    for(int y=0;y<32;++y)for(int x=0;x<32;++x){const auto at=size_t(y*64+x)*4;bytes[at]=bytes[at+3]=255;}
    ComPtr<IWICBitmap> context;
    const auto contextResult=factory->CreateBitmapFromMemory(64,32,GUID_WICPixelFormat32bppPRGBA,256,UINT(bytes.size()),bytes.data(),&context);
    helperRequire(13,SUCCEEDED(contextResult)&&context);
    ComPtr<IWICBitmap> image;
    const auto imageResult=factory->CreateBitmapFromSource(context.Get(),WICBitmapCacheOnLoad,&image);
    helperRequire(18,SUCCEEDED(imageResult)&&image);
    const auto destination=files.at("source.png");
    ComPtr<IWICStream> stream;ComPtr<IWICBitmapEncoder> encoder;
    auto hr=factory->CreateStream(&stream);
    if(SUCCEEDED(hr))hr=stream->InitializeFromFilename(destination.c_str(),GENERIC_WRITE);
    if(SUCCEEDED(hr))hr=factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder);
    if(SUCCEEDED(hr))hr=encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache);
    helperRequire(21,SUCCEEDED(hr)&&encoder);
    ComPtr<IWICBitmapFrameEncode> frame;
    hr=encoder->CreateNewFrame(&frame,nullptr);
    if(SUCCEEDED(hr))hr=frame->Initialize(nullptr);
    if(SUCCEEDED(hr))hr=frame->SetSize(64,32);
    auto format=GUID_WICPixelFormat32bppRGBA;
    if(SUCCEEDED(hr))hr=frame->SetPixelFormat(&format);
    if(SUCCEEDED(hr))hr=frame->SetResolution(72,72);
    ComPtr<IWICMetadataQueryWriter> metadata;
    if(SUCCEEDED(hr))hr=frame->GetMetadataQueryWriter(&metadata);
    if(SUCCEEDED(hr)){
        PROPVARIANT intent{};intent.vt=VT_UI1;intent.bVal=0;
        hr=metadata->SetMetadataByName(L"/sRGB/RenderingIntent",&intent);
    }
    if(SUCCEEDED(hr))hr=frame->WriteSource(image.Get(),nullptr);
    if(SUCCEEDED(hr))hr=frame->Commit();
    if(SUCCEEDED(hr))hr=encoder->Commit();
    checkAt(fixtureSource,23,SUCCEEDED(hr),"expect");
    return destination;
}

struct Snapshot {Document document;std::string active;};
struct Session {
    std::optional<Document> document;
    std::string active;
    History history;
    std::optional<editing_transform::TransformSessionState> transform;
    void create(int width,int height){
        history.begin("New Canvas",document,active);Document value;value.id=newId();value.width=width;value.height=height;
        document=std::move(value);active.clear();history.end(document,active);
    }
    Document& doc(){guard(document.has_value(),"Native session owns a document");return *document;}
    Layer& layer(){guard(!doc().layers.empty(),"Native session owns its active layer");for(auto& value:doc().layers)if(value.id==active)return value;throw std::runtime_error("Active ID resolves");}
    void importImage(const std::filesystem::path& file){
        const auto decoded=imaging::WicCodec::decode(file);
        guard(decoded.image.width==64&&decoded.image.height==32&&decoded.metadata.originalOrientation==1,"Imported original PNG fixture geometry/orientation");
        // The source's first import creates its document inside the Import edit.
        history.begin("Import",document,active);
        if(!document){Document value;value.id=newId();value.width=int(decoded.image.width);value.height=int(decoded.image.height);document=std::move(value);}
        Layer value;value.id=newId();value.name="source";value.transform={0,0,double(decoded.image.width),double(decoded.image.height)};
        value.raster=Raster::fromRgba(int(decoded.image.width),int(decoded.image.height),decoded.image.pixels.data(),decoded.image.stride);
        active=value.id;doc().layers.push_back(std::move(value));history.end(document,active);
        guard(layer().raster->pixel(0,0)==Pixel{255,0,0,255}&&layer().raster->pixel(63,31)==Pixel{},"Production PNG importer preserves fixture pixels");
    }
    void blank(){
        history.begin("New Layer",document,active);Layer value;value.id=newId();value.name="Layer";value.transform={0,0,double(doc().width),double(doc().height)};
        // Source addBlankLayer stores no asset until the first pixel operation.
        active=value.id;doc().layers.push_back(std::move(value));history.end(document,active);
    }
    Snapshot snapshot(int sourceLine){required(sourceLine,document.has_value());validateDocument(doc());return {doc(),active};}
    void apply(const Snapshot& snapshot,const char* action){
        guard(document&&document->id==snapshot.document.id,"Size result belongs to original document");
        history.begin(action,document,active);document=snapshot.document;active=snapshot.active;history.end(document,active);
    }
    void restore(std::optional<compositor::Snapshot> state){guard(state.has_value(),"Requested history entry exists");document=std::move(state->document);active=std::move(state->activeLayer);}
    void undo(){restore(history.undo());}
    void redo(){restore(history.redo());}
    void beginTransform(){
        guard(!transform&&document&&layer().raster,"Single visible raster transform is eligible");
        editing_transform::TransformSessionState state;state.original=doc();state.originalActive=active;state.target=active;
        state.ids={active};state.originalBox=state.draft=layer().transform;state.persistent=true;
        history.begin("Transform Layer",document,active);transform=std::move(state);
    }
    void previewTransform(const Transform& draft){
        guard(transform&&draft.valid(),"Valid owned transform draft");transform->draft=draft;
        document=transform->original;layer().transform=draft;
    }
    void commitTransform(){guard(transform.has_value(),"Owned transform edit exists");history.end(document,active);transform.reset();}
};
Snapshot canvasResize(const Snapshot& source,const editing::CanvasSizeOptions& options){return {editing::canvasResize(source.document,options),source.active};}
Snapshot imageResize(const Snapshot& source,const editing::ImageSizeOptions& options){return {editing::imageResize(source.document,options),source.active};}
bool noImages(const Document& document){return std::none_of(document.layers.begin(),document.layers.end(),[](const Layer& layer){return bool(layer.raster);});}
bool projectError(const std::function<void()>& action){
    try{action();}catch(const std::runtime_error& error){
        const std::string message=error.what();
        return message=="Canvas extension exceeds project budget"||message=="Resized image exceeds pixel budget";
    }return false;
}
imaging::DecodedImage exportPng(const Snapshot& snapshot,const std::filesystem::path& destination){
    (void)exportDocumentAtomic(snapshot.document,destination);
    return imaging::WicCodec::decode(destination);
}
Pixel colorAt(const imaging::RgbaImage& image,int x,int y,int sourceLine){
    required(sourceLine,x>=0&&y>=0&&x<int(image.width)&&y<int(image.height)&&image.stride>=size_t(image.width)*4&&image.pixels.size()>=image.stride*image.height);
    const auto at=size_t(y)*image.stride+size_t(x)*4;
    return {image.pixels[at],image.pixels[at+1],image.pixels[at+2],image.pixels[at+3]};
}

void anchors(){
    Files files;Session session;session.importImage(importFixture(files));session.beginTransform();required(14,session.transform.has_value());auto transform=session.transform->draft;
    transform.rotation=37;transform.flipX=true;session.previewTransform(transform);session.commitTransform();
    const auto source=session.snapshot(19);required(20,!source.document.layers.empty());const auto layer=source.document.layers.front();
    for(int delta:{5,-5})for(int anchor=0;anchor<=8;++anchor){
        const auto result=canvasResize(source,{64+delta,32+delta,anchor});required(25,!result.document.layers.empty());const auto& output=result.document.layers.front();
        const std::array<int,3> expected{0,delta==5?2:-3,delta};
        expect(27,output.transform.x==transform.x+expected[size_t(anchor%3)]);expect(28,output.transform.y==transform.y+expected[size_t(anchor/3)]);
        expect(29,output.transform.width==transform.width&&output.transform.height==transform.height);expect(30,output.transform.rotation==37&&output.transform.flipX);
        expect(31,output.id==layer.id);expect(32,output.raster==source.document.layers.front().raster);
    }
}
void units(){
    editing::CanvasSizeDraft draft(1000,500,100);draft.relative=true;draft.locked=true;draft.set(200,true);
    expect(42,draft.width==1200&&draft.height==600);expect(43,draft.displayed(false)==100);draft.set(-250,false);expect(45,draft.width==500&&draft.height==250);
    draft.relative=false;draft.unit=editing::CanvasUnit::Inches;draft.set(10,true);expect(49,draft.width==1000&&draft.height==500);
    draft.unit=editing::CanvasUnit::Percent;draft.set(50,true);expect(52,draft.width==500&&draft.height==250);
    draft.unit=editing::CanvasUnit::Centimeters;expect(54,std::abs(draft.displayed(true)-12.7)<.001);draft.unit=editing::CanvasUnit::Pixels;draft.set(0,true);expect(57,!draft.valid());
}
void colored_extension(){
    Files files;Session session;session.create(4,4);session.blank();required(64,session.document.has_value());const auto before=session.document;const auto input=session.snapshot(65);
    const auto output=canvasResize(input,{8,2,4,Pixel{255,0,0,255}});expect(70,output.document.layers.size()==2);expect(71,output.active==input.active);
    const auto bitmap=exportPng(output,files.at("extension.png"));required(73,bitmap.image.width&&bitmap.image.height);
    expect(74,colorAt(bitmap.image,0,0,74).a==255);expect(75,colorAt(bitmap.image,0,0,75).r/255.>.99);
    expect(76,colorAt(bitmap.image,3,0,76).a==0);expect(77,colorAt(bitmap.image,7,1,77).a==255);
    session.apply(output,"Canvas Size");session.undo();expect(80,session.document==before);session.redo();expect(82,session.doc().width==8&&session.doc().height==2);
    ProjectStore store(makeWicProjectCodec());const auto project=files.at("Canvas.comp");store.save(project,output.document,output.active);const auto loaded=store.load(project);
    const auto reopened=exportPng({loaded.document,loaded.activeLayer},files.at("reopened.png"));required(88,reopened.image.width&&reopened.image.height);
    expect(89,colorAt(reopened.image,3,0,89).a==0);expect(90,colorAt(reopened.image,0,0,90).r/255.>.99);
}
void allocation_free(){
    Session session;session.create(4,4);const auto input=session.snapshot(96);const auto large=canvasResize(input,{30000,30000});expect(98,noImages(large.document));
    const auto shrunk=canvasResize(input,{2,2,4,Pixel{255,255,255,255}});expect(101,noImages(shrunk.document)&&shrunk.document.layers.empty());
    expect(102,projectError([&]{(void)canvasResize(input,{30000,30000,4,Pixel{255,255,255,255}});}));expect(105,session.doc().width==4);
}
void image_identity(){
    Files files;Session session;session.importImage(importFixture(files));required(14,session.document.has_value());const auto original=session.document;
    required(15,!original->layers.empty());const auto layer=original->layers.front();const auto input=session.snapshot(16);
    const auto result=imageResize(input,{128,96,300,Transform::Sampling::Nearest});session.apply(result,"Image Size");
    expect(19,session.doc().width==128&&session.doc().height==96);expect(20,session.doc().resolution==300);expect(21,session.active==layer.id);
    required(22,!session.doc().layers.empty()&&session.doc().layers.front().raster);const auto asset=session.doc().layers.front().raster;
    expect(23,asset->width==128&&asset->height==96);const imaging::RgbaImage bitmap{uint32_t(asset->width),uint32_t(asset->height),size_t(asset->width)*4,asset->rgba()};
    expect(25,colorAt(bitmap,0,0,25).r/255.>.95);expect(26,colorAt(bitmap,127,0,26).a==0);session.undo();
    expect(28,session.document==original);expect(29,session.doc().layers.front().raster==layer.raster);session.redo();expect(31,session.doc().resolution==300);
}
void resolution_export(){
    Files files;Session session;session.create(32,16);session.blank();const auto before=session.snapshot(38);const auto resized=imageResize(before,{32,16,300});
    expect(41,resized.document.layers.front().transform==before.document.layers.front().transform);session.apply(resized,"Image Size");
    ProjectStore store(makeWicProjectCodec());const auto project=files.at("Size.comp");store.save(project,resized.document,resized.active);const auto loaded=store.load(project);expect(47,loaded.document.resolution==300);
    const auto png=files.at("dpi.png");(void)exportDocumentAtomic(loaded.document,png);
    ComPtr<IWICImagingFactory> factory;guard(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory))),"DPI reader factory");
    ComPtr<IWICBitmapDecoder> decoder;const auto sourceResult=factory->CreateDecoderFromFilename(png.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder);
    required(49,SUCCEEDED(sourceResult)&&decoder);ComPtr<IWICBitmapFrameDecode> frame;double dpiX=0,dpiY=0;
    auto properties=decoder->GetFrame(0,&frame);if(SUCCEEDED(properties))properties=frame->GetResolution(&dpiX,&dpiY);
    required(50,SUCCEEDED(properties)&&frame);expect(51,std::abs(dpiX-300)<1);session.undo();expect(53,session.doc().resolution==72);
}
void rotated_hidden(){
    Files files;Session session;session.importImage(importFixture(files));const auto snapshot=session.snapshot(61);required(62,!snapshot.document.layers.empty());
    auto input=snapshot;auto& record=input.document.layers.front();record.visible=false;record.transform={-16,4,64,32,90};
    const auto result=imageResize(input,{128,96,72,Transform::Sampling::Nearest});required(70,!result.document.layers.empty());const auto& output=result.document.layers.front();
    expect(71,!output.visible);expect(72,output.transform.rotation==0);expect(74,std::abs(output.transform.width-64)<=1);
    expect(75,std::abs(output.transform.height-192)<=1);expect(76,output.transform.y<0);
    expect(77,projectError([&]{(void)imageResize(input,{30000,30000,72});}));expect(80,session.doc().width==64);
}
}

int main(int argc,char** argv){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);QCoreApplication app(argc,argv);
    const auto initialized=CoInitializeEx(nullptr,COINIT_MULTITHREADED);if(FAILED(initialized))return 2;
    struct Apartment {~Apartment(){CoUninitialize();}} apartment;
    const std::map<std::string,void(*)()> cases{{"anchors",anchors},{"units",units},{"colored_extension",colored_extension},{"allocation_free",allocation_free},
        {"image_identity",image_identity},{"resolution_export",resolution_export},{"rotated_hidden",rotated_hidden}};
    std::string error;bool aborted=false;const std::string name=argc>1?argv[1]:"";
    try{guard(argc==3&&cases.contains(name),"Supply case and fresh output report");guard(!QFile::exists(QString::fromUtf8(argv[2])),"Output report must be fresh");
        currentSource=name=="image_identity"||name=="resolution_export"||name=="rotated_hidden"?imageSource:canvasSource;cases.at(name)();
    }catch(const std::exception& exception){error=exception.what();aborted=true;std::fprintf(stderr,"ERROR %s\n",error.c_str());}
    const bool passed=!aborted&&failures==0;
    if(argc==3){QJsonObject report{{"schema","DOCUMENT_SIZE_SOURCE_RESULT_V1"},{"case",QString::fromStdString(name)},
        {"status",passed?"passed":"failed"},{"checks",checks},{"check_count",checks.size()},{"failed_checks",failures},
        {"aborted",aborted},{"error",QString::fromStdString(error)},{"mac_differential",false}};
        QFile output(QString::fromUtf8(argv[2]));if(!output.open(QIODevice::WriteOnly|QIODevice::NewOnly))return 2;output.write(QJsonDocument(report).toJson());}
    std::printf("%s %s checks=%lld failures=%d\n",passed?"PASS":"FAIL",name.c_str(),static_cast<long long>(checks.size()),failures);
    return passed?0:1;
}
