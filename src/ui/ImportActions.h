#pragma once
#include "core/Document.h"
#include "imaging/image_types.h"
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>

namespace compositor::ui {
struct ImportState {
    std::optional<Document> document;
    std::string active;
    bool operator==(const ImportState&) const = default;
};
struct ImportBatch {
    std::vector<std::filesystem::path> files;
    std::optional<Point> point;
    // Keeps copied clipboard/promised image data alive until the queued request finishes.
    std::shared_ptr<void> lifetime;
    // Explicit File>Import has already passed its command/picker gates. Source
    // importImages waits only for project access; deferred drops retain their
    // stricter workspace guard before they can begin.
    enum class Origin { Deferred, Explicit };
    Origin origin{Origin::Deferred};
};
struct ImportResult {
    ImportState before, after;
    QStringList errors;
    std::vector<std::string> expandedGroups;
    size_t imported{};
    bool cancelled{};
    bool changed() const { return !cancelled && imported != 0; }
};
using ImportDecoder=std::function<imaging::DecodedImage(const std::filesystem::path&,const imaging::ImportOptions&)>;
imaging::DecodedImage decodeImportImage(const std::filesystem::path&,const imaging::ImportOptions&);
// Source EditorSession.swift:601-662. Files remain ordered; the first success in an
// empty project fixes canvas dimensions and ignores this entire batch's drop point.
// Cancellation is an explicit Windows operation: discard the complete batch result.
ImportResult prepareImportBatch(ImportState,const ImportBatch&,const ImportDecoder& = decodeImportImage,
                               imaging::ImportOptions = {});

class ImportQueue final : public QObject {
public:
    struct Host {
        std::function<std::optional<ImportState>(QObject*)> snapshot;
        std::function<bool(QObject*)> blocked;
        std::function<void(QObject*,bool)> busy;
        // Must atomically compare before, install after and record one history entry.
        std::function<void(QObject*,const ImportResult&)> commit;
        std::function<void(QObject*,const ImportResult&)> completed;
        // Optional request-aware guard; existing hosts keep their blocked rule.
        std::function<bool(QObject*,const ImportBatch&)> blockedRequest;
    };
    ImportQueue(Host,QObject* parent=nullptr,ImportDecoder = decodeImportImage);
    ~ImportQueue() override;
    uint64_t enqueue(QObject* target,ImportBatch);
    void cancel(QObject* target=nullptr);
    bool busy(QObject* target=nullptr) const;
    bool contains(QObject* target) const;
    bool idle() const;
    static ImportQueue* find(QObject* parent);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    void drain();
};
}
