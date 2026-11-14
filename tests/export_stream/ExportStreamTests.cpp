#include "core/DocumentExport.h"
#include "effects/Adjustments.h"
#include "imaging/wic_codec.h"
#include "imaging/hdr_codec.h"
#include "imaging/cmyk.h"
#include "../cmyk/ProfileFixture.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <windows.h>
#include <psapi.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <cmath>
using namespace compositor;
using namespace compositor::imaging;
#define REQUIRE(...) do {if(!(__VA_ARGS__))throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+": " #__VA_ARGS__);}while(false)
template<class F>void rejects(F action){bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}REQUIRE(rejected);}
std::filesystem::path path(const QTemporaryDir& dir,const char* name){return std::filesystem::path(dir.filePath(QString::fromUtf8(name)).toStdWString());}
std::vector<uint8_t> bytes(const std::filesystem::path& name){std::ifstream file(name,std::ios::binary);return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};}
void original(const std::filesystem::path& name){std::ofstream file(name,std::ios::binary);file<<"preserve original\0\xff";}
void only(const QTemporaryDir& directory,size_t files){REQUIRE(size_t(std::distance(std::filesystem::directory_iterator(std::filesystem::path(directory.path().toStdWString())),std::filesystem::directory_iterator{}))==files);}
Pixel pixel(const RgbaImage& image,int x,int y){auto*p=image.pixels.data()+size_t(y)*image.stride+size_t(x)*4;return {p[0],p[1],p[2],p[3]};}
void metadata(const std::filesystem::path& filename,ImageFormat format){
 using Microsoft::WRL::ComPtr;ComPtr<IWICImagingFactory> f;REQUIRE(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&f))));
 ComPtr<IWICBitmapDecoder> decoder;REQUIRE(SUCCEEDED(f->CreateDecoderFromFilename(filename.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoder)));GUID type{};REQUIRE(SUCCEEDED(decoder->GetContainerFormat(&type)));REQUIRE(type==(format==ImageFormat::Png?GUID_ContainerFormatPng:GUID_ContainerFormatJpeg));UINT frames=0;REQUIRE(SUCCEEDED(decoder->GetFrameCount(&frames))&&frames==1);ComPtr<IWICBitmapFrameDecode> frame;REQUIRE(SUCCEEDED(decoder->GetFrame(0,&frame)));
 if(format==ImageFormat::Png){ComPtr<IWICMetadataQueryReader> reader;REQUIRE(SUCCEEDED(frame->GetMetadataQueryReader(&reader)));PROPVARIANT value;PropVariantInit(&value);REQUIRE(SUCCEEDED(reader->GetMetadataByName(L"/sRGB/RenderingIntent",&value)));REQUIRE(value.vt==VT_UI1&&value.bVal==0);PropVariantClear(&value);}
 else{UINT count=0;REQUIRE(SUCCEEDED(frame->GetColorContexts(0,nullptr,&count))&&count>0);}
}
Document blank(int width,int height){Document d;d.id=newId();d.width=width;d.height=height;return d;}
RgbaRows pattern(){return [](uint32_t y,uint32_t rows,std::span<uint8_t> data,size_t stride){for(uint32_t row=0;row<rows;++row)for(size_t x=0;x<stride/4;++x){auto*p=data.data()+row*stride+x*4;unsigned a=unsigned((x*11+(y+row)*19)%256);p[0]=uint8_t((x*37+(y+row)*17)%(a+1));p[1]=uint8_t((x*11+(y+row)*53)%(a+1));p[2]=uint8_t((x*79+(y+row)*7)%(a+1));p[3]=uint8_t(a);}};}
void extent_guard(){QTemporaryDir temp;auto target=path(temp,"keep.png");original(target);auto before=bytes(target);int calls=0;auto source=[&](uint32_t,uint32_t,std::span<uint8_t>,size_t){++calls;};for(auto dims:{std::pair{30000U,30000U},{30001U,1U},{10001U,10000U},{0U,1U}})rejects([&]{encodeRowsAtomic(target,dims.first,dims.second,source);});auto d=blank(30000,30000);Raster::resetMaterializationCount();rejects([&]{exportDocumentAtomic(d,target);});REQUIRE(calls==0&&Raster::materializationCount()==0&&bytes(target)==before);only(temp,1);validateExportExtent(10000,10000);validateExportExtent(30000,3333);}
void row_png(){QTemporaryDir temp;auto target=path(temp,"rows.png");ExportOptions options;options.dpi=300;StreamExportLimits limits;limits.stripeRows=7;uint32_t next=0;auto fill=pattern();auto result=encodeRowsAtomic(target,19,65,[&](uint32_t y,uint32_t count,std::span<uint8_t> out,size_t stride){REQUIRE(y==next&&count<=7&&stride==76&&out.size()==count*stride);next+=count;fill(y,count,out,stride);},options,limits);REQUIRE(next==65&&result.stripeRows==7&&result.encodedBytes==bytes(target).size());auto decoded=WicCodec::decode(target);RgbaImage expected{19,65,76,std::vector<uint8_t>(19*65*4)};fill(0,65,expected.pixels,76);REQUIRE(decoded.image.pixels==expected.pixels&&result.preview.pixels==expected.pixels);REQUIRE(decoded.metadata.originalOrientation==1&&std::abs(decoded.metadata.dpiX-300)<.02&&std::abs(decoded.metadata.dpiY-300)<.02);metadata(target,ImageFormat::Png);only(temp,1);}
void png_options(){
 QTemporaryDir temp;auto target=path(temp,"options.png");
 for(bool transparent:{false,true})for(bool interlaced:{false,true})for(uint32_t stripe:{1U,7U,19U}){
  ExportOptions options;options.pngTransparency=transparent;options.pngInterlaced=interlaced;options.matteR=40;options.matteG=120;options.matteB=210;StreamExportLimits limits;limits.stripeRows=stripe;
  auto result=encodeRowsAtomic(target,19,19,pattern(),options,limits);auto file=bytes(target);REQUIRE(file.size()>29&&file[28]==uint8_t(interlaced));auto decoded=WicCodec::decode(target).image;std::vector<uint8_t> source(19*19*4);pattern()(0,19,source,19*4);
  for(size_t i=0;i<source.size();i+=4){if(transparent){for(int c=0;c<4;++c)REQUIRE(decoded.pixels[i+c]==source[i+c]);}else{const uint8_t matte[]{40,120,210};for(int c=0;c<3;++c)REQUIRE(decoded.pixels[i+c]==std::min(255U,unsigned(source[i+c])+(unsigned(matte[c])*(255-source[i+3])+127)/255));REQUIRE(decoded.pixels[i+3]==255);}}
  REQUIRE(result.preview.pixels==decoded.pixels&&result.encodedBytes==file.size());
 }
 auto d=blank(2,1);Layer layer;layer.id=newId();layer.name="Deep";layer.transform={0,0,2,1};auto deep=std::make_shared<DeepImage>();deep->width=2;deep->height=1;deep->samples={10000,5000,0,32768,0,0,0,0};layer.deep=deep;layer.raster=rasterFromDeep(*deep);d.layers.push_back(layer);
 for(bool transparent:{false,true})for(bool interlaced:{false,true}){ExportOptions options;options.pngTransparency=transparent;options.pngInterlaced=interlaced;options.matteR=40;options.matteG=120;options.matteB=210;exportDocumentAtomic(d,target,options);auto file=bytes(target);REQUIRE(file[24]==16&&file[28]==uint8_t(interlaced));auto result=WicCodec::decodeProjectDeepPng(target);if(transparent){REQUIRE(result->samples[0]==10000&&result->samples[3]==32768&&result->samples[7]==0);}else{REQUIRE(result->samples[3]==65535&&result->samples[7]==65535&&result->samples[4]==40*257&&result->samples[5]==120*257&&result->samples[6]==210*257);}}
 auto saved=bytes(target);ExportOptions cancelled;cancelled.pngInterlaced=true;cancelled.cancelled=[] {return true;};rejects([&]{exportDocumentAtomic(d,target,cancelled);});REQUIRE(bytes(target)==saved);
 int polls=0;cancelled.cancelled=[&]{return ++polls>=2;};rejects([&]{exportDocumentAtomic(d,target,cancelled);});REQUIRE(polls>=2&&bytes(target)==saved);
 auto large=blank(10000,10000);Layer largeLayer;largeLayer.id=newId();largeLayer.name="Deep";largeLayer.transform={0,0,10000,10000};auto tiny=std::make_shared<DeepImage>();tiny->width=1;tiny->height=1;tiny->samples={0,0,0,0};largeLayer.deep=tiny;largeLayer.raster=Raster::filled(1,1,{0,0,0,0});large.layers.push_back(largeLayer);ExportOptions tooLarge;tooLarge.pngInterlaced=true;rejects([&]{exportDocumentAtomic(large,target,tooLarge);});REQUIRE(bytes(target)==saved);
}
void large_png_interlaced(){
 QTemporaryDir temp;auto d=blank(10000,10000);auto file=path(temp,"large-interlaced.png");ExportOptions options;options.pngInterlaced=true;const auto start=std::chrono::steady_clock::now();auto result=exportDocumentAtomic(d,file,options);auto data=bytes(file);REQUIRE(data.size()>29&&data[28]==1&&result.encodedBytes==data.size());PROCESS_MEMORY_COUNTERS_EX memory{};memory.cb=sizeof(memory);REQUIRE(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory)));std::cout<<"png interlaced 100000000 pixels bytes="<<data.size()<<" peak_working_set="<<memory.PeakWorkingSetSize<<" peak_commit="<<memory.PeakPagefileUsage<<" elapsed_ms="<<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count()<<"\n";
}
void deep_png_interlaced(){auto d=blank(4096,4096);Layer layer;layer.id=newId();layer.name="Deep";layer.transform={0,0,4096,4096};auto tiny=std::make_shared<DeepImage>();tiny->width=1;tiny->height=1;tiny->samples={0,0,0,0};layer.deep=tiny;layer.raster=Raster::filled(1,1,{0,0,0,0});d.layers.push_back(layer);QTemporaryDir temp;ExportOptions options;options.pngInterlaced=true;const auto start=std::chrono::steady_clock::now();auto result=exportDocumentAtomic(d,path(temp,"deep.png"),options);PROCESS_MEMORY_COUNTERS_EX memory{};memory.cb=sizeof(memory);REQUIRE(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory)));std::cout<<"png16 interlaced pixels=16777216 bytes="<<result.encodedBytes<<" peak_working_set="<<memory.PeakWorkingSetSize<<" peak_commit="<<memory.PeakPagefileUsage<<" elapsed_ms="<<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count()<<"\n";}
Document mixed(){auto d=blank(1031,131);d.resolution=144;Layer folder;folder.id=newId();folder.name="Folder";folder.group=true;folder.transform={0,0,1031,131};folder.mask=Mask{std::make_shared<GrayRaster>(GrayRaster{3,3,{255,150,255,200,80,255,255,255,255}})};d.layers.push_back(folder);Layer base;base.id=newId();base.name="Pixels";base.parentId=folder.id;base.transform={5.25,3.75,1017,127,3,true,false};auto source=std::vector<uint8_t>(1017*127*4);pattern()(0,127,source,1017*4);base.raster=Raster::fromRgba(1017,127,source.data(),1017*4);d.layers.push_back(base);Layer clip;clip.id=newId();clip.name="Clip";clip.parentId=folder.id;clip.maskSourceId=base.id;clip.opacity=.55;clip.blend=Blend::Screen;clip.transform={-10,2,1040,129};clip.raster=Raster::filled(3,3,{20,80,30,128});clip.mask=Mask{std::make_shared<GrayRaster>(GrayRaster{2,2,{255,255,100,255}}),true,false,Transform{4,1,1000,120,7,false,true}};d.layers.push_back(clip);Layer grain;grain.id=newId();grain.name="Grain";grain.parentId=folder.id;grain.adjustmentJson=effects::defaultAdjustmentJson("Grain");auto object=QJsonDocument::fromJson(QByteArray::fromStdString(grain.adjustmentJson)).object();object["grainSettings"]=QJsonObject{{"amount",65},{"size",2.5},{"roughness",40},{"seed",917}};grain.adjustmentJson=QJsonDocument(object).toJson(QJsonDocument::Compact).toStdString();d.layers.push_back(grain);auto hidden=clip;hidden.id=newId();hidden.visible=false;hidden.maskSourceId.clear();hidden.raster=Raster::filled(1,1,{255,0,0,255});d.layers.push_back(hidden);d.selection=Selection{std::make_shared<GrayRaster>(GrayRaster{d.width,d.height,std::vector<uint8_t>(size_t(d.width)*d.height,0)})};return d;}
void canonical_partition(){QTemporaryDir temp;auto d=mixed();const auto before=d;const auto expected=SoftwareRenderer().render(d,0,0,d.width,d.height)->rgba();for(uint32_t stripe:{1U,7U,64U}){StreamExportLimits limits;limits.stripeRows=stripe;auto target=path(temp,("part-"+std::to_string(stripe)+".png").c_str());Raster::resetMaterializationCount();auto result=exportDocumentAtomic(d,target,{},limits);REQUIRE(Raster::materializationCount()==0&&d==before&&result.maxRenderPixelBytes<=8*1024*1024);auto decoded=WicCodec::decode(target);REQUIRE(decoded.image.pixels==expected);}}
void channel_pack(){
 QTemporaryDir temp;auto target=path(temp,"pack.png");auto d=blank(2,2);Layer layer;layer.id=newId();layer.name="Red";layer.visible=false;layer.transform={0,0,2,2};layer.raster=Raster::filled(2,2,{128,0,0,128});d.layers.push_back(layer);const auto before=d;
 std::array<std::string,4> sources{"L"+layer.id,"white",{}, {}};
 for(uint32_t stripe:{1U,2U}){StreamExportLimits limits;limits.stripeRows=stripe;auto result=exportChannelPackAtomic(d,target,sources,{},limits);REQUIRE(result.preview.pixels[0]==27&&result.preview.pixels[1]==255&&result.preview.pixels[3]==255);
  using Microsoft::WRL::ComPtr;ComPtr<IWICImagingFactory> factory;REQUIRE(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory))));ComPtr<IWICBitmapDecoder> decoder;REQUIRE(SUCCEEDED(factory->CreateDecoderFromFilename(target.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoder)));ComPtr<IWICBitmapFrameDecode> frame;REQUIRE(SUCCEEDED(decoder->GetFrame(0,&frame)));ComPtr<IWICFormatConverter> converter;REQUIRE(SUCCEEDED(factory->CreateFormatConverter(&converter)));REQUIRE(SUCCEEDED(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom)));std::array<uint8_t,16> pixels{};REQUIRE(SUCCEEDED(converter->CopyPixels(nullptr,8,16,pixels.data())));for(int i=0;i<4;++i)REQUIRE(pixels[i*4]==27&&pixels[i*4+1]==255&&pixels[i*4+2]==0&&pixels[i*4+3]==0);
 }
 auto saved=bytes(target);bool stop=false;ExportOptions during;during.cancelled=[&]{return stop;};StreamExportLimits one;one.stripeRows=1;rejects([&]{exportChannelPackAtomic(d,target,sources,during,one,[&](uint32_t,uint32_t){stop=true;});});REQUIRE(stop&&bytes(target)==saved);sources[0]="Lmissing";rejects([&]{exportChannelPackAtomic(d,target,sources);});REQUIRE(bytes(target)==saved&&d==before);sources[0]="L"+layer.id;ExportOptions options;options.cancelled=[] {return true;};rejects([&]{exportChannelPackAtomic(d,target,sources,options);});REQUIRE(bytes(target)==saved&&d==before);only(temp,1);

 // The group retains each child's visibility, while a clipped source still
 // uses its isolated, hidden base as an alpha dependency.
 auto stack=blank(2,2);Layer group;group.id=newId();group.name="Group";group.group=true;group.visible=false;group.transform={0,0,2,2};
 Layer visible;visible.id=newId();visible.name="Visible";visible.parentId=group.id;visible.transform={0,0,2,2};visible.raster=Raster::filled(2,2,{255,255,255,255});
 Layer hidden=visible;hidden.id=newId();hidden.name="Hidden";hidden.visible=false;hidden.raster=Raster::filled(2,2,{0,0,0,255});
 Layer base;base.id=newId();base.name="Base";base.transform={0,0,2,2};const uint8_t coverage[]{0,0,0,128,0,0,0,0,0,0,0,128,0,0,0,0};base.raster=Raster::fromRgba(2,2,coverage,8);
 Layer clipped;clipped.id=newId();clipped.name="Clipped";clipped.transform={0,0,2,2};clipped.raster=Raster::filled(2,2,{255,255,255,255});clipped.maskSourceId=base.id;
 stack.layers={group,visible,hidden,base,clipped};const auto stackBefore=stack;
 std::array<std::string,4> stackSources{"L"+group.id,"L"+clipped.id,"white","white"};
 for(uint32_t stripe:{1U,2U}){StreamExportLimits limits;limits.stripeRows=stripe;exportChannelPackAtomic(stack,target,stackSources,{},limits);auto decoded=WicCodec::decode(target);for(int y=0;y<2;++y){REQUIRE(pixel(decoded.image,0,y)==Pixel{255,128,255,255});REQUIRE(pixel(decoded.image,1,y)==Pixel{255,0,255,255});}REQUIRE(stack==stackBefore);}
}
void jpeg_matte_preview(){QTemporaryDir temp;auto d=blank(20,12);for(bool blue:{false,true}){ExportOptions options;options.format=ImageFormat::Jpeg;options.jpegQuality=blue?1:.85;options.matteR=options.matteG=blue?0:255;options.matteB=255;auto target=path(temp,blue?"blue.jpg":"white.jpg");auto result=exportDocumentAtomic(d,target,options);auto decoded=WicCodec::decode(target);REQUIRE(decoded.image.width==20&&decoded.image.height==12&&result.preview.width==20&&result.preview.height==12);auto p=pixel(decoded.image,0,0);REQUIRE(p.a==255&&std::abs(int(p.r)-options.matteR)<8&&std::abs(int(p.g)-options.matteG)<8&&std::abs(int(p.b)-options.matteB)<8);REQUIRE(result.preview.pixels==decoded.image.pixels);metadata(target,ImageFormat::Jpeg);}
 auto target=path(temp,"partial.jpg"),old=path(temp,"old.jpg");RgbaImage rgba{40,23,160,std::vector<uint8_t>(40*23*4)};pattern()(0,23,rgba.pixels,160);ExportOptions options;options.format=ImageFormat::Jpeg;options.matteR=32;options.matteG=128;options.matteB=220;auto result=encodeRowsAtomic(target,40,23,pattern(),options);WicCodec::encode(old,rgba,options);REQUIRE(WicCodec::decode(target).image.pixels==WicCodec::decode(old).image.pixels&&result.preview.pixels==WicCodec::decode(target).image.pixels);}
