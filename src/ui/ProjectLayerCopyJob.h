#pragma once
#include "ImportActions.h"
#include "layers/LayerOperations.h"
#include <QObject>
#include <QPointer>
#include <functional>
#include <memory>

namespace compositor::ui {
struct LayerCopyWork {
    Document source,destination;
    std::string root;
    std::optional<Point> point;
    QPointer<QObject> target;
    ImportState before;
};
// Copies provider rows in order, awaiting off-thread dependency baking before
// recording each independent destination edit. Hosts execute on the UI thread.
class ProjectLayerCopyJob final:public QObject {
public:
    struct Host {
        std::function<std::optional<LayerCopyWork>(const std::string&)> prepare;
        std::function<void(const LayerCopyWork&,layers::CopyResult)> commit;
        std::function<void(const LayerCopyWork&)> settled;
        std::function<void(const QString&)> error;
        std::function<void()> finished;
    };
    ProjectLayerCopyJob(std::vector<std::string>,Host,QObject* parent);
    ~ProjectLayerCopyJob()override;
    static ProjectLayerCopyJob* find(QObject* parent);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    void advance();
};
}
