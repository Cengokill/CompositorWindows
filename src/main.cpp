#include "ui/MainWindow.h"
#include "persistence/ProjectStore.h"
#include "imaging/wic_codec.h"
#include "imaging/onnx_subject_provider.h"
#include "core/DocumentExport.h"
#include <QTemporaryDir>
#include <QFileInfo>
#include <QApplication>
#include <QTimer>
#include <QFile>
#include <QDir>
#include <QMessageBox>
#include <QTextStream>

int main(int argc,char**argv){
    QApplication app(argc,argv);app.setApplicationName("Compositor");app.setOrganizationName("Compositor Windows");app.setApplicationVersion("0.1.0");
    app.setProperty("manualUpdatesOnly", true);
    const auto args=app.arguments();
    if(args.contains("--update-health-check")){
        try{
            auto model=QApplication::applicationDirPath()+"/models/birefnet-lite.onnx";
            if(!QFileInfo::exists(model)||!QFileInfo::exists(QApplication::applicationDirPath()+"/shaders/BrushCoverage.hlsl"))throw std::runtime_error("Packaged model or shader is missing");
            compositor::imaging::OnnxSubjectProvider provider(std::filesystem::path(model.toStdWString()));
            compositor::imaging::RgbaImage image{2,2,8,std::vector<uint8_t>(16,255)};
            provider.healthCheck();
            QTemporaryDir directory;if(!directory.isValid())throw std::runtime_error("No writable temporary directory");
            auto path=std::filesystem::path((directory.path()+"/check.png").toStdWString());compositor::imaging::WicCodec::encode(path,image,{});auto decoded=compositor::imaging::WicCodec::decode(path);if(decoded.image.pixels!=image.pixels)throw std::runtime_error("Image codec health check failed");
            return 0;
        }catch(const std::exception&e){QTextStream(stderr)<<e.what()<<Qt::endl;return 2;}
    }
    if(args.contains("--render-project")){
        try{
            int input=args.indexOf("--render-project"),output=args.indexOf("--output");
            if(input+1>=args.size()||output<0||output+1>=args.size())throw std::runtime_error("Usage: Compositor --render-project project.comp --output result.png");
            compositor::ProjectStore store(compositor::makeWicProjectCodec());auto opened=store.load(std::filesystem::path(args[input+1].toStdWString()));const auto&d=opened.document;
            compositor::imaging::ExportOptions options;options.dpi=d.resolution;
            compositor::imaging::StreamExportLimits limits;limits.previewWidth=limits.previewHeight=0;
            compositor::exportDocumentAtomic(d,std::filesystem::path(args[output+1].toStdWString()),options,limits);return 0;
        }catch(const std::exception&e){QTextStream(stderr)<<e.what()<<Qt::endl;return 2;}
    }
    compositor::MainWindow window(args.contains("--warp"));window.show();
    if(args.contains("--feasibility")||args.contains("--ui-test"))window.addFeasibilityDocument();
    if(args.contains("--ui-test")){
        int i=args.indexOf("--evidence");QString dir=i>=0&&i+1<args.size()?args[i+1]:"evidence/native-ui";QDir().mkpath(dir);
        QTimer::singleShot(200,&app,[&,dir]{try{window.exerciseNativeUi(dir);app.exit(0);}catch(const std::exception&e){QFile error(dir+"/failure.txt");if(error.open(QIODevice::WriteOnly))error.write(e.what());app.exit(1);}});
    }else{
        QStringList paths;for(int i=1;i<args.size();++i)if(!args[i].startsWith("--"))paths.append(args[i]);
        QTimer::singleShot(0,&window,[&window,paths]{window.receiveDropPaths(paths);});
    }
    return app.exec();
}
