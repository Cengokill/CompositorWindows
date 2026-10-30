#include "core/Document.h"
#include "core/DocumentExport.h"
#include "effects/Adjustments.h"
#include "filters/PixelFilters.h"
#include "graphics/StackRenderer.h"
#include "imaging/hdr_codec.h"
#include "persistence/ProjectStore.h"
#include "layers/LayerOperations.h"
#include "graphics/GrowingBrushSession.h"
#include "editing/DocumentGeometry.h"
#include "editing/PixelEdits.h"
#include "editing/GradientPreview.h"
#include <ImfInputFile.h>
#include <ImfOutputFile.h>
#include <ImfTiledOutputFile.h>
#include <ImfTileDescription.h>
#include <ImfHeader.h>
#include <ImfChannelList.h>
#include <ImfFrameBuffer.h>
#include <half.h>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QFile>
#include <fstream>
#include <cmath>
#include <iostream>

using namespace compositor;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F&& fn,const char* message){bool rejected=false;try{fn();}catch(const std::exception&){rejected=true;}require(rejected,message);}
void near(double value,double expected,double tolerance,const char* message){require(std::abs(value-expected)<=tolerance,message);}
Layer bright(int width=8,int height=4){
    auto pixels=makeDeep(width,height);for(int y=0;y<height;++y)for(int x=0;x<width;++x){float v=float(linearToSrgb(4.*x/(width-1)));pixels->setPixel(x,y,{v,v,v,1});}
    Layer layer;layer.id=newId();layer.name="HDR ramp";layer.deep=pixels;layer.raster=rasterFromDeep(*pixels);layer.transform={0,0,double(width),double(height)};return layer;
}
Document document(){Document d;d.id=newId();d.width=8;d.height=4;d.layers={bright()};return d;}
std::string exposure(double ev){auto json=QJsonDocument::fromJson(QByteArray::fromStdString(effects::defaultAdjustmentJson("Exposure"))).object();json["exposureSettings"]=QJsonObject{{"exposure",ev},{"offset",0},{"gamma",1}};return QJsonDocument(json).toJson(QJsonDocument::Compact).toStdString();}
std::filesystem::path native(const QString& path){return std::filesystem::path(path.toStdWString());}
// An independent OpenEXR writer supplies scene-linear FLOAT samples, including
// a nonzero data-window origin. This does not use the production encoder.
void externalExr(const std::filesystem::path& path,bool tiled=false,bool nonfinite=false){
    Imf::Header h(8,4);h.dataWindow()=Imath::Box2i({-3,7},{4,10});for(const char* name:{"R","G","B","A"})h.channels().insert(name,Imf::Channel(Imf::FLOAT));
    std::vector<WidePixel> values(32);for(int y=0;y<4;++y)for(int x=0;x<8;++x){float alpha=x==0||x==3?.5f:1.f;values[size_t(y)*8+x]={4.f*x/7*alpha,2.f*x/7*alpha,1.f*x/7*alpha,alpha};}
    if(nonfinite)values[7][0]=std::numeric_limits<float>::infinity();
    Imf::FrameBuffer frame;const char* names[]{"R","G","B","A"};for(int c=0;c<4;++c)frame.insert(names[c],Imf::Slice::Make(Imf::FLOAT,values[0].data()+c,h.dataWindow(),sizeof(WidePixel),8*sizeof(WidePixel)));
    if(tiled){h.setTileDescription(Imf::TileDescription(4,2,Imf::ONE_LEVEL));Imf::TiledOutputFile file(path.string().c_str(),h,1);file.setFrameBuffer(frame);file.writeTiles(0,file.numXTiles()-1,0,file.numYTiles()-1);}
    else {Imf::OutputFile file(path.string().c_str(),h,1);file.setFrameBuffer(frame);file.writePixels(4);}
}
}
void hdrNumericContracts(){
    // Exhaustively verify binary16 decode and round-to-nearest-even against Imath.
    for(unsigned i=0;i<65536;++i){Imath::half reference;reference.setBits(uint16_t(i));if(!reference.isFinite())continue;near(halfToFloat(uint16_t(i)),float(reference),0,"Half decode differs from Imath");require(floatToHalf(float(reference))==i,"Half round trip changed bits");}
    for(int i=-200000;i<=200000;++i){float value=i/17.f;require(floatToHalf(value)==Imath::half(value).bits(),"Half rounding differs from Imath");}
    rejects([]{floatToHalf(INFINITY);},"Infinite sample accepted");rejects([]{floatToHalf(70000);},"Half overflow accepted");
    near(blendWidePixel({2,2,2,1},{.5f,.5f,.5f,1},Blend::Multiply)[0],1,0,"HDR Multiply normalized SDR source by the highlight peak");
    near(blendWidePixel({2,2,2,1},{.5f,.5f,.5f,1},Blend::Screen)[0],1.5,0,"HDR Screen clipped highlights before blending");
    QTemporaryDir directory;require(directory.isValid(),"HDR temporary directory failed");auto root=native(directory.path());auto input=root/"external.exr";externalExr(input);
    auto imported=imaging::decodeExr(input);require(imported->isHdr()&&imported->width==8&&imported->height==4,"EXR import dimensions/depth");near(srgbToLinear(imported->pixel(7,0)[0]),4,.006,"EXR import clipped highlights");near(imported->pixel(0,0)[3],.5,0,"EXR alpha changed");
    near(srgbToLinear(imported->pixel(3,0)[0]/imported->pixel(3,0)[3])*imported->pixel(3,0)[3],4.*3/7*.5,.003,"EXR premultiplied color conversion changed alpha/color");
    externalExr(root/"tiled.exr",true);auto tiled=imaging::decodeExr(root/"tiled.exr");require(tiled->samples==imported->samples,"Tiled EXR differs from scanline EXR");
    externalExr(root/"infinite.exr",false,true);rejects([&]{imaging::decodeExr(root/"infinite.exr");},"Non-finite EXR accepted");
    auto encoded=root/"roundtrip.exr";imaging::encodeExr(encoded,*imported);auto reopened=imaging::decodeExr(encoded);near(reopened->pixel(7,0)[0],imported->pixel(7,0)[0],.002,"EXR roundtrip changed highlights");
    // Independent reader checks the file's scene-linear sample rather than
    // merely accepting a symmetric encoder/decoder color-space mistake.
    {Imf::InputFile file(encoded.string().c_str(),1);std::vector<WidePixel> values(32);Imf::FrameBuffer frame;const char* names[]{"R","G","B","A"};for(int c=0;c<4;++c)frame.insert(names[c],Imf::Slice(Imf::FLOAT,reinterpret_cast<char*>(values[0].data()+c),sizeof(WidePixel),8*sizeof(WidePixel)));file.setFrameBuffer(frame);file.readPixels(0,3);near(values[7][0],4,.006,"EXR encoded color is not scene-linear");}
    auto hdr=root/"radiance.hdr";{std::ofstream file(hdr,std::ios::binary);file<<"#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 2\n";const unsigned char pixels[]{128,64,32,131,128,64,32,129};file.write(reinterpret_cast<const char*>(pixels),8);}
    auto radiance=imaging::decodeRadiance(hdr);near(srgbToLinear(radiance->pixel(0,0)[0]),4,.006,"Radiance import clipped/changed RGBE");
    imaging::ImportOptions tiny;tiny.maxWorkingBytes=32;rejects([&]{imaging::decodeExr(input,tiny);},"EXR allocation budget ignored");
    tiny={};tiny.remainingPixels=31;rejects([&]{imaging::decodeExr(input,tiny);},"EXR remaining pixel budget ignored");
    tiny={};tiny.cancelled=[] {return true;};rejects([&]{imaging::decodeExr(input,tiny);},"EXR cancellation ignored");rejects([&]{imaging::decodeRadiance(hdr,tiny);},"Radiance cancellation ignored");
    {std::ofstream bad(root/"broken.exr",std::ios::binary);bad<<"not EXR";}rejects([&]{imaging::decodeExr(root/"broken.exr");},"Malformed EXR accepted");
    auto d=document();const auto original=d;const auto originalSamples=d.layers[0].deep->samples;auto adjusted=effects::applyAdjustment(d.layers[0].deep,exposure(-1));near(srgbToLinear(adjusted->pixel(7,0)[0]),2,.006,"Exposure lost the highlight range");
    adjusted=effects::applyAdjustment(d.layers[0].deep,exposure(-2));near(adjusted->pixel(7,0)[0],1,.002,"Two stops did not recover white");
    GrayRaster selected{8,4,std::vector<uint8_t>(32,0)};selected.pixels[7]=128;auto partial=effects::applyAdjustment(d.layers[0].deep,exposure(-2),&selected);near(partial->pixel(7,1)[0],d.layers[0].deep->pixel(7,1)[0],0,"Selection changed excluded HDR pixels");near(partial->pixel(7,0)[0],(d.layers[0].deep->pixel(7,0)[0]+1)/2,.004,"Soft selection coverage changed");
    auto identity=effects::applyAdjustment(d.layers[0].deep,exposure(0));require(identity==d.layers[0].deep,"Identity adjustment copied/changed HDR pixels");
    Layer live;live.id=newId();live.name="Exposure";live.transform={0,0,8,4};live.adjustmentJson=exposure(-2);d.layers.push_back(live);SoftwareRenderer renderer;auto visible=renderer.render(d,0,0,8,4);require(visible->pixel(6,0).r<250&&visible->pixel(7,0).r>=254,"Canvas adjusted the clipped display copy");
    auto rendered=renderer.renderDeep(d,0,0,8,4);near(rendered->pixel(7,0)[0],1,.002,"HDR compositor did not apply exposure");
    auto blurJson=QJsonDocument::fromJson(QByteArray::fromStdString(effects::defaultAdjustmentJson("Gaussian Blur"))).object();blurJson["filterSettings"]=QJsonObject{{"radius",1},{"angle",0},{"distance",0},{"amount",0},{"gaussian",false},{"monochromatic",false}};
    auto blurred=original;auto blurLayer=live;blurLayer.adjustmentJson=QJsonDocument(blurJson).toJson(QJsonDocument::Compact).toStdString();blurred.layers.push_back(blurLayer);auto fullBlur=renderer.renderDeep(blurred,0,0,8,4),rightBlur=renderer.renderDeep(blurred,4,0,4,4);for(int y=0;y<4;++y)for(int x=0;x<4;++x)near(fullBlur->pixel(x+4,y)[0],rightBlur->pixel(x,y)[0],.005,"HDR live blur changed across export tiles");
    d=original;Layer folder;folder.id=newId();folder.name="Folder";folder.group=true;folder.opacity=.5;folder.transform={0,0,8,4};d.layers[0].parentId=folder.id;d.layers.insert(d.layers.begin(),folder);rendered=renderer.renderDeep(d,0,0,8,4);near(rendered->pixel(7,0)[3],.5,0,"HDR folder opacity changed alpha");near(rendered->pixel(7,0)[0],original.layers[0].deep->pixel(7,0)[0]/2,.002,"HDR folder clipped the highlights");
    auto merged=layers::merge(d,{{folder.id},folder.id},renderer);require(merged.document.layers.size()==1&&merged.document.layers[0].deep,"HDR merge dropped deep pixels");near(merged.document.layers[0].deep->pixel(7,0)[0],rendered->pixel(7,0)[0],.002,"HDR merge pixels disagree");
    filters::Request request;request.source=original.layers[0].raster;request.deepSource=original.layers[0].deep;request.transform=original.layers[0].transform;request.kind=filters::Kind::GaussianBlur;request.settings.radius=1;request.preview=true;auto preview=filters::apply(request);request.preview=false;auto applied=filters::apply(request);require(preview.deep&&applied.deep,"Blur dropped HDR");
    // Compare using document coordinates because Apply may trim transparent padding.
    Document a=original,b=original;a.layers[0].deep=preview.deep;a.layers[0].raster=preview.raster;a.layers[0].transform=preview.transform;b.layers[0].deep=applied.deep;b.layers[0].raster=applied.raster;b.layers[0].transform=applied.transform;
    auto pa=renderer.renderDeep(a,0,0,8,4),pb=renderer.renderDeep(b,0,0,8,4);for(int y=0;y<4;++y)for(int x=0;x<8;++x)near(pa->pixel(x,y)[0],pb->pixel(x,y)[0],.005,"HDR blur preview differs from Apply");
    int polls=0;request.limits.cancelled=[&] {return ++polls>10;};rejects([&]{filters::apply(request);},"HDR filter cancellation ignored");require(original.layers[0].deep->samples==originalSamples,"Cancelled operation changed the source samples");
    auto converted=hdrTo16Bit(*original.layers[0].deep);require(!converted->isHdr()&&converted->at(7,0)[0]==65535,"Explicit HDR conversion did not clip at white");
    History history;std::optional<Document> state=original;const auto id=state->layers[0].id;history.begin("Convert HDR",state,id);state->layers[0].deep=converted;state->layers[0].raster=rasterFromDeep(*converted);history.end(state,id);require(history.undoCount()==1,"HDR conversion history count");auto undone=history.undo();require(undone&&undone->document==std::optional<Document>(original),"HDR undo did not restore original pixels");auto redone=history.redo();require(redone&&redone->document->layers[0].deep==converted,"HDR conversion redo lost precision state");
    auto resized=editing::imageResize(original,{16,8,72,Transform::Sampling::Nearest});require(resized.layers[0].deep&&resized.layers[0].deep->isHdr(),"Resize discarded HDR");near(resized.layers[0].deep->pixel(15,0)[0],original.layers[0].deep->pixel(7,0)[0],.002,"Resize clipped the source");
    graphics::BrushSessionSettings brush;brush.radius=1;brush.hardness=1;brush.opacity=1;brush.color={255,255,255};
    graphics::GrowingBrushSession stroke(original.layers[0],brush,8,4);stroke.begin({7.5,1.5});auto snapshot=stroke.commit();auto painted=snapshot->materializeLayer();require(painted.deep&&painted.deep->isHdr(),"Brush dropped HDR");near(painted.deep->pixel(7,1)[0],1,.01,"White brush on clipped white failed to replace HDR pixels");near(painted.deep->pixel(7,3)[0],original.layers[0].deep->pixel(7,3)[0],.002,"Brush altered untouched highlights");
    auto previewDocument=original;previewDocument.layers.push_back(live);auto previewImage=SoftwareRenderer(snapshot->renderPreview()).render(previewDocument,0,0,8,4);previewDocument.layers[0]=painted;auto committedImage=renderer.render(previewDocument,0,0,8,4);require(previewImage->pixel(7,1)==committedImage->pixel(7,1),"Brush HDR preview differs after Apply");
    std::cout<<"PASS HDR codec, half, exposure, selection, renderer, groups, merge, blur, cancellation and history contracts\n";
}
void hdrStorageContracts(){
    auto edits=document();auto filled=editing::editLayer(edits.layers[0],edits,editing::PixelEdit::Fill,{0,0,0,255},false);require(filled.deep&&filled.deep->isHdr()&&filled.deep->pixel(7,0)[0]==0,"HDR fill lost depth or failed to replace white");
    rejects([&]{SoftwareRenderer().render(edits,0,0,30000,1000);},"SDR output bypassed HDR render allocation bounds");
    editing::GradientSettings gradient;gradient.opacity=.5;gradient.style=editing::GradientStyle::ForegroundToBackground;auto ramp=editing::gradientLayer(edits.layers[0],edits,{0,0},{8,0},gradient,{0,0,0,255},{0,0,0,255},false);require(ramp.deep&&ramp.deep->isHdr(),"Gradient dropped HDR");near(ramp.deep->pixel(7,0)[0],edits.layers[0].deep->pixel(7,0)[0]/2,.003,"Gradient clipped highlights before blending");
    QTemporaryDir directory;require(directory.isValid(),"HDR project temporary directory failed");auto root=native(directory.path());auto d=document();auto project=root/"hdr.comp";ProjectStore store(makeWicProjectCodec());store.save(project,d,d.layers[0].id);auto opened=store.load(project);require(opened.readVersion==13&&opened.document.layers[0].deep&&opened.document.layers[0].deep->isHdr(),"HDR project did not reopen as v13 EXR");near(opened.document.layers[0].deep->pixel(7,0)[0],d.layers[0].deep->pixel(7,0)[0],.002,"HDR project lost highlights");
    require(std::filesystem::exists(project/"images"/(d.layers[0].id+".exr")),"HDR project did not write EXR asset");
    auto output=root/"output.exr";imaging::ExportOptions options;options.format=imaging::ImageFormat::Exr;exportDocumentAtomic(opened.document,output,options);auto pixels=imaging::decodeExr(output);near(pixels->pixel(7,0)[0],d.layers[0].deep->pixel(7,0)[0],.002,"EXR document export clipped highlights");
    std::ifstream originalFile(output,std::ios::binary);std::string before((std::istreambuf_iterator<char>(originalFile)),{});originalFile.close();options.cancelled=[] {return true;};rejects([&]{exportDocumentAtomic(d,output,options);},"Cancelled EXR export succeeded");std::ifstream unchanged(output,std::ios::binary);require(std::string((std::istreambuf_iterator<char>(unchanged)),{})==before,"Cancelled EXR export replaced destination");
    unchanged.close();options.cancelled={};rejects([&]{exportDocumentAtomic(d,output,options,{},[](uint32_t,uint32_t){throw std::runtime_error("Injected export interruption");});},"Interrupted EXR export succeeded");std::ifstream interrupted(output,std::ios::binary);require(std::string((std::istreambuf_iterator<char>(interrupted)),{})==before,"Interrupted EXR export replaced destination");interrupted.close();
    auto padded=d;Layer blur;blur.id=newId();blur.transform={0,0,8,4};auto settings=QJsonDocument::fromJson(QByteArray::fromStdString(effects::defaultAdjustmentJson("Gaussian Blur"))).object();settings["filterSettings"]=QJsonObject{{"radius",100},{"angle",0},{"distance",0},{"amount",0},{"gaussian",false},{"monochromatic",false}};blur.adjustmentJson=QJsonDocument(settings).toJson(QJsonDocument::Compact).toStdString();padded.layers.push_back(blur);imaging::StreamExportLimits limited;limited.maxBufferBytes=512*1024;rejects([&]{exportDocumentAtomic(padded,root/"limited.exr",options,limited);},"EXR export ignored halo memory budget");
    ProjectStore failingStore(makeWicProjectCodec(),[](SaveFaultPoint point){if(point==SaveFaultPoint::AfterStage)throw std::runtime_error("Injected HDR save interruption");});auto changed=d;changed.layers[0].name="Not installed";rejects([&]{failingStore.save(project,changed,changed.layers[0].id);},"Interrupted HDR save succeeded");require(store.load(project).document.layers[0].name==d.layers[0].name,"Interrupted HDR save replaced original project");
    QFile manifest(QString::fromStdWString((project/"manifest.json").wstring()));require(manifest.open(QIODevice::ReadOnly),"Read HDR manifest");auto json=QJsonDocument::fromJson(manifest.readAll()).object();manifest.close();json["version"]=12;require(manifest.open(QIODevice::WriteOnly),"Rewrite HDR manifest");manifest.write(QJsonDocument(json).toJson());manifest.close();rejects([&]{store.load(project);},"Old version accepted HDR asset");
}
