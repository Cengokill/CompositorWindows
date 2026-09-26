#pragma once
#include "core/Document.h"
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <functional>
#include <memory>

namespace compositor::ui {
struct WorkspaceDropRequest {
    QStringList paths;
    QPointer<QObject> destination;
    bool hasDestination{};
    std::optional<Point> point;
    std::shared_ptr<void> lifetime;
    // Runs once after this request settles; owner destruction discards it.
    // Individual provider errors can be reported after readable images import.
    std::function<void(bool cancelled)> completed;
};
// ProjectWorkspace.receive: wait before taking the workspace, then await every
// image URL before processing the next item. All host calls run on the UI thread.
class WorkspaceDropQueue final:public QObject {
public:
    struct Host {
        std::function<bool()> canBegin;
        std::function<bool()> canAdvance;
        std::function<void(bool)> managing;
        std::function<void(const QString&)> project;
        std::function<QObject*(QObject*,bool)> imageTarget;
        std::function<void(QObject*,const QString&,std::optional<Point>,std::shared_ptr<void>)> image;
        std::function<void(QObject*)> cancelImage;
        std::function<void(const QString&,const QString&)> error;
    };
    explicit WorkspaceDropQueue(Host,QObject* parent=nullptr);
    ~WorkspaceDropQueue() override;
    void enqueue(WorkspaceDropRequest);
    void imageFinished(QObject*,bool cancelled);
    void cancel(QObject* destination=nullptr);
    bool idle()const;
    bool waitingFor(QObject*)const;
    static WorkspaceDropQueue* find(QObject* parent);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    void drain();
    void finish();
};
}
