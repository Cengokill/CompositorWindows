#include "ProjectLayerDrag.h"
#include "layers/LayerOperations.h"
#include "persistence/ProjectStore.h"
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <algorithm>
#include <filesystem>

namespace compositor::ui {
namespace {
const Layer* findLayer(const Document& document,const std::string& id){
    for(const auto& layer:document.layers)if(layer.id==id)return &layer;
    return nullptr;
}
bool safeRelative(const QString& name){
    if(name.isEmpty()||name.startsWith('/')||name.startsWith('\\')||name.contains(':'))return false;
    const auto parts=name.split(QRegularExpression("[/\\\\]"),Qt::SkipEmptyParts);
    return std::none_of(parts.begin(),parts.end(),[](const QString& part){return part=="..";});
}
QByteArray packDirectory(const std::filesystem::path& root){
    QByteArray bytes;
    QDataStream stream(&bytes,QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    std::vector<std::pair<QString,std::filesystem::path>> files;
    for(const auto& entry:std::filesystem::recursive_directory_iterator(root)){
        if(!entry.is_regular_file())continue;
        const auto relative=std::filesystem::relative(entry.path(),root);
        const auto name=QString::fromStdWString(relative.generic_wstring());
        if(!safeRelative(name))return {};
        files.emplace_back(name,entry.path());
    }
    stream<<quint32(files.size());
    for(const auto& [name,path]:files){
        QFile file(QString::fromStdWString(path.wstring()));
        if(!file.open(QIODevice::ReadOnly))return {};
        const auto data=file.readAll();
        if(file.error()!=QFileDevice::NoError)return {};
        stream<<name<<data;
    }
    return stream.status()==QDataStream::Ok?bytes:QByteArray{};
}
}
QStringList ProjectLayerMimeData::formats() const {
    auto listed=QMimeData::formats();
    if(transfer_&&!listed.contains(QLatin1String(layerTransferMime)))listed.append(QLatin1String(layerTransferMime));
    return listed;
}
bool ProjectLayerMimeData::hasFormat(const QString& mimeType) const {
    return mimeType==QLatin1String(layerTransferMime)?bool(transfer_):QMimeData::hasFormat(mimeType);
}
QVariant ProjectLayerMimeData::retrieveData(const QString& mimeType,QMetaType preferredType) const {
    if(mimeType==QLatin1String(layerTransferMime)&&transfer_)return transfer_();
    return QMimeData::retrieveData(mimeType,preferredType);
}
QByteArray packageLayerTransfer(const Document& document,const std::vector<std::string>& ids){
    try{
        if(ids.empty())return {};
        std::unordered_set<std::string> nested;
        for(const auto& id:ids){const auto inside=layers::descendants(document,id);nested.insert(inside.begin(),inside.end());}
        std::vector<std::string> roots;
        for(const auto& id:ids)if(!nested.contains(id)&&findLayer(document,id))roots.push_back(id);
        if(roots.empty())return {};
        Document assembled;
        assembled.id=newId();
        assembled.width=document.width;
        assembled.height=document.height;
        assembled.resolution=document.resolution;
        std::vector<std::string> transferred;
        for(const auto& id:roots){
            const auto* layer=findLayer(document,id);
            const Point anchor=layer->transform.fromUnit({.5,.5});
            auto copied=layers::copySubtree(document,id,assembled,anchor);
            const auto mapped=copied.ids.find(id);
            if(mapped==copied.ids.end())return {};
            transferred.push_back(mapped->second);
            assembled=std::move(copied.edit.document);
        }
        QTemporaryDir directory;
        if(!directory.isValid())return {};
        const auto path=std::filesystem::path(directory.path().toStdWString())/L"transfer.comp";
        ProjectStore(makeWicProjectCodec()).save(path,assembled,transferred.front());
        QFile order(directory.filePath("transfer.comp/roots.txt"));
        if(!order.open(QIODevice::WriteOnly|QIODevice::Truncate))return {};
        for(const auto& id:transferred)if(order.write(QByteArray::fromStdString(id+"\n"))<0)return {};
        order.close();
        return packDirectory(path);
    }catch(...){return {};}
}
std::optional<LayerTransfer> documentFromLayerTransfer(const QByteArray& bytes){
    if(bytes.isEmpty())return {};
    try{
        QDataStream stream(bytes);
        stream.setVersion(QDataStream::Qt_6_0);
        quint32 count=0;
        stream>>count;
        if(stream.status()!=QDataStream::Ok||count==0||count>10000)return {};
        QTemporaryDir directory;
        if(!directory.isValid())return {};
        QDir root(directory.path());
        for(quint32 index=0;index<count;++index){
            QString name;QByteArray data;
            stream>>name>>data;
            if(stream.status()!=QDataStream::Ok||!safeRelative(name))return {};
            const auto parts=name.split(QRegularExpression("[/\\\\]"),Qt::SkipEmptyParts);
            QString relative;
            for(int part=0;part<parts.size()-1;++part){relative+=parts[part]+'/';if(!root.mkpath(relative))return {};}
            QFile file(root.filePath(parts.join('/')));
            if(!file.open(QIODevice::WriteOnly|QIODevice::Truncate)||file.write(data)!=data.size())return {};
        }
        auto opened=ProjectStore(makeWicProjectCodec()).load(std::filesystem::path(directory.path().toStdWString()));
        LayerTransfer transfer;
        transfer.document=std::move(opened.document);
        QFile order(root.filePath("roots.txt"));
        if(order.open(QIODevice::ReadOnly)){
            for(const auto& line:order.readAll().split('\n')){
                const auto id=line.trimmed().toStdString();
                if(!id.empty()&&findLayer(transfer.document,id))transfer.roots.push_back(id);
            }
        }
        if(transfer.roots.empty()){
            for(const auto& entry:layers::entries(transfer.document,true)){
                const auto* layer=findLayer(transfer.document,entry.id);
                if(layer&&layer->parentId.empty())transfer.roots.push_back(layer->id);
            }
        }
        if(transfer.roots.empty())return {};
        return transfer;
    }catch(...){return {};}
}
}
