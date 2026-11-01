#pragma once
#include "core/Document.h"
#include <QObject>
#include <QString>
#include <functional>
#include <memory>
class QWidget;
namespace compositor {
struct EditorProject;
namespace ui {
class LastFilterJob final:public QObject {
public:
    struct Host {
        std::function<void(Layer)> commit;
        std::function<void()> settled;
        std::function<void(const QString&)> error;
    };
    LastFilterJob(std::function<Layer(std::function<bool()>)> work,Host,QWidget* parent,EditorProject* owner,const QString& progressText={});
    ~LastFilterJob()override;
    void cancel();
    EditorProject* owner()const;
    static LastFilterJob* find(QObject* parent);
    static LastFilterJob* find(QObject* parent,EditorProject* owner);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
}
