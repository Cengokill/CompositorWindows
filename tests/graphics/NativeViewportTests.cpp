#include "ui/NativeCanvas.h"
#include "ui/MainWindow.h"
#include <objbase.h>
#include <QApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QMouseEvent>
#include <QTabletEvent>
#include <QWheelEvent>
#include <QNativeGestureEvent>
#include <QPointingDevice>
#include <QThread>
#include <algorithm>
#include <functional>
#include <iostream>
#include <stdexcept>
using namespace compositor;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void closeEnough(double a,double b,const char* message){require(std::abs(a-b)<1e-7,message);}
void closeEnough(QPointF a,QPointF b,const char* message){closeEnough(a.x(),b.x(),message);closeEnough(a.y(),b.y(),message);}
void flush(){QElapsedTimer timer;timer.start();do{QApplication::processEvents();QThread::msleep(5);}while(timer.elapsed()<40);}
void waitFor(int milliseconds){QElapsedTimer timer;timer.start();do{QApplication::processEvents();QThread::msleep(5);}while(timer.elapsed()<milliseconds);}
void pointer(NativeCanvas& canvas,QEvent::Type type,QPointF position,Qt::MouseButton button,Qt::MouseButtons buttons){QMouseEvent event(type,position,canvas.mapToGlobal(position),button,buttons,Qt::NoModifier);QApplication::sendEvent(&canvas,&event);}
std::shared_ptr<const Raster> stripes(){std::vector<Pixel> pixels(16*16);for(int y=0;y<16;++y)for(int x=0;x<16;++x)pixels[size_t(y)*16+x]=x%2?Pixel{0,0,255,255}:Pixel{255,0,0,255};return Raster::fromRgba(16,16,reinterpret_cast<const uint8_t*>(pixels.data()),16*4);}
}
int main(int argc,char** argv){const auto apartment=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(apartment)&&apartment!=RPC_E_CHANGED_MODE)return 2;QApplication app(argc,argv);int passed=0,failed=0;auto test=[&](const char* name,const std::function<void()>& run){try{run();++passed;std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& error){++failed;std::cout<<"FAIL "<<name<<": "<<error.what()<<'\n';}};
    test("native_actual_pixels_physical_dpr2",[&]{NativeCanvas canvas(true);canvas.resize(400,300);canvas.setRaster(stripes());canvas.show();flush();closeEnough(canvas.devicePixelRatioF(),2,"Run this physical-pixel witness with QT_SCALE_FACTOR=2 on the 100% display");canvas.zoomAt(1,QPointF(canvas.width()/2.,canvas.height()/2.));canvas.repaint();flush();canvas.repaint();flush();require(canvas.deviceReady(),"WARP canvas device unavailable");require(canvas.deviceError().isEmpty(),"WARP canvas draw failed");closeEnough(canvas.pointsPerPixel(),.5,"Widget uses DIP zoom");auto image=canvas.captureRendered();require(image.width()==800&&image.height()==600,"Swap chain dimensions do not follow physical pixels");auto origin=canvas.viewMapping().toView({0,0});const int left=int(std::lround(origin.x*2)),top=int(std::lround(origin.y*2));for(int x=0;x<16;++x){const auto actual=image.pixelColor(left+x,top+7),expected=x%2?QColor(0,0,255):QColor(255,0,0);if(actual!=expected)std::cout<<"Physical sample "<<x<<" actual "<<actual.red()<<','<<actual.green()<<','<<actual.blue()<<" origin "<<left<<','<<top<<'\n';require(actual==expected,"Document stripes are not exactly one physical pixel wide");}if(argc>1)require(image.save(QString::fromLocal8Bit(argv[1])),"Capture file save failed");std::cout<<"WARP capture "<<image.width()<<'x'<<image.height()<<" DPR "<<canvas.devicePixelRatioF()<<" document width physical 16\n";});
    test("native_fit_resize_pan_and_zoom_anchor",[]{NativeCanvas canvas(true);canvas.resize(896,696);canvas.setRaster(Raster::filled(400,300));canvas.show();flush();auto fitZoom=[&]{const auto frame=canvas.contentRect();return std::min(std::max(1.,frame.width()-96)/400,std::max(1.,frame.height()-96)/300)*canvas.devicePixelRatioF();};canvas.fit();closeEnough(canvas.zoom,fitZoom(),"Fit uses the area inside the rulers");canvas.resize(496,396);flush();closeEnough(canvas.zoom,fitZoom(),"Widget followsFit resize");closeEnough(canvas.pan,{},"Fit pan");canvas.panBy({70,-20});require(!canvas.followsFit(),"Widget pan retained fit");const auto centerOf=[&]{return canvas.contentRect().center();};auto centerDoc=canvas.documentPoint(centerOf());canvas.resize(700,500);flush();closeEnough(canvas.documentPoint(centerOf()),centerDoc,"Widget manual viewport resize shifted center");const QPointF anchor{87,124};auto doc=canvas.documentPoint(anchor);canvas.zoomAt(7,anchor);closeEnough(canvas.documentPoint(anchor),doc,"Widget anchored zoom moved document point");});
    test("native_repeated_hand_mouse_events_and_release",[]{NativeCanvas canvas(true);canvas.resize(400,300);canvas.setRaster(Raster::filled(600,400));canvas.show();flush();canvas.zoomAt(1,canvas.contentRect().center());QPointF press;int downs=0,moves=0,ups=0,cancels=0;canvas.pointerDown=[&](QPointF p,Qt::KeyboardModifiers){press=p;++downs;};canvas.pointerMove=[&](QPointF p,Qt::KeyboardModifiers){canvas.panBy((p-press)*canvas.pointsPerPixel());++moves;};canvas.pointerUp=[&](QPointF,Qt::KeyboardModifiers){++ups;};canvas.pointerCancel=[&]{++cancels;};pointer(canvas,QEvent::MouseButtonPress,{100,100},Qt::LeftButton,Qt::LeftButton);for(QPointF p:std::array<QPointF,4>{{{110,100},{120,100},{130,107},{125,112}}})pointer(canvas,QEvent::MouseMove,p,Qt::NoButton,Qt::LeftButton);pointer(canvas,QEvent::MouseButtonRelease,{125,112},Qt::LeftButton,Qt::NoButton);closeEnough(canvas.pan,{25,12},"Hand repeated mouse events stalled");require(downs==1&&moves==4&&ups==1&&cancels==0,"Normal release triggered cancellation");require(QWidget::mouseGrabber()!=&canvas,"Normal release kept mouse grab");pointer(canvas,QEvent::MouseButtonPress,{100,100},Qt::LeftButton,Qt::LeftButton);QEvent deactivate(QEvent::WindowDeactivate);QApplication::sendEvent(&canvas,&deactivate);require(cancels==1&&QWidget::mouseGrabber()!=&canvas,"Deactivation failed to cancel/release mouse exactly once");});
    test("native_viewport_patch_lod_uses_physical_scale",[]{NativeCanvas canvas(true);canvas.resize(400,300);canvas.setDocumentSize(30000,30000);double requestX=0,requestY=0,requestW=0,requestH=0,requestStep=0;int requests=0;canvas.viewportProvider=[&](double x,double y,double w,double h,double step){requestX=x;requestY=y;requestW=w;requestH=h;requestStep=step;++requests;return CompositeViewport{};};canvas.show();flush();canvas.zoomAt(.5,{200,150});canvas.repaint();flush();const auto frame=canvas.contentRect();const double scale=canvas.pointsPerPixel();closeEnough(requestStep,1/canvas.zoom,"LOD counted DIPs instead of physical pixels");closeEnough(requestW,frame.width()/scale,"Viewport document width omitted backingScale");closeEnough(requestH,frame.height()/scale,"Viewport document height omitted backingScale");closeEnough(QPointF(requestX,requestY),canvas.documentPoint(frame.topLeft()),"Patch origin differs from event mapping");require(requests>0,"Viewport provider never called");canvas.setDocumentSize(0,0);const auto before=requests;canvas.repaint();flush();require(requests==before,"Empty document retained stale provider render");});
    test("native_gradient_overlay_outside_document_without_patch",[]{NativeCanvas canvas(true);canvas.resize(400,300);canvas.setDocumentSize(20,20);canvas.viewportProvider=[](double,double,double,double,double){return CompositeViewport{};};canvas.show();flush();canvas.zoomAt(2,{200,150});canvas.setGradientLine(std::pair{Point{-130,-40},Point{-30,-40}});canvas.repaint();flush();canvas.repaint();flush();require(canvas.deviceReady(),"Gradient canvas device unavailable");auto image=canvas.captureRendered();const auto midpoint=canvas.viewMapping().toView({-80,-40});const auto color=image.pixelColor(int(midpoint.x*2),int(midpoint.y*2));require(color.red()>220&&color.green()>220&&color.blue()>220,"Gradient line missing outside document or absent patch");const auto endpoint=canvas.viewMapping().toView({-130,-40});const auto handle=image.pixelColor(int((endpoint.x+3)*2),int(endpoint.y*2));require(handle.red()>220&&handle.green()>220&&handle.blue()>220,"Gradient endpoint missing");canvas.setGradientLine(std::nullopt);canvas.repaint();flush();canvas.repaint();flush();image=canvas.captureRendered();require(image.pixelColor(int(midpoint.x*2),int(midpoint.y*2)).red()<80,"Cancelled gradient line remained");});
    test("native_brush_ring_hardness_and_clone_marker_render",[&]{NativeCanvas canvas(true);canvas.resize(400,300);canvas.setDocumentSize(20,20);canvas.show();flush();canvas.setBrushCursor(Point{100,100},40,.5,Point{160,100});canvas.repaint();flush();canvas.repaint();flush();auto image=canvas.captureRendered();require(image.pixelColor(158,200).red()>180&&image.pixelColor(160,200).red()<80,"Outer brush ring white/black outline missing");bool inner=false;for(int y=178;y<=222;++y)for(int x=178;x<=222;++x){const double radius=std::hypot(x-200.,y-200.);if(radius>17&&radius<23&&image.pixelColor(x,y).red()>180)inner=true;}require(inner,"Hardness ring missing");require(image.pixelColor(320,200).red()<80&&image.pixelColor(324,202).red()>180,"Clone source crosshair missing");if(argc>1)require(image.save(QString::fromLocal8Bit(argv[1])+".brush.png"),"Brush cursor screenshot save failed");});
    test("native_precise_discrete_wheel_magnify_and_navigation_guard",[]{NativeCanvas canvas(true);canvas.resize(400,300);canvas.setDocumentSize(200,100);canvas.show();flush();canvas.zoomAt(1,{200,150});const auto initial=canvas.pan;const QPointF anchor{70,90};auto wheel=[&](QPoint pixel,QPoint angle,Qt::KeyboardModifiers modifiers){QWheelEvent event(anchor,canvas.mapToGlobal(anchor),pixel,angle,Qt::NoButton,modifiers,Qt::NoScrollPhase,false);QApplication::sendEvent(&canvas,&event);};wheel({3,7},{},{});closeEnough(canvas.pan,initial+QPointF(3,7),"Precise scroll lost DIP delta");wheel({},{120,-120},{});closeEnough(canvas.pan,initial+QPointF(15,-5),"Discrete scroll normalization differs");const auto scrolled=canvas.pan;const auto zoomBeforeCtrl=canvas.zoom;wheel({},{0,120},Qt::ControlModifier);closeEnough(canvas.zoom,zoomBeforeCtrl,"Ctrl wheel still zooms");closeEnough(canvas.pan,scrolled+QPointF(0,12),"Ctrl wheel did not pan");const auto document=canvas.documentPoint(anchor);wheel({0,10},{},Qt::AltModifier);closeEnough(canvas.zoom,std::exp(.15),"Alt wheel forward did not zoom in");closeEnough(canvas.documentPoint(anchor),document,"Wheel zoom moved anchor");QNativeGestureEvent magnify(Qt::ZoomNativeGesture,QPointingDevice::primaryPointingDevice(),2,anchor,anchor,canvas.mapToGlobal(anchor),.25,{});QApplication::sendEvent(&canvas,&magnify);closeEnough(canvas.zoom,std::exp(.15)*1.25,"Native magnify not proportional");closeEnough(canvas.documentPoint(anchor),document,"Native magnify moved anchor");const auto zoomed=canvas.zoom;wheel({0,-10},{},Qt::AltModifier);closeEnough(canvas.zoom,zoomed*std::exp(-.15),"Alt wheel backward did not zoom out");const auto beforeNotch=canvas.zoom;wheel({},{0,120},Qt::AltModifier);closeEnough(canvas.zoom,beforeNotch*1.25,"Alt wheel notch did not zoom in");wheel({},{0,-120},Qt::AltModifier);closeEnough(canvas.zoom,beforeNotch,"Alt wheel notch did not zoom out");const double zoom=canvas.zoom;const auto pan=canvas.pan;canvas.navigationAllowed=[]{return false;};wheel({0,20},{},{});wheel({0,20},{},Qt::ControlModifier);QApplication::sendEvent(&canvas,&magnify);closeEnough(canvas.zoom,zoom,"Busy gesture allowed zoom");closeEnough(canvas.pan,pan,"Busy gesture allowed pan");});
    test("native_navigator_rectangle_click_drag_and_limits",[]{
        NativeCanvas canvas(true);canvas.resize(400,300);canvas.setDocumentSize(800,600);canvas.show();flush();
        canvas.fit();flush();require(!canvas.navigatorPlacement().visible,"Fitted document still showed the navigator");
        int tools=0;canvas.pointerDown=[&](QPointF,Qt::KeyboardModifiers){++tools;};canvas.pointerMove=[&](QPointF,Qt::KeyboardModifiers){++tools;};canvas.pointerUp=[&](QPointF,Qt::KeyboardModifiers){++tools;};
        const QPointF anchor{200,150};
        for(double zoom:std::array<double,3>{2,4,8}){
            canvas.zoomAt(zoom,anchor);flush();
            const auto placement=canvas.navigatorPlacement();require(placement.visible,"Zoomed document hid the navigator");
            const auto frame=canvas.contentRect();
            const double visibleW=std::min(800.,frame.width()/canvas.pointsPerPixel()),visibleH=std::min(600.,frame.height()/canvas.pointsPerPixel());
            closeEnough(placement.viewport.width()/placement.map.width(),visibleW/800,"Navigator width diverged from the visible document");
            closeEnough(placement.viewport.height()/placement.map.height(),visibleH/600,"Navigator height diverged from the visible document");
        }
        canvas.zoomAt(4,anchor);const auto before=canvas.navigatorPlacement();canvas.panBy({36,-18});flush();
        const auto shifted=canvas.navigatorPlacement();
        const double scale=canvas.pointsPerPixel();
        closeEnough(shifted.viewport.x(),before.viewport.x()-36/scale/800*before.map.width(),"Pan did not move the navigator rectangle");
        closeEnough(shifted.viewport.y(),before.viewport.y()+18/scale/600*before.map.height(),"Vertical pan did not move the navigator rectangle");
        const auto map=canvas.navigatorPlacement().map;
        const auto viewCenter=[&]{return canvas.contentRect().center();};
        pointer(canvas,QEvent::MouseButtonPress,map.center(),Qt::LeftButton,Qt::LeftButton);
        closeEnough(canvas.documentPoint(viewCenter()),QPointF(400,300),"Navigator click did not center the document");
        pointer(canvas,QEvent::MouseMove,map.topLeft()+QPointF(-40,-30),Qt::NoButton,Qt::LeftButton);
        closeEnough(canvas.documentPoint(viewCenter()),QPointF(0,0),"Navigator drag left the document");
        pointer(canvas,QEvent::MouseMove,map.bottomRight()+QPointF(50,40),Qt::NoButton,Qt::LeftButton);
        closeEnough(canvas.documentPoint(viewCenter()),QPointF(800,600),"Navigator drag passed the opposite document corner");
        pointer(canvas,QEvent::MouseButtonRelease,map.bottomRight(),Qt::LeftButton,Qt::NoButton);
        require(tools==0,"Navigator input reached the active tool");
        require(QWidget::mouseGrabber()!=&canvas,"Navigator release kept the mouse grab");
    });
    test("native_navigator_defers_samples_until_editing_pauses",[]{
        NativeCanvas canvas(true);canvas.resize(400,300);canvas.setDocumentSize(800,600);canvas.show();flush();canvas.zoomAt(4,{200,150});
        int calls=0;canvas.navigatorProvider=[&](int edge){++calls;require(edge>=32&&edge<=256,"Navigator requested an unbounded composite");auto raster=Raster::filled(80,60,{255,0,0,255});return CompositeViewport{raster,0,0,10,800,600};};
        canvas.repaint();flush();require(calls==0,"Repaint sampled the navigator");
        canvas.scheduleNavigator();waitFor(120);require(calls==0,"Navigator sampled before the idle interval");
        canvas.scheduleNavigator();waitFor(180);require(calls==0,"A new edit did not restart the navigator idle interval");
        waitFor(250);require(calls==1,"Navigator missed the idle sample");
        canvas.setNavigatorDeferred(true);canvas.scheduleNavigator();waitFor(450);require(calls==1,"Navigator sampled during a stroke");
        canvas.setNavigatorDeferred(false);waitFor(450);require(calls==2,"Navigator stayed deferred after the stroke");
        canvas.repaint();flush();canvas.repaint();flush();auto image=canvas.captureRendered();
        const auto map=canvas.navigatorPlacement().map;const double dpr=canvas.devicePixelRatioF();
        const QPoint sample(int(std::lround((map.left()+8)*dpr)),int(std::lround((map.top()+8)*dpr)));
        const auto color=image.pixelColor(sample);
        require(color.red()>200&&color.green()<40&&color.blue()<40,"Navigator composite missing at this display scale");
    });
    test("native_hidden_navigator_composites_only_after_it_becomes_visible",[]{
        NativeCanvas canvas(true);canvas.resize(400,300);canvas.setDocumentSize(800,600);canvas.show();flush();
        require(!canvas.navigatorPlacement().visible,"Fitted document showed the navigator");
        int calls=0;canvas.navigatorProvider=[&](int){++calls;return CompositeViewport{Raster::filled(80,60,{0,180,0,255}),0,0,10,800,600};};
        canvas.scheduleNavigator();waitFor(450);require(calls==0,"Fitted canvas composited a hidden navigator");
        canvas.zoomAt(4,{200,150});waitFor(450);require(calls==1,"Zoom did not composite the navigator that became visible");
        canvas.fit();canvas.scheduleNavigator();waitFor(450);require(calls==1,"Fitting the canvas composited the hidden navigator again");
    });
    test("native_navigator_project_stroke_keeps_overview_until_release",[]{
        MainWindow window(true);
        Document document;document.id=newId();document.width=480;document.height=360;
        Layer layer;layer.id=newId();layer.name="Navigator";layer.opacity=1;layer.raster=Raster::filled(480,360,{255,255,255,255});layer.transform={0,0,480,360,0,false,false,Transform::Sampling::Nearest};document.layers={layer};
        auto& project=window.addProject(std::move(document));window.resize(960,720);window.show();flush();
        auto* canvas=project.canvas;canvas->zoomAt(8,{canvas->width()/2.,canvas->height()/2.});flush();
        require(canvas->navigatorPlacement().visible,"Project navigator hidden while zoomed");
        waitFor(500);const auto original=*project.document;int calls=0;auto provider=canvas->navigatorProvider;
        canvas->navigatorProvider=[&](int edge){++calls;return provider(edge);};
        QKeyEvent key(QEvent::KeyPress,Qt::Key_B,Qt::NoModifier);QApplication::sendEvent(&window,&key);
        canvas->scheduleNavigator();
        const auto center=canvas->documentPoint({canvas->width()/2.,canvas->height()/2.});
        canvas->pointerDown(center,{});canvas->pointerMove(center+QPointF(48,0),{});
        require(canvas->navigatorDeferred()&&project.document&&*project.document==original&&project.history.undoCount()==0,"Brush step committed or refreshed the navigator");
        waitFor(450);require(calls==0,"Navigator composite refreshed during the stroke");
        canvas->pointerUp(center+QPointF(48,0),{});
        require(!canvas->navigatorDeferred()&&project.history.undoCount()==1&&project.history.undoName()=="Brush","Brush release did not schedule one undoable navigator update");
        waitFor(500);require(calls>=1,"Navigator missed the composite after the stroke");
        auto image=canvas->captureRendered();const auto map=canvas->navigatorPlacement().map;const double dpr=canvas->devicePixelRatioF();
        bool ink=false;for(double y=map.top()+4;y<map.bottom()-4;y+=2)for(double x=map.left()+4;x<map.right()-4;x+=2){const auto color=image.pixelColor(int(std::lround(x*dpr)),int(std::lround(y*dpr)));if(color.red()<80&&color.green()<80&&color.blue()<80)ink=true;}
        require(ink,"Reduced navigator omitted the finished stroke");
    });
    test("native_minimap_and_wheel_keep_the_current_patch",[]{
        NativeCanvas canvas(true);canvas.resize(400,300);canvas.setRaster(Raster::filled(800,600,{20,20,20,255}));canvas.show();flush();
        auto patch=Raster::filled(32,24,{20,20,20,255});int calls=0;
        canvas.viewportProvider=[&](double,double,double,double,double){++calls;return CompositeViewport{patch,0,0,1,800,600};};
        canvas.zoomAt(4,{200,150});canvas.repaint();const auto baseline=calls;require(baseline>0,"Initial patch was not rendered");
        const auto map=canvas.navigatorPlacement().map;require(canvas.navigatorPlacement().visible,"Zoom did not show the navigator");
        pointer(canvas,QEvent::MouseButtonPress,map.center(),Qt::LeftButton,Qt::LeftButton);
        pointer(canvas,QEvent::MouseMove,map.center()+QPointF(12,-8),Qt::NoButton,Qt::LeftButton);
        pointer(canvas,QEvent::MouseMove,map.center()+QPointF(24,-16),Qt::NoButton,Qt::LeftButton);
        canvas.repaint();require(calls==baseline,"Minimap drag rendered a new patch on each move");
        pointer(canvas,QEvent::MouseButtonRelease,map.center()+QPointF(24,-16),Qt::LeftButton,Qt::NoButton);canvas.repaint();
        require(calls>baseline,"Minimap release did not refresh the patch");
        const auto afterDrag=calls;const QPointF anchor{80,60};
        QWheelEvent wheel(anchor,canvas.mapToGlobal(anchor),QPoint(0,10),QPoint(),Qt::NoButton,Qt::AltModifier,Qt::NoScrollPhase,false);
        QApplication::sendEvent(&canvas,&wheel);canvas.repaint();require(calls==afterDrag,"Alt wheel rendered a new patch before the notch settled");
        waitFor(50);canvas.repaint();require(calls>afterDrag,"Alt wheel never refreshed the patch");
    });
    test("native_minimap_drag_shows_overview_before_the_sharp_patch",[]{
        NativeCanvas canvas(true);canvas.resize(400,300);canvas.setDocumentSize(800,600);canvas.show();flush();
        canvas.viewportProvider=[](double x,double y,double,double,double){return CompositeViewport{Raster::filled(16,16,{0,0,40,255}),x,y,1,800,600};};
        canvas.zoomAt(4,{200,150});canvas.repaint();
        canvas.navigatorProvider=[](int){return CompositeViewport{Raster::filled(80,60,{255,0,0,255}),0,0,10,800,600};};
        canvas.scheduleNavigator();waitFor(400);canvas.repaint();flush();
        const auto map=canvas.navigatorPlacement().map;require(map.width()>0,"Navigator missing before the stand-in check");
        pointer(canvas,QEvent::MouseButtonPress,map.center(),Qt::LeftButton,Qt::LeftButton);
        pointer(canvas,QEvent::MouseMove,map.bottomRight(),Qt::NoButton,Qt::LeftButton);
        canvas.repaint();
        const double dpr=canvas.devicePixelRatioF();
        const QPointF inside(canvas.width()/2.-24,canvas.height()/2.-24);
        const auto color=canvas.captureRendered().pixelColor(int(std::lround(inside.x()*dpr)),int(std::lround(inside.y()*dpr)));
        require(color.red()>180&&color.green()<60&&color.blue()<80,"Newly revealed canvas area stayed empty during minimap movement");
        pointer(canvas,QEvent::MouseButtonRelease,map.bottomRight(),Qt::LeftButton,Qt::NoButton);
    });
    test("native_plain_layer_move_keeps_the_viewport_patch",[]{
        MainWindow window(true);
        Document document;document.id=newId();document.width=800;document.height=600;
        Layer layer;layer.id=newId();layer.name="Photo";layer.opacity=1;layer.raster=Raster::filled(300,200,{255,0,0,255});layer.transform={20,30,300,200};document.layers={layer};
        auto& project=window.addProject(std::move(document));window.resize(900,700);window.show();flush();
        auto* canvas=project.canvas;canvas->fit();flush();canvas->repaint();
        int calls=0;auto provider=canvas->viewportProvider;canvas->viewportProvider=[&](double x,double y,double w,double h,double units){++calls;return provider(x,y,w,h,units);};
        canvas->pointerDown({170,130},{});canvas->repaint();const int baseline=calls;require(baseline>0,"Move press did not paint");
        canvas->pointerMove({250,190},{});canvas->repaint();require(calls==baseline,"Plain layer move composited on drag");
        require(canvas->hasMovePreview(),"Plain layer move did not use the direct preview");
        const double dpr=canvas->devicePixelRatioF();auto sample=[&](Point documentPoint){auto view=canvas->viewMapping().toView(documentPoint);return canvas->captureRendered().pixelColor(int(std::lround(view.x*dpr)),int(std::lround(view.y*dpr)));};
        const auto moved=sample({120,110});require(moved.red()>240&&moved.green()<40&&moved.blue()<40,"Moved layer was not drawn at the pointer");
        const auto vacated=sample({30,40});require(!(vacated.red()>240&&vacated.green()<40),"Moved layer stayed at its original position");
        canvas->pointerUp({250,190},{});canvas->repaint();require(calls>baseline,"Move release did not composite the document");
        require(!canvas->hasMovePreview()&&project.document->layers.front().transform.x>20,"Move release did not keep the new placement");
    });
    test("native_tablet_pointer_mapping_no_pressure_or_duplicate_mouse",[]{NativeCanvas canvas(true);canvas.resize(400,300);canvas.setDocumentSize(200,100);canvas.show();flush();canvas.zoomAt(1,{200,150});QPointingDevice pen("Synthetic witness",717,QInputDevice::DeviceType::Stylus,QPointingDevice::PointerType::Pen,QInputDevice::Capability::Position|QInputDevice::Capability::Pressure,1,3);int downs=0,moves=0,ups=0;QPointF first,last;canvas.pointerDown=[&](QPointF p,Qt::KeyboardModifiers){first=p;++downs;};canvas.pointerMove=[&](QPointF p,Qt::KeyboardModifiers){last=p;++moves;};canvas.pointerUp=[&](QPointF,Qt::KeyboardModifiers){++ups;};auto tablet=[&](QEvent::Type type,QPointF position,double pressure,Qt::MouseButton button,Qt::MouseButtons buttons){QTabletEvent event(type,&pen,position,canvas.mapToGlobal(position),pressure,0,0,0,0,0,Qt::NoModifier,button,buttons);QApplication::sendEvent(&canvas,&event);require(event.isAccepted(),"Tablet event allowed duplicate mouse synthesis");};tablet(QEvent::TabletPress,{100,100},.2,Qt::LeftButton,Qt::LeftButton);pointer(canvas,QEvent::MouseButtonPress,{100,100},Qt::LeftButton,Qt::LeftButton);tablet(QEvent::TabletMove,{110,106},.9,Qt::NoButton,Qt::LeftButton);tablet(QEvent::TabletRelease,{110,106},0,Qt::LeftButton,Qt::NoButton);require(downs==1&&moves==1&&ups==1,"Tablet generated duplicate or missing transaction events");closeEnough(first,canvas.documentPoint({100,100}),"Tablet initial document mapping");closeEnough(last,canvas.documentPoint({110,106}),"Tablet update document mapping");});
    test("native_rulers_track_zoom_pan_and_guide_drags",[]{
        NativeCanvas canvas(true);canvas.resize(640,480);canvas.setDocumentSize(400,300);canvas.show();flush();
        auto roundTrip=[&](const char* label){
            const auto frame=canvas.contentRect();
            closeEnough(frame.left(),NativeCanvas::rulerThickness,"Ruler inset");
            closeEnough(frame.top(),NativeCanvas::rulerThickness,label);
            const QPointF sample(frame.left()+frame.width()*0.37,frame.top()+frame.height()*0.41);
            const auto document=canvas.documentPoint(sample);
            const auto view=canvas.viewMapping().toView({document.x(),document.y()});
            closeEnough(QPointF(view.x,view.y),sample,label);
            const double major=canvas.rulerMajorInterval();
            require(major>0&&std::isfinite(major),"Ruler interval");
            const double tick=std::round(document.x()/major)*major;
            const auto tickView=canvas.viewMapping().toView({tick,0});
            closeEnough(canvas.documentPoint(QPointF(tickView.x,sample.y())).x(),tick,"Ruler tick left the document pixel");
        };
        canvas.fit();roundTrip("fit");
        const auto anchor=canvas.contentRect().center();
        canvas.zoomAt(1,anchor);roundTrip("actual pixels");
        canvas.zoomAt(4,anchor);canvas.panBy({36,-18});roundTrip("zoomed pan");
        canvas.repaint();flush();canvas.repaint();flush();
        require(canvas.deviceReady()&&canvas.deviceError().isEmpty(),"Ruler canvas did not draw");
        const double dpr=canvas.devicePixelRatioF();
        const auto image=canvas.captureRendered();
        const QPoint corner(int(std::lround(4*dpr)),int(std::lround(4*dpr)));
        const auto band=image.pixelColor(corner);
        require(band.red()>35&&band.red()<70&&band.blue()>40&&band.blue()<80,"Ruler band was not drawn");
        const double major=canvas.rulerMajorInterval();
        const auto mid=canvas.documentPoint(canvas.contentRect().center());
        const double tick=std::round(mid.x()/major)*major;
        const auto tickView=canvas.viewMapping().toView({tick,0});
        bool mark=false;
        const int x=int(std::lround(tickView.x*dpr)),y=int(std::lround((NativeCanvas::rulerThickness-3)*dpr));
        for(int dy=-2;dy<=2&&!mark;++dy)for(int dx=-2;dx<=2;++dx){const auto color=image.pixelColor(x+dx,y+dy);if(color.red()>160&&color.green()>160&&color.blue()>160)mark=true;}
        require(mark,"Ruler tick is not on its document pixel");
        int finished=0,canceled=0;NativeCanvas::GuidePreview committed{};
        canvas.guideDragFinished=[&](bool horizontal,double position,int index){++finished;committed={horizontal,position,index};};
        canvas.guideDragCanceled=[&]{++canceled;};
        const auto frame=canvas.contentRect();
        const QPointF ruler(frame.left()+48,6);
        pointer(canvas,QEvent::MouseButtonPress,ruler,Qt::LeftButton,Qt::LeftButton);
        const QPointF placed(frame.center().x(),frame.top()+70);
        pointer(canvas,QEvent::MouseMove,placed,Qt::NoButton,Qt::LeftButton);
        require(canvas.guidePreview()&&canvas.guidePreview()->horizontal&&canvas.guides.empty(),"Create preview wrote a guide");
        closeEnough(canvas.guidePreview()->position,canvas.documentPoint(placed).y(),"Preview left the pointer");
        pointer(canvas,QEvent::MouseButtonRelease,ruler,Qt::LeftButton,Qt::NoButton);
        require(finished==0&&canceled==1&&!canvas.guidePreview(),"Release on the ruler committed a guide");
        pointer(canvas,QEvent::MouseButtonPress,{6,frame.top()+40},Qt::LeftButton,Qt::LeftButton);
        pointer(canvas,QEvent::MouseMove,{frame.left()+80,frame.top()+40},Qt::NoButton,Qt::LeftButton);
        QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(&canvas,&escape);
        require(finished==0&&canceled==2&&!canvas.guidePreview(),"Escape committed a guide");
        pointer(canvas,QEvent::MouseButtonPress,ruler,Qt::LeftButton,Qt::LeftButton);
        pointer(canvas,QEvent::MouseMove,placed,Qt::NoButton,Qt::LeftButton);
        pointer(canvas,QEvent::MouseButtonRelease,placed,Qt::LeftButton,Qt::NoButton);
        require(finished==1&&committed.horizontal&&committed.index<0&&canvas.guides.empty(),"Create committed into the canvas guide list");
        closeEnough(committed.position,canvas.documentPoint(placed).y(),"Created guide missed the pointer");
        canvas.showGuides=true;canvas.guides.push_back({true,committed.position});
        const auto line=canvas.viewMapping().toView({0,committed.position});
        const QPointF onLine(frame.center().x(),line.y),moved(frame.center().x(),line.y+22);
        pointer(canvas,QEvent::MouseButtonPress,onLine,Qt::LeftButton,Qt::LeftButton);
        pointer(canvas,QEvent::MouseMove,moved,Qt::NoButton,Qt::LeftButton);
        require(canvas.guides.front().second==committed.position,"Move preview edited the guide");
        pointer(canvas,QEvent::MouseButtonRelease,moved,Qt::LeftButton,Qt::NoButton);
        require(finished==2&&committed.index==0&&committed.horizontal,"Move did not report the existing guide");
        closeEnough(committed.position,canvas.documentPoint(moved).y(),"Moved guide missed the pointer");
        closeEnough(canvas.guides.front().second,canvas.documentPoint(onLine).y(),"Move wrote the canvas guide list");
    });
    std::cout<<"{\"suite\":\"native_viewport\",\"passed\":"<<passed<<",\"failed\":"<<failed<<",\"mac_differential\":false,\"backend\":\"D3D11_WARP\"}\n";return failed?1:0;
}
