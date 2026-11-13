#include "effects/Adjustments.h"
#include "graphics/SamplingSource.h"
#include "imaging/wic_codec.h"
#include "layers/LayerOperations.h"
#include "persistence/ProjectStore.h"
#include "ui/MainWindow.h"
#include "ui/ViewportCompositeJob.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QShortcut>
#include <QThread>
#include <QWheelEvent>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <psapi.h>
#include <objbase.h>

using namespace compositor;
using Clock=std::chrono::steady_clock;
using namespace std::chrono_literals;

namespace {
void require(bool condition,const char* message){
    if(!condition)throw std::runtime_error(message);
}

void waitUntil(const std::function<bool()>& ready,const char* timeout="Timed out waiting for a presented frame"){
    const auto deadline=Clock::now()+45s;
    while(!ready()&&Clock::now()<deadline){
        QApplication::processEvents();
        QThread::msleep(1);
    }
    require(ready(),timeout);
}

std::shared_ptr<const Raster> noise(int edge,int layer){
    std::vector<Pixel> pixels(size_t(edge)*edge);
    for(int y=0;y<edge;++y)for(int x=0;x<edge;++x){
        uint32_t bits=uint32_t(x)*73856093u^uint32_t(y)*19349663u^uint32_t(layer+1)*83492791u;
        bits=(bits^(bits>>16))*0x7feb352du;
        bits=(bits^(bits>>15))*0x846ca68bu;
        bits^=bits>>16;
        const unsigned alpha=layer==0?255:48+(bits>>24)%176;
        const auto channel=[alpha](uint32_t value){return uint8_t(((value&255)*alpha+127)/255);};
        pixels[size_t(y)*edge+x]={channel(bits),channel(bits>>8),channel(bits>>16),uint8_t(alpha)};
    }
    return Raster::fromRgba(edge,edge,reinterpret_cast<const uint8_t*>(pixels.data()),size_t(edge)*4);
}

Document fixture(int edge,int visible,int hidden,bool complex,bool lens){
    Document document;
    document.id=newId();
    document.width=document.height=edge;
    const int count=visible+hidden;
    // At high layer counts use smaller source rasters scaled to the full canvas
    // so the same view workload remains within the project's source-pixel budget.
    const int source=std::min(edge,int(std::sqrt(double(maxDocumentImagePixels)/count)));
    for(int index=0;index<count;++index){
        Layer layer;
        layer.id=newId();
        layer.transform={0,0,double(edge),double(edge)};
        layer.visible=index<visible;
        layer.raster=noise(source,index);
        if(complex&&index==1){
            auto mask=std::make_shared<GrayRaster>();
            mask->width=mask->height=source;
            mask->pixels.resize(size_t(source)*source);
            for(int y=0;y<source;++y)for(int x=0;x<source;++x)
                mask->pixels[size_t(y)*source+x]=uint8_t((x*13+y*17)%256);
            layer.mask=Mask{mask};
            layer.blend=Blend::Screen;
        }
        if(complex&&index==2){
            layer.maskSourceId=document.layers.back().id;
            layer.blend=Blend::Multiply;
        }
        document.layers.push_back(std::move(layer));
    }
    if(complex||lens){
        Layer adjustment;
        adjustment.id=newId();
        adjustment.adjustmentJson=effects::defaultAdjustmentJson(lens?"Lens Effects":"Invert");
        adjustment.transform={0,0,double(edge),double(edge)};
        document.layers.push_back(std::move(adjustment));
    }
    return document;
}

Document duplicatePhoto(const std::filesystem::path& path){
    const auto image=imaging::WicCodec::decode(path).image;
    const auto raster=Raster::fromRgba(int(image.width),int(image.height),image.pixels.data(),image.stride);
    Document document;
    document.id=newId();
    document.width=raster->width;
    document.height=raster->height;
    Layer layer;
    layer.id=newId();
    layer.transform={0,0,double(raster->width),double(raster->height)};
    layer.raster=raster;
    document.layers.push_back(layer);
    for(int index=1;index<5;++index)
        document=layers::duplicateLayer(document,{},layer.id).document;
    return document;
}

int photoWindow(QApplication& app){
    const auto* photo=_wgetenv(L"ZOOM_PHOTO");
    require(photo,"Set ZOOM_PHOTO to the local JPEG");
    MainWindow window;
    auto& project=window.addProject(duplicatePhoto(photo));
    auto& canvas=*project.canvas;
    canvas.setFixedSize(640,640);
    canvas.navigatorProvider={};
    canvas.showPixelGrid=false;
    canvas.setRulersVisible(false);
    canvas.setTransformOverlay(std::nullopt);
    canvas.pointerHover={};
    const auto* initialZoom=_wgetenv(L"ZOOM_START");
    const double start=initialZoom?std::stod(initialZoom):.25;
    const auto reset=[&]{
        canvas.zoomAt(start,canvas.contentRect().center());
        canvas.pan={};
        canvas.invalidateComposite();
        canvas.update();
    };
    auto* shortcut=new QShortcut(QKeySequence(Qt::Key_F8),&window);
    QObject::connect(shortcut,&QShortcut::activated,&window,reset);
    window.show();
    reset();
    require(canvas.devicePixelRatioF()==1,"Desktop probe requires display scaling at 100%");
    const auto origin=canvas.mapToGlobal(QPoint(0,0));
    std::cout<<"CANVAS "<<origin.x()<<' '<<origin.y()<<' '<<canvas.width()<<' '<<canvas.height()<<std::endl;
    return app.exec();
}

void wheel(NativeCanvas& canvas,QPointF anchor,int direction){
    QWheelEvent event(anchor,canvas.mapToGlobal(anchor),{},{0,direction*120},
        Qt::NoButton,Qt::AltModifier,Qt::NoScrollPhase,false);
    QApplication::sendEvent(&canvas,&event);
}

int pixelDifference(const QImage& actual,const QImage& expected){
    require(actual.size()==expected.size()&&!actual.isNull(),"Unexpected frame dimensions");
    int difference=0;
    for(int y=0;y<actual.height();++y){
        const auto* a=actual.constScanLine(y);
        const auto* b=expected.constScanLine(y);
        for(int x=0;x<actual.width()*4;++x)difference=std::max(difference,std::abs(int(a[x])-int(b[x])));
    }
    return difference;
}

void checkFrames(bool lens,int interval,const std::filesystem::path& photo={}){
    const auto document=photo.empty()?fixture(352,3,1,true,lens):duplicatePhoto(photo);
    CompositeCache referenceCache;
    std::shared_ptr<const LayerRenderPreview> referencePreview;
    if(!photo.empty()){
        auto original=std::make_shared<LayerRenderPreview>();
        original->layer=document.layers.back();
        original->identity=std::make_shared<int>(0);
        referencePreview=std::move(original);
    }
    NativeCanvas reference(true);
    reference.setFixedSize(512,512);
    reference.setDocumentSize(document.width,document.height);
    reference.viewportProvider=[&](double x,double y,double w,double h,double units){
        return referenceCache.renderViewport(document,x,y,w,h,units,64,256,referencePreview);
    };
    reference.move(0,0);
    reference.show();
    MainWindow window(true);
    auto& project=window.addProject(document);
    auto& canvas=*project.canvas;
    canvas.setFixedSize(reference.size());
    window.move(550,0);
    window.show();
    window.showNormal();
    window.raise();
    canvas.setCursor(Qt::ArrowCursor);
    canvas.pointerHover={};
    canvas.navigatorProvider={};
    const auto missing=std::filesystem::absolute(L"__missing_zoom_test_profile__.icc");
    reference.setDisplayProfileOverride(missing);
    canvas.setDisplayProfileOverride(missing);
    canvas.setRulersVisible(false);
    canvas.showGuides=false;
    canvas.showPixelGrid=false;
    canvas.setTransformOverlay(std::nullopt);
    const auto anchor=QPointF(240,244);
    const auto physicalScale=canvas.devicePixelRatioF();
    const auto initial=.41*physicalScale;
    canvas.zoomAt(initial,anchor);
    QElapsedTimer initialization;
    initialization.start();
    while((!canvas.deviceReady()||!reference.deviceReady())&&initialization.elapsed()<3000){
        QApplication::processEvents();
        canvas.repaint();
        reference.repaint();
        QThread::msleep(1);
    }
    if(!canvas.deviceReady()||!reference.deviceReady()){
        // Force the initial draw even when the launcher hid the startup window.
        canvas.captureRendered();
        reference.captureRendered();
    }

    const auto captureReference=[&](double zoom,QPointF pan){
        reference.zoom=zoom;
        reference.pan=pan;
        return reference.captureRendered();
    };
    const auto original=captureReference(canvas.zoom,canvas.pan);
    canvas.repaint();
    std::vector<NativeCanvas::PresentedFrame> frames;
    canvas.readPresentationPixels=true;
    canvas.observePresentation=[&](auto frame){frames.push_back(std::move(frame));};
    const auto anchorBefore=canvas.documentPoint(anchor);
    // Paint on the next event-loop iteration, as the normal desktop paint does.
    auto next=Clock::now();
    for(int direction:{1,1,-1,-1,1}){
        waitUntil([&]{return Clock::now()>=next;});
        wheel(canvas,anchor,direction);
        canvas.repaint();
        QApplication::processEvents();
        next+=std::chrono::milliseconds(interval);
    }
    const auto expectFinal=[&]{
        return !frames.empty()&&frames.back().viewportReady&&frames.back().zoom==canvas.zoom&&frames.back().pan==canvas.pan;
    };
    waitUntil(expectFinal);
    const auto finalZoom=canvas.zoom;
    const auto finalPan=canvas.pan;
    canvas.observePresentation={};
    const auto anchorAfter=canvas.documentPoint(anchor);
    require(std::abs(anchorBefore.x()-anchorAfter.x())<1e-7&&std::abs(anchorBefore.y()-anchorAfter.y())<1e-7,
        "Wheel zoom moved the document point under the cursor");
    require(*project.document==document&&!project.history.canUndo()&&!project.history.modified(),
        "Wheel zoom changed the document or history");

    const auto final=captureReference(finalZoom,finalPan);
    // Negative control: intentionally stretch the old view into a new frame.
    const auto stretched=original.scaled(final.width()*5/4,final.height()*5/4,Qt::IgnoreAspectRatio,Qt::SmoothTransformation)
        .copy(final.width()/8,final.height()/8,final.width(),final.height());
    require(pixelDifference(stretched,final)>0,"Frame oracle accepted a resampled placeholder");
    int maximum=0;
    for(const auto& frame:frames){
        const auto expected=captureReference(frame.zoom,frame.pan);
        maximum=std::max(maximum,pixelDifference(frame.pixels,expected));
    }
    std::cout<<"frames="<<frames.size()<<" max_channel_difference="<<maximum<<'\n';
    require(maximum==0,"A presented zoom frame differs from the synchronous render");

    if(lens||interval!=30||!photo.empty())return;
    auto* zoomJob=dynamic_cast<ui::ViewportCompositeJob*>(
        canvas.findChild<QObject*>("wheelZoomCompositeJob",Qt::FindDirectChildrenOnly));
    require(zoomJob,"Canvas zoom worker is missing");
    auto gate=std::make_shared<std::atomic_bool>(false);
    zoomJob->setProceedGate(gate);
    frames.clear();
    canvas.observePresentation=[&](auto frame){frames.push_back(std::move(frame));};
    wheel(canvas,anchor,-1);
    canvas.repaint();
    QApplication::processEvents();
    require(frames.empty(),"Canvas presented a frame while its zoom render was held");
    auto& other=window.addProject(fixture(96,1,0,false,false),"Zoom test second tab",false);
    other.canvas->repaint();
    auto* tabs=qobject_cast<QTabWidget*>(window.centralWidget());
    require(tabs&&tabs->currentWidget()==other.page,"Second test project did not become active");
    gate->store(true);
    QApplication::processEvents();
    require(frames.empty(),"A hidden canvas presented a zoom result");
    tabs->setCurrentWidget(project.page);
    waitUntil(expectFinal,"Original tab did not render its latest zoom after switching back");
    canvas.observePresentation={};
    require(*project.document==document&&!project.history.modified(),"Switching tabs changed the original document");
    gate->store(false);
    wheel(canvas,anchor,1);
    const auto closingIndex=tabs->currentIndex();
    require(QMetaObject::invokeMethod(tabs,"tabCloseRequested",Qt::DirectConnection,Q_ARG(int,closingIndex)),
        "Could not send the real tab close signal");
    QApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    require(tabs->currentWidget()==other.page&&other.document&&other.document->width==96,
        "Closing a tab during zoom affected its neighbor");
    std::cout<<"PASS zoom invalidation on tab switch and close\n";
}

double milliseconds(Clock::duration duration){
    return std::chrono::duration<double,std::milli>(duration).count();
}

void summarize(const char* label,std::vector<double> values){
    require(!values.empty(),"No timing samples");
    std::sort(values.begin(),values.end());
    const auto percentile=[&](double p){return values[size_t(std::ceil(values.size()*p))-1];};
    std::cout<<' '<<label<<"_p50="<<percentile(.5)<<" p95="<<percentile(.95)<<" max="<<values.back();
}

void benchmark(int edge,int visible,int hidden,bool lens,int notches,int interval,int direction,int trials,bool cold){
    require(edge>=256&&edge<=10000&&visible>=1&&hidden>=0&&visible+hidden<=100,
        "Invalid benchmark document size or layer count");
    require(notches>=1&&notches<=20&&interval>=0&&interval<=1000&&(direction==1||direction==-1)&&trials>=1,
        "Invalid benchmark wheel sequence");
    const auto* photo=_wgetenv(L"ZOOM_PHOTO");
    const auto document=photo?duplicatePhoto(photo):fixture(edge,visible,hidden,false,lens);
    require(!photo||(std::max(document.width,document.height)==edge&&visible==5&&hidden==0&&!lens),
        "Photo mode requires the longest image edge and exactly five visible layers");
    MainWindow window;
    auto& project=window.addProject(document);
    auto& canvas=*project.canvas;
    canvas.setFixedSize(640,640);
    window.show();
    window.showNormal();
    window.raise();
    const auto anchor=canvas.contentRect().center();
    const double low=512./std::max(document.width,document.height),high=low*std::pow(1.25,notches);
    const double start=direction>0?low:std::min(high,32.);
    std::vector<double> first,last,event,spacing,dispatch;
    size_t dropped=0;
    std::cout<<"edge="<<edge<<" visible="<<visible<<" hidden="<<hidden<<" lens="<<lens
             <<" notches="<<notches<<" interval_ms="<<interval<<" direction="<<direction
             <<" cold="<<cold<<" platform="<<QApplication::platformName().toStdString()<<std::endl;
    for(int trial=-2;trial<trials;++trial){
        bool initialReady=false;
        canvas.observePresentation=[&](const auto& frame){initialReady|=frame.viewportReady&&frame.zoom==canvas.zoom;};
        canvas.zoomAt(start,anchor);
        if(cold){
            project.composite.reset();
            canvas.invalidateComposite();
        }
        canvas.repaint();
        waitUntil([&]{return initialReady;},"Initial benchmark view did not present");
        if(cold)graphics::ReducedSourceCache::shared().clear();
        struct Notch {Clock::time_point due;double zoom{};QPointF pan;bool presented{};};
        std::vector<Notch> sent;
        const auto begin=Clock::now();
        auto previous=begin;
        canvas.observePresentation=[&](const auto& frame){
            const auto now=Clock::now();
            if(trial>=0)spacing.push_back(milliseconds(now-previous));
            previous=now;
            if(!frame.viewportReady)return;
            for(size_t i=0;i<sent.size();++i){
                auto& notch=sent[i];
                if(notch.presented||frame.zoom!=notch.zoom||frame.pan!=notch.pan)continue;
                notch.presented=true;
                if(trial<0)continue;
                const double latency=milliseconds(now-notch.due);
                event.push_back(latency);
                if(i==0)first.push_back(latency);
                if(i+1==size_t(notches))last.push_back(latency);
            }
        };
        for(int i=0;i<notches;++i){
            const auto due=begin+std::chrono::milliseconds(i*interval);
            waitUntil([&]{return Clock::now()>=due;});
            if(trial>=0)dispatch.push_back(milliseconds(Clock::now()-due));
            wheel(canvas,anchor,direction);
            sent.push_back({due,canvas.zoom,canvas.pan,false});
        }
        waitUntil([&]{return sent.back().presented;},"Final benchmark view did not present");
        canvas.observePresentation={};
        if(trial>=0)dropped+=std::count_if(sent.begin(),sent.end(),[](const auto& notch){return !notch.presented;});
    }
    require(canvas.deviceError().isEmpty(),"Native canvas reported a presentation error");
    PROCESS_MEMORY_COUNTERS memory{};
    require(GetProcessMemoryInfo(GetCurrentProcess(),&memory,sizeof(memory)),"Cannot read peak working set");
    std::cout<<"adapter="<<canvas.adapterName().toStdString()<<" dpr="<<canvas.devicePixelRatioF();
    if(!first.empty())summarize("first_notch",first);
    summarize("presented_notches",event);
    summarize("last_notch",last);
    summarize("present_interval",spacing);
    summarize("dispatch_delay",dispatch);
    std::cout<<" superseded="<<dropped<<" peak_working_set="<<memory.PeakWorkingSetSize<<'\n';
}
}