void jpeg_modes(){
 QTemporaryDir temp;cmyk_test::ProfileFile fixture;auto profile=readCmykProfile(std::filesystem::path(fixture.path.toStdWString()));
 for(bool cmyk:{false,true}){std::vector<std::vector<uint8_t>> images;std::array<size_t,3> sizes{};int n=0;
  for(auto mode:{JpegMode::Standard,JpegMode::Optimized,JpegMode::Progressive}){ExportOptions options;options.format=ImageFormat::Jpeg;options.jpegMode=mode;options.dpi=144;if(cmyk)options.cmykProfile=profile;auto target=path(temp,("mode-"+std::to_string(n)+".jpg").c_str());
   auto rows=[](uint32_t y,uint32_t count,std::span<uint8_t> out,size_t stride){for(uint32_t row=0;row<count;++row)for(size_t x=0;x<stride/4;++x){auto* p=out.data()+row*stride+x*4;p[0]=uint8_t(x);p[1]=uint8_t(y+row);p[2]=uint8_t((x+y+row)/2);p[3]=255;}};
   auto result=encodeRowsAtomic(target,256,256,rows,options);auto file=bytes(target);REQUIRE(file.size()>4&&file[0]==0xff&&file[1]==0xd8);int sof=0;size_t pos=2;while(pos+3<file.size()){REQUIRE(file[pos++]==0xff);while(pos<file.size()&&file[pos]==0xff)++pos;REQUIRE(pos<file.size());const auto marker=file[pos++];if(marker==0xda||marker==0xd9)break;REQUIRE(pos+1<file.size());const size_t length=(size_t(file[pos])<<8)|file[pos+1];REQUIRE(length>=2&&pos+length<=file.size());if(marker==0xc0||marker==0xc2)sof=marker;pos+=length;}REQUIRE(sof==(mode==JpegMode::Progressive?0xc2:0xc0)&&result.encodedBytes==file.size());auto decoded=WicCodec::decode(target);REQUIRE(decoded.image.width==256&&decoded.image.height==256&&std::abs(decoded.metadata.dpiX-144)<1&&std::abs(decoded.metadata.dpiY-144)<1);if(cmyk)REQUIRE(decoded.metadata.cmykProfile&&decoded.metadata.cmykProfile->sha256==profile->sha256);images.push_back(decoded.image.pixels);sizes[n++]=file.size();
  }
  std::vector<uint8_t> expected(256*256*4);for(unsigned y=0;y<256;++y)for(unsigned x=0;x<256;++x){auto* p=expected.data()+(size_t(y)*256+x)*4;p[0]=uint8_t(x);p[1]=uint8_t(y);p[2]=uint8_t((x+y)/2);p[3]=255;}if(cmyk){CmykTransform transform(profile);std::vector<uint8_t> ink(expected.size()),roundtrip(expected.size());transform.encode(expected,ink,{255,255,255});transform.decode(ink,roundtrip);expected=std::move(roundtrip);}
  for(const auto& image:images){uint64_t difference=0;for(size_t i=0;i<image.size();++i)difference+=std::abs(int(expected[i])-int(image[i]));REQUIRE(double(difference)/image.size()<12.);}
  for(size_t k=1;k<images.size();++k){uint64_t difference=0;for(size_t i=0;i<images[0].size();++i)difference+=std::abs(int(images[0][i])-int(images[k][i]));REQUIRE(double(difference)/images[0].size()<12.);}
  REQUIRE(sizes[1]<=sizes[0]);
 }
 auto target=path(temp,"budget.jpg");original(target);const auto saved=bytes(target);ExportOptions tooLarge;tooLarge.format=ImageFormat::Jpeg;tooLarge.jpegMode=JpegMode::Progressive;tooLarge.cmykProfile=profile;int calls=0;rejects([&]{encodeRowsAtomic(target,10000,10000,[&](uint32_t,uint32_t,std::span<uint8_t>,size_t){++calls;},tooLarge);});REQUIRE(calls==0&&bytes(target)==saved);
 ExportOptions invalid;invalid.format=ImageFormat::Jpeg;invalid.jpegMode=static_cast<JpegMode>(999);rejects([&]{encodeRowsAtomic(target,32,32,pattern(),invalid);});REQUIRE(bytes(target)==saved);
 for(auto mode:{JpegMode::Standard,JpegMode::Optimized,JpegMode::Progressive}){bool stop=false;ExportOptions options;options.format=ImageFormat::Jpeg;options.jpegMode=mode;options.cancelled=[&]{return stop;};StreamExportLimits limits;limits.stripeRows=1;rejects([&]{encodeRowsAtomic(target,32,32,pattern(),options,limits,[&](uint32_t,uint32_t){stop=true;});});REQUIRE(stop&&bytes(target)==saved);}
 for(auto mode:{JpegMode::Standard,JpegMode::Optimized,JpegMode::Progressive})for(double quality:{0.,1.}){ExportOptions options;options.format=ImageFormat::Jpeg;options.jpegMode=mode;options.jpegQuality=quality;encodeRowsAtomic(target,32,32,pattern(),options);auto decoded=WicCodec::decode(target);REQUIRE(decoded.image.width==32&&decoded.image.height==32);}
}
void large_jpeg_mode(JpegMode mode){QTemporaryDir temp;auto d=blank(10000,10000);ExportOptions options;options.format=ImageFormat::Jpeg;options.jpegMode=mode;auto target=path(temp,"large.jpg");const auto start=std::chrono::steady_clock::now();auto result=exportDocumentAtomic(d,target,options);PROCESS_MEMORY_COUNTERS_EX memory{};memory.cb=sizeof(memory);REQUIRE(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory)));std::cout<<"jpeg mode="<<int(mode)<<" pixels=100000000 bytes="<<result.encodedBytes<<" peak_working_set="<<memory.PeakWorkingSetSize<<" peak_commit="<<memory.PeakPagefileUsage<<" elapsed_ms="<<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count()<<"\n";}
void cancel_atomic(){QTemporaryDir temp;auto target=path(temp,"cancel.png");original(target);auto before=bytes(target);bool stop=false;ExportOptions options;options.cancelled=[&]{return stop;};int calls=0;bool wasCancelled=false;try{encodeRowsAtomic(target,50,200,[&](uint32_t,uint32_t,std::span<uint8_t> data,size_t){++calls;std::fill(data.begin(),data.end(),0);},options,{},[&](uint32_t,uint32_t){stop=true;});}catch(const ExportCancelled&){wasCancelled=true;}REQUIRE(wasCancelled&&calls==1&&bytes(target)==before);only(temp,1);}
void row_failure_atomic(){
 QTemporaryDir temp;auto target=path(temp,"failure.png");original(target);auto before=bytes(target);
 rejects([&]{encodeRowsAtomic(target,30,200,[](uint32_t y,uint32_t,std::span<uint8_t> data,size_t){if(y)throw std::runtime_error("injected row failure");std::fill(data.begin(),data.end(),0);});});REQUIRE(bytes(target)==before);
 rejects([&]{encodeRowsAtomic(target,1,1,[](uint32_t,uint32_t,std::span<uint8_t> data,size_t){data[0]=255;data[1]=data[2]=data[3]=0;});});REQUIRE(bytes(target)==before);
 for(auto format:{ImageFormat::Exr,ImageFormat::Pdf,ImageFormat(999)}){ExportOptions option;option.format=format;rejects([&]{encodeRowsAtomic(target,10,10,pattern(),option);});}
 for(double dpi:{0.,-1.,std::numeric_limits<double>::infinity()}){ExportOptions option;option.dpi=dpi;rejects([&]{encodeRowsAtomic(target,10,10,pattern(),option);});}
 ExportOptions nan;nan.jpegQuality=std::numeric_limits<double>::quiet_NaN();rejects([&]{encodeRowsAtomic(target,10,10,pattern(),nan);});
 auto invalid=StreamExportLimits{};invalid.maxBufferBytes=10;rejects([&]{encodeRowsAtomic(target,10,10,pattern(),{},invalid);});
 auto directory=path(temp,"directory");std::filesystem::create_directory(directory);original(directory/"project.comp");auto protectedBytes=bytes(directory/"project.comp");
 rejects([&]{encodeRowsAtomic(directory,10,10,pattern());});REQUIRE(std::filesystem::is_directory(directory)&&bytes(directory/"project.comp")==protectedBytes&&bytes(target)==before);only(temp,2);
 // Empty image layers with masks still export transparency. A malformed live
 // effect must remain an error even though its input has no image pixels.
 auto empty=blank(19,17);Layer emptyLayer;emptyLayer.id=newId();emptyLayer.name="Empty masked layer";emptyLayer.mask=Mask{std::make_shared<GrayRaster>(GrayRaster{1,1,{128}})};empty.layers.push_back(emptyLayer);
 exportDocumentAtomic(empty,target);auto blankImage=WicCodec::decode(target);REQUIRE(std::all_of(blankImage.image.pixels.begin(),blankImage.image.pixels.end(),[](uint8_t value){return value==0;}));const auto savedBlank=bytes(target);
 empty.layers[0].adjustmentJson="{\"kind\":\"Unsupported test effect\"}";rejects([&]{exportDocumentAtomic(empty,target);});REQUIRE(bytes(target)==savedBlank);
 empty.layers[0].adjustmentJson.clear();empty.height=200;bool cancelledBlank=false;ExportOptions blankOptions;blankOptions.cancelled=[&]{return cancelledBlank;};rejects([&]{exportDocumentAtomic(empty,target,blankOptions,{},[&](uint32_t,uint32_t){cancelledBlank=true;});});REQUIRE(bytes(target)==savedBlank);only(temp,2);
}
void copy_atomic(){
 QTemporaryDir temp;auto source=path(temp,"source.bin"),target=path(temp,"target.bin");{std::ofstream out(source,std::ios::binary);std::vector<char> block(1024*1024,'a');for(int i=0;i<4;++i){block[0]=char(i);out.write(block.data(),std::streamsize(block.size()));}}original(target);auto before=bytes(target);
 int polls=0;bool stopped=false;try{copyEncodedAtomic(source,target,[&]{return ++polls==3;});}catch(const ExportCancelled&){stopped=true;}REQUIRE(stopped&&bytes(target)==before);only(temp,2);
 rejects([&]{copyEncodedAtomic(path(temp,"absent.bin"),target);});REQUIRE(bytes(target)==before);only(temp,2);
 auto directory=path(temp,"directory");std::filesystem::create_directory(directory);original(directory/"project.comp");auto protectedBytes=bytes(directory/"project.comp");
 rejects([&]{copyEncodedAtomic(directory,target);});rejects([&]{copyEncodedAtomic(source,directory);});REQUIRE(bytes(target)==before&&bytes(directory/"project.comp")==protectedBytes);only(temp,3);
 copyEncodedAtomic(source,target);REQUIRE(bytes(source)==bytes(target));only(temp,3);
}
void large(bool narrow){QTemporaryDir temp;auto d=blank(narrow?30000:10000,narrow?3333:10000);auto before=d;Raster::resetMaterializationCount();auto start=std::chrono::steady_clock::now();for(auto format:{ImageFormat::Png,ImageFormat::Jpeg}){ExportOptions options;options.format=format;auto file=path(temp,format==ImageFormat::Png?"large.png":"large.jpg");auto result=exportDocumentAtomic(d,file,options);REQUIRE(result.preview.width<=520&&result.preview.height<=320&&!result.preview.pixels.empty());REQUIRE(result.peakOwnedPixelBytes+result.maxRenderPixelBytes<64*1024*1024);auto p=pixel(result.preview,0,0);REQUIRE(p== (format==ImageFormat::Png?Pixel{}:Pixel{255,255,255,255}));metadata(file,format);std::cout<<"format="<<(format==ImageFormat::Png?"png":"jpeg")<<" width="<<d.width<<" height="<<d.height<<" bytes="<<result.encodedBytes<<" owned_pixel_bytes="<<result.peakOwnedPixelBytes<<" render_bound="<<result.maxRenderPixelBytes<<"\n";}PROCESS_MEMORY_COUNTERS_EX memory{};memory.cb=sizeof(memory);REQUIRE(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory)));std::cout<<"peak_working_set="<<memory.PeakWorkingSetSize<<" peak_commit="<<memory.PeakPagefileUsage<<" elapsed_ms="<<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count()<<"\n";std::cout.flush();REQUIRE(memory.PeakWorkingSetSize<256*1024*1024&&memory.PeakPagefileUsage<256*1024*1024);REQUIRE(d==before&&Raster::materializationCount()==0);}
// ---- PDF export. The reader below knows just enough of the file to check what the exporter promises: a valid cross-reference
// table, one page of the expected size, and the pixels of its single image, which it rewraps as a PNG for WIC to decode.
uint32_t crc32(const uint8_t* data,size_t size){static const auto table=[]{std::array<uint32_t,256> t{};for(uint32_t n=0;n<256;++n){uint32_t c=n;for(int k=0;k<8;++k)c=c&1?0xEDB88320u^(c>>1):c>>1;t[n]=c;}return t;}();uint32_t c=0xFFFFFFFFu;for(size_t i=0;i<size;++i)c=table[(c^data[i])&0xFF]^(c>>8);return c^0xFFFFFFFFu;}
void pngChunk(std::vector<uint8_t>& out,const char* type,const std::vector<uint8_t>& data){auto be=[&](uint32_t v){for(int s=24;s>=0;s-=8)out.push_back(uint8_t(v>>s));};be(uint32_t(data.size()));const auto start=out.size();out.insert(out.end(),type,type+4);out.insert(out.end(),data.begin(),data.end());be(crc32(out.data()+start,data.size()+4));}
struct PdfPage{double width{},height{};int pages{};bool lossless{};RgbaImage image;};
PdfPage readPdf(const std::filesystem::path& file){
 const auto data=bytes(file);const std::string s(data.begin(),data.end());PdfPage page;const auto npos=std::string::npos;
 REQUIRE(s.rfind("%PDF-",0)==0&&s.size()>5&&s.compare(s.size()-6,5,"%%EOF")==0);
 // The cross-reference table must point at each object, and startxref at the table.
 const auto start=s.rfind("startxref\n");REQUIRE(start!=npos);const auto xref=size_t(std::stoull(s.substr(start+10)));REQUIRE(s.compare(xref,5,"xref\n")==0&&s.compare(xref+5,2,"0 ")==0);
 const auto count=std::stoi(s.substr(xref+7));REQUIRE(count==7&&s.find("/Size 7")!=npos);
 for(int id=1;id<count;++id){const auto entry=s.substr(xref+9+size_t(id)*20+0,20);REQUIRE(entry.size()==20&&entry.substr(10)==" 00000 n \n");const auto offset=size_t(std::stoull(entry.substr(0,10)));REQUIRE(s.compare(offset,std::to_string(id).size()+6,std::to_string(id)+" 0 obj")==0);}
 for(size_t at=0;(at=s.find("/Type /Page ",at))!=npos;++at)++page.pages;
 const auto box=s.find("/MediaBox [0 0 ");REQUIRE(box!=npos);size_t used=0;const auto boxText=s.substr(box+15,80);page.width=std::stod(boxText,&used);page.height=std::stod(boxText.substr(used));REQUIRE(boxText.find(']')!=npos);
 const auto image=s.find("/Subtype /Image");REQUIRE(image!=npos);const auto number=[&](const char* key){const auto at=s.find(key,image);REQUIRE(at!=npos);return std::stoul(s.substr(at+strlen(key)));};
 const auto width=uint32_t(number("/Width ")),height=uint32_t(number("/Height ")),length=uint32_t(number("/Length "));
 page.lossless=s.find("/Filter /FlateDecode",image)!=npos&&s.find("/Predictor 15",image)!=npos&&s.find("/Colors 3",image)!=npos&&s.find("DCTDecode")==npos;
 const auto streamAt=s.find("stream\n",image);REQUIRE(streamAt!=npos);const auto payload=streamAt+7;REQUIRE(payload+length<=data.size());
 std::vector<uint8_t> png{0x89,'P','N','G',0x0D,0x0A,0x1A,0x0A},ihdr;for(auto v:{width,height})for(int shift=24;shift>=0;shift-=8)ihdr.push_back(uint8_t(v>>shift));ihdr.insert(ihdr.end(),{8,2,0,0,0});
 pngChunk(png,"IHDR",ihdr);pngChunk(png,"IDAT",std::vector<uint8_t>(data.begin()+std::ptrdiff_t(payload),data.begin()+std::ptrdiff_t(payload+length)));pngChunk(png,"IEND",{});
 const auto rebuilt=std::filesystem::path(file.native()+L".image.png");{std::ofstream out(rebuilt,std::ios::binary);out.write(reinterpret_cast<const char*>(png.data()),std::streamsize(png.size()));}
 page.image=WicCodec::decode(rebuilt).image;std::filesystem::remove(rebuilt);return page;
}
// The matte formula the JPEG export uses, applied to a canvas rendered by the document's own renderer.
RgbaImage mattedExpectation(const Document& d,std::array<uint8_t,3> matte){
 const auto source=SoftwareRenderer().render(d,0,0,d.width,d.height)->rgba();RgbaImage out{uint32_t(d.width),uint32_t(d.height),size_t(d.width)*4,std::vector<uint8_t>(source.size())};
 for(size_t i=0;i<source.size();i+=4){for(int c=0;c<3;++c)out.pixels[i+size_t(c)]=uint8_t(std::min(255u,unsigned(source[i+size_t(c)])+(unsigned(matte[size_t(c)])*(255-source[i+3])+127)/255));out.pixels[i+3]=255;}
 return out;
}
Document translucentLeft(int width,int height,double dpi){
 // Pixels (partly transparent) fill the left half only, so the right half is transparent canvas.
 auto d=blank(width,height);d.resolution=dpi;Layer layer;layer.id=newId();layer.name="Left half";layer.transform={0,0,double(width/2),double(height)};
 auto source=std::vector<uint8_t>(size_t(width/2)*size_t(height)*4);pattern()(0,uint32_t(height),source,size_t(width/2)*4);layer.raster=Raster::fromRgba(width/2,height,source.data(),size_t(width/2)*4);d.layers.push_back(layer);return d;
}
void pdf_page_size(){
 QTemporaryDir temp;struct Case{int width,height;double dpi,scale;uint32_t outWidth,outHeight;};
 for(auto c:{Case{300,200,150,1,300,200},Case{1031,131,72,1,1031,131},Case{300,200,300,.5,150,100},Case{97,41,96,2,194,82},Case{64,48,96,.01,1,1}}){
  auto d=blank(c.width,c.height);d.resolution=c.dpi;ExportOptions options;options.format=ImageFormat::Pdf;options.scale=c.scale;auto target=path(temp,"page.pdf");
  auto result=exportDocumentAtomic(d,target,options);auto page=readPdf(target);
  REQUIRE(page.pages==1&&page.lossless);REQUIRE(page.image.width==c.outWidth&&page.image.height==c.outHeight);
  // Print size is the exported pixels at the document's resolution, in PDF points (1/72 in), whatever the scale.
  REQUIRE(std::abs(page.width-c.outWidth*72./c.dpi)<.001&&std::abs(page.height-c.outHeight*72./c.dpi)<.001);
  REQUIRE(std::abs(page.width/page.height-double(c.outWidth)/c.outHeight)<.001);
  REQUIRE(result.encodedBytes==bytes(target).size()&&result.preview.width==std::min(c.outWidth,520u)&&result.preview.height<=std::min(c.outHeight,320u)&&result.preview.height>0);
  only(temp,1);
 }
 // Scale never leaves the export limits, and never changes the proportions by more than a pixel.
 REQUIRE(scaledExportSize(30000,3333,2).width==30000&&scaledExportSize(10000,10000,2).width==10000&&scaledExportSize(10000,10000,2).height==10000);
 const auto wide=scaledExportSize(20000,4000,3);REQUIRE(uint64_t(wide.width)*wide.height<=100000000&&wide.width<=30000&&std::abs(double(wide.width)/wide.height-5.)<.01);
 REQUIRE(scaledExportSize(1000,800,1).width==1000&&scaledExportSize(1000,800,.5).height==400);
 for(double scale:{0.,-1.,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()})rejects([&]{scaledExportSize(10,10,scale);});
 // A document's own print size is kept as pixels / resolution even at 100%.
 auto d=blank(150,100);d.resolution=300;ExportOptions options;options.format=ImageFormat::Pdf;auto target=path(temp,"print.pdf");exportDocumentAtomic(d,target,options);auto page=readPdf(target);REQUIRE(std::abs(page.width-36)<.001&&std::abs(page.height-24)<.001);
}
void pdf_scale_background(){
 QTemporaryDir temp;auto d=translucentLeft(64,40,96);const auto before=d;auto target=path(temp,"background.pdf");
 const auto exportAt=[&](double scale,std::array<uint8_t,3> matte){ExportOptions options;options.format=ImageFormat::Pdf;options.scale=scale;options.matteR=matte[0];options.matteG=matte[1];options.matteB=matte[2];auto result=exportDocumentAtomic(d,target,options);return std::pair{readPdf(target),result};};
 // 100%: the file's pixels are the canvas over the background, exactly, and the preview shows the same.
 const std::array<uint8_t,3> blue{20,40,250};
 auto [full,fullResult]=exportAt(1,{255,255,255});const auto white=mattedExpectation(d,{255,255,255});
 REQUIRE(full.image.pixels==white.pixels&&fullResult.preview.pixels==white.pixels);
 auto [tinted,tintedResult]=exportAt(1,blue);const auto tintedExpected=mattedExpectation(d,blue);
 REQUIRE(tinted.image.pixels==tintedExpected.pixels&&tintedResult.preview.pixels==tintedExpected.pixels&&tinted.image.pixels!=full.image.pixels);
 REQUIRE(pixel(tinted.image,63,0)==Pixel{blue[0],blue[1],blue[2],255}&&pixel(full.image,63,0)==Pixel{255,255,255,255});   // transparent canvas shows the background
 // 50%: half the pixels and half the printed size; the background still fills what the layer leaves transparent, and the
 // preview is the exported page.
 auto [half,halfResult]=exportAt(.5,blue);
 REQUIRE(half.image.width==32&&half.image.height==20&&std::abs(half.width*2-full.width)<.001&&std::abs(half.height*2-full.height)<.001);
 REQUIRE(pixel(half.image,31,0)==Pixel{blue[0],blue[1],blue[2],255}&&pixel(half.image,31,19)==Pixel{blue[0],blue[1],blue[2],255}&&pixel(half.image,2,10)!=Pixel{blue[0],blue[1],blue[2],255});
 REQUIRE(halfResult.preview.width==32&&halfResult.preview.pixels==half.image.pixels);
 // Resampling is premultiplied: a fully transparent neighbour adds no color to an opaque edge, so no pixel is darker than the
 // background-only or layer-only extremes.
 for(size_t i=0;i<half.image.pixels.size();i+=4)REQUIRE(half.image.pixels[i+3]==255);
 // 200%: the page doubles.
 auto [doubled,doubledResult]=exportAt(2,blue);REQUIRE(doubled.image.width==128&&doubled.image.height==80&&std::abs(doubled.width-full.width*2)<.001);
 REQUIRE(pixel(doubled.image,127,79)==Pixel{blue[0],blue[1],blue[2],255});
 // The document is a snapshot; nothing about it changed.
 REQUIRE(d==before);only(temp,1);
}
void pdf_scale_stripes(){
 // A scaled page does not depend on the stripe size: every stripe reads the same resampled rows a single stripe would.
 QTemporaryDir temp;auto d=translucentLeft(64,40,96);const std::array<uint8_t,3> blue{20,40,250};
 ExportOptions options;options.format=ImageFormat::Pdf;options.scale=.75;options.matteR=blue[0];options.matteG=blue[1];options.matteB=blue[2];
 auto reference=path(temp,"reference.pdf");const auto referenceResult=exportDocumentAtomic(d,reference,options);const auto expected=readPdf(reference);
 REQUIRE(expected.image.width==48&&expected.image.height==30);
 for(uint32_t stripe:{1U,7U}){StreamExportLimits limits;limits.stripeRows=stripe;auto target=path(temp,("stripes-"+std::to_string(stripe)+".pdf").c_str());auto result=exportDocumentAtomic(d,target,options,limits);auto page=readPdf(target);REQUIRE(page.image.pixels==expected.image.pixels&&result.preview.pixels==referenceResult.preview.pixels);}
 only(temp,3);
}
void pdf_cancel_atomic(){
 QTemporaryDir temp;auto target=path(temp,"keep.pdf");original(target);const auto before=bytes(target);auto d=translucentLeft(64,40,72);
 // Cancelled at every stage, with and without scaling: while rendering rows, after the second progress report, and after the last one.
 for(double scale:{1.,.5})for(int stage=0;stage<3;++stage){
  bool stop=false;int rowCalls=0,progressCalls=0;ExportOptions options;options.format=ImageFormat::Pdf;options.scale=scale;options.cancelled=[&]{return stop;};
  StreamExportLimits limits;limits.stripeRows=8;bool wasCancelled=false;
  try{encodePdfAtomic(target,64,40,[&](uint32_t,uint32_t,std::span<uint8_t> out,size_t){if(stage==0&&++rowCalls==2)stop=true;std::fill(out.begin(),out.end(),0);},options,limits,[&](uint32_t done,uint32_t total){++progressCalls;if((stage==1&&progressCalls==2)||(stage==2&&done==total))stop=true;});}
  catch(const ExportCancelled&){wasCancelled=true;}
  REQUIRE(wasCancelled&&bytes(target)==before);only(temp,1);
 }
 // A failing export leaves the destination alone too: bad rows, bad destination, bad settings.
 rejects([&]{ExportOptions options;options.format=ImageFormat::Pdf;encodePdfAtomic(target,8,8,[](uint32_t,uint32_t,std::span<uint8_t> out,size_t){out[0]=255;out[1]=out[2]=out[3]=0;},options);});REQUIRE(bytes(target)==before);
 rejects([&]{ExportOptions options;options.format=ImageFormat::Pdf;options.scale=.5;encodePdfAtomic(target,8,8,[](uint32_t,uint32_t,std::span<uint8_t> out,size_t){out[0]=255;out[1]=out[2]=out[3]=0;},options);});REQUIRE(bytes(target)==before);
 rejects([&]{ExportOptions options;options.format=ImageFormat::Pdf;options.scale=std::numeric_limits<double>::quiet_NaN();exportDocumentAtomic(d,target,options);});
 rejects([&]{ExportOptions options;options.format=ImageFormat::Png;encodePdfAtomic(target,8,8,pattern(),options);});
 rejects([&]{ExportOptions options;options.format=ImageFormat::Pdf;options.dpi=0;encodePdfAtomic(target,8,8,pattern(),options);});
 rejects([&]{ExportOptions options;options.format=ImageFormat::Pdf;encodePdfAtomic(target,30001,1,pattern(),options);});
 auto directory=path(temp,"directory.pdf");std::filesystem::create_directory(directory);original(directory/"project.comp");const auto protectedBytes=bytes(directory/"project.comp");
 ExportOptions options;options.format=ImageFormat::Pdf;rejects([&]{exportDocumentAtomic(d,directory,options);});REQUIRE(std::filesystem::is_directory(directory)&&bytes(directory/"project.comp")==protectedBytes&&bytes(target)==before);only(temp,2);
 // The WIC encoders do not take PDF.
 rejects([&]{encodeRowsAtomic(target,10,10,pattern(),options);});REQUIRE(bytes(target)==before);only(temp,2);
 // Finally an uncancelled export does replace the file.
 exportDocumentAtomic(d,target,options);REQUIRE(bytes(target)!=before&&readPdf(target).pages==1);only(temp,2);
}
void large_pdf(){
 // Unscaled PDF export streams in stripes like PNG and JPEG, so a very large canvas stays inside the same memory bounds.
 QTemporaryDir temp;auto d=blank(10000,10000);d.resolution=300;Raster::resetMaterializationCount();ExportOptions options;options.format=ImageFormat::Pdf;auto file=path(temp,"large.pdf");
 auto result=exportDocumentAtomic(d,file,options);REQUIRE(result.preview.width<=520&&result.preview.height<=320&&!result.preview.pixels.empty());REQUIRE(result.peakOwnedPixelBytes+result.maxRenderPixelBytes<64*1024*1024);REQUIRE(pixel(result.preview,0,0)==Pixel{255,255,255,255});
 const auto text=bytes(file);const std::string s(text.begin(),text.end());REQUIRE(s.find("/MediaBox [0 0 2400.0000 2400.0000]")!=std::string::npos&&s.find("/Width 10000")!=std::string::npos&&result.encodedBytes==text.size());
 PROCESS_MEMORY_COUNTERS_EX memory{};memory.cb=sizeof(memory);REQUIRE(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory)));
 std::cout<<"format=pdf bytes="<<result.encodedBytes<<" owned_pixel_bytes="<<result.peakOwnedPixelBytes<<" render_bound="<<result.maxRenderPixelBytes<<" peak_working_set="<<memory.PeakWorkingSetSize<<" peak_commit="<<memory.PeakPagefileUsage<<"\n";
 REQUIRE(memory.PeakWorkingSetSize<256*1024*1024&&memory.PeakPagefileUsage<256*1024*1024&&Raster::materializationCount()==0);
}

