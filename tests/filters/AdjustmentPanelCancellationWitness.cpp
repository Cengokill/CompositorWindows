#include "ui/AdjustmentDialog.h"
#include "ui/EditPanelSession.h"
#include "effects/Adjustments.h"
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QTest>
#include <QTimer>
#include <chrono>
#include <cstdio>
using namespace compositor;
using Clock=std::chrono::steady_clock;
int main(int argc,char** argv){try{
    QApplication app(argc,argv);if(argc!=2&&argc!=3)throw std::runtime_error("Provide grain_close, hsv_close or grain_destroy and optional --lifecycle");
    const bool lifecycle=argc==3&&std::string(argv[2])=="--lifecycle";
    if(argc==3&&!lifecycle)throw std::runtime_error("Unknown mode");
    const std::string key=argv[1];if(key!="grain_close"&&key!="hsv_close"&&key!="grain_destroy")throw std::runtime_error("Unknown case");
    const auto kind=key=="hsv_close"?"Hue/Saturation":"Grain";
    Document original;original.id="cancel-panel";original.width=original.height=4096;Layer layer;layer.id="pixels";layer.transform={0,0,4096,4096};layer.raster=Raster::filled(4096,4096,{70,100,150,255});original.layers={layer};const auto saved=original;
    auto json=QJsonDocument::fromJson(QByteArray::fromStdString(effects::defaultAdjustmentJson(kind))).object();
    if(key=="hsv_close")json["hue"]=120;else json["grainSettings"]=QJsonObject{{"amount",70},{"size",1.5},{"roughness",50},{"seed",77}};
    AdjustmentDialogOptions options;options.initialAdjustmentJson=QJsonDocument(json).toJson(QJsonDocument::Compact).toStdString();
    int commits=0,closed=0,latePreviews=0;bool requested=false,requestedWhilePending=false;auto owner=std::make_unique<QWidget>();
    ui::EditPanelHost host;host.valid=[] {return true;};host.commit=[&](auto){++commits;};host.closed=[&]{++closed;};host.preview=[&](auto preview){if(requested&&preview)++latePreviews;};
    QPointer<ui::EditPanelSession> session=openAdjustmentPanel(owner.get(),original,layer.id,kind,false,false,options,std::move(host));
    Clock::time_point requestAt;double destroyMs=0;
    QTimer::singleShot(20,&app,[&]{requested=true;requestAt=Clock::now();if(session){auto* box=session->panel()->findChild<QDialogButtonBox*>();requestedWhilePending=box&&!box->button(QDialogButtonBox::Apply)->isEnabled();}if(key=="grain_destroy"){owner.reset();destroyMs=std::chrono::duration<double,std::milli>(Clock::now()-requestAt).count();}else if(session)session->cancel();});
    QElapsedTimer timeout;timeout.start();while(!requested||session){if(timeout.elapsed()>5000)throw std::runtime_error("Cancellation disposal exceeded5s");QApplication::processEvents(QEventLoop::AllEvents,10);QTest::qWait(1);}
    const double latency=std::chrono::duration<double,std::milli>(Clock::now()-requestAt).count();
    // Sanitizer and concurrent CTest runs check lifetime and publication only.
    // The fixed100ms responsiveness gate is measured separately while idle.
    const bool unchanged=original==saved,passed=requestedWhilePending&&(lifecycle||latency<=100)&&commits==0&&latePreviews==0&&unchanged&&(key=="grain_destroy"||closed==1);
    if(lifecycle)std::printf("LIFECYCLE ONLY: latency gate not evaluated\n");
    std::printf("{\"case\":\"%s\",\"request_delay_ms\":20,\"gate_ms\":%s,\"disposal_latency_ms\":%.4f,\"parent_destruction_ms\":%.4f,\"commits\":%d,\"closed_callbacks\":%d,\"late_previews\":%d,\"source_immutable\":%s,\"pending_at_request\":%s,\"passed\":%s}\n",key.c_str(),lifecycle?"null":"100",latency,destroyMs,commits,closed,latePreviews,unchanged?"true":"false",requestedWhilePending?"true":"false",passed?"true":"false");
    return passed?0:1;
}catch(const std::exception& error){std::fprintf(stderr,"INVALID RUN %s\n",error.what());return 2;}}

