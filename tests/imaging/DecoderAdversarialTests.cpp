#include "ui/ImportActions.h"
#include "imaging/wic_codec.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <windows.h>
#include <psapi.h>
#include <iostream>

using namespace compositor;
namespace {
constexpr quint64 workingLimit=64*1024*1024;
constexpr quint64 processLimit=512*1024*1024;
constexpr int timeoutMs=5000;
void require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
QByteArray read(const QString& path){QFile file(path);require(file.open(QIODevice::ReadOnly),"Cannot read corpus source");return file.readAll();}
void write(const QString& path,const QByteArray& data){QFile file(path);require(file.open(QIODevice::WriteOnly|QIODevice::NewOnly),"Cannot create corpus evidence");require(file.write(data)==data.size(),"Cannot write corpus evidence");}
QString hash(const QByteArray& data){return QString::fromLatin1(QCryptographicHash::hash(data,QCryptographicHash::Sha256).toHex());}
std::filesystem::path native(const QString& path){return std::filesystem::path(path.toStdWString());}
void bigEndian(QByteArray& bytes,int offset,quint32 value){for(int i=0;i<4;++i)bytes[offset+i]=char(value>>(24-8*i));}
quint32 crc32(const QByteArray& bytes){quint32 crc=0xffffffffU;for(const auto byte:bytes){crc^=quint8(byte);for(int bit=0;bit<8;++bit)crc=(crc>>1)^(0xedb88320U&quint32(-qint32(crc&1)));}return ~crc;}
struct Case {QString id,path,expectation;bool tinyBudget{},cancelled{};};
QJsonObject child(const QString& path,bool tinyBudget,bool cancelled){
    imaging::ImportOptions options;options.maxWorkingBytes=workingLimit;options.remainingPixels=tinyBudget?1:2000000;
    options.cancelled=[cancelled]{return cancelled;};
    try{const auto decoded=ui::decodeImportImage(native(path),options);imaging::validate(decoded.image);
        imaging::checkedBytes(decoded.image.width,decoded.image.height,4,options);
        return {{"outcome","decoded"},{"width",int(decoded.image.width)},{"height",int(decoded.image.height)},{"pixels_sha256",hash(QByteArray(reinterpret_cast<const char*>(decoded.image.pixels.data()),qsizetype(decoded.image.pixels.size())))}};
    }catch(const std::bad_alloc&){return {{"outcome","allocation_failure"}};}
    catch(const std::exception& error){return {{"outcome","rejected"},{"reason",QString::fromUtf8(error.what())}};}
}
}
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);const auto args=app.arguments();std::cout<<std::unitbuf;
    if(args.size()==5&&args[1]=="--decode"){
        const auto result=child(args[2],args[3]=="1",args[4]=="1");
        std::cout<<QJsonDocument(result).toJson(QJsonDocument::Compact).toStdString()<<'\n';return 0;
    }
    try{
        require(args.size()==3,"Provide source root and evidence directory");
        QDir().mkpath(args[2]);QTemporaryDir directory(args[2]+"/decoder-corpus-XXXXXX");require(directory.isValid(),"Cannot create corpus directory");directory.setAutoRemove(false);
        const auto root=args[1];std::vector<Case> cases;
        auto add=[&](QString id,const QByteArray& bytes,QString expectation="bounded",bool tiny=false,bool cancel=false){
            auto path=directory.filePath(id+".payload");write(path,bytes);cases.push_back({id,path,expectation,tiny,cancel});
        };
        imaging::RgbaImage source{31,23,31*4,std::vector<uint8_t>(31*23*4)};
        quint32 state=0xc011ab1eU;auto random=[&]{state=state*1664525U+1013904223U;return state;};
        for(size_t i=0;i<source.pixels.size();i+=4){const auto alpha=uint8_t(random()>>24);for(int c=0;c<3;++c)source.pixels[i+c]=uint8_t(random()%(quint32(alpha)+1));source.pixels[i+3]=alpha;}
        std::vector<std::pair<QString,QByteArray>> seeds;
        for(auto entry:{std::pair{"png",imaging::ImageFormat::Png},std::pair{"jpeg",imaging::ImageFormat::Jpeg},std::pair{"tiff",imaging::ImageFormat::Tiff}}){
            auto path=directory.filePath(QString("seed-%1.image").arg(entry.first));imaging::ExportOptions options;options.format=entry.second;imaging::WicCodec::encode(native(path),source,options);seeds.emplace_back(entry.first,read(path));
        }
        const auto heicPath=root+"/dependencies/imaging/libheif/tests/data/with-alpha-512x512.heic";
        seeds.emplace_back("heic",read(heicPath));
        for(const auto& [name,bytes]:seeds){
            add(name+"-valid-disguised",bytes,"decoded");
            for(int i=0;i<5;++i){const qsizetype length=i==0?0:i==1?1:i==2?11:i==3?bytes.size()/2:bytes.size()-1;add(name+QString("-truncated-%1").arg(i),bytes.left(length));}
            for(int i=0;i<8;++i){auto changed=bytes;for(int j=0;j<1+i%4;++j){auto offset=qsizetype(random()%quint32(changed.size()));changed[offset]=char(quint8(changed[offset])^quint8(1U<<(random()%8)));}add(name+QString("-mutated-%1").arg(i),changed);}
            auto trailing=bytes;for(int i=0;i<1024;++i)trailing.append(char(random()>>24));add(name+"-trailing",trailing);
            add(name+"-pixel-budget",bytes,"rejected",true);add(name+"-cancelled",bytes,"rejected",false,true);
        }
        const auto png=seeds[0].second;
        for(auto dimensions:{std::pair{30001U,23U},std::pair{2001U,1000U},std::pair{0xffffffffU,0xffffffffU}}){
            auto changed=png;bigEndian(changed,16,dimensions.first);bigEndian(changed,20,dimensions.second);bigEndian(changed,29,crc32(changed.mid(12,17)));
            add(QString("png-dimensions-%1-%2").arg(dimensions.first).arg(dimensions.second),changed,"rejected");
        }
        add("unsupported-gif",QByteArray::fromHex("47494638396101000100800000000000ffffff2c00000000010001000002024401003b"),"rejected");
        add("oversized-heif-box",QByteArray::fromHex("ffffffff66747970686569630000000068656963"),"rejected");
        add("random-header",QByteArray(1024,char(-1)),"rejected");
        QJsonArray results;int failed=0,decoded=0,rejected=0;quint64 maximumWorking=0,maximumCommit=0;QElapsedTimer whole;whole.start();
        for(const auto& item:cases){
            QJsonObject result{{"id",item.id},{"path",item.path},{"input_sha256",hash(read(item.path))},{"expectation",item.expectation},{"tiny_pixel_budget",item.tinyBudget},{"cancelled_before_decode",item.cancelled}};
            QProcess process;HANDLE handle=nullptr;
            try{
                process.setProgram(app.applicationFilePath());process.setArguments({"--decode",item.path,item.tinyBudget?"1":"0",item.cancelled?"1":"0"});process.start();require(process.waitForStarted(5000),"Decoder child failed to start");
                handle=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ,FALSE,DWORD(process.processId()));require(handle!=nullptr,"Cannot monitor decoder child memory");
                QElapsedTimer timer;timer.start();quint64 peakWorking=0,peakCommit=0;
                do {PROCESS_MEMORY_COUNTERS_EX memory{};memory.cb=sizeof(memory);
                    require(GetProcessMemoryInfo(handle,reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory)),"Cannot inspect child memory");
                    peakWorking=std::max(peakWorking,quint64(memory.PeakWorkingSetSize));peakCommit=std::max(peakCommit,quint64(memory.PeakPagefileUsage));
                    require(peakWorking<=processLimit&&peakCommit<=processLimit,"Decoder exceeded frozen 512MiB process budget");
                    require(timer.elapsed()<=timeoutMs,"Decoder exceeded frozen five-second limit");
                }while(!process.waitForFinished(10));
                maximumWorking=std::max(maximumWorking,peakWorking);maximumCommit=std::max(maximumCommit,peakCommit);
                result["elapsed_ms"]=double(timer.elapsed());result["peak_working_set"]=double(peakWorking);result["peak_commit"]=double(peakCommit);
                require(process.exitStatus()==QProcess::NormalExit&&process.exitCode()==0,"Decoder process crashed or returned failure");
                const auto bytes=process.readAllStandardOutput();const auto output=QJsonDocument::fromJson(bytes).object();
                require(!output.empty(),"Decoder emitted no result");const auto outcome=output["outcome"].toString();
                require(outcome=="decoded"||outcome=="rejected","Decoder exhausted allocation budget");
                require(item.expectation=="bounded"||item.expectation==outcome,"Decoder violated explicit acceptance or rejection case");
                if(outcome=="decoded")++decoded;else ++rejected;result["decoder"]=output;result["passed"]=true;
            }catch(const std::exception& error){if(process.state()!=QProcess::NotRunning){process.kill();process.waitForFinished(5000);}result["passed"]=false;result["error"]=QString::fromUtf8(error.what());result["stdout"]=QString::fromUtf8(process.readAllStandardOutput());++failed;}
            if(handle)CloseHandle(handle);result["stderr"]=QString::fromUtf8(process.readAllStandardError());results.append(result);
            std::cout<<(result["passed"].toBool()?"PASS ":"FAIL ")<<item.id.toStdString()<<'\n';
        }
        QJsonObject report{{"schema_version",1},{"corpus","DECODER_ADVERSARIAL_V1"},{"seed","0xc011ab1e"},{"cases",results},{"passed",int(cases.size())-failed},{"failed",failed},{"decoded",decoded},{"rejected",rejected},{"timeout_per_case_ms",timeoutMs},{"process_limit_bytes",double(processLimit)},{"import_working_limit_bytes",double(workingLimit)},{"maximum_peak_working_set",double(maximumWorking)},{"maximum_peak_commit",double(maximumCommit)},{"elapsed_ms",double(whole.elapsed())},{"heic_seed_sha256",hash(seeds.back().second)},{"executable_sha256",hash(read(app.applicationFilePath()))},{"third_party_decoders_instrumented",false}};
        write(directory.filePath("report.json"),QJsonDocument(report).toJson());
        std::cout<<"Decoder corpus "<<cases.size()-size_t(failed)<<'/'<<cases.size()<<" passed, "<<decoded<<" decoded, "<<rejected<<" rejected. Evidence "<<directory.path().toStdString()<<'\n';return failed?1:0;
    }catch(const std::exception& error){std::cerr<<"FAIL corpus setup: "<<error.what()<<'\n';return 2;}
}