void lens_partition(){
    QTemporaryDir temp;auto d=blank(513,53);Layer photo;photo.id=newId();photo.name="Photo";photo.transform={0,0,513,53};
    std::vector<uint8_t> pixels(513*53*4,0);for(size_t i=3;i<pixels.size();i+=4)pixels[i]=255;
    for(int y=16;y<28;++y)for(int x=5;x<17;++x)for(int c=0;c<3;++c)pixels[(size_t(y)*513+x)*4+c]=255;
    photo.raster=Raster::fromRgba(513,53,pixels.data(),513*4);d.layers.push_back(photo);
    Layer lens;lens.id=newId();lens.name="Lens Effects";lens.transform=photo.transform;lens.opacity=.7;
    lens.mask=Mask{std::make_shared<GrayRaster>(GrayRaster{1,1,{170}}),true,false,Transform{0,0,513,53}};
    for(const char* mode:{"Anamorphic Flare","Prism"}){
        auto json=QJsonDocument::fromJson(QByteArray::fromStdString(effects::defaultAdjustmentJson("Lens Effects"))).object();
        auto group=json["filterLayerSettings"].toObject(),settings=group["lens"].toObject();settings["effect"]=mode;group["lens"]=settings;json["filterLayerSettings"]=group;
        lens.adjustmentJson=QJsonDocument(json).toJson(QJsonDocument::Compact).toStdString();d.layers.resize(1);d.layers.push_back(lens);
        if(std::string_view(mode)=="Prism"){Layer blur;blur.id=newId();blur.name="Gaussian Blur";blur.transform=photo.transform;blur.adjustmentJson=effects::defaultAdjustmentJson("Gaussian Blur");d.layers.push_back(blur);}
        auto expected=SoftwareRenderer().render(d,0,0,d.width,d.height)->rgba();
        for(uint32_t rows:{1U,7U,53U}){StreamExportLimits limits;limits.stripeRows=rows;auto target=path(temp,("lens-"+std::string(mode)+"-"+std::to_string(rows)+".png").c_str());exportDocumentAtomic(d,target,{},limits);REQUIRE(WicCodec::decode(target).image.pixels==expected);}
        ExportOptions exr;exr.format=ImageFormat::Exr;StreamExportLimits stripes;stripes.stripeRows=7;
        auto target=path(temp,("lens-"+std::string(mode)+".exr").c_str());exportDocumentAtomic(d,target,exr,stripes);
        auto decoded=decodeExr(target);for(int y=0;y<d.height;++y)for(int x=0;x<d.width;++x){auto p=decoded->pixel(x,y);for(int c=0;c<4;++c)REQUIRE(std::abs(p[c]-expected[(size_t(y)*d.width+x)*4+c]/255.f)<.02f);}
    }
}
void lens_inactive_large(){
    QTemporaryDir temp;auto d=blank(2500,2001);Layer photo;photo.id=newId();photo.name="Photo";photo.transform={0,0,1,1};photo.raster=Raster::filled(1,1,{128,80,40,255});d.layers.push_back(photo);
    Layer group;group.id=newId();group.name="Group";group.group=true;
    Layer lens;lens.id=newId();lens.name="Lens Effects";lens.transform={0,0,2500,2001};lens.adjustmentJson=effects::defaultAdjustmentJson("Lens Effects");lens.parentId=group.id;
    d.layers.push_back(group);d.layers.push_back(lens);
    for(int mode=0;mode<3;++mode){d.layers[1].visible=mode!=0;d.layers[1].opacity=mode==1?0:1;d.layers[2].opacity=mode==2?0:1;
        auto target=path(temp,("inactive-"+std::to_string(mode)+".png").c_str());exportDocumentAtomic(d,target);auto decoded=WicCodec::decode(target).image;REQUIRE(pixel(decoded,0,0)==Pixel{128,80,40,255});}
}
void tonal_partition(){
    QTemporaryDir temp;auto d=blank(513,53);Layer photo;photo.id=newId();photo.name="Photo";photo.transform={0,0,513,53};
    std::vector<uint8_t> pixels(513*53*4);for(int y=0;y<53;++y)for(int x=0;x<513;++x){auto i=size_t(y*513+x)*4;pixels[i]=pixels[i+1]=pixels[i+2]=uint8_t(40+(x*3+y*5)%160);pixels[i+3]=255;}photo.raster=Raster::fromRgba(513,53,pixels.data(),513*4);d.layers.push_back(photo);
    Layer v;v.id=newId();v.name="Tonal Contrast";v.transform=photo.transform;auto json=QJsonDocument::fromJson(QByteArray::fromStdString(effects::defaultAdjustmentJson("Tonal Contrast"))).object();auto group=json["filterLayerSettings"].toObject();auto settings=group["tonal"].toObject();settings["radius"]=4;settings["amount"]=90;group["tonal"]=settings;json["filterLayerSettings"]=group;v.adjustmentJson=QJsonDocument(json).toJson(QJsonDocument::Compact).toStdString();v.mask=Mask{std::make_shared<GrayRaster>(GrayRaster{1,1,{170}}),true,false,Transform{0,0,513,53}};v.opacity=.7;d.layers.push_back(v);
    const auto before=d;const auto expected=SoftwareRenderer().render(d,0,0,d.width,d.height)->rgba();REQUIRE(expected!=pixels);
    for(uint32_t rows:{1U,7U,53U}){StreamExportLimits limits;limits.stripeRows=rows;auto target=path(temp,("tonal-"+std::to_string(rows)+".png").c_str());exportDocumentAtomic(d,target,{},limits);auto decoded=WicCodec::decode(target).image;REQUIRE(decoded.pixels==expected&&d==before);}
}

