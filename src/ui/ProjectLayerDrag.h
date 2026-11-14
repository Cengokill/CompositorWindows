#pragma once
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include "core/Document.h"
#include <QMimeData>
#include <QPointer>
#include <QUuid>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace compositor::ui {
inline constexpr auto projectLayerMime="application/x-compositor-project-layers";
inline constexpr auto layerTransferMime="application/x-compositor-layer-transfer";
inline constexpr auto projectLayerIdentityProperty="compositorDragSession";
inline QString projectLayerIdentity(QObject* owner){return owner?owner->property(projectLayerIdentityProperty).toString():QString{};}
struct ProjectLayerPayload{QString session;std::vector<std::string> ids;QPointer<QObject> owner;bool scoped{};};
class ProjectLayerMimeData final:public QMimeData {
public:
    QPointer<QObject> owner;
    ProjectLayerMimeData(QObject* source,const std::vector<std::string>& ids):owner(source){
        if(!source||ids.empty())return;
        auto token=projectLayerIdentity(source);if(token.isEmpty()){token=QUuid::createUuid().toString(QUuid::WithoutBraces);source->setProperty(projectLayerIdentityProperty,token);}
        QJsonArray rows;for(const auto& id:ids)rows.append(QString::fromStdString(id));
        setData(projectLayerMime,QJsonDocument(QJsonObject{{"session",token},{"layers",rows}}).toJson(QJsonDocument::Compact));
    }
    void setTransfer(std::function<QByteArray()> transfer){transfer_=std::move(transfer);}
    QStringList formats() const override;
    bool hasFormat(const QString& mimeType) const override;
protected:
    QVariant retrieveData(const QString& mimeType,QMetaType preferredType) const override;
private:
    std::function<QByteArray()> transfer_;
};
struct LayerTransfer { Document document; std::vector<std::string> roots; };
QByteArray packageLayerTransfer(const Document&,const std::vector<std::string>& roots);
std::optional<LayerTransfer> documentFromLayerTransfer(const QByteArray&);
inline std::optional<ProjectLayerPayload> projectLayerPayload(const QMimeData* mime){
    if(!mime||!mime->hasFormat(projectLayerMime))return {};
    const auto bytes=mime->data(projectLayerMime);if(bytes.size()>2*1024*1024)return {};
    const auto parsed=QJsonDocument::fromJson(bytes);if(!parsed.isObject())return {};const auto object=parsed.object();
    const auto session=object.value("session");const auto layers=object.value("layers");
    if(!session.isString()||QUuid(session.toString()).isNull()||!layers.isArray())return {};
    ProjectLayerPayload result;result.session=session.toString();const auto rows=layers.toArray();if(rows.empty()||rows.size()>10000)return {};
    for(const auto& value:rows){if(!value.isString())return {};const auto id=value.toString();if(id.isEmpty()||id.size()>128||id.contains(QChar(0)))return {};result.ids.push_back(id.toStdString());}
    if(const auto* local=dynamic_cast<const ProjectLayerMimeData*>(mime)){if(!local->owner)return {};result.owner=local->owner;result.scoped=true;}
    return result;
}
}
