#include "ui/NativeCanvas.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include <icm.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace compositor;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int linear(int value){const double v=value/255.;return int(std::lround(255*(v<=.04045?v/12.92:std::pow((v+.055)/1.055,2.4))));}
std::filesystem::path srgb(){DWORD size=0;GetColorDirectoryW(nullptr,nullptr,&size);std::wstring directory(size,L'\0');require(GetColorDirectoryW(nullptr,directory.data(),&size),"Cannot locate system sRGB profile");directory.resize(wcslen(directory.c_str()));return std::filesystem::path(directory)/L"sRGB Color Space Profile.icm";}
QImage capture(NativeCanvas& canvas){canvas.repaint();QApplication::processEvents();QTest::qWait(40);require(canvas.deviceReady()&&canvas.deviceError().isEmpty(),"Native presentation device failed");return canvas.captureRendered();}
int compare(const QImage& actual,const QImage& before,bool decode){
    require(actual.size()==before.size(),"Presentation dimensions changed");int maximum=0;
    for(int y=0;y<actual.height();++y)for(int x=0;x<actual.width();++x){const auto a=actual.pixelColor(x,y),b=before.pixelColor(x,y);for(int c=0;c<3;++c){const int av=c==0?a.red():c==1?a.green():a.blue(),bv=c==0?b.red():c==1?b.green():b.blue();maximum=std::max(maximum,std::abs(av-(decode?linear(bv):bv)));}}
    return maximum;
}
}
int main(int argc,char** argv){
    QApplication app(argc,argv);const auto args=app.arguments();
    if(args.size()!=4){std::cerr<<"Provide case, linear ICC and evidence directory\n";return 2;}
    QDir().mkpath(args[3]);QJsonObject report{{"case",args[1]},{"threshold",2},{"scope","Visible native WARP presentation; no physical calibrated-display or Mac comparison"}};
    try{
        std::vector<Pixel> pixels(128*96);
        for(int y=0;y<96;++y)for(int x=0;x<128;++x){const uint8_t alpha=x<32?0:x<96?128:255;pixels[size_t(y)*128+x]={uint8_t((x*2)*alpha/255),uint8_t((y*2)*alpha/255),uint8_t(180*alpha/255),alpha};}
        const auto raster=Raster::fromRgba(128,96,reinterpret_cast<const uint8_t*>(pixels.data()),128*4);
        const auto original=raster->rgba();NativeCanvas canvas(true);canvas.resize(480,360);canvas.setRaster(raster);canvas.show();require(QTest::qWaitForWindowExposed(&canvas,5000),"Native window not exposed");
        canvas.zoomAt(2,{240,180});const auto missing=std::filesystem::path(args[3].toStdWString())/L"missing-test-profile.icc";
        canvas.setDisplayProfileOverride(missing);auto baseline=capture(canvas);require(!canvas.presentationConvertsColor()&&!canvas.presentationDiagnostic().isEmpty(),"Explicit missing-profile fallback was not reported");
        const auto linearPath=std::filesystem::path(args[2].toStdWString());QImage result;int maximum=0;
        if(args[1]=="linear_frame"||args[1]=="profile_switch"||args[1]=="recreate_resize"){
            canvas.setDisplayProfileOverride(linearPath);result=capture(canvas);require(canvas.presentationConvertsColor(),"Validated linear profile was not used");maximum=compare(result,baseline,true);require(maximum<=2,"Native ICC conversion exceeds frozen two-byte analytic tolerance");
            require(canvas.presentationBytes()==uint64_t(result.width())*result.height()*4,"Presentation allocation is not the bounded physical viewport");
            if(args[1]=="profile_switch"){canvas.setDisplayProfileOverride(missing);result=capture(canvas);require(compare(result,baseline,false)==0,"Returning to sRGB changed canonical presentation pixels");}
            if(args[1]=="recreate_resize"){
                canvas.recreateDevice();require(compare(capture(canvas),baseline,true)<=2,"Device recreation lost monitor conversion");
                canvas.resize(520,390);canvas.setDisplayProfileOverride(missing);baseline=capture(canvas);canvas.setDisplayProfileOverride(linearPath);result=capture(canvas);maximum=compare(result,baseline,true);require(maximum<=2,"Resize lost the physical presentation profile or DPI");
            }
        }else if(args[1]=="srgb_identity"){
            canvas.setDisplayProfileOverride(srgb());result=capture(canvas);require(canvas.presentationConvertsColor(),"System sRGB profile did not initialize");maximum=compare(result,baseline,false);require(maximum==0,"sRGB presentation must be exact identity");
        }else if(args[1]=="fallback"){
            result=capture(canvas);require(compare(result,baseline,false)==0&&canvas.presentationBytes()==0,"Fallback altered source or allocated a conversion surface");
        }else if(args[1]=="budget_failure"){
            canvas.setPresentationBudget(1);canvas.setDisplayProfileOverride(linearPath);result=capture(canvas);
            require(!canvas.presentationConvertsColor()&&canvas.presentationBytes()==0&&!canvas.presentationDiagnostic().isEmpty(),"Budget failure must report an explicit sRGB fallback");
            const auto diagnostic=canvas.presentationDiagnostic();canvas.refreshDisplayProfile();result=capture(canvas);
            require(canvas.presentationDiagnostic()==diagnostic&&compare(result,baseline,false)==0,"Unchanged profile refresh lost a failed-conversion diagnostic or altered fallback pixels");
            canvas.setPresentationBudget(128ULL*1024*1024);result=capture(canvas);
            require(canvas.presentationConvertsColor()&&canvas.presentationDiagnostic().isEmpty(),"Restoring the budget did not recover color conversion");maximum=compare(result,baseline,true);require(maximum<=2,"Recovered presentation conversion exceeds tolerance");
        }else if(args[1]=="profile_events"){
            const auto watches=canvas.profileWatchStatuses();require(watches.size()==platform::DisplayProfileWatcher::defaultTargets().size(),"Native canvas did not register display profile notifications");QJsonArray watchReport;for(const auto& watch:watches)watchReport.append(QJsonObject{{"available",watch.available},{"ancestor",watch.ancestor},{"error",double(watch.error)},{"diagnostic",QString::fromStdString(watch.diagnostic)}});report["profile_watches"]=watchReport;
            canvas.setDisplayProfileOverride(std::nullopt);capture(canvas);const auto start=canvas.profileDiscoveryCount();for(int i=0;i<5;++i)capture(canvas);
            require(canvas.profileDiscoveryCount()==start,"Repaint performs profile discovery or disk IO");QEvent activate(QEvent::WindowActivate);QApplication::sendEvent(&canvas,&activate);require(canvas.profileDiscoveryCount()>start,"Window activation did not refresh monitor profile");result=capture(canvas);
        }else throw std::runtime_error("Unknown case");
        require(raster->rgba()==original,"Monitor conversion modified canonical raster bytes");
        const auto prefix=args[3]+"/"+args[1];require(baseline.save(prefix+"-srgb.png")&&result.save(prefix+"-display.png"),"Cannot preserve native screenshots");
        report["passed"]=true;report["maximum_error"]=maximum;report["dpr"]=canvas.devicePixelRatioF();report["physical_width"]=result.width();report["physical_height"]=result.height();report["profile_sha256"]=canvas.presentationProfileHash();report["diagnostic"]=canvas.presentationDiagnostic();report["viewport_bytes"]=double(canvas.presentationBytes());
        std::cout<<"PASS "<<args[1].toStdString()<<" max="<<maximum<<" dpr="<<canvas.devicePixelRatioF()<<'\n';
    }catch(const std::exception& error){report["passed"]=false;report["error"]=error.what();std::cerr<<"FAIL "<<error.what()<<'\n';}
    QFile file(args[3]+"/"+args[1]+".json");if(!file.open(QIODevice::WriteOnly))return 2;file.write(QJsonDocument(report).toJson());return report["passed"].toBool()?0:1;
}
