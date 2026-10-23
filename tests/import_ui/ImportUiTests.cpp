#include "ui/ImportActions.h"
#include "ui/WorkspaceDropQueue.h"
#include "ui/RawImportDialog.h"
#include "ui/PropertyControls.h"
#include "ui/MainWindow.h"
#include "imaging/psd/PsdDocument.h"
#include "imaging/wic_codec.h"
#include <QAction>
#include <QApplication>
#include <QDragEnterEvent>
#include <QDir>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressDialog>
#include <QPushButton>
#include <QSlider>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTest>
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
#include <iostream>
#include <map>
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
struct PsdFixtureLayer { int top{},left{},bottom{},right{}; std::array<std::vector<uint8_t>,3> channels; std::array<uint8_t,3> solidChannels{}; bool generatedSolid{}; PsdFixtureCompression compression{PsdFixtureCompression::Raw}; std::string name{"Fixture"}; };
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
        record.insert(record.end(),{'8','B','I','M','n','o','r','m'});record.push_back(255);record.push_back(0);record.push_back(0);record.push_back(0);
        std::vector<uint8_t> extra;psdU32(extra,0);psdU32(extra,0);extra.push_back(uint8_t(layer.name.size()));extra.insert(extra.end(),layer.name.begin(),layer.name.end());while(extra.size()%4)extra.push_back(0);psdU32(record,uint32_t(extra.size()));record.insert(record.end(),extra.begin(),extra.end());records.push_back(std::move(record));
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
bool reviewRaw(const std::filesystem::path& file,bool editExposure,bool accept,std::map<std::filesystem::path,imaging::RawAdjustments>& settings){
    const ui::RawImportDialog::Developer stub=[](const std::filesystem::path&,const imaging::ImportOptions&,std::uint32_t){
        imaging::RawDevelopment developed;
        developed.image.image={2,2,8,std::vector<std::uint8_t>(16,255)};
        developed.image.metadata.decoder="stub";
        developed.capabilities={true,true,true,true,true,2000,12000};
        developed.asShot={0.0,0.0,0.0,5000};
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
        button->click();
    });
    QTimer watchdog;watchdog.setSingleShot(true);watchdog.setInterval(15000);
    QObject::connect(&watchdog,&QTimer::timeout,[&]{timedOut=true;poll.stop();if(auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget()))dialog->reject();});
    poll.start();watchdog.start();
    const auto accepted=ui::reviewRawImports(nullptr,{file},settings,stub);
    poll.stop();watchdog.stop();
    require(!timedOut,"RAW review dialog never became ready");
    return accepted;
}
int main(int argc,char**argv){QApplication app(argc,argv);int passed=0,failed=0;auto test=[&](const char* name,const std::function<void()>&run){try{run();++passed;std::cout<<"PASS "<<name<<'\n';}catch(const std::exception&e){++failed;std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n';}};
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
        QApplication::processEvents();
        require(QFileInfo(project.path)==QFileInfo(dest)&&QDir(dest).exists(),"Save As did not write the chosen .comp directory");
        require(window.findChildren<QFileDialog*>().isEmpty(),"Save Project As used a Qt file dialog");
        window.setProperty("testNativeSavePath",temporary.filePath("saved-again.comp"));
        save->trigger();
        QApplication::processEvents();
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
        std::map<std::filesystem::path,imaging::RawAdjustments> settings;
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
        std::map<std::filesystem::path,imaging::RawAdjustments> settings;
        require(!reviewRaw(raw,true,false,settings),"Cancelled RAW review reported acceptance");
        require(settings.empty(),"Cancelled RAW review recorded settings");
    });
    test("raw review skips non-RAW files without a dialog",[&]{
        QTemporaryDir temporary;require(temporary.isValid(),"Temporary directory failed");
        std::map<std::filesystem::path,imaging::RawAdjustments> settings;
        const auto png=std::filesystem::path(temporary.filePath("plain.png").toStdWString());
        require(ui::reviewRawImports(nullptr,{png},settings,imaging::developRaw),"Non-RAW file was blocked by the RAW review");
        require(settings.empty(),"Non-RAW file recorded RAW settings");
    });
    std::cout<<"IMPORT_UI_SUMMARY passed="<<passed<<" failed="<<failed<<'\n';return failed?1:0;
}