void sharpen_partition(){
    QTemporaryDir temp;auto d=blank(513,53);Layer photo;photo.id=newId();photo.name="Photo";photo.transform={0,0,513,53};
    std::vector<uint8_t> pixels(513*53*4);for(int y=0;y<53;++y)for(int x=0;x<513;++x){auto i=size_t(y*513+x)*4;pixels[i]=pixels[i+1]=pixels[i+2]=uint8_t(40+(x*3+y*5)%160);pixels[i+3]=255;}photo.raster=Raster::fromRgba(513,53,pixels.data(),513*4);d.layers.push_back(photo);
    Layer v;v.id=newId();v.name="Sharpen";v.transform=photo.transform;auto json=QJsonDocument::fromJson(QByteArray::fromStdString(effects::defaultAdjustmentJson("Sharpen"))).object();auto group=json["filterLayerSettings"].toObject();auto settings=group["sharpen"].toObject();settings["radius"]=4;settings["amount"]=150;group["sharpen"]=settings;json["filterLayerSettings"]=group;v.adjustmentJson=QJsonDocument(json).toJson(QJsonDocument::Compact).toStdString();v.mask=Mask{std::make_shared<GrayRaster>(GrayRaster{1,1,{170}}),true,false,Transform{0,0,513,53}};v.opacity=.7;d.layers.push_back(v);
    const auto before=d;const auto expected=SoftwareRenderer().render(d,0,0,d.width,d.height)->rgba();REQUIRE(expected!=pixels);
    for(uint32_t rows:{1U,7U,53U}){StreamExportLimits limits;limits.stripeRows=rows;auto target=path(temp,("sharpen-"+std::to_string(rows)+".png").c_str());exportDocumentAtomic(d,target,{},limits);auto decoded=WicCodec::decode(target).image;REQUIRE(decoded.pixels==expected&&d==before);}
}

