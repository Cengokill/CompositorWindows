#include "ui/AdjustmentDialog.h"
#include "ui/AdjustmentAdvancedControls.h"
#include "effects/Adjustments.h"
#include <Windows.h>
#include <QApplication>
#include <QDialog>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

using namespace compositor;
namespace {
using Histogram=effects_tools::LevelsHistogram;
void require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
template<class Fn>void wait(Fn test,const char* message){QElapsedTimer timer;timer.start();while(!test()){if(timer.elapsed()>30000)throw std::runtime_error(message);QApplication::processEvents(QEventLoop::AllEvents,20);QTest::qWait(1);}}
struct Fixture {Document document;std::vector<Pixel> renderedInput;int previewWidth{},previewHeight{};};
Pixel pattern(int x,int y,bool alpha){
    if(alpha){const auto a=std::array<uint8_t,4>{0,64,128,255}[size_t((x+3*y)%4)];return {a,uint8_t(a/2),0,a};}
    const auto v=uint8_t(y==1?0:x%2?180:80);return {v,v,v,255};
}
Fixture fixture(const std::string& name){
    Fixture f;auto& d=f.document;d.id="live-histogram-"+name;d.width=9001;d.height=3;
    if(name=="portrait_odd"){d.width=3;d.height=9001;}
    else if(name=="boundary_8000")d.width=8000;
    else if(name=="small_control"){d.width=31;d.height=7;}
    else if(name=="row_major_fractional"){d.width=513;d.height=519;}
    const bool fractional=name=="alpha_weighted"||name=="row_major_fractional";
    const int sourceWidth=name=="composite_before_reduce"?4501:d.width;
    std::vector<Pixel> source(size_t(sourceWidth)*d.height);
    for(int y=0;y<d.height;++y)for(int x=0;x<sourceWidth;++x){
        auto value=name=="portrait_odd"?pattern(y,x,false):pattern(x,y,fractional);
        if(name=="row_major_fractional"){const auto a=uint8_t(1+(x*67+y*53)%255);value={a,0,0,a};}
        if(name=="composite_before_reduce")value=x==145?Pixel{37,77,113,255}:Pixel{80,80,80,255};
        source[size_t(y)*sourceWidth+x]=value;
    }
    Layer base;base.id="base";base.name="Histogram input";base.transform={0,0,double(d.width),double(d.height)};base.transform.sampling=Transform::Sampling::Nearest;
    base.raster=Raster::fromRgba(sourceWidth,d.height,reinterpret_cast<const uint8_t*>(source.data()),size_t(sourceWidth)*4);
    d.layers.push_back(base);f.renderedInput.resize(size_t(d.width)*d.height);
    // Source AdjustmentEditing renders the full document first. This fixture's
    // only visible input is an axis-aligned nearest layer; its document pixels
    // are computed independently, before the second preview reduction.
    for(int y=0;y<d.height;++y)for(int x=0;x<d.width;++x){const int sx=int(std::floor((x+.5)*sourceWidth/d.width));f.renderedInput[size_t(y)*d.width+x]=source[size_t(y)*sourceWidth+sx];}
    Layer adjustment;adjustment.id="levels";adjustment.name="Levels";adjustment.transform=base.transform;effects_tools::LevelsSettings settings;settings.ranges[0].outputWhite=0;
    adjustment.adjustmentJson=effects_tools::withLevelsSettings(effects::defaultAdjustmentJson("Levels"),settings);d.layers.push_back(adjustment);
    Layer above;above.id="above";above.name="Excluded above";above.transform=base.transform;above.raster=Raster::filled(d.width,d.height,{255,0,0,255});d.layers.push_back(above);
    // A live adjustment histogram deliberately has selection:nil. A zero mask
    // makes accidental document-selection weighting observable.
    if(name=="input_scope_selection")d.selection=Selection{std::make_shared<GrayRaster>(GrayRaster{d.width,d.height,std::vector<uint8_t>(size_t(d.width)*d.height,0)})};
    const double factor=std::min(1.,8000./std::max(d.width,d.height));
    f.previewWidth=std::max(1,int(d.width*factor));f.previewHeight=std::max(1,int(d.height*factor));validateDocument(d);return f;
}
Histogram scalarHistogram(const Fixture& f,bool preview){
    Histogram bins{};const int width=preview?f.previewWidth:f.document.width,height=preview?f.previewHeight:f.document.height;
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){
        const int sx=int(std::floor((x+.5)*f.document.width/width)),sy=int(std::floor((y+.5)*f.document.height/height));
        const auto p=f.renderedInput[size_t(sy)*f.document.width+sx];if(!p.a)continue;
        const double weight=p.a/255.0;const std::array<uint8_t,3> channels{p.r,p.g,p.b};
        // Literal LevelsPixels.c12-22 scalar formula, row-major order. This
        // oracle calls neither the native histogram helper nor the C kernel.
        for(int c=0;c<3;++c){const int value=int(std::min(255.,std::round(channels[size_t(c)]*255.0/p.a)));bins[size_t(c+1)][size_t(value)]+=weight;bins[0][size_t(value)]+=weight/3.0;}
    }
    return bins;
}
QJsonArray jsonBins(const Histogram& bins){QJsonArray result;for(const auto& channel:bins){QJsonArray values;for(double value:channel)values.append(value);result.append(values);}return result;}
QJsonObject run(const std::string& name){
    auto f=fixture(name);const auto before=f.document;const auto expected=scalarHistogram(f,true),full=scalarHistogram(f,false);
    if(std::max(f.document.width,f.document.height)>8000)require(expected!=full,"Fixture distinguishes source preview from full-resolution histogram");
    if(name=="composite_before_reduce")require(expected[1][37]==4&&expected[2][77]==4&&expected[3][113]==4,"Two-stage nearest samples column145 twice per output row; direct source reduction would count it once");
    bool closed=false,committed=false;QString failure;QWidget parent;ui::EditPanelHost host;host.valid=[] {return true;};host.closed=[&]{closed=true;};host.commit=[&](ui::EditPanelResult){committed=true;};host.error=[&](const QString& text){failure=text;};
    auto* session=openAdjustmentPanel(&parent,f.document,"levels","Levels",true,true,{},std::move(host));
    QPointer<ui::EditPanelSession> owner=session;LevelsAdvancedControls* controls=nullptr;
    for(auto* child:session->panel()->findChildren<QObject*>())if(auto* item=dynamic_cast<LevelsAdvancedControls*>(child))controls=item;
    require(controls,"Actual asynchronous live Levels controls exist");wait([&]{return controls->histogramReady()||!failure.isEmpty();},"Actual live Levels histogram completes");require(failure.isEmpty(),"Live Levels histogram reports no error");
    const auto actual=controls->histogram();double maximum=0;int differing=0;for(size_t c=0;c<4;++c)for(size_t b=0;b<256;++b){maximum=std::max(maximum,std::abs(actual[c][b]-expected[c][b]));if(actual[c][b]!=expected[c][b])++differing;}
    session->cancel();wait([&]{return closed&&!owner;},"Cancelled panel retires after owned workers complete");
    require(!committed&&f.document==before,"Histogram and Cancel preserve canonical input");
    QJsonObject record{{"case",QString::fromStdString(name)},{"status",differing?"failed":"passed"},{"source_width",f.document.width},{"source_height",f.document.height},{"preview_width",f.previewWidth},{"preview_height",f.previewHeight},{"source_preview_vs_full_differs",expected!=full},{"differing_bins",differing},{"max_bin_difference",maximum},{"comparison","exact double bins with source row-major accumulation; no tolerance"},{"expected",jsonBins(expected)},{"actual",jsonBins(actual)}};
    return record;
}
}
int main(int argc,char** argv){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);QApplication app(argc,argv);
    const std::set<std::string> cases{"landscape_odd","portrait_odd","boundary_8000","small_control","alpha_weighted","row_major_fractional","input_scope_selection","composite_before_reduce"};
    if(argc!=3||!cases.contains(argv[1]))return 2;QJsonObject report;int code=0;
    try{report=run(argv[1]);if(report["status"]!="passed")code=1;}
    catch(const std::exception& error){report={{"case",argv[1]},{"status","error"},{"error",error.what()}};code=2;}
    QFile output(QString::fromLocal8Bit(argv[2]));if(!output.open(QIODevice::WriteOnly|QIODevice::NewOnly))return 2;output.write(QJsonDocument(report).toJson());output.close();
    std::printf("%s %s differing_bins=%d max_bin_difference=%.17g\n",code?"FAIL":"PASS",argv[1],report["differing_bins"].toInt(),report["max_bin_difference"].toDouble());return code;
}