int main(int argc,char** argv){
    std::vector<std::string> arguments(argv+1,argv+argc);
    const auto apartment=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if(FAILED(apartment)&&apartment!=RPC_E_CHANGED_MODE)return 2;
    struct Apartment {
        HRESULT result;
        ~Apartment(){if(SUCCEEDED(result))CoUninitialize();}
    } com{apartment};
    QApplication app(argc,argv);
    try{
        if(arguments.size()==1&&arguments.front()=="photo-window")return photoWindow(app);
        if(arguments.size()==1&&arguments.front()=="photo-project"){
            const auto* photo=_wgetenv(L"ZOOM_PHOTO");
            const auto* destination=_wgetenv(L"ZOOM_PROJECT");
            require(photo&&destination,"Set ZOOM_PHOTO and ZOOM_PROJECT");
            const auto document=duplicatePhoto(photo);
            ProjectStore(makeWicProjectCodec()).save(destination,document,document.layers.back().id);
            return 0;
        }
        if(arguments.size()==1&&arguments.front()=="photo-frames"){
            const auto* photo=_wgetenv(L"ZOOM_PHOTO");
            require(photo,"Set ZOOM_PHOTO to the local JPEG");
            checkFrames(false,30,photo);
            checkFrames(false,100,photo);
            return 0;
        }
        if(arguments.empty()||(arguments.size()==1&&arguments.front()=="frames")){
            checkFrames(false,30);
            checkFrames(false,100);
            checkFrames(true,10);
        }
        else if(arguments.size()==9)benchmark(std::stoi(arguments[0]),std::stoi(arguments[1]),std::stoi(arguments[2]),
            std::stoi(arguments[3])!=0,std::stoi(arguments[4]),std::stoi(arguments[5]),std::stoi(arguments[6]),
            std::stoi(arguments[7]),std::stoi(arguments[8])!=0);
        else throw std::runtime_error("Usage: native_zoom_tests [edge visible hidden lens notches interval_ms direction trials cold]");
    }catch(const std::exception& error){
        std::cerr<<error.what()<<'\n';
        return 1;
    }
    return 0;
}
