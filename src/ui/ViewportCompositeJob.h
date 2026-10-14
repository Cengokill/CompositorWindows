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
        uint64_t gesture{};
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
        uint64_t gesture{};
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
// Background stills for one layer drag. Direct2D upload stays on the UI thread.
// The moving layer is rendered over its full painted bounds at the viewport's
// sampling scale, rather than being drawn through a separate layer transform.
struct MovePreviewBounds { double x{},y{},width{},height{}; int pixelsWide{},pixelsHigh{}; bool valid{}; };
MovePreviewBounds movingLayerPreviewBounds(const Layer& layer,double units);
class MovePreviewJob final:public QObject {
public:
    struct Request {
        uint64_t gesture{};
        std::string layerId;
        Document back,front,movingLayer;
        bool wantFront{},masked{};
        double anchorX{},anchorY{};
        double viewX{},viewY{},viewWidth{},viewHeight{},units{1};
        int pixelsWide{},pixelsHigh{};
        double movingX{},movingY{};
        int movingPixelsWide{},movingPixelsHigh{};
    };
    struct Result {
        uint64_t gesture{},epoch{};
        std::string layerId;
        std::shared_ptr<const Raster> base,front,plate,sprite;
        bool masked{};
        double anchorX{},anchorY{};
        double viewX{},viewY{},units{1};
        double movingX{},movingY{};
        QString error;
        bool stale{};
    };
    using Receiver=std::function<void(Result)>;
    MovePreviewJob(Receiver receiver,QObject* parent);
    ~MovePreviewJob() override;
    void submit(Request request);
    void invalidate();
    void shutdown();
    bool covers(uint64_t gesture) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    void start(Request request);
};
}
}
