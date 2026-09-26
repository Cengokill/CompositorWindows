#pragma once
#include "layers/LayerOperations.h"
#include <QObject>
#include <QString>
#include <functional>
#include <memory>
class QWidget;
namespace compositor::ui {
class LayerMergeJob final:public QObject {
public:
    struct Host {
        std::function<void(layers::EditResult)> commit;
        std::function<void()> settled;
        std::function<void(const QString&)> error;
    };
    LayerMergeJob(Document,layers::SelectionState,Host,QWidget* parent);
    ~LayerMergeJob()override;
    void cancel();
    static LayerMergeJob* find(QObject* parent);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