void vignette_partition(){
    QTemporaryDir temp;
    for(auto size:{std::pair{513,73},std::pair{73,513}}){
        auto d=blank(size.first,size.second);Layer photo;photo.id=newId();photo.name="Photo";photo.transform={0,0,double(d.width),double(d.height)};photo.raster=Raster::filled(d.width,d.height,{60,90,30,128});d.layers.push_back(photo);
        Layer v;v.id=newId();v.name="Vignette";v.transform=photo.transform;auto json=QJsonDocument::fromJson(QByteArray::fromStdString(effects::defaultAdjustmentJson("Vignette"))).object();auto group=json["filterLayerSettings"].toObject();auto settings=group["vignette"].toObject();settings["amount"]=79;settings["roundness"]=-30;settings["color"]=QJsonObject{{"red",.8},{"green",.2},{"blue",.4}};group["vignette"]=settings;json["filterLayerSettings"]=group;v.adjustmentJson=QJsonDocument(json).toJson(QJsonDocument::Compact).toStdString();v.mask=Mask{std::make_shared<GrayRaster>(GrayRaster{1,1,{170}}),true,false,Transform{0,0,double(d.width),double(d.height)}};v.opacity=.7;d.layers.push_back(v);
        const auto before=d;const auto expected=SoftwareRenderer().render(d,0,0,d.width,d.height)->rgba();
        for(uint32_t rows:{1U,7U,64U}){StreamExportLimits limits;limits.stripeRows=rows;auto target=path(temp,("vignette-"+std::to_string(d.width)+"-"+std::to_string(rows)+".png").c_str());exportDocumentAtomic(d,target,{},limits);auto decoded=WicCodec::decode(target).image;REQUIRE(decoded.pixels==expected&&d==before);}
    }
}

