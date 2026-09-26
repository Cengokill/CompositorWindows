#pragma once
#include "ui/WorkspaceDropQueue.h"
#include <cstdint>
#include <functional>
#include <memory>
class QMimeData;
struct IDataObject;
namespace compositor::ui {
struct VirtualDropLimits {
    uint32_t maxItems{256};
    uint64_t maxFileBytes{400000000},maxTotalBytes{1200000000};
    uint32_t chunkBytes{65536};
    int softTimeoutMs{5000};
};
struct VirtualDropResult {
    WorkspaceDropRequest request;
    QString error;
    bool cancelled{},asynchronous{},timedOut{};
    uint64_t bytesCopied{};
    uint32_t maximumChunk{};
};
class VirtualDropJob final:public QObject {
public:
    using Completed=std::function<void(VirtualDropResult)>;
    using Progress=std::function<void(uint64_t,bool)>;
    ~VirtualDropJob() override;
    void cancel();
    bool finished()const;
    // Native capture occurs during Drop. Native pointers never cross apartments
    // without COM marshaling. The job outlives a destroyed UI owner and discards
    // its callback; normal application auto-quit waits for provider cleanup.
    static VirtualDropJob* startNative(IDataObject*,WorkspaceDropRequest,
                                       QObject* owner,Completed,Progress={},VirtualDropLimits={});
    static VirtualDropJob* start(const QMimeData&,WorkspaceDropRequest,
                                 QObject* owner,Completed,Progress={},VirtualDropLimits={});
    static bool hasVirtualFiles(const QMimeData&);
private:
    struct Impl;
    explicit VirtualDropJob(std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl_;
    void finish();
};
}
