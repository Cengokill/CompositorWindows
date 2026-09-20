#include "persistence/ProjectStore.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>

using namespace compositor;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
Document fixture(bool edited){
    Document document;document.id="00000000-0000-4000-8000-000000000001";document.width=31;document.height=23;document.resolution=144;
    Layer layer;layer.id="00000000-0000-4000-8000-000000000002";layer.name=edited?"New committed image":"Original image";layer.transform={-2.25,3.5,17,13,23,true,false};
    layer.raster=Raster::filled(17,13,edited?Pixel{40,80,120,160}:Pixel{16,32,48,64});
    layer.mask=Mask{std::make_shared<GrayRaster>(GrayRaster{3,2,{0,64,128,192,254,255}}),false,false,Transform{1,2,16,12,13}};
    document.layers.push_back(layer);return document;
}
QByteArray bytes(const QString& path){QFile file(path);require(file.open(QIODevice::ReadOnly),"Cannot inspect project bytes");return file.readAll();}
void equivalent(Document actual,const Document& expected){
    require(actual.layers.size()==expected.layers.size(),"Recovered layer count differs");
    for(size_t i=0;i<actual.layers.size();++i){auto& layer=actual.layers[i];const auto& source=expected.layers[i];require(layer.raster&&layer.raster->rgba()==source.raster->rgba(),"Recovered image pixels differ");require(layer.mask&&layer.mask->raster->pixels==source.mask->raster->pixels,"Recovered mask pixels differ");layer.raster=source.raster;layer.mask->raster=source.mask->raster;}
    require(actual==expected,"Recovered document metadata differs");
}
}
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);std::cout<<std::unitbuf;const auto arguments=app.arguments();
    if(arguments.size()==4&&arguments[1]=="--writer"){
        try{const int requested=arguments[3].toInt();auto document=fixture(true);
            ProjectStore store(makeWicProjectCodec(),[&](SaveFaultPoint point){if(int(point)==requested){std::cout<<"READY "<<int(point)<<'\n';for(;;)QThread::msleep(100);}});
            store.save(std::filesystem::path(arguments[2].toStdWString()),document,document.layers[0].id);return 3;
        }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}
    }
    const QString evidence=arguments.size()>1?arguments[1]:QDir::tempPath();QDir().mkpath(evidence);
    QTemporaryDir directory(evidence+"/process-save-XXXXXX");if(!directory.isValid())return 2;directory.setAutoRemove(false);
    QJsonArray results;int failed=0;
    for(int point=0;point<5;++point){QJsonObject result{{"fault_point",point}};QProcess writer;
        try{
            const auto path=directory.filePath(QString("stage-%1 実証.comp").arg(point));const auto native=std::filesystem::path(path.toStdWString());
            ProjectStore store(makeWicProjectCodec());const auto original=fixture(false);store.save(native,original,original.layers[0].id);
            const auto before=bytes(path+"/manifest.json");writer.setProgram(QCoreApplication::applicationFilePath());writer.setArguments({"--writer",path,QString::number(point)});writer.start();require(writer.waitForStarted(10000),"Writer process did not start");
            QByteArray output;while(!output.contains('\n')&&writer.state()!=QProcess::NotRunning){if(!writer.waitForReadyRead(15000))break;output+=writer.readAllStandardOutput();}
            result["writer_stdout"]=QString::fromUtf8(output);result["writer_stderr"]=QString::fromUtf8(writer.readAllStandardError());
            require(output.contains("READY "+QByteArray::number(point)),"Writer did not reach requested save boundary");result["writer_pid"]=double(writer.processId());
            bool busy=false;try{store.load(native);}catch(const std::exception& error){busy=std::string(error.what()).find("busy in another process")!=std::string::npos;}
            require(busy,"Concurrent reader did not respect writer mutex");
            writer.kill();require(writer.waitForFinished(10000),"Killed writer did not exit");require(writer.exitStatus()==QProcess::CrashExit,"Writer exited normally instead of forced termination");
            store.recover(native);const auto recovered=store.load(native);const bool newInstalled=point>=int(SaveFaultPoint::AfterInstall);equivalent(recovered.document,fixture(newInstalled));
            if(!newInstalled)require(bytes(path+"/manifest.json")==before,"Recovery modified original manifest bytes");
            require(!QFile::exists(path+".compositor-save.json"),"Recovery left a transaction journal");
            const auto orphan=QDir(directory.path()).entryList({QFileInfo(path).fileName()+".stage-*"},QDir::Dirs|QDir::NoDotAndDotDot);
            require(orphan.size()==(point==int(SaveFaultPoint::AfterStage)?1:0),"Unexpected retained stage count");
            result["passed"]=true;result["recovered"]=newInstalled?"new":"original";result["unreferenced_stage_preserved"]=!orphan.empty();std::cout<<"PASS process-save "<<point<<'\n';
        }catch(const std::exception& error){if(writer.state()!=QProcess::NotRunning){writer.kill();writer.waitForFinished(10000);}result["passed"]=false;result["error"]=error.what();++failed;std::cout<<"FAIL process-save "<<point<<": "<<error.what()<<'\n';}
        results.append(result);
    }
    QFile report(directory.filePath("report.json"));if(report.open(QIODevice::WriteOnly))report.write(QJsonDocument(QJsonObject{{"cases",results},{"passed",5-failed},{"failed",failed},{"actual_process_termination",true},{"power_loss_tested",false}}).toJson());
    std::cout<<"Evidence "<<directory.path().toStdString()<<'\n';return failed?1:0;
}
