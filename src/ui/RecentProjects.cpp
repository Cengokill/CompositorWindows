#include "RecentProjects.h"
#include <QDir>
#include <QFileInfo>
#include <QSettings>

namespace compositor::ui {
namespace {
QString normalizedPath(const QString& path){
    if(path.trimmed().isEmpty()||!QDir::isAbsolutePath(path))return {};
    return QDir::cleanPath(QDir::fromNativeSeparators(path));
}
bool contains(const QStringList& paths,const QString& path){
    for(const auto& item:paths)if(item.compare(path,Qt::CaseInsensitive)==0)return true;
    return false;
}
void store(QSettings& settings,const QStringList& paths){
    if(paths.isEmpty())settings.remove(recentProjectsKey);else settings.setValue(recentProjectsKey,paths);
    settings.sync();
}
}
QStringList recentProjects(QSettings& settings){
    const auto value=settings.value(recentProjectsKey);
    QStringList stored;
    if(value.typeId()==QMetaType::QStringList)stored=value.toStringList();
    else if(value.typeId()==QMetaType::QString)stored={value.toString()};
    QStringList result;
    for(const auto& item:stored){
        const auto path=normalizedPath(item);
        if(path.isEmpty()||contains(result,path))continue;
        result.push_back(path);
        if(result.size()==recentProjectLimit)break;
    }
    return result;
}
void recordRecentProject(QSettings& settings,const QString& path){
    const auto normalized=normalizedPath(path);
    if(normalized.isEmpty())return;
    QStringList paths{normalized};
    for(const auto& item:recentProjects(settings))if(item.compare(normalized,Qt::CaseInsensitive)!=0)paths.push_back(item);
    store(settings,paths.mid(0,recentProjectLimit));
}
QStringList pruneRecentProjects(QSettings& settings){
    const auto stored=recentProjects(settings);
    QStringList existing;
    for(const auto& path:stored)if(QFileInfo(path).isDir())existing.push_back(path);
    if(existing!=stored||settings.value(recentProjectsKey).toStringList()!=stored)store(settings,existing);
    return existing;
}
void clearRecentProjects(QSettings& settings){store(settings,{});}
QStringList recentProjectLabels(const QStringList& paths){
    QStringList labels;
    for(const auto& path:paths){
        const QFileInfo info(path);
        int matches=0;
        for(const auto& other:paths)if(QFileInfo(other).fileName().compare(info.fileName(),Qt::CaseInsensitive)==0)++matches;
        auto label=info.fileName();
        if(matches>1)label+=QString::fromUtf8(" \u2014 ")+QDir::toNativeSeparators(info.absolutePath());
        labels.push_back(label.replace('&',"&&"));
    }
    return labels;
}
}