int main(int argc,char**argv){QCoreApplication app(argc,argv);const auto hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);try{REQUIRE(SUCCEEDED(hr));std::map<std::string,void(*)()> tests{{"sharpen_partition",sharpen_partition},{"lens_inactive_large",lens_inactive_large},{"lens_partition",lens_partition},{"tonal_partition",tonal_partition},{"vignette_partition",vignette_partition},{"channel_pack",channel_pack},{"extent_guard",extent_guard},{"row_png",row_png},{"png_options",png_options},{"large_png_interlaced",large_png_interlaced},{"deep_png_interlaced",deep_png_interlaced},{"canonical_partition",canonical_partition},{"jpeg_matte_preview",jpeg_matte_preview},{"jpeg_modes",jpeg_modes},{"jpeg_large_standard",[]{large_jpeg_mode(JpegMode::Standard);}},{"jpeg_large_optimized",[]{large_jpeg_mode(JpegMode::Optimized);}},{"jpeg_large_progressive",[]{large_jpeg_mode(JpegMode::Progressive);}},{"cancel_atomic",cancel_atomic},{"row_failure_atomic",row_failure_atomic},{"copy_atomic",copy_atomic},{"pdf_page_size",pdf_page_size},{"pdf_scale_background",pdf_scale_background},{"pdf_scale_stripes",pdf_scale_stripes},{"pdf_cancel_atomic",pdf_cancel_atomic},{"large_pdf",large_pdf},{"large_blank",[]{large(false);}},{"large_narrow",[]{large(true);}}};REQUIRE(argc==2&&tests.contains(argv[1]));tests.at(argv[1])();std::cout<<"PASS "<<argv[1]<<"\n";CoUninitialize();return 0;}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<"\n";if(SUCCEEDED(hr))CoUninitialize();return 1;}}
