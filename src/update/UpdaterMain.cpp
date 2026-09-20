#include "Updater.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <iostream>

int wmain(int argc,wchar_t** argv){try{
    QStringList args;for(int i=1;i<argc;++i)args.push_back(QString::fromWCharArray(argv[i]));
    auto value=[&](const QString& flag){auto i=args.indexOf(flag);if(i<0||i+1>=args.size())throw std::runtime_error(("Missing "+flag).toStdString());return args[i+1];};
    if(args.isEmpty()||(args[0]!="check"&&args[0]!="install"&&args[0]!="recover"&&args[0]!="--uninstall-payloads"))throw std::runtime_error("Usage: CompositorUpdater check|install|recover|--uninstall-payloads --root directory --test-feed directory-or-loopback-url --allow-test-key");
    compositor::update::Updater updater(std::filesystem::path(value("--root").toStdWString()));
    compositor::update::Options options;options.allowTestKey=args.contains("--allow-test-key");std::uint64_t transferred=0,cancelAfter=0;
    if(args.contains("--cancel-after-bytes")){bool valid=false;cancelAfter=value("--cancel-after-bytes").toULongLong(&valid);if(!valid||!options.allowTestKey)throw std::runtime_error("Invalid test cancellation option");}
    options.progress=[&](auto done,auto){transferred=done;};options.cancelled=[&]{return cancelAfter&&transferred>=cancelAfter;};
    if(args.contains("--fault")){if(!options.allowTestKey)throw std::runtime_error("Fault injection requires test trust opt-in");auto fault=value("--fault");using F=compositor::update::Options::Fault;if(fault=="after-version")options.fault=F::AfterVersionRename;else if(fault=="after-activation")options.fault=F::AfterPendingActivation;else if(fault=="crash-after-activation")options.fault=F::CrashAfterPendingActivation;else if(fault=="before-finalize")options.fault=F::BeforeFinalize;else throw std::runtime_error("Unknown fault injection");}
    QJsonObject output;
    if(args[0]=="--uninstall-payloads"){auto removed=updater.uninstallPayloads(options);output={{"removedFiles",qint64(removed.removedFiles)},{"retainedPaths",QJsonArray::fromStringList(removed.retainedPaths)}};}
    else if(args[0]=="recover"){auto s=updater.recover();output={{"current",s.current.text()},{"pending",s.pending}};}
    else if(args[0]=="check"){auto c=updater.check(value("--test-feed"),options);output={{"available",c.available},{"version",c.manifest.version.text()},{"channel",c.manifest.channel}};}
    else{auto result=updater.install(value("--test-feed"),options);output={{"installed",result.installed},{"version",result.version}};}
    std::cout<<QJsonDocument(output).toJson(QJsonDocument::Compact).constData()<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}}

