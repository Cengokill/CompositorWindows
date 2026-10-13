#pragma once
#include "core/Document.h"
#include <QObject>
#include <QString>
#include <atomic>
#include <functional>
#include <memory>

namespace compositor {
struct EditorProject;
namespace ui {
// One background viewport composite at a time. A newer request replaces any
// queued snapshot; a bumped epoch drops results that outlive undo, cancel, or
// a new gesture. The worker owns an isolated CompositeCache.
class ViewportCompositeJob final:public QObject {
public:
    struct Request {
        EditorProject* owner{};
        Document document;
        double x{},y{},width{},height{},units{1};
        double zoom{1},panX{},panY{};
        int canvasWidth{},canvasHeight{};
    };
    struct Result {
        EditorProject* owner{};
        Document document;
        CompositeCache cache;
        CompositeViewport view;
        double zoom{1},panX{},panY{};
        int canvasWidth{},canvasHeight{};
        QString error;
        bool stale{};
        uint64_t epoch{};
    };
    using Receiver=std::function<void(Result)>;
    ViewportCompositeJob(Receiver receiver,QObject* parent);
    ~ViewportCompositeJob() override;
    void submit(Request request);
    void invalidate();
    void shutdown();
    void setProceedGate(std::shared_ptr<std::atomic_bool> gate);
    uint64_t epoch() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    void start(Request request);
};
}
}
