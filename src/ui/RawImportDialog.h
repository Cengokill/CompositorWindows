#pragma once
#include "imaging/image_types.h"
#include "imaging/raw_develop.h"
#include <QDialog>
#include <QFutureWatcher>
#include <QString>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>

class QCheckBox;
class QLabel;
class QPushButton;
class QTimer;

namespace compositor::ui {
class TrackSlider;

// Settings accepted per file. A file without an entry imports as shot.
using RawSettings = std::map<std::filesystem::path, imaging::RawAdjustments>;

// Reviews RAW development before any document change. Previews run on a worker through the
// same developRaw path used by the final import; results from superseded settings are discarded.
class RawImportDialog final : public QDialog {
public:
    using Developer = std::function<imaging::RawDevelopment(const std::filesystem::path&, const imaging::ImportOptions&, std::uint32_t previewMaxSide)>;
    explicit RawImportDialog(const std::filesystem::path& file, Developer developer = imaging::developRaw, QWidget* parent = nullptr);
    ~RawImportDialog() override;
    // Settings for the full-resolution import. Controls the decoder cannot change stay unset (as shot).
    imaging::RawAdjustments settings() const { return adjustments_; }

protected:
    void reject() override;

private:
    struct Result {
        std::uint64_t generation{};
        std::optional<imaging::RawDevelopment> developed;
        QString error;
    };
    void build();
    void edited();
    void startPreview();
    void finishPreview();
    void initializeControls(const imaging::RawDevelopment& developed);
    void showExplanation(const imaging::RawCapabilities& capabilities);
    void setStatus(const QString& text, bool failed);

    std::filesystem::path file_;
    Developer developer_;
    std::shared_ptr<std::atomic_bool> cancelled_;
    imaging::RawAdjustments adjustments_;
    bool initialized_{};
    bool running_{};
    std::uint64_t generation_{};
    QFutureWatcher<Result> watcher_;
    QTimer* editTimer_{};
    QLabel* preview_{};
    QLabel* status_{};
    QLabel* explanation_{};
    QLabel* exposureValue_{};
    QLabel* kelvinValue_{};
    QLabel* tintValue_{};
    QLabel* noiseValue_{};
    TrackSlider* exposure_{};
    TrackSlider* kelvin_{};
    TrackSlider* tint_{};
    TrackSlider* noise_{};
    QCheckBox* lensCorrection_{};
    QPushButton* importButton_{};
};

// Reviews each RAW file in order and records accepted settings. Cancelling any review
// returns false, so the caller can stop before the document changes.
bool reviewRawImports(QWidget* parent, const std::vector<std::filesystem::path>& files,
                      RawSettings& settings,
                      RawImportDialog::Developer developer = imaging::developRaw);
}
