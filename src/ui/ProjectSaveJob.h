#pragma once
#include "core/Document.h"
#include <QObject>
#include <QString>
#include <functional>
#include <memory>
#include <string>

namespace compositor::ui {
// Writes one captured document snapshot through ProjectStore on a worker thread.
// The worker owns value copies only. Completion runs on the UI thread, and the job
// then deletes itself. The UI keeps editing while the write is pending.
class ProjectSaveJob final:public QObject {
public:
    struct Host {
        // Receives an empty string on success, otherwise the store's error message.
        std::function<void(const QString& error)> completed;
        // Runs on the worker thread before the write. Tests use it to hold a write open.
        std::function<void()> beforeWrite;
    };
    ProjectSaveJob(Document,std::string activeLayer,QString path,Host,QObject* parent);
    ~ProjectSaveJob()override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
