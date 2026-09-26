#include "ui/WorkspaceDropQueue.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace compositor;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void pump(int ms=35){QElapsedTimer t;t.start();while(t.elapsed()<ms){QApplication::processEvents();QThread::msleep(1);}}
void until(const std::function<bool()>& ready){QElapsedTimer t;t.start();while(!ready()&&t.elapsed()<3000)pump(5);require(ready(),"queued operation completes within functional test timeout");}
struct Fixture {
    QObject owner,target;bool allowed=true,advance=true,managed{};QStringList order;int cancelCalls{};std::unique_ptr<ui::WorkspaceDropQueue> queue;
    Fixture(){ui::WorkspaceDropQueue::Host host;
        host.canBegin=[this]{return allowed;};host.canAdvance=[this]{return advance;};host.managing=[this](bool value){managed=value;};
        host.project=[this](const QString& path){order.append(path);};host.imageTarget=[this](QObject* value,bool captured){return captured?value:&target;};
        host.image=[this](QObject*,const QString& path,std::optional<Point>,std::shared_ptr<void>){order.append(path);};
        host.cancelImage=[this](QObject*){++cancelCalls;};host.error=[](const QString&,const QString&){throw std::runtime_error("Unexpected host error");};
        queue=std::make_unique<ui::WorkspaceDropQueue>(std::move(host),&owner);
    }
    ui::WorkspaceDropRequest request(QStringList paths,std::function<void(bool)> done){ui::WorkspaceDropRequest r;r.paths=std::move(paths);r.destination=&target;r.hasDestination=true;r.completed=std::move(done);return r;}
};
}
int main(int argc,char**argv){QApplication app(argc,argv);std::cout<<std::unitbuf;if(argc!=3)return 2;const std::string key=argv[1];const std::filesystem::path out(argv[2]);if(std::filesystem::exists(out))return 2;std::filesystem::create_directories(out);bool pass=false;std::string error;
    try{Fixture f;
        if(key=="request_completion_order"){
            f.queue->enqueue(f.request({"first.png","second.png"},[&](bool cancelled){require(!cancelled&&!f.managed,"request completes after workspace release");f.order.append("completed");}));
            f.queue->enqueue(f.request({"later.png"},{}));require(f.order==QStringList{"first.png"},"first image alone starts before its completion");
            f.queue->imageFinished(&f.target,false);until([&]{return f.order.size()>=2;});require(f.order==QStringList{"first.png","second.png"},"second image waits for first import");
            f.queue->imageFinished(&f.target,false);until([&]{return f.order.size()>=4;});require(f.order==QStringList{"first.png","second.png","completed","later.png"},"per-request error callback runs before later request starts");f.queue->imageFinished(&f.target,false);until([&]{return f.queue->idle();});require(f.queue->idle(),"all requests settle");
        }else if(key=="completion_releases_temporary_storage"){
            auto temp=std::make_shared<QTemporaryDir>();require(temp->isValid(),"temporary lifetime fixture");const auto path=temp->filePath("image.png");QFile file(path);require(file.open(QIODevice::WriteOnly),"temporary image file");file.write("bytes");file.close();std::weak_ptr<QTemporaryDir> weak=temp;bool called=false;
            auto request=f.request({path},[&](bool cancelled){require(!cancelled&&weak.expired()&&!QFileInfo::exists(path),"completed callback observes released temporary storage");called=true;});request.lifetime=temp;temp.reset();f.queue->enqueue(std::move(request));require(!weak.expired(),"request retains bytes while importing");f.queue->imageFinished(&f.target,false);until([&]{return called;});require(called&&f.queue->idle(),"completion delivered once after import");
        }else if(key=="cancel_waits_for_active_import"){
            int completed=0;f.queue->enqueue(f.request({"first.png","must-not-import.png"},[&](bool cancelled){require(cancelled,"active cancellation result");++completed;}));f.queue->cancel(&f.target);pump();require(f.cancelCalls==1&&completed==0&&!f.queue->idle(),"Cancel requests import cleanup before completing");f.queue->imageFinished(&f.target,true);until([&]{return completed!=0;});require(completed==1&&f.queue->idle()&&f.order==QStringList{"first.png"},"active cancellation completes once and skips remaining files");
        }else if(key=="cancel_pending_request"){
            f.allowed=false;int completed=0;f.queue->enqueue(f.request({"never.png"},[&](bool cancelled){require(cancelled,"pending cancellation result");++completed;}));f.queue->cancel(&f.target);until([&]{return completed!=0;});require(completed==1&&f.queue->idle()&&f.order.isEmpty(),"pending request cancellation delivers once without starting import");
        }else if(key=="destroyed_queue_discards_callback"){
            int completed=0;f.queue->enqueue(f.request({"image.png"},[&](bool){++completed;}));f.queue->imageFinished(&f.target,false);f.queue.reset();pump();require(completed==0,"destroyed owner prevents queued completion callback");
        }else if(key=="empty_request_completes"){
            int completed=0;f.queue->enqueue(f.request({},[&](bool cancelled){require(!cancelled,"empty result is not cancellation");++completed;}));require(completed==0,"empty completion is queued outside provider capture");until([&]{return completed!=0;});require(completed==1&&f.queue->idle()&&f.order.isEmpty(),"all-unreadable request still reports one completion");
        }else if(key=="empty_error_keeps_fifo_order"){
            f.queue->enqueue(f.request({"first.png"},[&](bool cancelled){if(!cancelled)f.order.append("first-completed");}));
            f.queue->enqueue(f.request({},[&](bool cancelled){if(!cancelled)f.order.append("all-invalid-error");}));
            f.queue->enqueue(f.request({"later.png"},{}));pump();require(f.order==QStringList{"first.png"},"later all-invalid request cannot report while earlier import is active");
            f.queue->imageFinished(&f.target,false);until([&]{return f.order.size()>=4;});std::cout<<"empty_error_order="<<f.order.join("|").toStdString()<<'\n';require(f.order==QStringList{"first.png","first-completed","all-invalid-error","later.png"},"empty provider error retains its request's FIFO position");f.queue->imageFinished(&f.target,false);until([&]{return f.queue->idle();});require(f.queue->idle(),"FIFO empty error fixture settles");
        }else throw std::runtime_error("Unknown case");pass=true;
    }catch(const std::exception& e){error=e.what();}
    QJsonObject result{{"schema","VIRTUAL_DROP_QUEUE_COMPLETION_V1"},{"case",QString::fromStdString(key)},{"passed",pass},{"failed_predicate",QString::fromStdString(error)}};QFile file(QString::fromStdWString((out/L"results.json").wstring()));if(!file.open(QIODevice::WriteOnly))return 3;file.write(QJsonDocument(result).toJson());std::cout<<(pass?"PASS ":"FAIL ")<<key<<" "<<error<<'\n';return pass?0:1;
}
