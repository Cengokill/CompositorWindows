#include "ui/NativeCanvas.h"
#include "editing/Selection.h"
#include <QApplication>
#include <QPointer>
#include <QTimer>
#include <atomic>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
using namespace compositor;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Figure {std::vector<Point> lines;size_t cubics{};bool closed{};};
class Sink final:public ID2D1SimplifiedGeometrySink {
    std::atomic<ULONG> references_{1};
public:
    D2D1_FILL_MODE fill{D2D1_FILL_MODE_ALTERNATE};std::vector<Figure> figures;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(id==__uuidof(IUnknown)||id==__uuidof(ID2D1SimplifiedGeometrySink)){*out=this;AddRef();return S_OK;}return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++references_;}
    ULONG STDMETHODCALLTYPE Release()override{const auto left=--references_;if(!left)delete this;return left;}
    void STDMETHODCALLTYPE SetFillMode(D2D1_FILL_MODE mode)override{fill=mode;}
    void STDMETHODCALLTYPE SetSegmentFlags(D2D1_PATH_SEGMENT)override{}
    void STDMETHODCALLTYPE BeginFigure(D2D1_POINT_2F point,D2D1_FIGURE_BEGIN)override{figures.push_back({{{point.x,point.y}}});}
    void STDMETHODCALLTYPE AddLines(const D2D1_POINT_2F* points,UINT count)override{for(UINT i=0;i<count;++i)figures.back().lines.push_back({points[i].x,points[i].y});}
    void STDMETHODCALLTYPE AddBeziers(const D2D1_BEZIER_SEGMENT*,UINT count)override{figures.back().cubics+=count;}
    void STDMETHODCALLTYPE EndFigure(D2D1_FIGURE_END end)override{figures.back().closed=end==D2D1_FIGURE_END_CLOSED;}
    HRESULT STDMETHODCALLTYPE Close()override{return S_OK;}
};
Microsoft::WRL::ComPtr<Sink> stream(const editing::SelectionOutline& outline,std::array<float,6> matrix={1,0,0,1,0,0}){Microsoft::WRL::ComPtr<Sink> sink;sink.Attach(new Sink);outline.writeGeometry(sink.Get(),matrix);require(SUCCEEDED(sink->Close()),"Sink close");std::cout<<"sink fill="<<int(sink->fill)<<" figures="<<sink->figures.size();for(const auto& figure:sink->figures)std::cout<<" [lines="<<figure.lines.size()<<",cubics="<<figure.cubics<<",closed="<<figure.closed<<"]";std::cout<<'\n';return sink;}
void transform(){const auto outline=editing::SelectionOutline::rectangle({10,20,30,40});auto sink=stream(outline,{2,0,0,3,5,7});require(sink->figures.size()==1&&sink->figures[0].closed,"Rectangle remains one closed contour");const auto& points=sink->figures[0].lines;require(points.size()==4,"Rectangle retains four exact corners");for(const auto point:points)require((point.x==25||point.x==85)&&(point.y==67||point.y==187),"View transform applies to every endpoint");require(outline.bounds()==editing::Rect{10,20,30,40},"Streaming leaves canonical path untouched");}
void curves(){auto sink=stream(editing::SelectionOutline::ellipse({10,20,30,40}),{32,0,0,32,-100,-200});require(sink->figures.size()==1&&sink->figures[0].closed,"Ellipse remains closed");require(sink->figures[0].cubics>=4,"Ellipse preserves cubic curves at high zoom");}
void holes(){const auto path=editing::SelectionOutline::rectangle({0,0,40,40}).combined(editing::SelectionOutline::rectangle({10,10,20,20}),editing::SelectionMode::Subtract,true);auto sink=stream(path);require(sink->figures.size()==2,"Hole remains two separate contours");std::array<double,2> area{};for(size_t i=0;i<2;++i){const auto& f=sink->figures[i];require(f.closed&&f.lines.size()>=4,"Each hole contour stays closed");for(size_t p=0;p<f.lines.size();++p){const auto a=f.lines[p],b=f.lines[(p+1)%f.lines.size()];area[i]+=a.x*b.y-b.x*a.y;}}require(area[0]*area[1]<0,"Inner and outer contours retain opposite winding");require(std::abs(area[0]+area[1])==2400,"Streaming retains the rectangle minus hole area exactly");}
void invalid(){const auto path=editing::SelectionOutline::rectangle({0,0,4,4});bool rejected=false;try{path.writeGeometry(nullptr,{1,0,0,1,0,0});}catch(const std::invalid_argument&){rejected=true;}require(rejected,"Missing sink rejected");rejected=false;try{stream(path,{1,0,0,1,std::numeric_limits<float>::quiet_NaN(),0});}catch(const std::invalid_argument&){rejected=true;}require(rejected,"Nonfinite view transform rejected");require(stream(editing::SelectionOutline{})->figures.empty(),"Empty source exports no contours");}
void timer(){auto owner=std::make_unique<NativeCanvas>(true);owner->resize(400,300);owner->setRaster(Raster::filled(32,32));auto outline=std::make_shared<editing::SelectionOutline>(editing::SelectionOutline::rectangle({5,5,10,10}));owner->setSelection(outline->rasterize(32,32),outline);QPointer<QTimer> timer=owner->findChild<QTimer*>("selectionAntsTimer");require(timer&&!timer->isActive()&&timer->interval()==120&&timer->timerType()==Qt::PreciseTimer,"Hidden canvas has source120ms timer stopped");owner->show();QApplication::processEvents();require(timer->isActive(),"Visible nonempty selection animates");owner->hide();QApplication::processEvents();require(!timer->isActive(),"Hidden canvas stops repaint timer");owner->show();QApplication::processEvents();require(timer->isActive(),"Showing canvas resumes animation");owner->setSelection({});require(!timer->isActive(),"Deselect stops timer");auto empty=std::make_shared<GrayRaster>();empty->width=empty->height=32;empty->pixels.resize(32*32);owner->setSelection(empty);require(!timer->isActive(),"Explicit empty coverage stops timer");owner->setSelection(empty,std::make_shared<editing::SelectionOutline>(editing::SelectionOutline::rectangle({-20,4,10,10})));require(timer->isActive(),"Nonempty off-document canonical outline remains visible and active");owner->setDocumentSize(0,0);require(!timer->isActive(),"Clearing document stops timer");owner.reset();require(!timer,"QObject timer dies with canvas");}
}
int main(int argc,char** argv){QApplication app(argc,argv);const std::map<std::string,void(*)()> cases{{"geometry_transform",transform},{"geometry_curves",curves},{"geometry_holes",holes},{"geometry_invalid",invalid},{"timer_lifecycle",timer}};int failed=0;if(argc>1&&!cases.contains(argv[1]))return 2;for(const auto& [name,run]:cases)if(argc==1||name==argv[1])try{run();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& error){++failed;std::cerr<<"FAIL "<<name<<": "<<error.what()<<'\n';}return failed?1:0;}
