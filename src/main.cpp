#include "ui/MainWindow.h"
#include "ui/LocalizedText.h"
#include "ui/WhatsNew.h"
#include "AppLogging.h"
#include "persistence/ProjectStore.h"
#include "imaging/wic_codec.h"
#include "imaging/onnx_subject_provider.h"
#include "core/DocumentExport.h"
#include "plugin/UxpTranslator.h"
#include "plugin/PluginHost.h"
#include "plugin/UxpNativeHost.h"
#include "plugin/PluginRuntime.h"
#include "plugin/ScriptRuntime.h"
#include "CompositorVersion.h"
#include <QTemporaryDir>
#include <QFileInfo>
#include <QApplication>
#include <QTimer>
#include <QFile>
#include <QDir>
#include <QMessageBox>
#include <QTextStream>
#include <QTranslator>

int main(int argc,char**argv){
    QApplication app(argc,argv);app.setApplicationName("Compositor");app.setOrganizationName("Compositor Windows");app.setApplicationVersion(COMPOSITOR_VERSION_STRING);
    // The command catalog and shortcut settings key off English text; the translator
    // only changes what the interface displays.
    QTranslator translator;compositor::ui::installChineseTranslation(translator);
    compositor::logging::install();
    compositor::logging::trace(QString("startup executable=%1 arguments=%2")
        .arg(QCoreApplication::applicationFilePath(),app.arguments().join(" | ")));
    app.setProperty("manualUpdatesOnly", true);
    const auto args=app.arguments();
    if(args.contains("--check-release-notes"))return compositor::ui::installedReleaseNotes().empty()?2:0;
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
    if(args.contains("--uxp-analyse")){
        // Diagnostics: analyse a UXP plugin directory without opening a window.
        try{
            const int index=args.indexOf("--uxp-analyse");
            if(index+1>=args.size())throw std::runtime_error("Usage: Compositor --uxp-analyse <plugin-directory>");
            compositor::plugin::UxpImportResult result;QString error;
            if(!compositor::plugin::UxpTranslator::analyse(args[index+1],result,error))throw std::runtime_error(error.toStdString());
            // A WIN32 application has no console, so the report goes beside the plugin.
            const QString report=QDir(args[index+1]).filePath("compositor-analyse.txt");
            QFile file(report);
            if(!file.open(QIODevice::WriteOnly|QIODevice::Text))throw std::runtime_error("Cannot write analyse report");
            QTextStream out(&file);
            out<<"id="<<result.id<<"\nname="<<result.name<<"\nentry="<<result.entryFile<<"\n";
            out<<"mapped="<<result.mapped.size()<<"\nunsupported="<<result.unsupported.join("|")<<"\n";
            out<<"notes="<<result.manifestNotes.join("|")<<"\n";
            return result.mapped.isEmpty()&&result.entryFile.isEmpty()?3:0;
        }catch(const std::exception&e){QTextStream(stderr)<<e.what()<<Qt::endl;return 2;}
    }    if(args.contains("--uxp-build")){
        // Diagnostics: translate, write and compile a UXP plugin without the UI.
        try{
            const int index=args.indexOf("--uxp-build");
            if(index+1>=args.size())throw std::runtime_error("Usage: Compositor --uxp-build <plugin-directory>");
            const QString source=args[index+1];
            compositor::plugin::UxpImportResult result;QString error;
            if(!compositor::plugin::UxpTranslator::analyse(source,result,error))throw std::runtime_error(error.toStdString());
            const QString output=QDir(QCoreApplication::applicationDirPath()).filePath("plugins/"+result.id);
            if(!compositor::plugin::UxpTranslator::writeFiles(result,output,error))throw std::runtime_error(error.toStdString());
            const QString binary=QDir(output).filePath(result.id+".compositor-plugin.dll");
            const QString sdk=QDir(QCoreApplication::applicationDirPath()).filePath("sdk");
            if(!compositor::plugin::UxpTranslator::compile(result,output,sdk,binary,error))throw std::runtime_error(error.toStdString());
            QFile report(QDir(source).filePath("compositor-build.txt"));
            if(report.open(QIODevice::WriteOnly|QIODevice::Text))report.write(binary.toUtf8());
            return 0;
        }catch(const std::exception&e){
            QFile report(QDir(args[args.indexOf("--uxp-build")+1]).filePath("compositor-build.txt"));
            if(report.open(QIODevice::WriteOnly|QIODevice::Text))report.write(e.what());
            return 2;
        }
    }    if(args.contains("--js-selftest")){
        // Diagnostics: proves the JavaScript engine process runs a plugin bundle and
        // returns the mutations the plugin recorded.
        QFile file(QDir(QCoreApplication::applicationDirPath()).filePath("js-selftest.txt"));
        file.open(QIODevice::WriteOnly|QIODevice::Text);
        QTextStream out(&file);
        const QString engine=compositor::plugin::ScriptRuntime::findEngine();
        out<<"engine="<<(engine.isEmpty()?QStringLiteral("(none)"):engine)<<"\n";
        const QString shim=QStringLiteral("function record(op,index,value){__compositorMutations.push({op:op,index:index,value:value});}");
        const QString snapshot=QStringLiteral("{\"hasDocument\":true,\"doc\":{\"width\":64},\"layers\":[{\"opacity\":1}]}");
        const QString plugin=QStringLiteral("function run(){ record('opacity',0,0.5); }");
        const auto result=compositor::plugin::ScriptRuntime::runBundle(shim,snapshot,plugin,QStringLiteral("selftest.js"));
        out<<"ok="<<result.ok<<"\nmutations="<<result.mutationsJson<<"\nerror="<<result.error<<"\noutput="<<result.output<<"\n";
        out.flush();
        return result.ok?0:2;
    }    if(args.contains("--uxp-list")){
        // Diagnostics: report what the plugin scan finds beside the executable.
        QFile file(QDir(QCoreApplication::applicationDirPath()).filePath("uxp-list.txt"));
        file.open(QIODevice::WriteOnly|QIODevice::Text);
        QTextStream out(&file);
        const int rootIndex=args.indexOf("--uxp-list");
        const QString root=rootIndex+1<args.size()&&!args[rootIndex+1].startsWith("--")
            ? args[rootIndex+1] : compositor::plugin::defaultPluginDirectory();
        out<<"directory="<<root<<"\nexists="<<QDir(root).exists()<<"\n";
        for(const auto& info : QDir(root).entryInfoList(QDir::Dirs|QDir::NoDotAndDotDot,QDir::Name)){
            compositor::plugin::UxpPlugin plugin;QString error;
            const bool ok=compositor::plugin::UxpNativeHost::discover(info.absoluteFilePath(),plugin,error);
            out<<"folder="<<info.fileName()<<" ok="<<ok<<" id="<<plugin.id<<" entry="<<plugin.entryPath
               <<" scripts="<<plugin.scriptPaths.size()<<" err="<<error<<"\n";
        }
        out.flush();
        return 0;
    }    if(args.contains("--uxp-run")){
        // Diagnostics: load one UXP plugin and run it against a scratch document.
        try{
            const int index=args.indexOf("--uxp-run");
            if(index+1>=args.size())throw std::runtime_error("Usage: Compositor --uxp-run <plugin-directory>");
            compositor::plugin::UxpPlugin plugin;QString error;
            if(!compositor::plugin::UxpNativeHost::discover(args[index+1],plugin,error))throw std::runtime_error(error.toStdString());
            compositor::plugin::PluginRuntime runtime;
            compositor::Document document;document.id="uxp-diagnostic";document.width=64;document.height=64;
            compositor::Layer layer;layer.id="base";layer.name="Background";
            layer.raster=compositor::Raster::filled(document.width,document.height,{0,0,0,255});
            document.layers.push_back(layer);
            std::optional<compositor::Document> storage=std::move(document);
            runtime.setDocumentProvider([&storage]()->compositor::Document*{return &*storage;});
            runtime.setActiveLayerProvider([]{return int32_t(0);});
            runtime.setEditInvoker([&storage](const char*,const compositor::plugin::PluginRuntime::EditOperation&operation){operation(*storage);return true;});
            runtime.setLogSink([](const QString&message){compositor::logging::trace(QStringLiteral("uxp: ")+message);});
            runtime.setTitleProvider([]{return QStringLiteral("Diagnostic");});
            compositor::plugin::UxpNativeHost host(&runtime);
            const auto result=host.run(plugin,error);
            QFile report(QDir(args[index+1]).filePath("compositor-uxp-run.txt"));
            if(report.open(QIODevice::WriteOnly|QIODevice::Text)){
                QTextStream out(&report);
                out<<"plugin="<<plugin.id<<"\nentry="<<plugin.entryPath<<"\nresult="<<int(result)<<"\nerror="<<error<<"\n";
            }
            return result==COMPOSITOR_OK?0:2;
        }catch(const std::exception&e){
            QFile report(QDir(args[args.indexOf("--uxp-run")+1]).filePath("compositor-uxp-run.txt"));
            if(report.open(QIODevice::WriteOnly|QIODevice::Text))report.write(e.what());
            return 2;
        }
    }    if(args.contains("--js-selftest")){
        // Diagnostics: proves the JavaScript engine process runs a plugin bundle and
        // returns the mutations the plugin recorded.
        QFile file(QDir(QCoreApplication::applicationDirPath()).filePath("js-selftest.txt"));
        file.open(QIODevice::WriteOnly|QIODevice::Text);
        QTextStream out(&file);
        const QString engine=compositor::plugin::ScriptRuntime::findEngine();
        out<<"engine="<<(engine.isEmpty()?QStringLiteral("(none)"):engine)<<"\n";
        const QString shim=QStringLiteral("function record(op,index,value){__compositorMutations.push({op:op,index:index,value:value});}");
        const QString snapshot=QStringLiteral("{\"hasDocument\":true,\"doc\":{\"width\":64},\"layers\":[{\"opacity\":1}]}");
        const QString plugin=QStringLiteral("function run(){ record('opacity',0,0.5); }");
        const auto result=compositor::plugin::ScriptRuntime::runBundle(shim,snapshot,plugin,QStringLiteral("selftest.js"));
        out<<"ok="<<result.ok<<"\nmutations="<<result.mutationsJson<<"\nerror="<<result.error<<"\noutput="<<result.output<<"\n";
        out.flush();
        return result.ok?0:2;
    }    if(args.contains("--uxp-list")){
        // Diagnostics: report what the plugin scan finds beside the executable.
        QFile file(QDir(QCoreApplication::applicationDirPath()).filePath("uxp-list.txt"));
        file.open(QIODevice::WriteOnly|QIODevice::Text);
        QTextStream out(&file);
        const int rootIndex=args.indexOf("--uxp-list");
        const QString root=rootIndex+1<args.size()&&!args[rootIndex+1].startsWith("--")
            ? args[rootIndex+1] : compositor::plugin::defaultPluginDirectory();
        out<<"directory="<<root<<"\nexists="<<QDir(root).exists()<<"\n";
        for(const auto& info : QDir(root).entryInfoList(QDir::Dirs|QDir::NoDotAndDotDot,QDir::Name)){
            compositor::plugin::UxpPlugin plugin;QString error;
            const bool ok=compositor::plugin::UxpNativeHost::discover(info.absoluteFilePath(),plugin,error);
            out<<"folder="<<info.fileName()<<" ok="<<ok<<" id="<<plugin.id<<" entry="<<plugin.entryPath
               <<" scripts="<<plugin.scriptPaths.size()<<" err="<<error<<"\n";
        }
        out.flush();
        return 0;
    }    if(args.contains("--uxp-run")){
        // Diagnostics: load one UXP plugin and run it against a scratch document.
        try{
            const int index=args.indexOf("--uxp-run");
            if(index+1>=args.size())throw std::runtime_error("Usage: Compositor --uxp-run <plugin-directory>");
            compositor::plugin::UxpPlugin plugin;QString error;
            if(!compositor::plugin::UxpNativeHost::discover(args[index+1],plugin,error))throw std::runtime_error(error.toStdString());
            compositor::plugin::PluginRuntime runtime;
            compositor::Document document;document.id="uxp-diagnostic";document.width=64;document.height=64;
            compositor::Layer layer;layer.id="base";layer.name="Background";
            layer.raster=compositor::Raster::filled(document.width,document.height,{0,0,0,255});
            document.layers.push_back(layer);
            std::optional<compositor::Document> storage=std::move(document);
            runtime.setDocumentProvider([&storage]()->compositor::Document*{return &*storage;});
            runtime.setActiveLayerProvider([]{return int32_t(0);});
            runtime.setEditInvoker([&storage](const char*,const compositor::plugin::PluginRuntime::EditOperation&operation){operation(*storage);return true;});
            runtime.setLogSink([](const QString&message){compositor::logging::trace(QStringLiteral("uxp: ")+message);});
            runtime.setTitleProvider([]{return QStringLiteral("Diagnostic");});
            compositor::plugin::UxpNativeHost host(&runtime);
            const auto result=host.run(plugin,error);
            QFile report(QDir(args[index+1]).filePath("compositor-uxp-run.txt"));
            if(report.open(QIODevice::WriteOnly|QIODevice::Text)){
                QTextStream out(&report);
                out<<"plugin="<<plugin.id<<"\nentry="<<plugin.entryPath<<"\nresult="<<int(result)<<"\nerror="<<error<<"\n";
            }
            return result==COMPOSITOR_OK?0:2;
        }catch(const std::exception&e){
            QFile report(QDir(args[args.indexOf("--uxp-run")+1]).filePath("compositor-uxp-run.txt"));
            if(report.open(QIODevice::WriteOnly|QIODevice::Text))report.write(e.what());
            return 2;
        }
    }    if(args.contains("--render-project")){
        try{
            int input=args.indexOf("--render-project"),output=args.indexOf("--output");
            if(input+1>=args.size()||output<0||output+1>=args.size())throw std::runtime_error("Usage: Compositor --render-project project.comp --output result.png");
            compositor::ProjectStore store(compositor::makeWicProjectCodec());auto opened=store.load(std::filesystem::path(args[input+1].toStdWString()));const auto&d=opened.document;
            compositor::imaging::ExportOptions options;options.dpi=d.resolution;
            compositor::imaging::StreamExportLimits limits;limits.previewWidth=limits.previewHeight=0;
            compositor::exportDocumentAtomic(d,std::filesystem::path(args[output+1].toStdWString()),options,limits);return 0;
        }catch(const std::exception&e){QTextStream(stderr)<<e.what()<<Qt::endl;return 2;}
    }
    compositor::ui::installDialogLocalizer(nullptr);compositor::MainWindow window(args.contains("--warp"));window.show();
    if(args.contains("--feasibility")||args.contains("--ui-test"))window.addFeasibilityDocument();
    if(args.contains("--ui-test")){
        int i=args.indexOf("--evidence");QString dir=i>=0&&i+1<args.size()?args[i+1]:"evidence/native-ui";QDir().mkpath(dir);
        QTimer::singleShot(200,&app,[&,dir]{try{window.exerciseNativeUi(dir);app.exit(0);}catch(const std::exception&e){QFile error(dir+"/failure.txt");if(error.open(QIODevice::WriteOnly))error.write(e.what());app.exit(1);}});
    }else{
        QStringList paths;for(int i=1;i<args.size();++i)if(!args[i].startsWith("--"))paths.append(args[i]);
        QTimer::singleShot(0,&window,[&window,paths]{
            window.receiveDropPaths(paths);
            window.startWhatsNew([&window]{window.startAutomaticUpdateChecks();});
        });
    }
    return app.exec();
}
