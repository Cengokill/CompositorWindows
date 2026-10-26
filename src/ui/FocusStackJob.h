#pragma once
#include "core/Document.h"
#include "ImportActions.h"
#include <QObject>
#include <QString>
#include <filesystem>
#include <functional>
#include <memory>
#include <vector>
class QWidget;
namespace compositor::ui {
// Decodes and focus-stacks files on a worker. Cancel or destruction discards the result;
// commit runs on the UI thread only for an uncancelled success.
class FocusStackJob final:public QObject {
public:
    struct Host {
        std::function<void(std::shared_ptr<const Raster>)> commit;
        std::function<void()> settled;
        std::function<void(const QString&)> error;
    };
    FocusStackJob(std::vector<std::filesystem::path>,Host,QWidget* parent,ImportDecoder = decodeImportImage);
    ~FocusStackJob()override;
    void cancel();
    static FocusStackJob* find(QObject* parent);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
// Worker body, exposed for tests. Throws on invalid input, budget or cancellation.
std::shared_ptr<const Raster> runFocusStack(const std::vector<std::filesystem::path>&,const ImportDecoder&,
    const std::function<bool()>& cancelled,std::uint64_t maxWorkingBytes=1200000000);
}
