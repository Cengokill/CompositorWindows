#include "ui/ImportActions.h"
#include "core/DocumentExport.h"
#include "../cmyk/Fixtures.h"
#include <QSettings>
#include "ui/WorkspaceDropQueue.h"
#include "ui/RawImportDialog.h"
#include "ui/PropertyControls.h"
#include "ui/MainWindow.h"
#include "editing/Shapes.h"
#include "imaging/psd/PsdDocument.h"
#include "imaging/wic_codec.h"
#include "imaging/hdr_codec.h"
#include "persistence/ProjectStore.h"
#include <QAction>
#include <QApplication>
#include <QDragEnterEvent>
#include <QDir>
#include <QDialog>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressDialog>
#include <QPushButton>
#include <QSlider>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextLayout>
#include <QTimer>
#include <QThread>
#include <QTreeWidget>
#include <knownfolders.h>
#include <shlobj.h>
#include <QUrl>
#include <atomic>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <string_view>
#include <vector>
using namespace compositor;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void until(const std::function<bool()>& ready){QElapsedTimer timer;timer.start();while(!ready()){if(timer.elapsed()>15000)throw std::runtime_error("Import test timed out");QTest::qWait(5);}QApplication::processEvents();}
std::filesystem::path path(const QString&s){return std::filesystem::path(s.toStdWString());}
QString image(const QString& directory,const QString& name,int width=64,int height=32){
    imaging::RgbaImage rgba{uint32_t(width),uint32_t(height),size_t(width)*4,std::vector<uint8_t>(size_t(width)*height*4)};
    for(int y=0;y<height;++y)for(int x=0;x<width/2;++x){auto*i=&rgba.pixels[size_t(y)*rgba.stride+x*4];i[0]=i[3]=255;}
    auto filename=directory+"/"+name;imaging::WicCodec::encode(path(filename),rgba);return filename;
}
Document document(int width=128,int height=128){Document d;d.id=newId();d.width=width;d.height=height;return d;}
struct Fixture {
    QObject target;ui::ImportState state;History history;bool blocked{},working{};int commits{},completions{},cancellations{};QStringList errors;
    ui::ImportQueue::Host host(){return {
        [this](QObject* who)->std::optional<ui::ImportState>{require(QThread::currentThread()==qApp->thread(),"Snapshot left GUI thread");return who==&target?std::optional(state):std::nullopt;},
        [this](QObject*){return blocked;},
        [this](QObject*,bool value){require(QThread::currentThread()==qApp->thread(),"Busy left GUI thread");working=value;},
        [this](QObject*,const ui::ImportResult&r){require(QThread::currentThread()==qApp->thread(),"Commit left GUI thread");if(state!=r.before)throw std::runtime_error("Destination changed");history.begin("Import Images",state.document,state.active);state=r.after;history.end(state.document,state.active);++commits;},
        [this](QObject*,const ui::ImportResult&r){require(QThread::currentThread()==qApp->thread(),"Completion left GUI thread");++completions;cancellations+=r.cancelled?1:0;errors.append(r.errors);}
    };}
};
bool drop(MainWindow&w,const QMimeData& mime,QPoint point){QDragEnterEvent enter(point,Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&w,&enter);if(!enter.isAccepted())return false;QDropEvent event(point,Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&w,&event);return event.isAccepted();}
void finish(MainWindow&w){auto*q=ui::ImportQueue::find(&w);require(q,"Missing actual UI import queue");until([&]{auto*d=ui::WorkspaceDropQueue::find(&w);return q->idle()&&(!d||d->idle());});for(auto*m:w.findChildren<QMessageBox*>())m->accept();QApplication::processEvents();}
QPoint canvasPoint(MainWindow&w,Point p){auto*c=w.canvas();const auto view=c->viewMapping().toView(p);return c->mapTo(&w,QPoint(int(std::round(view.x)),int(std::round(view.y))));}
enum class PsdFixtureCompression { Raw, PackBits, Zip, ZipPrediction, ZipFixedHuffman, Unsupported };
using PsdFixtureBlocks=std::vector<std::pair<std::string,std::vector<uint8_t>>>;
struct PsdFixtureLayer { int top{},left{},bottom{},right{}; std::array<std::vector<uint8_t>,3> channels; std::array<uint8_t,3> solidChannels{}; bool generatedSolid{}; PsdFixtureCompression compression{PsdFixtureCompression::Raw}; std::string name{"Fixture"}; uint8_t opacity{255}; bool hidden{}; PsdFixtureBlocks blocks; };
void psdU16(std::vector<uint8_t>& out,uint16_t value){out.push_back(uint8_t(value>>8));out.push_back(uint8_t(value));}
void psdU32(std::vector<uint8_t>& out,uint32_t value){for(int shift=24;shift>=0;shift-=8)out.push_back(uint8_t(value>>shift));}
void psdU64(std::vector<uint8_t>& out,uint64_t value){for(int shift=56;shift>=0;shift-=8)out.push_back(uint8_t(value>>shift));}
std::vector<uint8_t> packBitsRow(const uint8_t* data,size_t size){
    std::vector<uint8_t> out;size_t at=0;
    while(at<size){size_t run=1;while(at+run<size&&data[at+run]==data[at]&&run<128)++run;
        if(run>=3){out.push_back(uint8_t(int8_t(1-int(run))));out.push_back(data[at]);at+=run;continue;}
        const size_t begin=at;at+=run;
        while(at<size&&at-begin<128){size_t next=1;while(at+next<size&&data[at+next]==data[at]&&next<128)++next;if(next>=3)break;at+=std::min(next,size_t(128)-(at-begin));}
        const size_t count=at-begin;out.push_back(uint8_t(count-1));out.insert(out.end(),data+begin,data+at);
    }
    return out;
}
uint32_t zipAdler(const std::vector<uint8_t>& raw){uint32_t a=1,b=0;for(uint8_t value:raw){a=(a+value)%65521;b=(b+a)%65521;}return (b<<16)|a;}
std::vector<uint8_t> zipStored(const std::vector<uint8_t>& raw){
    std::vector<uint8_t> out{0x78,0x01};size_t at=0;
    do{const size_t count=std::min<size_t>(65535,raw.size()-at);const bool final=at+count==raw.size();out.push_back(final?1:0);const uint16_t n=uint16_t(count);out.push_back(uint8_t(n));out.push_back(uint8_t(n>>8));out.push_back(uint8_t(~n));out.push_back(uint8_t((~n)>>8));out.insert(out.end(),raw.begin()+at,raw.begin()+at+count);at+=count;}while(at<raw.size());
    psdU32(out,zipAdler(raw));return out;
}
struct ZipBits { std::vector<uint8_t> bytes; uint32_t bits{}; int count{}; void put(uint32_t value,int width){bits|=value<<count;count+=width;while(count>=8){bytes.push_back(uint8_t(bits));bits>>=8;count-=8;}} void putMsb(uint32_t code,int length){for(int i=length-1;i>=0;--i)put((code>>i)&1u,1);} void flush(){if(count){bytes.push_back(uint8_t(bits));bits=0;count=0;}} };
void zipFixedLiteral(ZipBits& bits,uint8_t value){if(value<144)bits.putMsb(0x30u+value,8);else bits.putMsb(0x190u+(value-144u),9);}
void zipFixedLength(ZipBits& bits,int length){
    static const int base[]={3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
    static const int extra[]={0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
    int index=0;while(index<28&&length>=base[index+1])++index;
    const int symbol=257+index;if(symbol<280)bits.putMsb(uint32_t(symbol-256),7);else bits.putMsb(0xC0u+uint32_t(symbol-280),8);
    if(extra[index])bits.put(uint32_t(length-base[index]),extra[index]);
}
void zipFixedDistance(ZipBits& bits,int distance){
    static const int base[]={1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
    static const int extra[]={0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
    int index=0;while(index<29&&distance>=base[index+1])++index;
    bits.putMsb(uint32_t(index),5);if(extra[index])bits.put(uint32_t(distance-base[index]),extra[index]);
}
std::vector<uint8_t> zipFixedHuffman(const std::vector<uint8_t>& raw){
    ZipBits bits;bits.put(1,1);bits.put(1,2);
    size_t at=0;
    while(at<raw.size()){
        size_t run=1;while(at+run<raw.size()&&raw[at+run]==raw[at]&&run<size_t(1)+258*128)++run;
        if(run>=4){zipFixedLiteral(bits,raw[at]);size_t copied=1;while(copied<run){const int n=int(std::min<size_t>(258,run-copied));zipFixedLength(bits,n);zipFixedDistance(bits,1);copied+=size_t(n);}at+=run;continue;}
        constexpr size_t period=32;
        if(at>=period){size_t match=0;while(at+match<raw.size()&&match<258&&raw[at+match]==raw[at-period+match])++match;
            if(match>=3){zipFixedLength(bits,int(match));zipFixedDistance(bits,int(period));at+=match;continue;}}
        zipFixedLiteral(bits,raw[at++]);
    }
    bits.putMsb(0,7);bits.flush();
    std::vector<uint8_t> out{0x78,0x01};out.insert(out.end(),bits.bytes.begin(),bits.bytes.end());psdU32(out,zipAdler(raw));return out;
}
std::vector<uint8_t> psdChannelData(const PsdFixtureLayer& layer,size_t channel,size_t width,size_t height,bool psb){
    const auto& samples=layer.channels[channel];std::vector<uint8_t> out;
    if(layer.compression==PsdFixtureCompression::Raw){psdU16(out,0);out.insert(out.end(),samples.begin(),samples.end());return out;}
    if(layer.compression==PsdFixtureCompression::Unsupported){psdU16(out,99);return out;}
    if(layer.compression==PsdFixtureCompression::PackBits){psdU16(out,1);std::vector<std::vector<uint8_t>> rows;std::vector<uint8_t> solidRow;if(layer.generatedSolid)solidRow.assign(width,layer.solidChannels[channel]);for(size_t y=0;y<height;++y)rows.push_back(packBitsRow(layer.generatedSolid?solidRow.data():samples.data()+y*width,width));for(const auto& row:rows)if(psb)psdU32(out,uint32_t(row.size()));else psdU16(out,uint16_t(row.size()));for(const auto& row:rows)out.insert(out.end(),row.begin(),row.end());return out;}
    std::vector<uint8_t> encoded=samples;
    const uint16_t compression=layer.compression==PsdFixtureCompression::ZipPrediction?3:2;
    if(compression==3)for(size_t y=0;y<height;++y)for(size_t x=width;x-->1;)encoded[y*width+x]=uint8_t(encoded[y*width+x]-encoded[y*width+x-1]);
    psdU16(out,compression);auto zip=layer.compression==PsdFixtureCompression::ZipFixedHuffman?zipFixedHuffman(encoded):zipStored(encoded);out.insert(out.end(),zip.begin(),zip.end());return out;
}
std::vector<uint8_t> psdFixture(int documentWidth,int documentHeight,const std::vector<PsdFixtureLayer>& layers,bool psb=false){
    std::vector<std::vector<uint8_t>> records,channelData;
    for(const auto& layer:layers){const size_t width=size_t(int64_t(layer.right)-layer.left),height=size_t(int64_t(layer.bottom)-layer.top);std::vector<uint8_t> record;
        psdU32(record,uint32_t(layer.top));psdU32(record,uint32_t(layer.left));psdU32(record,uint32_t(layer.bottom));psdU32(record,uint32_t(layer.right));psdU16(record,3);
        for(int channel=0;channel<3;++channel){auto packed=psdChannelData(layer,size_t(channel),width,height,psb);psdU16(record,uint16_t(channel));if(psb)psdU64(record,uint64_t(packed.size()));else psdU32(record,uint32_t(packed.size()));channelData.push_back(std::move(packed));}
        record.insert(record.end(),{'8','B','I','M','n','o','r','m'});record.push_back(layer.opacity);record.push_back(0);record.push_back(layer.hidden?0x02:0);record.push_back(0);
        std::vector<uint8_t> extra;psdU32(extra,0);psdU32(extra,0);extra.push_back(uint8_t(layer.name.size()));extra.insert(extra.end(),layer.name.begin(),layer.name.end());while(extra.size()%4)extra.push_back(0);
        for(const auto& [key,data]:layer.blocks){extra.insert(extra.end(),{'8','B','I','M'});extra.insert(extra.end(),key.begin(),key.end());psdU32(extra,uint32_t(data.size()));extra.insert(extra.end(),data.begin(),data.end());if(data.size()%2)extra.push_back(0);}
        psdU32(record,uint32_t(extra.size()));record.insert(record.end(),extra.begin(),extra.end());records.push_back(std::move(record));
    }
    std::vector<uint8_t> layerInfo;psdU16(layerInfo,uint16_t(layers.size()));for(const auto& record:records)layerInfo.insert(layerInfo.end(),record.begin(),record.end());for(const auto& data:channelData)layerInfo.insert(layerInfo.end(),data.begin(),data.end());
    std::vector<uint8_t> section;if(psb)psdU64(section,uint64_t(layerInfo.size()));else psdU32(section,uint32_t(layerInfo.size()));section.insert(section.end(),layerInfo.begin(),layerInfo.end());psdU32(section,0);
    std::vector<uint8_t> out{'8','B','P','S'};psdU16(out,psb?2:1);out.insert(out.end(),6,0);psdU16(out,3);psdU32(out,uint32_t(documentHeight));psdU32(out,uint32_t(documentWidth));psdU16(out,8);psdU16(out,3);psdU32(out,0);psdU32(out,0);if(psb)psdU64(out,uint64_t(section.size()));else psdU32(out,uint32_t(section.size()));out.insert(out.end(),section.begin(),section.end());return out;
}
PsdFixtureLayer solidPsdLayer(int left,int top,int width,int height,uint8_t red,uint8_t green,uint8_t blue,PsdFixtureCompression compression=PsdFixtureCompression::Raw){
    PsdFixtureLayer layer;layer.left=left;layer.top=top;layer.right=left+width;layer.bottom=top+height;layer.compression=compression;const size_t count=size_t(width)*size_t(height);layer.channels={std::vector<uint8_t>(count,red),std::vector<uint8_t>(count,green),std::vector<uint8_t>(count,blue)};return layer;
}
PsdFixtureLayer solidPackBitsPsdLayer(int left,int top,int width,int height,uint8_t red,uint8_t green,uint8_t blue){PsdFixtureLayer layer;layer.left=left;layer.top=top;layer.right=left+width;layer.bottom=top+height;layer.compression=PsdFixtureCompression::PackBits;layer.generatedSolid=true;layer.solidChannels={red,green,blue};return layer;}
}
// Runs the RAW review with a stub developer, answering the modal dialog once its preview is ready.
// The real LibRaw development is covered by migration.contract; this checks only the review flow.
bool reviewRaw(const std::filesystem::path& file,bool editExposure,bool accept,ui::RawSettings& settings,bool cameraRaw=false){
    const ui::RawImportDialog::Developer stub=[](const std::filesystem::path&,const imaging::ImportOptions&,std::uint32_t){
        imaging::RawDevelopment developed;
        developed.image.image={2,2,8,std::vector<std::uint8_t>(16,255)};
        developed.image.metadata.decoder="stub";
        developed.capabilities={true,true,2000,12000};
        developed.asShot={0.0,5000};
        return developed;
    };
    bool timedOut=false;
    QTimer poll;poll.setInterval(10);
    QObject::connect(&poll,&QTimer::timeout,[&]{
        auto* dialog=QApplication::activeModalWidget();
        if(!dialog||dialog->objectName()!="rawImportDialog")return;
        auto* button=dialog->findChild<QPushButton*>("rawImportButton");
        if(!button||!button->isEnabled())return;
        poll.stop();
        if(!accept){QTest::keyClick(dialog,Qt::Key_Escape);return;}
        if(editExposure)dialog->findChild<QSlider*>("rawExposureSlider")->setValue(100);
        if(cameraRaw)dialog->findChild<QPushButton*>("rawCameraRawButton")->click();else button->click();
    });
    QTimer watchdog;watchdog.setSingleShot(true);watchdog.setInterval(15000);
    QObject::connect(&watchdog,&QTimer::timeout,[&]{timedOut=true;poll.stop();if(auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget()))dialog->reject();});
    poll.start();watchdog.start();
    const auto accepted=ui::reviewRawImports(nullptr,{file},settings,stub);
    poll.stop();watchdog.stop();
    require(!timedOut,"RAW review dialog never became ready");
    return accepted;
}
// Photoshop vector blocks, built in the descriptor layout the reader searches by key. Path points are
// y-then-x 8.24 fixed-point fractions of the canvas, as in the PSD specification.
namespace {
using Knot=std::array<double,6>; // in.x, in.y, anchor.x, anchor.y, out.x, out.y
void putTag(std::vector<uint8_t>& out,const char* tag){out.insert(out.end(),tag,tag+4);}
void putKey(std::vector<uint8_t>& out,const char* key){const size_t length=std::strlen(key);psdU32(out,uint32_t(length));out.insert(out.end(),key,key+length);}
void putBits(std::vector<uint8_t>& out,double value){uint64_t bits;std::memcpy(&bits,&value,sizeof bits);psdU64(out,bits);}
void putUnit(std::vector<uint8_t>& out,const char* key,double value){putKey(out,key);putTag(out,"UntF");putTag(out,"#Pxl");putBits(out,value);}
void putDouble(std::vector<uint8_t>& out,const char* key,double value){putKey(out,key);putTag(out,"doub");putBits(out,value);}
void putLong(std::vector<uint8_t>& out,const char* key,uint32_t value){putKey(out,key);putTag(out,"long");psdU32(out,value);}
void putBool(std::vector<uint8_t>& out,const char* key,bool value){putKey(out,key);putTag(out,"bool");out.push_back(value?1:0);}
Knot sharp(double x,double y){return {x,y,x,y,x,y};}
std::vector<Knot> rectangleKnots(double l,double t,double r,double b){return {sharp(l,t),sharp(r,t),sharp(r,b),sharp(l,b)};}
std::vector<Knot> roundedKnots(double l,double t,double r,double b,double radius){return {sharp(l+radius,t),sharp(r-radius,t),sharp(r,t+radius),sharp(r,b-radius),sharp(r-radius,b),sharp(l+radius,b),sharp(l,b-radius),sharp(l,t+radius)};}
std::vector<Knot> ellipseKnots(double l,double t,double r,double b){const double cx=(l+r)/2,cy=(t+b)/2,kx=(r-l)/2*0.5523,ky=(b-t)/2*0.5523;return {{cx-kx,t,cx,t,cx+kx,t},{r,cy-ky,r,cy,r,cy+ky},{cx+kx,b,cx,b,cx-kx,b},{l,cy+ky,l,cy,l,cy-ky}};}
// `vogk`: keyOriginType 1 = rectangle, 2 = rounded rectangle, 5 = ellipse.
std::vector<uint8_t> origination(uint32_t type,double top,double left,double bottom,double right,std::array<double,4> radii={}){
    std::vector<uint8_t> out;putLong(out,"keyOriginType",type);putKey(out,"keyOriginShapeBBox");
    putUnit(out,"Top ",top);putUnit(out,"Left",left);putUnit(out,"Btom",bottom);putUnit(out,"Rght",right);
    if(type==2){putKey(out,"keyOriginRRectRadii");const char* corners[]={"topLeft","topRight","bottomRight","bottomLeft"};for(size_t i=0;i<4;++i)putUnit(out,corners[i],radii[i]);}
    return out;
}
void putPoint(std::vector<uint8_t>& out,double x,double y,double width,double height){psdU32(out,uint32_t(int32_t(std::llround(y/height*16777216.0))));psdU32(out,uint32_t(int32_t(std::llround(x/width*16777216.0))));}
// Each subpath record is type 0 (closed) followed by its knot count; each knot is a type 1 record.
std::vector<uint8_t> vectorPath(const std::vector<std::vector<Knot>>& subpaths,double width,double height){
    std::vector<uint8_t> out(8,0);
    for(const auto& subpath:subpaths){
        psdU16(out,0);psdU16(out,uint16_t(subpath.size()));out.insert(out.end(),22,0);
        for(const auto& knot:subpath){psdU16(out,1);for(size_t i=0;i<6;i+=2)putPoint(out,knot[i],knot[i+1],width,height);}
    }
    return out;
}
std::vector<uint8_t> solidFill(double red,double green,double blue){std::vector<uint8_t> out;putTag(out,"SoCo");psdU32(out,16);putDouble(out,"Rd  ",red);putDouble(out,"Grn ",green);putDouble(out,"Bl  ",blue);return out;}
std::vector<uint8_t> gradientFill(){std::vector<uint8_t> out;putTag(out,"GdFl");psdU32(out,16);putDouble(out,"Rd  ",0);putDouble(out,"Grn ",0);putDouble(out,"Bl  ",0);return out;}
std::vector<uint8_t> strokeState(bool stroke,bool fill){std::vector<uint8_t> out;putBool(out,"strokeEnabled",stroke);putBool(out,"fillEnabled",fill);return out;}
// A solid stroke with a width in pixels, an opacity percentage, and an optional alignment name or dash list.
std::vector<uint8_t> strokeDescriptor(double width,double red,double green,double blue,const char* alignment=nullptr,bool dashed=false,double opacity=100){
    std::vector<uint8_t> out;putBool(out,"strokeEnabled",true);putBool(out,"fillEnabled",true);putUnit(out,"strokeStyleLineWidth",width);putDouble(out,"strokeStyleOpacity",opacity);
    putKey(out,"strokeStyleContent");putKey(out,"solidColorLayer");putDouble(out,"Rd  ",red);putDouble(out,"Grn ",green);putDouble(out,"Bl  ",blue);
    if(alignment)putKey(out,alignment);
    putKey(out,"strokeStyleLineDashSet");putTag(out,"VlLs");psdU32(out,dashed?1:0);
    return out;
}
PsdFixtureBlocks shapeBlocks(std::vector<uint8_t> originationBlock,std::vector<uint8_t> path,std::vector<uint8_t> fill,std::vector<uint8_t> stroke={}){
    PsdFixtureBlocks blocks{{"vsms",std::move(path)},{"vscg",std::move(fill)}};
    if(!originationBlock.empty())blocks.push_back({"vogk",std::move(originationBlock)});
    if(!stroke.empty())blocks.push_back({"vstk",std::move(stroke)});
    return blocks;
}
PsdFixtureLayer shapeLayer(int left,int top,int right,int bottom,PsdFixtureBlocks blocks,std::string name="Shape"){
    PsdFixtureLayer layer;layer.left=left;layer.top=top;layer.right=right;layer.bottom=bottom;layer.name=std::move(name);layer.blocks=std::move(blocks);return layer;
}
imaging::PsdImport importPsd(const std::vector<PsdFixtureLayer>& layers,int width=64,int height=48,bool psb=false){
    const auto bytes=psdFixture(width,height,layers,psb);return imaging::readPsd(bytes.data(),bytes.size());
}
// A plain pixel layer keeps the file importable when the shape layer under test is dropped.
PsdFixtureLayer plainLayer(){auto layer=solidPsdLayer(0,0,2,2,9,9,9);layer.name="Plain";return layer;}
bool hasWarning(const imaging::PsdImport& imported,const char* fragment){for(const auto& warning:imported.warnings)if(warning.find(fragment)!=std::string::npos)return true;return false;}
// Answers the PSD warning dialog with the named button while the import is running.
bool answerPsdWarnings(const char* button,const std::function<void()>& action){
    QTimer poll;poll.setInterval(10);bool seen=false;
    QObject::connect(&poll,&QTimer::timeout,[&]{
        auto* dialog=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if(!dialog||dialog->objectName()!="psdImportWarnings")return;
        seen=true;dialog->findChild<QPushButton*>(button)->click();
    });
    poll.start();action();poll.stop();return seen;
}
QString writePdfFixture(const QString& directory, int pages = 2) {
    QByteArray bytes("%PDF-1.4\n");
    std::vector<qsizetype> offsets{0};
    const auto object = [&](const QByteArray& body) {
        offsets.push_back(bytes.size());
        bytes += QByteArray::number(offsets.size() - 1) + " 0 obj\n" + body + "\nendobj\n";
    };
    object("<< /Type /Catalog /Pages 2 0 R >>");
    QByteArray children;
    for (int i = 0; i < pages; ++i) children += QByteArray::number(3 + i * 2) + " 0 R ";
    object("<< /Type /Pages /Count " + QByteArray::number(pages) + " /Kids [" + children + "] >>");
    for (int i = 0; i < pages; ++i) {
        const QByteArray geometry = i == 0 ? "/MediaBox [0 0 200 100]"
            : "/MediaBox [0 0 120 80] /CropBox [10 20 110 70] /Rotate 90";
        object("<< /Type /Page /Parent 2 0 R " + geometry
               + " /Resources << >> /Contents " + QByteArray::number(4 + i * 2) + " 0 R >>");
        const QByteArray marks = i == 0 ? "1 0 0 rg 0 50 50 50 re f\n" : "0 0 1 rg 10 20 50 50 re f\n";
        object("<< /Length " + QByteArray::number(marks.size()) + " >>\nstream\n" + marks + "endstream");
    }
    const auto xref = bytes.size();
    bytes += "xref\n0 " + QByteArray::number(offsets.size()) + "\n0000000000 65535 f \n";
    for (size_t i = 1; i < offsets.size(); ++i)
        bytes += QByteArray::number(offsets[i]).rightJustified(10, '0') + " 00000 n \n";
    bytes += "trailer\n<< /Size " + QByteArray::number(offsets.size()) + " /Root 1 0 R >>\nstartxref\n"
        + QByteArray::number(xref) + "\n%%EOF\n";
    const auto name = directory + "/pages.PDF";
    QFile file(name);
    require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "PDF fixture write failed");
    return name;
}

void pdfImportTests(const std::function<void(const char*, const std::function<void()>&)>& test) {
    test("pdf_layers_pixels_history_project_and_export", [] {
        QTemporaryDir temp;
        const auto file = writePdfFixture(temp.path());
        MainWindow window(true);
        auto* project = &window.addEmptyProject();
        window.importSelectedImages({file}, project->canvas);
        finish(window);
        require(project->document.has_value(), "No PDF document was created");
        const auto& doc = *project->document;
        require(doc.width == 200 && doc.height == 100 && doc.resolution == 72, "Wrong first-page canvas");
        require(doc.layers.size() == 2 && doc.layers[0].name == "Page 2" && doc.layers[1].name == "Page 1"
                && project->active == doc.layers[1].id, "PDF page order or active page is wrong");
        const auto& rotated = *doc.layers[0].raster;
        require(rotated.width == 50 && rotated.height == 100, "PDF crop or rotation is wrong");
        require(rotated.pixel(20, 10) == Pixel{0,0,255,255} && rotated.pixel(20, 75) == Pixel{},
                "Rotated page content or transparency is wrong");
        require(doc.layers[1].raster->pixel(10, 10) == Pixel{255,0,0,255}
                && doc.layers[1].raster->pixel(150, 80) == Pixel{}, "Page 1 pixels or transparency are wrong");

        const auto comp = path(temp.filePath("pages.comp"));
        ProjectStore store(makeWicProjectCodec());
        store.save(comp, doc, project->active);
        const auto reopened = store.load(comp);
        require(reopened.activeLayer == project->active && reopened.document.layers.size() == 2
                && reopened.document.layers[0].raster->rgba() == rotated.rgba()
                && reopened.document.layers[1].raster->rgba() == doc.layers[1].raster->rgba(),
                "Reopened project lost imported page pixels or selection");
        const auto png = path(temp.filePath("rendered.png"));
        exportDocumentAtomic(reopened.document, png);
        const auto decoded = imaging::WicCodec::decode(png);
        auto output = Raster::fromRgba(int(decoded.image.width), int(decoded.image.height),
                                       decoded.image.pixels.data(), decoded.image.stride);
        require(output->pixel(10, 10) == Pixel{255,0,0,255} && output->pixel(95, 10) == Pixel{0,0,255,255}
                && output->pixel(150, 80) == Pixel{}, "Exported PDF page composite is wrong");
        require(project->history.undoCount() == 1 && !project->history.undo()->document, "PDF import did not undo as one step");
        const auto redo = project->history.redo();
        require(redo && redo->document == doc && redo->activeLayer == project->active, "Redo lost the imported pages");
    });
    test("pdf_mixed_import_and_drop_at_destination_resolution", [] {
        QTemporaryDir temp;
        const auto pdf = writePdfFixture(temp.path());
        const auto png = image(temp.path(), "image.png");
        auto d = document(128, 80);
        d.resolution = 144;
        MainWindow window(true);
        auto& project = window.addProject(d);
        window.importSelectedImages({png, pdf}, project.canvas);
        finish(window);
        const auto& imported = project.document->layers;
        require(imported.size() == 3 && imported.front().name == "image" && project.history.undoCount() == 1,
                "Mixed import order or history is wrong");
        require(imported.back().raster->width == 400 && imported.back().raster->height == 200
                && imported.back().transform.width == 128 && project.document->width == 128,
                "PDF did not keep source DPI and fit the existing canvas");
        window.receiveDropPaths({pdf}, project.canvas, Point{72,40});
        finish(window);
        require(project.document->layers.size() == 5 && project.history.undoCount() == 2
                && project.document->layers.back().transform.x == 8, "PDF drop lost its point or split history");
    });
    test("pdf_rejected_inputs_and_cancel_preserve_workspace", [] {
        QTemporaryDir temp;
        const auto pdf = writePdfFixture(temp.path(), 20);
        const QString folder(135, QLatin1Char('W'));
        require(QDir(temp.path()).mkdir(folder), "Could not create a long PDF fixture path");
        const auto bad = temp.filePath(folder + "/broken.pdf");
        { QFile file(bad); require(file.open(QIODevice::WriteOnly), "Bad PDF fixture write failed"); file.write("broken"); }
        // An independent, empty one-page fixture encrypted by pypdf. Its password is compositor-pdf-test.
        const auto encrypted = QByteArray::fromBase64(
    "JVBERi0xLjMKJeLjz9MKMSAwIG9iago8PAovUHJvZHVjZXIgPGM5MjAwYzgxN2M+Cj4+CmVuZG9iagoyIDAgb2JqCjw8Ci9U"
    "eXBlIC9QYWdlcwovQ291bnQgMQovS2lkcyBbIDQgMCBSIF0KPj4KZW5kb2JqCjMgMCBvYmoKPDwKL1R5cGUgL0NhdGFsb2cK"
    "L1BhZ2VzIDIgMCBSCj4+CmVuZG9iago0IDAgb2JqCjw8Ci9UeXBlIC9QYWdlCi9SZXNvdXJjZXMgPDwKPj4KL01lZGlhQm94"
    "IFsgMC4wIDAuMCAyNCAyNCBdCi9QYXJlbnQgMiAwIFIKPj4KZW5kb2JqCjUgMCBvYmoKPDwKL1YgMgovUiAzCi9MZW5ndGgg"
    "MTI4Ci9QIDQyOTQ5NjcyOTIKL0ZpbHRlciAvU3RhbmRhcmQKL08gPDM2NmVkMGRhYTNlYWE0Y2RmMjY4OGNjNDAxMDEwYzVm"
    "NWQyNzgzYTUzM2I2ZmJmOTJhZmU3MmE3NTFhYjMyMTA+Ci9VIDxlNDYzN2MzZTJiZjg3YmE0YmUzZTBmYTFiZGFmMGMwZTI4"
    "YmY0ZTVlNGU3NThhNDE2NDAwNGU1NmZmZmEwMTA4Pgo+PgplbmRvYmoKeHJlZgowIDYKMDAwMDAwMDAwMCA2NTUzNSBmIAow"
    "MDAwMDAwMDE1IDAwMDAwIG4gCjAwMDAwMDAwNTkgMDAwMDAgbiAKMDAwMDAwMDExOCAwMDAwMCBuIAowMDAwMDAwMTY3IDAw"
    "MDAwIG4gCjAwMDAwMDAyNTkgMDAwMDAgbiAKdHJhaWxlcgo8PAovU2l6ZSA2Ci9Sb290IDMgMCBSCi9JbmZvIDEgMCBSCi9J"
    "RCBbIDwzNTYzNjU2MjMwMzQzMzMyMzk2NjMwMzYzODY0MzgzODYyMzEzNTM1Mzg2NDMyNjMzNDYyNjY2MjY1Mzc2MjM1PiA8"
    "MzU2MzY1NjIzMDM0MzMzMjM5NjYzMDM2Mzg2NDM4Mzg2MjMxMzUzNTM4NjQzMjYzMzQ2MjY2NjI2NTM3NjIzNT4gXQovRW5j"
    "cnlwdCA1IDAgUgo+PgpzdGFydHhyZWYKNDc0CiUlRU9GCg=="
        );
        const auto locked = temp.filePath(folder + "/locked.pdf");
        { QFile file(locked); require(file.open(QIODevice::WriteOnly) && file.write(encrypted) == encrypted.size(),
                                      "Encrypted PDF fixture write failed"); }
        MainWindow window(true);
        auto& project = window.addProject(document());
        const auto before = project.document;
        auto* tabs = window.findChild<QTabWidget*>();
        const auto count = tabs->count();
        QString errorText;
        bool pathWrapped = false;
        bool cancel = false;
        bool cancelled = false;
        QTimer poll;
        QObject::connect(&poll, &QTimer::timeout, &window, [&] {
            for (auto* dialog : window.findChildren<QDialog*>()) {
                if (!dialog->isVisible()) continue;
                const auto name = dialog->objectName();
                const bool previousErrorBox = qobject_cast<QMessageBox*>(dialog) && dialog->windowTitle() == "Import PDF";
                if (name != "pdfImportError" && name != "workspaceDropError" && !previousErrorBox) continue;
                if (auto* text = dialog->findChild<QTextEdit*>()) {
                    errorText = text->toPlainText();
                    const auto* firstLine = text->document()->firstBlock().layout();
                    pathWrapped = firstLine && firstLine->lineCount() > 1;
                }
                dialog->accept();
            }
            if (!cancel || cancelled) return;
            if (auto* progress = window.findChild<QDialog*>("pdfImportPreflight")) {
                if (auto* button = progress->findChild<QPushButton*>()) {
                    cancelled = true;
                    button->click();
                }
            }
        });
        poll.start(0);
        window.openPath(bad);
        require(errorText.contains(QDir::toNativeSeparators(bad)) && pathWrapped,
                "The malformed PDF error did not show and wrap its full path");
        errorText.clear();
        window.openPath(locked);
        require(errorText.contains("password-protected"), "Locked PDF was not distinguished from a malformed file");
        for (const auto& rejected : {bad, locked}) {
            errorText.clear();
            pathWrapped = false;
            window.receiveDropPaths({rejected}, project.canvas, {});
            until([&] { return ui::WorkspaceDropQueue::find(&window)->idle(); });
            require(errorText.contains(QDir::toNativeSeparators(rejected)) && pathWrapped,
                    "The drop error did not show and wrap the full PDF path");
        }
        cancel = true;
        window.openPath(pdf);
        poll.stop();
        require(cancelled && project.document == before && project.history.undoCount() == 0 && tabs->count() == count,
                "Failed or cancelled PDF import changed the workspace");
    });
    test("pdf_failed_file_does_not_abort_mixed_picker_import", [] {
        QTemporaryDir temp;
        const auto bad = temp.filePath("broken.pdf");
        QFile file(bad);
        require(file.open(QIODevice::WriteOnly) && file.write("broken") == 6, "Bad PDF fixture write failed");
        file.close();
        const auto png = image(temp.path(), "valid.png");
        const auto pdf = writePdfFixture(temp.path());

        MainWindow window(true);
        auto& project = window.addEmptyProject();
        // Also dismiss the old preflight dialog so a regression fails by assertion.
        QTimer poll;
        QObject::connect(&poll, &QTimer::timeout, &window, [&] {
            for (auto* box : window.findChildren<QMessageBox*>())
                if (box->isVisible() && box->windowTitle() == "Import PDF") box->accept();
        });
        poll.start(0);
        window.importSelectedImages({bad, png, pdf}, project.canvas);
        poll.stop();
        auto* queue = ui::ImportQueue::find(&window);
        require(queue, "A broken PDF prevented the mixed selection from reaching the import queue");
        until([&] { return queue->idle() && window.findChild<QMessageBox*>("imageImportErrors"); });
        auto* error = window.findChild<QMessageBox*>("imageImportErrors");
        const auto details = error->detailedText();
        error->accept();
        require(details.contains("broken.pdf") && !details.contains("valid.png") && !details.contains("pages.PDF"),
                "The import queue did not report only the broken PDF");
        require(project.document && project.document->layers.size() == 3
                && project.document->layers.front().name == "valid" && project.history.undoCount() == 1,
                "The valid PNG and PDF did not import together as one undo step");
    });
    test("pdf_cancel_keeps_gui_events_running_until_worker_settles", [] {
        QWidget parent;
        ui::PreparedPdfs prepared;
        std::atomic_bool entered{}, release{}, sawCancellation{};
        bool clicked = false, processedWhileWaiting = false;
        QTimer poll;
        QObject::connect(&poll, &QTimer::timeout, &parent, [&] {
            auto* progress = parent.findChild<QDialog*>("pdfImportPreflight");
            if (!entered || !progress || !progress->isVisible()) return;
            if (!clicked) {
                clicked = true;
                progress->findChild<QPushButton*>()->click();
            } else if (sawCancellation) {
                processedWhileWaiting = true;
                release = true;
            }
        });
        poll.start(1);
        const auto accepted = ui::preparePdfImports(&parent, {L"stalled.pdf"}, std::nullopt, prepared, {},
            [&](const auto&, double, const auto& options) {
                QElapsedTimer deadline;
                deadline.start();
                entered = true;
                while (!release && deadline.elapsed() < 2000) {
                    sawCancellation = options.cancelled();
                    QThread::msleep(1);
                }
                // Simulate a native operation that returns a result after Cancel.
                return std::vector{Raster::filled(1, 1)};
            });
        require(clicked && sawCancellation && processedWhileWaiting,
                "Cancel stopped GUI events while the PDF worker was still running");
        require(!accepted && prepared.empty(), "A late PDF result survived Cancel");
    });
    test("pdf_mixed_import_charges_prepared_tiles_before_decoding_images", [] {
        QTemporaryDir temp;
        const auto first = path(image(temp.path(), "first.png"));
        const auto second = path(image(temp.path(), "second.png"));
        const std::filesystem::path pdf(L"prepared.pdf");
        auto page = Raster::filled(72, 72);
        ui::ImportBatch batch;
        batch.files = {first, pdf, second};
        batch.pdfs.emplace(pdf, ui::PreparedPdf{72., {page}});

        imaging::ImportOptions options;
        options.maxWorkingBytes = page->retainedBytes() + 4096;
        auto result = ui::prepareImportBatch({}, batch, ui::decodeImportImage, options);
        require(result.imported == 1 && result.importedPdf && result.errors.size() == 2
                && result.after.document->layers.front().raster == page,
                "PNG decodes ignored the prepared PDF tiles already resident in memory");

        options.maxWorkingBytes = 2 * page->retainedBytes() + 64 * 32 * 4;
        result = ui::prepareImportBatch({}, batch, ui::decodeImportImage, options);
        require(result.imported == 2 && result.importedPdf && result.errors.size() == 1
                && result.errors.front().startsWith("second.png:"),
                "The second PNG ignored the tiles retained by the first PNG and PDF");
    });
    test("pdf_page_failure_does_not_keep_earlier_pages", [] {
        QTemporaryDir temp;
        const auto pdf = writePdfFixture(temp.path());
        Fixture f;
        ui::ImportQueue q(f.host());
        // The first page fits, but the second page pushes this valid existing document past its import budget.
        auto d = document(10000, 9998);
        Layer existing;
        existing.id = newId();
        existing.name = "Existing";
        existing.raster = Raster::filled(10000, 9998);
        existing.transform = {0,0,10000,9998};
        d.layers.push_back(existing);
        f.state = {d, existing.id};
        q.enqueue(&f.target, {{path(pdf)}});
        until([&] { return q.idle(); });
        require(f.errors.size() == 1 && f.state.document == d && f.history.undoCount() == 0 && f.commits == 0,
                "Budget failure installed a partial PDF");
    });
}

}
int main(int argc,char**argv){QApplication app(argc,argv);int passed=0,failed=0;auto test=[&](const char* name,const std::function<void()>&run){try{run();++passed;std::cout<<"PASS "<<name<<'\n';}catch(const std::exception&e){++failed;std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n';}};
    if (argc == 2 && std::string_view(argv[1]) == "--pdf") {
        pdfImportTests(test);
        return failed ? 1 : 0;
    }
    QTemporaryDir temporary;require(temporary.isValid(),"Missing temporary fixture directory");const auto first=image(temporary.path(),QString::fromUtf8("first-画像.png")),second=image(temporary.path(),"second.png"),missing=temporary.path()+"/missing.png";
    test("source_empty_partial_failure_one_undo",[&]{Fixture f;ui::ImportQueue q(f.host());q.enqueue(&f.target,{{path(first),path(missing),path(second)},Point{999,999}});until([&]{return q.idle();});require(f.state.document&&f.state.document->width==64&&f.state.document->height==32&&f.state.document->resolution==72,"First successful image dimensions/default DPI wrong");require(f.state.document->layers.size()==2&&f.state.document->layers[0].transform.x==0&&f.state.document->layers[0].transform.y==0,"Empty batch did not ignore drop point");require(f.state.document->layers[0].name==QString::fromUtf8("first-画像").toStdString()&&f.state.document->layers[1].name=="second","Provider order or Unicode basename lost");require(f.state.active==f.state.document->layers.back().id&&f.errors.size()==1&&!f.working,"Active/error/import state wrong");require(f.history.undoCount()==1,"Batch made more than one history entry");auto imported=f.state.document;auto pixels=imported->layers[0].raster;f.state.document=f.history.undo()->document;require(!f.state.document,"Undo did not restore empty project");auto redo=f.history.redo();f.state={redo->document,redo->activeLayer};require(f.state.document==imported&&f.state.document->layers[0].raster==pixels,"Redo lost original immutable imported pixels");q.enqueue(&f.target,{{path(missing)}});until([&]{return q.idle();});require(f.history.undoCount()==1&&f.commits==1,"All-failed batch added history");});
    test("source_floor_drop_parent_and_aggregate_budget",[&]{auto d=document(1000,800);Layer group;group.id="folder";group.group=true;group.transform={0,0,1000,800};d.layers={group};ui::ImportState before{d,"folder"};auto result=ui::prepareImportBatch(before,{{path(first),path(second)},Point{300.9,250.9}});require(result.after.document->layers[1].transform.x==268&&result.after.document->layers[1].transform.y==234,"Drop did not floor center minus half-size");require(result.after.document->layers[1].parentId=="folder"&&result.after.document->layers[2].parentId=="folder"&&result.expandedGroups.size()==2,"Active parent did not carry through ordered batch");require(before.document==d&&d.layers.size()==1,"Preparation mutated original");imaging::ImportOptions options;options.remainingPixels=4096;result=ui::prepareImportBatch({},{{path(first),path(second),path(first)}},ui::decodeImportImage,options);require(result.imported==2&&result.errors.size()==1,"Aggregate image budget not applied between files");});
    test("fifo_queued_batches_separate_undo_and_worker_thread",[&]{Fixture f;std::atomic_bool release{},entered{};auto decoder=[&](const auto&p,const auto&o){require(QThread::currentThread()!=qApp->thread(),"Decode ran on GUI thread");entered=true;while(!release&&!o.cancelled())QThread::msleep(1);return ui::decodeImportImage(p,o);};ui::ImportQueue q(f.host(),nullptr,decoder);q.enqueue(&f.target,{{path(first)}});q.enqueue(&f.target,{{path(second)}});until([&]{return entered.load();});require(q.busy(&f.target)&&q.contains(&f.target)&&!f.state.document,"Queued work exposed a partial document");release=true;until([&]{return q.idle();});require(f.completions==2&&f.history.undoCount()==2&&f.state.document->layers.size()==2,"FIFO lost a batch");require(f.history.undo()->document->layers.size()==1&&!f.history.undo()->document,"Queued batches did not undo separately");});
    test("project_busy_defers_and_cancel_discards_original",[&]{Fixture f;f.state.document=document();f.blocked=true;auto original=f.state;ui::ImportQueue q(f.host());q.enqueue(&f.target,{{path(first),path(second)}});QTest::qWait(60);require(!q.busy()&&!f.working&&f.state==original,"Project busy did not defer decoding");q.cancel(&f.target);require(q.idle()&&f.cancellations==1&&f.state==original&&f.history.undoCount()==0,"Queued cancellation changed state");f.blocked=false;q.enqueue(&f.target,{{path(first),path(second)}});q.cancel(&f.target);until([&]{return q.idle();});require(f.cancellations==2&&f.state==original&&f.history.undoCount()==0,"Active cancellation committed prepared images");});
    test("stale_destination_and_destroyed_target_do_not_commit",[&]{Fixture f;std::atomic_bool release{},entered{};auto decoder=[&](const auto&p,const auto&o){entered=true;while(!release&&!o.cancelled())QThread::msleep(1);return ui::decodeImportImage(p,o);};ui::ImportQueue q(f.host(),nullptr,decoder);q.enqueue(&f.target,{{path(first)}});until([&]{return entered.load();});f.state.document=document(77,33);release=true;until([&]{return q.idle();});require(f.state.document->width==77&&f.commits==0&&f.errors.size()==1,"Stale worker overwrote changed destination");QObject* target=new QObject;bool callback=false;auto host=f.host();host.snapshot=[&](QObject*){return std::optional(ui::ImportState{});};host.busy=[](QObject*,bool){};host.commit=[&](QObject*,const auto&){callback=true;};host.completed=[&](QObject*,const auto&){callback=true;};release=false;entered=false;ui::ImportQueue dead(host,nullptr,decoder);dead.enqueue(target,{{path(first)}});until([&]{return entered.load();});delete target;release=true;until([&]{return dead.idle();});require(!callback,"Destroyed target received worker completion");});
    test("real_wic_exif_and_profile_required_fixtures",[&]{const auto fixtures=std::filesystem::path(__FILE__).parent_path().parent_path().parent_path()/"evidence"/"imaging";auto oriented=fixtures/"orientation-6.tif";require(std::filesystem::is_regular_file(oriented),"Required EXIF fixture absent");const auto result=ui::prepareImportBatch({},{{oriented}});require(result.imported==1&&result.after.document->width==2&&result.after.document->height==3,"Import batch lost WIC EXIF orientation");auto profile=fixtures/"adobe-rgb.png";require(std::filesystem::is_regular_file(profile),"Required profile fixture absent");require(ui::decodeImportImage(profile,{}).metadata.profileStatus==imaging::ProfileStatus::ConvertedEmbeddedProfile,"Embedded profile was not normalized");});
    test("qt_canvas_batch_drop_document_coordinates_and_history",[&]{MainWindow w(true);auto&p=w.addProject(document(1000,800));w.show();QApplication::processEvents();w.canvas()->zoomAt(.75,w.canvas()->rect().center());w.canvas()->panBy({70,-35});const auto where=canvasPoint(w,{300,250});const auto actual=w.canvas()->documentPoint(w.canvas()->mapFrom(&w,where));QMimeData mime;mime.setUrls({QUrl::fromLocalFile(first),QUrl::fromLocalFile(second)});require(drop(w,mime,where),"Native drop rejected");finish(w);require(p.document->layers.size()==2&&p.history.undoCount()==2,"Source workspace drop must create one undo step per URL");const auto&l=p.document->layers[0];require(l.transform.x==std::floor(actual.x()-32)&&l.transform.y==std::floor(actual.y()-16),"Native canvas drop did not convert viewport coordinates");require(p.active==p.document->layers.back().id&&!p.maskSelected,"Import did not select final image target");});
    test("qt_cancel_preserves_existing_document_and_undo",[&]{MainWindow w(true);auto d=document();Layer old;old.id="old";old.transform={0,0,128,128};old.raster=Raster::filled(128,128,{20,30,40,255});d.layers={old};auto&p=w.addProject(d);w.show();QApplication::processEvents();const auto before=p.document;QMimeData mime;mime.setUrls({QUrl::fromLocalFile(first),QUrl::fromLocalFile(second)});require(drop(w,mime,canvasPoint(w,{64,64})),"Cancel test drop rejected");auto*q=ui::ImportQueue::find(&w);require(q,"Queue absent");q->cancel(p.canvas);finish(w);require(p.document==before&&p.history.undoCount()==0&&!p.importing,"Cancelled native import altered original/history");});
    test("qt_bitmap_drop_lifetime_and_empty_tab_external_routing",[&]{MainWindow w(true);auto&p=w.addProject(document());w.show();QApplication::processEvents();QImage bitmap(8,6,QImage::Format_ARGB32);bitmap.fill(QColor(255,0,0,128));{QMimeData mime;mime.setImageData(bitmap);require(drop(w,mime,canvasPoint(w,{64,64})),"Bitmap drop rejected");}finish(w);require(p.document->layers.size()==1&&p.document->layers[0].raster->width==8&&p.document->layers[0].raster->pixel(0,0)==Pixel{128,0,0,128},"Copied bitmap lifetime/premultiplication failed");MainWindow external(true);external.openPath(first);external.openPath(second);finish(external);auto*tabs=external.findChild<QTabWidget*>();require(tabs&&tabs->count()==2,"Rapid external images reused an already reserved empty tab");});
    test("default_100mp_budget_side_and_corrupt_partial_failure",[&]{auto d=document();Layer full;full.id="full-budget";full.transform={0,0,10000,10000};full.raster=Raster::filled(10000,10000);d.layers={full};ui::ImportState before{d,full.id};auto result=ui::prepareImportBatch(before,{{path(first)}});require(result.imported==0&&result.errors.size()==1&&result.after==before,"100MP document limit underflowed or changed original");imaging::ImportOptions tiny;tiny.maxSide=63;result=ui::prepareImportBatch({},{{path(first)}},ui::decodeImportImage,tiny);require(result.imported==0&&result.errors.size()==1,"Side budget bypassed by batch decoder");auto corrupt=temporary.filePath("corrupt.jpg");QFile file(corrupt);require(file.open(QIODevice::WriteOnly)&&file.write("not an image")==12,"Could not write corrupt fixture");file.close();result=ui::prepareImportBatch({},{{path(corrupt),path(first)}});require(result.imported==1&&result.errors.size()==1&&result.after.document->width==64,"Corrupt first image prevented later success");});
    test("psd_in_limit_partial_layer_preserves_pixels_and_position",[&]{
        auto layer=solidPsdLayer(-1,-1,4,4,0,40,80);for(int y=0;y<4;++y)for(int x=0;x<4;++x)layer.channels[0][size_t(y)*4+x]=uint8_t(y*10+x);
        const auto bytes=psdFixture(2,2,{layer});const auto imported=imaging::readPsd(bytes.data(),bytes.size());
        require(imported.document.layers.size()==1,"Partially visible PSD layer was not imported");const auto& result=imported.document.layers[0];
        require(result.transform.x==-1&&result.transform.y==-1&&result.transform.width==4&&result.transform.height==4,"In-limit PSD layer lost its off-canvas pixels");
        require(result.raster&&result.raster->pixel(1,1)==Pixel{11,40,80,255}&&result.raster->pixel(2,1)==Pixel{12,40,80,255}&&result.raster->pixel(1,2)==Pixel{21,40,80,255}&&result.raster->pixel(2,2)==Pixel{22,40,80,255},"In-limit PSD layer changed visible channel samples");
    });
    test("psd_oversized_partial_crop_clips_each_canvas_edge",[&]{
        const auto verify=[&](int documentWidth,int documentHeight,int left,int top,int width,int height,int expectedX,int expectedY,int expectedWidth,int expectedHeight){auto layer=solidPsdLayer(left,top,width,height,201,72,33);const auto bytes=psdFixture(documentWidth,documentHeight,{layer});const auto imported=imaging::readPsd(bytes.data(),bytes.size());require(imported.document.layers.size()==1,"Oversized PSD layer with a visible crop was skipped");const auto& result=imported.document.layers[0];require(result.transform.x==expectedX&&result.transform.y==expectedY&&result.transform.width==expectedWidth&&result.transform.height==expectedHeight,"Oversized PSD layer was cropped to the wrong canvas bounds");require(result.raster&&result.raster->pixel(0,0)==Pixel{201,72,33,255},"Oversized PSD crop changed visible channel values");};
        verify(2,1,-30000,0,30001,1,0,0,1,1);
        verify(2,1,1,0,30001,1,1,0,1,1);
        verify(1,2,0,-30000,1,30001,0,0,1,1);
        verify(1,2,0,1,1,30001,0,1,1,1);
    });
    test("psd_layer_over_pixel_budget_decodes_only_visible_rle_rows",[&]{
        auto layer=solidPackBitsPsdLayer(-10000,-9999,10001,10000,143,89,27);const auto bytes=psdFixture(1,1,{layer});const auto imported=imaging::readPsd(bytes.data(),bytes.size());
        require(imported.document.layers.size()==1,"Oversized PSD layer with a one-pixel visible intersection was skipped");
        require(imported.document.layers[0].transform.x==0&&imported.document.layers[0].transform.y==0&&imported.document.layers[0].transform.width==1&&imported.document.layers[0].transform.height==1,"Over-budget PSD source was not reduced to the visible crop");
        require(imported.document.layers[0].raster->pixel(0,0)==Pixel{143,89,27,255},"Over-budget PSD crop changed its visible pixel");
    });
    test("psd_oversized_partial_crop_raw_packbits_and_zip_prediction",[&]{
        for(const auto compression:{PsdFixtureCompression::Raw,PsdFixtureCompression::PackBits,PsdFixtureCompression::Zip,PsdFixtureCompression::ZipPrediction}){
            auto layer=solidPsdLayer(-30000,0,30001,1,201,72,33,compression);const auto bytes=psdFixture(2,1,{layer});const auto imported=imaging::readPsd(bytes.data(),bytes.size());
            require(imported.document.layers.size()==1,"Oversized PSD source with a small visible crop was skipped");const auto& result=imported.document.layers[0];
            require(result.transform.x==0&&result.transform.y==0&&result.transform.width==1&&result.transform.height==1,"Oversized PSD crop has the wrong on-canvas bounds");
            require(result.raster&&result.raster->pixel(0,0)==Pixel{201,72,33,255},"Oversized PSD crop changed visible channel values");
        }
    });
    test("psb_oversized_partial_crop",[&]{
        auto layer=solidPsdLayer(-30000,0,30001,1,91,102,113);const auto bytes=psdFixture(2,1,{layer},true);const auto imported=imaging::readPsd(bytes.data(),bytes.size());
        require(imported.document.layers.size()==1&&imported.document.layers[0].raster->pixel(0,0)==Pixel{91,102,113,255},"Oversized PSB layer was not cropped to its visible pixels");
        require(imported.document.layers[0].transform.x==0&&imported.document.layers[0].transform.width==1,"PSB crop lost its canvas placement");
    });
    test("psd_oversized_zip_fixed_huffman_backreferences",[&]{
        auto solid=solidPsdLayer(-39999,0,40000,1,201,72,33,PsdFixtureCompression::ZipFixedHuffman);
        const auto solidBytes=psdFixture(1,1,{solid});const auto solidImported=imaging::readPsd(solidBytes.data(),solidBytes.size());
        require(solidImported.document.layers.size()==1,"Oversized ZIP Huffman PSD layer was skipped");
        require(solidImported.document.layers[0].transform.x==0&&solidImported.document.layers[0].transform.width==1,"ZIP Huffman crop lost its canvas placement");
        require(solidImported.document.layers[0].raster&&solidImported.document.layers[0].raster->pixel(0,0)==Pixel{201,72,33,255},"ZIP Huffman distance-1 copies lost the cropped pixel");
        auto patterned=solidPsdLayer(-30000,0,30001,1,0,40,80,PsdFixtureCompression::ZipFixedHuffman);
        for(int x=0;x<30001;++x)patterned.channels[0][size_t(x)]=uint8_t(10+(x%32));
        const auto patternedBytes=psdFixture(1,1,{patterned});const auto patternedImported=imaging::readPsd(patternedBytes.data(),patternedBytes.size());
        require(patternedImported.document.layers.size()==1&&patternedImported.document.layers[0].raster->pixel(0,0)==Pixel{uint8_t(10+(30000%32)),40,80,255},"ZIP Huffman distance-32 copies lost the cropped pixel");
    });
    test("psd_merged_image_raw_and_packbits_without_layers",[&]{
        const auto merged=[&](uint16_t compression){std::vector<uint8_t> out{'8','B','P','S'};psdU16(out,1);out.insert(out.end(),6,0);psdU16(out,3);psdU32(out,1);psdU32(out,2);psdU16(out,8);psdU16(out,3);psdU32(out,0);psdU32(out,0);psdU32(out,0);psdU16(out,compression);
            const std::array<std::array<uint8_t,2>,3> planes{{{10,11},{20,21},{30,31}}};
            if(compression==0){for(const auto& plane:planes)out.insert(out.end(),plane.begin(),plane.end());return out;}
            std::vector<std::vector<uint8_t>> rows;for(const auto& plane:planes)rows.push_back(packBitsRow(plane.data(),plane.size()));for(const auto& row:rows)psdU16(out,uint16_t(row.size()));for(const auto& row:rows)out.insert(out.end(),row.begin(),row.end());return out;};
        for(const uint16_t compression:{uint16_t(0),uint16_t(1)}){const auto bytes=merged(compression);const auto imported=imaging::readPsd(bytes.data(),bytes.size());
            require(imported.document.layers.size()==1&&imported.document.layers[0].raster,"Merged PSD image was not imported");
            require(imported.document.layers[0].raster->pixel(0,0)==Pixel{10,20,30,255}&&imported.document.layers[0].raster->pixel(1,0)==Pixel{11,21,31,255},"Merged PSD image changed channel samples");}
    });
    test("psd_fully_off_canvas_layer_skips_channel_decode",[&]{
        auto offCanvas=solidPsdLayer(-30001,0,30000,1,255,0,0,PsdFixtureCompression::Unsupported);offCanvas.name="off canvas";
        auto visible=solidPsdLayer(0,0,1,1,14,25,36);visible.name="visible";const auto bytes=psdFixture(2,1,{offCanvas,visible});const auto imported=imaging::readPsd(bytes.data(),bytes.size());
        require(imported.document.layers.size()==1&&imported.document.layers[0].name=="visible","Fully off-canvas PSD layer was decoded or interfered with visible layers");
        require(imported.document.layers[0].raster->pixel(0,0)==Pixel{14,25,36,255},"Visible PSD layer changed after skipping an off-canvas channel");
        require(imported.report.find("outside the document canvas")!=std::string::npos,"Skipped off-canvas layer was not reported");
    });
    test("qt_prebusy_bitmap_rejected_without_mutation",[&]{MainWindow w(true);auto&p=w.addProject(document());w.show();QApplication::processEvents();p.projectBusy=true;const auto before=p.document;QImage bitmap(7,5,QImage::Format_ARGB32);bitmap.fill(Qt::green);{QMimeData mime;mime.setImageData(bitmap);require(!drop(w,mime,canvasPoint(w,{64,64})),"ContentView102 must reject a prebusy bitmap drop");}require(ui::ImportQueue::find(&w)==nullptr,"Rejected bitmap must not allocate an import queue");QTest::keyClick(&w,Qt::Key_N,Qt::ControlModifier);QTest::keyClick(&w,Qt::Key_Z,Qt::ControlModifier);QTest::qWait(40);require(p.document==before&&p.history.undoCount()==0,"Busy native commands mutated the project");p.projectBusy=false;require(p.document->layers.empty(),"Rejected bitmap appeared after the busy state cleared");});
    test("welcome_import_starts_in_downloads_and_accepts_psd",[&]{
        PWSTR known=nullptr;
        require(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads,KF_FLAG_DEFAULT,nullptr,&known))&&known,"Downloads folder unavailable");
        const auto expected=QDir::cleanPath(QString::fromWCharArray(known));
        CoTaskMemFree(known);
        require(QDir(expected).exists()&&MainWindow::userDownloadsDirectory()==expected,"Welcome import does not start in Downloads");
        std::vector<uint8_t> bytes;auto u16=[&](int v){bytes.push_back(uint8_t(v>>8));bytes.push_back(uint8_t(v));};auto u32=[&](uint32_t v){for(int shift=24;shift>=0;shift-=8)bytes.push_back(uint8_t(v>>shift));};
        bytes.insert(bytes.end(),{'8','B','P','S'});u16(1);for(int i=0;i<6;++i)bytes.push_back(0);u16(3);u32(1);u32(1);u16(8);u16(3);u32(0);u32(0);u32(0);u16(0);bytes.push_back(255);bytes.push_back(0);bytes.push_back(0);
        const auto file=temporary.filePath("welcome.psd");QFile psd(file);require(psd.open(QIODevice::WriteOnly)&&psd.write(reinterpret_cast<const char*>(bytes.data()),qint64(bytes.size()))==qint64(bytes.size()),"Could not write PSD fixture");psd.close();
        MainWindow window(true);window.show();QApplication::processEvents();
        require(window.findChild<QPushButton*>("welcomeImportImage")!=nullptr,"Welcome import button missing");
        window.importSelectedImages({file});
        QApplication::processEvents(QEventLoop::AllEvents);
        auto* tabs=window.findChild<QTabWidget*>();
        require(tabs&&tabs->count()==1&&tabs->tabText(tabs->currentIndex()).contains("welcome.psd"),"Selected PSD did not replace the empty welcome");
        auto* layers=window.findChild<QTreeWidget*>();
        require(layers&&layers->topLevelItemCount()==1&&layers->topLevelItem(0)->text(0).contains("Background"),"Imported PSD layer was not listed");
        window.openPath(file);
        QApplication::processEvents(QEventLoop::AllEvents);
        require(tabs->count()==2&&tabs->tabText(tabs->currentIndex()).contains("welcome.psd"),"Opening a PSD added an empty project tab");
    });
    test("file_import_shares_welcome_downloads_picker",[&]{
        MainWindow window(true);window.show();QApplication::processEvents();
        require(MainWindow::userDownloadsDirectory()==window.userDownloadsDirectory(),"File import does not share the welcome Downloads folder");
        QAction* import=nullptr;
        for(auto* action:window.findChildren<QAction*>())if(action->property("commandId").toString()=="file.import")import=action;
        require(import!=nullptr,"File > Import Image command missing");
        window.setProperty("testNativeImportPaths",QStringList{first});
        import->trigger();
        until([&]{return ui::ImportQueue::find(&window)!=nullptr;});
        finish(window);
        auto* tabs=window.findChild<QTabWidget*>();
        require(tabs&&tabs->count()==1,"File > Import Image did not import through the production command");
        auto* layers=window.findChild<QTreeWidget*>();
        require(layers&&layers->topLevelItemCount()==1,"File > Import Image did not add the injected image");
        require(window.findChildren<QFileDialog*>().isEmpty(),"File > Import Image still uses a Qt file dialog");
    });
    test("file_import_pins_originating_project",[&]{
        MainWindow window(true);
        auto& origin=window.addProject(document(64,64),"Origin");
        auto& other=window.addProject(document(80,80),"Other",false);
        window.show();QApplication::processEvents();
        auto* tabs=window.findChild<QTabWidget*>();
        require(tabs&&tabs->count()==2,"Pinned import fixture did not keep two projects");
        tabs->setCurrentIndex(0);
        QApplication::processEvents();
        QAction* import=nullptr;
        for(auto* action:window.findChildren<QAction*>())if(action->property("commandId").toString()=="file.import")import=action;
        require(import!=nullptr,"File > Import Image command missing");
        window.setProperty("testNativeImportPaths",QStringList{first});
        import->trigger();
        tabs->setCurrentIndex(1);
        until([&]{return ui::ImportQueue::find(&window)!=nullptr;});
        finish(window);
        require(origin.document&&origin.document->layers.size()==1,"File import did not land on the originating project");
        require(other.document&&other.document->layers.empty(),"File import followed the tab selected while the picker was open");
        require(window.findChildren<QFileDialog*>().isEmpty(),"Pinned File import used a Qt file dialog");
    });
    test("save_project_as_uses_native_path_without_qt_dialog",[&]{
        const auto dest=temporary.filePath("saved-native.comp");
        MainWindow window(true);auto& project=window.addProject(document(32,24),"Untitled");window.show();QApplication::processEvents();
        QAction* save=nullptr,*saveAs=nullptr;
        for(auto* action:window.findChildren<QAction*>()){
            const auto id=action->property("commandId").toString();
            if(id=="file.save")save=action;
            if(id=="file.save_as")saveAs=action;
        }
        require(save&&saveAs,"File > Save Project commands missing");
        window.setProperty("testNativeSavePath",dest);
        saveAs->trigger();
        until([&]{return project.saveJob.isNull();});
        require(QFileInfo(project.path)==QFileInfo(dest)&&QDir(dest).exists(),"Save As did not write the chosen .comp directory");
        require(window.findChildren<QFileDialog*>().isEmpty(),"Save Project As used a Qt file dialog");
        window.setProperty("testNativeSavePath",temporary.filePath("saved-again.comp"));
        save->trigger();
        until([&]{return project.saveJob.isNull();});
        require(QFileInfo(project.path)==QFileInfo(dest),"Repeat Save reopened a picker after a path was chosen");
    });
    test("save_as_reports_busy_native_picker",[&]{
        MainWindow window(true);auto& project=window.addProject(document(32,24),"Untitled");window.show();QApplication::processEvents();
        QAction* saveAs=nullptr;
        for(auto* action:window.findChildren<QAction*>())if(action->property("commandId").toString()=="file.save_as")saveAs=action;
        require(saveAs,"File > Save Project As command missing");
        const auto before=project.path;
        window.setProperty("nativePickerInProgress",true);
        saveAs->trigger();
        QApplication::processEvents();
        require(project.path==before&&!QFileInfo(temporary.filePath("busy-save.comp")).exists(),"Save As wrote a project while another picker was open");
        require(window.statusBar()->currentMessage().contains("file picker"),"Busy picker produced no user-visible message");
        require(window.findChildren<QFileDialog*>().isEmpty(),"Busy Save As opened a Qt file dialog");
    });
    test("qt_opened_psd_report_lists_depth_and_merged_image",[&]{
        std::vector<uint8_t> bytes;auto u16=[&](int v){bytes.push_back(uint8_t(v>>8));bytes.push_back(uint8_t(v));};auto u32=[&](uint32_t v){for(int s=24;s>=0;s-=8)bytes.push_back(uint8_t(v>>s));};
        bytes.insert(bytes.end(),{'8','B','P','S'});u16(1);for(int i=0;i<6;++i)bytes.push_back(0);u16(3);u32(2);u32(2);u16(16);u16(3);u32(0);u32(0);u32(0);u16(0);
        for(int i=0;i<4;++i){bytes.push_back(255);bytes.push_back(0);}for(int i=0;i<16;++i)bytes.push_back(0);
        const auto file=temporary.filePath("merged-16.psd");QFile psd(file);require(psd.open(QIODevice::WriteOnly),"Could not write PSD fixture");require(psd.write(reinterpret_cast<const char*>(bytes.data()),qint64(bytes.size()))==qint64(bytes.size()),"Could not write PSD bytes");psd.close();
        MainWindow w(true);w.show();QApplication::processEvents();
        w.openPath(file);
        QApplication::processEvents();
        auto* tabs=w.findChild<QTabWidget*>();
        require(tabs&&tabs->count()==1&&tabs->tabText(tabs->currentIndex()).contains("merged-16.psd"),"Opened 16-bit PSD did not replace the empty welcome");
        auto* layers=w.findChild<QTreeWidget*>();
        require(layers&&layers->topLevelItemCount()==1&&layers->topLevelItem(0)->text(0).contains("Background"),"Opened 16-bit PSD did not import the merged image");
        require(w.findChildren<QMessageBox*>().isEmpty(),"Opened PSD showed a success dialog");
    });
    test("raw review records an edited exposure and leaves untouched files as shot",[&]{
        QTemporaryDir temporary;require(temporary.isValid(),"Temporary directory failed");
        const std::filesystem::path raw(temporary.filePath("edited.dng").toStdWString());
        ui::RawSettings settings;
        require(reviewRaw(raw,true,true,settings),"Accepted RAW review returned cancel");
        require(settings.count(raw)&&settings[raw].exposure&&*settings[raw].exposure==1.0,"Edited exposure was not recorded");
        require(!settings[raw].kelvin&&!settings[raw].tint&&!settings[raw].noiseReduction,"Untouched RAW settings were recorded");
        const std::filesystem::path untouched(temporary.filePath("untouched.arw").toStdWString());
        require(reviewRaw(untouched,false,true,settings),"Accepted untouched RAW review returned cancel");
        require(settings[untouched]==imaging::RawAdjustments{},"Untouched RAW review recorded settings");
    });
    test("raw review cancel adds nothing to the batch",[&]{
        QTemporaryDir temporary;require(temporary.isValid(),"Temporary directory failed");
        const std::filesystem::path raw(temporary.filePath("cancelled.cr2").toStdWString());
        ui::RawSettings settings;
        require(!reviewRaw(raw,true,false,settings),"Cancelled RAW review reported acceptance");
        require(settings.empty(),"Cancelled RAW review recorded settings");
    });
    test("raw review records the camera raw handoff",[&]{
        QTemporaryDir temporary;require(temporary.isValid(),"Temporary directory failed");
        const std::filesystem::path raw(temporary.filePath("camera.dng").toStdWString());
        ui::RawSettings settings;
        require(reviewRaw(raw,true,true,settings,true),"Camera Raw review was not accepted");
        require(settings.openCameraRaw&&*settings.openCameraRaw==raw&&settings[raw].exposure==1.0,"Camera Raw choice or settings were not preserved");
    });
    test("raw review permits only one camera raw handoff per batch",[&]{
        QTemporaryDir temporary;require(temporary.isValid(),"Temporary directory failed");
        const std::filesystem::path firstRaw(temporary.filePath("first.dng").toStdWString());
        const std::filesystem::path secondRaw(temporary.filePath("second.dng").toStdWString());
        const ui::RawImportDialog::Developer stub=[](const std::filesystem::path&,const imaging::ImportOptions&,std::uint32_t){
            imaging::RawDevelopment result;result.image.image={2,2,8,std::vector<std::uint8_t>(16,255)};
            result.capabilities={true,true,2000,12000};result.asShot={0.0,5000};return result;
        };
        int reviewed=0;bool secondDisabled=false,timedOut=false;
        QTimer poll;poll.setInterval(10);
        QObject::connect(&poll,&QTimer::timeout,[&]{
            auto* dialog=QApplication::activeModalWidget();
            if(!dialog||dialog->objectName()!="rawImportDialog")return;
            auto* import=dialog->findChild<QPushButton*>("rawImportButton");
            auto* camera=dialog->findChild<QPushButton*>("rawCameraRawButton");
            if(!import||!camera||!import->isEnabled())return;
            if(reviewed++==0)camera->click();else {auto* notice=dialog->findChild<QLabel*>("rawCameraRawBatchNotice");secondDisabled=!camera->isEnabled()&&notice&&notice->isVisible();import->click();}
        });
        QTimer watchdog;watchdog.setSingleShot(true);watchdog.setInterval(15000);
        QObject::connect(&watchdog,&QTimer::timeout,[&]{timedOut=true;poll.stop();if(auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget()))dialog->reject();});
        ui::RawSettings settings;poll.start();watchdog.start();
        const auto accepted=ui::reviewRawImports(nullptr,{firstRaw,secondRaw},settings,stub);
        poll.stop();watchdog.stop();
        require(!timedOut&&accepted&&reviewed==2&&secondDisabled,"The second RAW review did not explain and enforce the one-image limit");
        require(settings.openCameraRaw&&*settings.openCameraRaw==firstRaw&&settings.count(secondRaw),"The first Camera Raw choice was overwritten");
    });
    test("camera raw handoff tracks the correct imported layer",[&]{
        ui::ImportBatch batch;batch.files={path(first),path(second)};batch.openCameraRaw=path(first);
        auto result=ui::prepareImportBatch({},batch);
        require(result.imported==2&&result.after.document&&result.cameraRawLayer==result.after.document->layers[0].id,"Handoff selected a different layer in a mixed batch");
        require(result.cameraRawLayer!=result.after.active,"Test did not cover a non-final layer");
    });
    test("camera raw import waits for an existing filter panel",[&]{
        const char* fixture=std::getenv("COMPOSITOR_RAW_FIXTURE");
        if(!fixture||!*fixture){std::cout<<"SKIP camera raw import panel interaction: COMPOSITOR_RAW_FIXTURE is not set\n";return;}
        const QString raw=QString::fromLocal8Bit(fixture);
        require(QFileInfo::exists(raw),"COMPOSITOR_RAW_FIXTURE does not exist");
        MainWindow w(true);auto& project=w.addProject(document());w.show();QApplication::processEvents();
        w.importSelectedImages({first},project.canvas);finish(w);
        QAction* gaussian=nullptr;QAction* import=nullptr;
        for(auto* action:w.findChildren<QAction*>()){
            const auto id=action->property("commandId").toString();
            if(id=="filter.gaussian")gaussian=action;else if(id=="file.import")import=action;
        }
        require(gaussian&&gaussian->isEnabled()&&import,"Filter or import command unavailable");
        gaussian->trigger();QApplication::processEvents();
        QPointer<QDialog> original;
        for(auto* panel:w.findChildren<QDialog*>("filterPanel"))if(panel->isVisible())original=panel;
        require(original&&import->isEnabled(),"Existing filter panel or File > Import unavailable");
        bool selected=false;
        QTimer poll;poll.setInterval(10);
        QObject::connect(&poll,&QTimer::timeout,&w,[&]{
            auto* dialog=QApplication::activeModalWidget();
            if(!dialog||dialog->objectName()!="rawImportDialog"||selected)return;
            auto* button=dialog->findChild<QPushButton*>("rawCameraRawButton");
            if(button&&button->isEnabled()){selected=true;button->click();}
        });
        w.setProperty("testNativeImportPaths",QStringList{raw});poll.start();import->trigger();
        QElapsedTimer elapsed;elapsed.start();
        auto* queue=ui::ImportQueue::find(&w);
        while((!selected||queue->idle())&&elapsed.elapsed()<30000)QTest::qWait(10);
        poll.stop();
        require(selected&&!queue->idle()&&original&&original->isVisible(),"Camera Raw request was not queued behind the existing panel");
        require(project.document&&project.document->layers.size()==1,"Import started before the existing edit was closed");
        original->reject();
        elapsed.restart();QDialog* camera=nullptr;
        while(elapsed.elapsed()<30000&&!camera){
            QTest::qWait(10);
            for(auto* panel:w.findChildren<QDialog*>("filterPanel"))
                if(panel!=original&&panel->isVisible()&&panel->windowTitle()=="Camera Raw Filter")camera=panel;
        }
        require(camera&&queue->idle(),"Queued import did not open Camera Raw after closing the existing panel");
        require(project.document&&project.document->layers.size()==2&&project.active==project.document->layers.back().id,"Camera Raw did not target the imported layer");
        camera->reject();QApplication::processEvents();
    });
    test("raw review skips non-RAW files without a dialog",[&]{
        QTemporaryDir temporary;require(temporary.isValid(),"Temporary directory failed");
        ui::RawSettings settings;
        const auto png=std::filesystem::path(temporary.filePath("plain.png").toStdWString());
        require(ui::reviewRawImports(nullptr,{png},settings,imaging::developRaw),"Non-RAW file was blocked by the RAW review");
        require(settings.empty(),"Non-RAW file recorded RAW settings");
    });
    test("psd_rectangle_shape_replaces_cached_pixels_and_keeps_layer_properties",[&]{
        auto layer=solidPsdLayer(10,12,30,28,0,0,255);layer.name="Rect";layer.opacity=128;layer.hidden=true;
        layer.blocks=shapeBlocks(origination(1,12,10,28,30),vectorPath({rectangleKnots(10,12,30,28)},64,48),solidFill(200,100,50),strokeState(false,true));
        const auto imported=importPsd({layer});
        require(imported.document.layers.size()==1&&imported.warnings.empty(),"Editable rectangle reported a warning or was dropped");
        const auto& result=imported.document.layers[0];
        require(result.name=="Rect"&&result.transform.x==10&&result.transform.y==12&&result.transform.width==20&&result.transform.height==16,"Rectangle placement or size changed");
        require(!result.visible&&std::abs(result.opacity-128/255.)<1e-6,"Rectangle lost visibility or opacity");
        const auto style=editing::decodeShapeStyle(result.shapeJson);
        require(style.kind==editing::ShapeKind::Rectangle&&style.cornerRadius==0&&std::abs(style.red-200/255.)<1e-9,"Rectangle style was not decoded from the fill");
        require(result.raster&&result.raster->width==20&&result.raster->height==16&&result.raster->pixel(5,5)==Pixel{200,100,50,255},"Shape raster kept the cached blue pixels");
    });
    test("psd_rounded_ellipse_and_uncached_shapes_become_editable",[&]{
        auto rounded=shapeLayer(0,0,20,10,shapeBlocks(origination(2,0,0,10,20,{4,4,4,4}),vectorPath({roundedKnots(0,0,20,10,4)},64,48),solidFill(10,20,30)),"Rounded");
        auto ellipse=shapeLayer(30,5,50,15,shapeBlocks(origination(5,5,30,15,50),vectorPath({ellipseKnots(30,5,50,15)},64,48),solidFill(0,255,0)),"Ellipse");
        const auto imported=importPsd({rounded,ellipse});
        require(imported.warnings.empty()&&imported.document.layers.size()==2,"Uncached rounded or elliptical shape was not imported");
        const auto& r=imported.document.layers[0];const auto& e=imported.document.layers[1];
        require(editing::decodeShapeStyle(r.shapeJson).cornerRadius==4&&r.raster->pixel(10,5)==Pixel{10,20,30,255}&&r.raster->pixel(0,0)==Pixel{0,0,0,0},"Rounded rectangle lost its radius or coverage");
        require(editing::decodeShapeStyle(e.shapeJson).kind==editing::ShapeKind::Ellipse&&e.raster->pixel(10,5)==Pixel{0,255,0,255}&&e.raster->pixel(0,0)==Pixel{0,0,0,0},"Ellipse coverage is wrong");
    });
    test("psb_and_fractional_rectangle_round_raster_size_once",[&]{
        auto layer=solidPsdLayer(10,12,30,28,0,0,255);
        layer.blocks=shapeBlocks(origination(1,12.4,10.6,28.2,30.9),vectorPath({rectangleKnots(10.6,12.4,30.9,28.2)},64,48),solidFill(200,100,50));
        const auto imported=importPsd({layer},64,48,true);
        require(imported.warnings.empty()&&imported.document.layers.size()==1,"PSB fractional rectangle reported a warning or was dropped");
        const auto& result=imported.document.layers[0];
        require(std::abs(result.transform.x-10.6)<1e-9&&result.transform.width==20&&result.transform.height==16&&result.raster->width==20&&result.raster->height==16,"Fractional PSB rectangle did not round its raster once");
        require(result.raster->pixel(5,5)==Pixel{200,100,50,255},"PSB shape raster has the wrong fill");
    });
    test("psd_unsupported_vector_shapes_keep_pixels_with_warning",[&]{
        const auto cached=[](PsdFixtureBlocks blocks,const char* name){auto layer=solidPsdLayer(10,12,30,28,0,0,255);layer.name=name;layer.blocks=std::move(blocks);return layer;};
        const auto rect=vectorPath({rectangleKnots(10,12,30,28)},64,48);
        auto truncated=vectorPath({rectangleKnots(10,12,30,28)},64,48);truncated.resize(truncated.size()-26);
        struct Case { const char* fragment; PsdFixtureLayer layer; };
        const std::vector<Case> cases{
            {"gradient fills",cached(shapeBlocks(origination(1,12,10,28,30),rect,gradientFill()),"Gradient")},
            {"dashed strokes",cached(shapeBlocks(origination(1,12,10,28,30),rect,solidFill(200,100,50),strokeDescriptor(4,0,0,255,nullptr,true)),"Dashed")},
            {"semi-transparent strokes",cached(shapeBlocks(origination(1,12,10,28,30),rect,solidFill(200,100,50),strokeDescriptor(4,0,0,255,nullptr,false,50)),"Translucent")},
            {"could not be read",cached(shapeBlocks(origination(1,12,10,28,30),rect,solidFill(200,100,50),strokeState(true,true)),"Stroke")},
            {"does not match",cached(shapeBlocks(origination(1,12,10,28,30),vectorPath({rectangleKnots(13,12,33,28)},64,48),solidFill(200,100,50)),"Offset")},
            {"unequal corner radii",cached(shapeBlocks(origination(2,12,10,28,30,{4,4,4,8}),vectorPath({roundedKnots(10,12,30,28,4)},64,48),solidFill(200,100,50)),"Unequal")},
            {"compound",cached(shapeBlocks(origination(1,12,10,28,30),vectorPath({rectangleKnots(10,12,30,28),rectangleKnots(10,12,30,28)},64,48),solidFill(200,100,50)),"Compound")},
            {"truncated",cached(shapeBlocks(origination(1,12,10,28,30),truncated,solidFill(200,100,50)),"Truncated")},
            {"only rectangle and ellipse",cached(shapeBlocks(origination(7,12,10,28,30),rect,solidFill(200,100,50)),"Type")},
            {"bounds could not be read",cached(shapeBlocks(origination(1,std::nan(""),10,28,30),rect,solidFill(200,100,50)),"NaN")},
            {"not an axis-aligned rectangle",cached(shapeBlocks({},vectorPath({{sharp(20,12),sharp(30,24),sharp(20,36),sharp(10,24)}},64,48),solidFill(200,100,50)),"Diamond")},
        };
        for(const auto& item:cases){
            const auto imported=importPsd({item.layer,plainLayer()});
            require(imported.document.layers.size()==2,"Unsupported shape layer was dropped instead of kept as pixels");
            const Layer* shape=nullptr;for(const auto& layer:imported.document.layers)if(layer.name==item.layer.name)shape=&layer;
            require(shape&&shape->shapeJson.empty()&&shape->raster&&shape->raster->pixel(5,5)==Pixel{0,0,255,255},"Unsupported shape became editable or lost its cached pixels");
            require(hasWarning(imported,item.fragment),"Unsupported shape reported no conversion warning");
        }
    });
    test("psd_shape_solid_fill_in_soco_block_becomes_editable",[&]{
        // Photoshop stores this fill in a SoCo block without the class prefix that the vscg form carries.
        const auto fill=solidFill(200,100,50);
        auto blocks=shapeBlocks(origination(1,12,10,28,30),vectorPath({rectangleKnots(10,12,30,28)},64,48),{});
        blocks.push_back({"SoCo",std::vector<uint8_t>(fill.begin()+4,fill.end())});
        auto layer=solidPsdLayer(10,12,30,28,0,0,255);layer.blocks=std::move(blocks);
        const auto imported=importPsd({layer});
        require(imported.warnings.empty()&&imported.document.layers.size()==1,"SoCo fill shape reported a warning or was dropped");
        const auto& result=imported.document.layers[0];
        require(editing::decodeShapeStyle(result.shapeJson).kind==editing::ShapeKind::Rectangle&&result.raster->pixel(5,5)==Pixel{200,100,50,255},"SoCo fill was not used for the editable rectangle");
    });
    test("psd_centred_stroke_becomes_editable_stroke_inside_bounds",[&]{
        auto layer=solidPsdLayer(10,12,30,28,0,0,255);layer.name="Stroked";
        layer.blocks=shapeBlocks(origination(1,12,10,28,30),vectorPath({rectangleKnots(10,12,30,28)},64,48),solidFill(200,100,50),strokeDescriptor(4,0,0,255));
        const auto imported=importPsd({layer});
        require(imported.warnings.empty()&&imported.document.layers.size()==1,"Editable stroke reported a warning or was dropped");
        const auto& result=imported.document.layers[0];
        // The 4 px centred stroke grows the 20 x 16 path by 2 px on each side, so the band fills the first 4 px of the raster.
        require(result.transform.x==8&&result.transform.y==10&&result.transform.width==24&&result.transform.height==20,"Centred stroke did not grow the shape bounds");
        const auto style=editing::decodeShapeStyle(result.shapeJson);
        require(style.strokeEnabled&&style.strokeWidth==4&&style.strokeBlue==1&&style.red==200/255.,"Stroke colour or width was not decoded");
        require(result.raster->pixel(0,10)==Pixel{0,0,255,255}&&result.raster->pixel(3,10)==Pixel{0,0,255,255},"Stroke band is not blue at the outer edge");
        require(result.raster->pixel(4,10)==Pixel{200,100,50,255}&&result.raster->pixel(12,10)==Pixel{200,100,50,255},"Fill is not inside the stroke band");
    });
    test("psd_inside_stroke_keeps_shape_bounds",[&]{
        auto layer=solidPsdLayer(10,12,30,28,0,0,255);layer.name="Inside";
        layer.blocks=shapeBlocks(origination(1,12,10,28,30),vectorPath({rectangleKnots(10,12,30,28)},64,48),solidFill(200,100,50),strokeDescriptor(4,0,0,255,"strokeStyleAlignInside"));
        const auto imported=importPsd({layer});
        require(imported.warnings.empty()&&imported.document.layers.size()==1,"Inside stroke reported a warning or was dropped");
        const auto& result=imported.document.layers[0];
        require(result.transform.x==10&&result.transform.y==12&&result.transform.width==20&&result.transform.height==16,"Inside stroke changed the shape bounds");
        require(result.raster->pixel(0,10)==Pixel{0,0,255,255}&&result.raster->pixel(3,10)==Pixel{0,0,255,255},"Inside stroke band is not at the outer edge");
        require(result.raster->pixel(4,10)==Pixel{200,100,50,255},"Fill is not inside the inside-aligned stroke band");
    });
    test("psd_outside_stroke_grows_shape_bounds_by_full_width",[&]{
        auto layer=solidPsdLayer(10,12,30,28,0,0,255);layer.name="Outside";
        layer.blocks=shapeBlocks(origination(1,12,10,28,30),vectorPath({rectangleKnots(10,12,30,28)},64,48),solidFill(200,100,50),strokeDescriptor(4,0,0,255,"strokeStyleAlignOutside"));
        const auto imported=importPsd({layer});
        require(imported.warnings.empty()&&imported.document.layers.size()==1,"Outside stroke reported a warning or was dropped");
        const auto& result=imported.document.layers[0];
        // The 4 px outside stroke adds its full width to each side of the 20 x 16 path.
        require(result.transform.x==6&&result.transform.y==8&&result.transform.width==28&&result.transform.height==24,"Outside stroke did not grow the shape bounds by its full width");
        require(result.raster->pixel(0,10)==Pixel{0,0,255,255}&&result.raster->pixel(3,10)==Pixel{0,0,255,255},"Outside stroke band is not at the outer edge");
        require(result.raster->pixel(4,10)==Pixel{200,100,50,255}&&result.raster->pixel(23,10)==Pixel{200,100,50,255},"Fill is not inside the outside-aligned stroke band");
    });
    test("psd_path_without_origination_is_not_inferred_from_bounds",[&]{
        const auto diamond=vectorPath({{sharp(20,12),sharp(30,24),sharp(20,36),sharp(10,24)}},64,48);
        const auto imported=importPsd({shapeLayer(10,12,30,36,shapeBlocks({},diamond,solidFill(200,100,50)),"Diamond"),plainLayer()});
        require(imported.document.layers.size()==1&&imported.document.layers[0].name=="Plain","Undecidable path was imported as a layer");
        require(hasWarning(imported,"could not be imported"),"Dropped path did not report its omission");
        const auto rect=vectorPath({rectangleKnots(10,12,30,28)},64,48);
        const auto axis=importPsd({shapeLayer(10,12,30,28,shapeBlocks({},rect,solidFill(200,100,50)),"Axis")});
        require(axis.warnings.empty()&&axis.document.layers.size()==1&&!axis.document.layers[0].shapeJson.empty(),"Sharp axis-aligned rectangle without origination was not editable");
    });
    test("psd_oversized_shape_is_reported_not_truncated",[&]{
        auto layer=shapeLayer(-20000,0,20000,1,shapeBlocks(origination(1,0,-20000,1,20000),vectorPath({rectangleKnots(-20000,0,20000,1)},30000,1),solidFill(200,100,50)),"Huge");
        const auto imported=importPsd({layer,plainLayer()},30000,1);
        require(hasWarning(imported,"too large"),"Oversized shape reported no limit");
        require(imported.document.layers.size()==1&&imported.document.layers[0].name=="Plain","Oversized shape was partially installed");
    });
    test("psd_shape_edit_save_reopen_keeps_editable_shape",[&]{
        QTemporaryDir temporary;require(temporary.isValid(),"Temporary directory failed");
        auto layer=solidPsdLayer(10,12,30,28,0,0,255);
        layer.blocks=shapeBlocks(origination(1,12,10,28,30),vectorPath({rectangleKnots(10,12,30,28)},64,48),solidFill(200,100,50));
        auto imported=importPsd({layer});
        auto shape=imported.document.layers[0];
        auto style=editing::decodeShapeStyle(shape.shapeJson);style.red=0.25;
        shape=editing::restyleShape(shape,style);
        require(shape.raster->pixel(5,5)==Pixel{64,100,50,255},"Restyling an imported shape did not redraw its fill");
        shape.transform.width=24;shape=editing::redrawShape(shape);
        require(shape.raster->width==24&&shape.raster->pixel(5,5)==Pixel{64,100,50,255},"Resizing an imported shape did not redraw it");
        auto edited=imported.document;edited.layers[0]=shape;
        const auto project=path(temporary.filePath("shape.comp"));
        ProjectStore(makeWicProjectCodec()).save(project,edited,edited.layers[0].id);
        auto reopened=ProjectStore(makeWicProjectCodec()).load(project);
        const auto& result=reopened.document.layers[0];
        require(result.shapeJson==shape.shapeJson&&result.transform.width==24&&result.transform.height==16,"Reopened shape lost its editable metadata or placement");
        require(result.raster&&result.raster->pixel(5,5)==Pixel{64,100,50,255},"Reopened shape lost its redrawn pixels");
    });
    test("psd_warning_dialog_cancel_leaves_workspace_unchanged",[&]{
        QTemporaryDir temporary;require(temporary.isValid(),"Temporary directory failed");
        auto unsupported=solidPsdLayer(10,12,30,28,0,0,255);unsupported.name="Gradient";unsupported.blocks=shapeBlocks(origination(1,12,10,28,30),vectorPath({rectangleKnots(10,12,30,28)},64,48),gradientFill());
        const auto bytes=psdFixture(64,48,{unsupported,plainLayer()});
        const QString file=temporary.filePath("warnings.psd");QFile out(file);require(out.open(QIODevice::WriteOnly)&&out.write(QByteArray(reinterpret_cast<const char*>(bytes.data()),qsizetype(bytes.size())))==qint64(bytes.size()),"PSD fixture write failed");out.close();
        MainWindow w(true);auto& p=w.addProject(document());w.show();QApplication::processEvents();
        auto* tabs=w.findChild<QTabWidget*>();require(tabs,"Tab widget absent");const int before=tabs->count();const auto original=p.document;
        require(answerPsdWarnings("psdCancelButton",[&]{w.openPath(file);}),"PSD warning dialog did not appear");
        QApplication::processEvents();
        require(tabs->count()==before&&p.document==original&&p.history.undoCount()==0,"Cancelled PSD import changed tabs or history");
    });
    test("psd_warning_dialog_continue_imports_pixels_and_warning_layer",[&]{
        QTemporaryDir temporary;require(temporary.isValid(),"Temporary directory failed");
        auto unsupported=solidPsdLayer(10,12,30,28,0,0,255);unsupported.name="Gradient";unsupported.blocks=shapeBlocks(origination(1,12,10,28,30),vectorPath({rectangleKnots(10,12,30,28)},64,48),gradientFill());
        const auto bytes=psdFixture(64,48,{unsupported,plainLayer()});
        const QString file=temporary.filePath("continue.psd");QFile out(file);require(out.open(QIODevice::WriteOnly)&&out.write(QByteArray(reinterpret_cast<const char*>(bytes.data()),qsizetype(bytes.size())))==qint64(bytes.size()),"PSD fixture write failed");out.close();
        MainWindow w(true);w.addProject(document());w.show();QApplication::processEvents();
        auto* tabs=w.findChild<QTabWidget*>();require(tabs,"Tab widget absent");const int before=tabs->count();
        require(answerPsdWarnings("psdContinueButton",[&]{w.openPath(file);}),"PSD warning dialog did not appear");
        QApplication::processEvents();
        require(tabs->count()==before+1,"Continue did not open the imported PSD");
        require(tabs->tabText(tabs->currentIndex())=="continue.psd","Continue did not select the imported PSD tab");
        auto* layers=w.findChild<QTreeWidget*>();require(layers,"Layer tree absent");
        QStringList names;for(QTreeWidgetItemIterator it(layers);*it;++it)names<<(*it)->text(0);
        require(names.join("|").contains("Gradient")&&names.join("|").contains("Plain"),"Continued PSD import did not show both layers");
    });
    test("cmyk_missing_profile_cancel_all_entry_points",[&]{
        QSettings settings;const auto oldBytes=settings.value("cmyk/fallbackBytes"),oldName=settings.value("cmyk/fallbackName");settings.remove("cmyk/fallbackBytes");settings.remove("cmyk/fallbackName");
        struct Restore{std::function<void()> run;~Restore(){run();}} restore{[&]{if(oldBytes.isValid())settings.setValue("cmyk/fallbackBytes",oldBytes);if(oldName.isValid())settings.setValue("cmyk/fallbackName",oldName);}};
        const auto bytes=cmyk_test::psd();const auto file=temporary.filePath("unprofiled-cmyk.psd");QFile output(file);require(output.open(QIODevice::WriteOnly),"CMYK fixture write");output.write(reinterpret_cast<const char*>(bytes.data()),qint64(bytes.size()));output.close();
        for(int entry=0;entry<3;++entry){MainWindow window(true);auto& project=window.addProject(document());const auto before=project.document;auto* tabs=window.findChild<QTabWidget*>();require(tabs,"Missing tabs");const int tabCount=tabs->count();bool asked=false;
            QTimer timer;timer.setInterval(10);QObject::connect(&timer,&QTimer::timeout,&window,[&]{if(auto* message=window.findChild<QMessageBox*>("missingCmykProfile")){asked=true;message->done(QMessageBox::Cancel);}});timer.start();
            if(entry==0)window.openPath(file);else if(entry==1)window.importSelectedImages({first,file},project.canvas);else window.receiveDropPaths({first,file},project.canvas,{});
            // A cancelled drop batch never creates an ImportQueue, so wait only for the drop review.
            if(entry==2)until([&]{auto*d=ui::WorkspaceDropQueue::find(&window);return !d||d->idle();});timer.stop();require(asked&&project.document==before&&project.history.undoCount()==0&&tabs->count()==tabCount,"CMYK cancellation changed document/history/tabs or imported earlier RGB file");
        }
    });
    test("hdr_explicit_batch_preserves_pixels_and_one_history_entry",[&]{
        QTemporaryDir directory;auto exr=directory.filePath("bright.exr");auto pixels=makeDeep(8,4);for(int y=0;y<4;++y)for(int x=0;x<8;++x)pixels->setPixel(x,y,{2,1,.5f,1});imaging::encodeExr(path(exr),*pixels);
        auto png=image(directory.path(),"ordinary.png",8,4);MainWindow w(true);auto& p=w.addProject(document());const auto before=p.document;
        w.importSelectedImages({png,exr},p.canvas);finish(w);
        require(p.document->layers.size()==2&&p.document->layers.back().deep&&p.document->layers.back().deep->isHdr(),"HDR import lost deep pixels");
        require(p.document->layers.back().deep->pixel(7,0)[0]>1.99f&&p.history.undoCount()==1,"HDR batch clipped pixels or split history");
        require(p.history.undo()->document==before,"HDR batch undo did not restore document");
    });
    test("hdr_failed_preflight_does_not_open_tab_or_commit_mixed_batch",[&]{
        QTemporaryDir directory;auto bad=directory.filePath("broken.exr");{QFile file(bad);require(file.open(QIODevice::WriteOnly),"Bad HDR fixture write");file.write("not an EXR");}
        auto png=image(directory.path(),"ordinary.png");MainWindow w(true);auto& p=w.addProject(document());auto before=p.document;auto* tabs=w.findChild<QTabWidget*>();const int count=tabs->count();bool sawError=false;
        QTimer poll;poll.setInterval(0);QObject::connect(&poll,&QTimer::timeout,&w,[&]{for(auto* box:w.findChildren<QMessageBox*>()){sawError=true;box->accept();}});poll.start();w.importSelectedImages({png,bad},p.canvas);poll.stop();
        require(sawError&&p.document==before&&p.history.undoCount()==0&&tabs->count()==count,"Failed HDR preflight changed workspace/history");
    });
    test("hdr_cancelled_preflight_preserves_workspace",[&]{
        QTemporaryDir directory;auto exr=directory.filePath("large.exr");imaging::encodeExr(path(exr),1000,1000,[](int,std::span<WidePixel> row){std::fill(row.begin(),row.end(),WidePixel{2,1,.5f,1});});
        MainWindow w(true);auto& p=w.addProject(document());const auto before=p.document;auto* tabs=w.findChild<QTabWidget*>();const int count=tabs->count();bool cancelled=false;
        QTimer poll;poll.setInterval(0);QObject::connect(&poll,&QTimer::timeout,&w,[&]{if(auto* progress=w.findChild<QProgressDialog*>("hdrImportPreflight")){if(auto* button=progress->findChild<QPushButton*>()){cancelled=true;button->click();poll.stop();}}});poll.start();w.openPath(exr);poll.stop();
        require(cancelled&&p.document==before&&p.history.undoCount()==0&&tabs->count()==count,"Cancelled HDR preflight changed tabs/document/history");
    });
    test("hdr_workspace_drop_retains_hdr_and_url_history",[&]{
        QTemporaryDir directory;auto exr=directory.filePath("dropped.exr");auto pixels=makeDeep(8,4);for(int y=0;y<4;++y)for(int x=0;x<8;++x)pixels->setPixel(x,y,{2,1,.5f,1});imaging::encodeExr(path(exr),*pixels);
        MainWindow w(true);auto& p=w.addProject(document());w.show();QApplication::processEvents();QMimeData mime;mime.setUrls({QUrl::fromLocalFile(exr)});require(drop(w,mime,canvasPoint(w,{64,64})),"HDR drop rejected");finish(w);require(p.document->layers.size()==1&&p.document->layers[0].deep&&p.document->layers[0].deep->isHdr()&&p.history.undoCount()==1,"HDR drop lost pixels/history");
    });
    std::cout<<"IMPORT_UI_SUMMARY passed="<<passed<<" failed="<<failed<<'\n';return failed?1:0;
}
