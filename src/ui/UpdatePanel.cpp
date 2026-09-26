#include "UpdatePanel.h"
#include "update/Updater.h"
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <memory>
#include <optional>
#include <stdexcept>

namespace compositor::ui {
namespace {
struct Configuration {
    std::filesystem::path root;
    QString feed, runningVersion, information;
    bool configured{};
};
Configuration configuration(const QString& applicationDirectory) {
    Configuration result;
    if (QCoreApplication::instance()->property("manualUpdatesOnly").toBool()) {
        result.information = "Automatic updates are not configured for this community preview. Install a new MSI or extract a new portable download to update Compositor.";
        return result;
    }
    QDir version(applicationDirectory);
    result.runningVersion = version.dirName();
    QDir versions = version;
    if (!versions.cdUp() || versions.dirName().compare("versions", Qt::CaseInsensitive) != 0) {
        result.information = "Updates are not configured for this development build. This copy is not in a versioned installation.";
        return result;
    }
    try { (void)update::Version::parse(result.runningVersion); }
    catch (const std::exception&) {
        result.information = "Updates are not configured for this development build. The installation version is invalid.";
        return result;
    }
    if (!versions.cdUp()) throw std::runtime_error("Cannot locate the update installation root");
    result.root = std::filesystem::path(versions.absolutePath().toStdWString());
    const QString path = versions.filePath("update-source.json");
    if (!QFileInfo::exists(path)) {
        result.information = "Updates are not configured for this development build. No update source is configured, and production updates are not available.";
        return result;
    }
    const auto wide = path.toStdWString();
    const DWORD attributes = GetFileAttributesW(wide.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
        throw std::runtime_error("Update configuration must be an ordinary file");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > 16384)
        throw std::runtime_error("Cannot read the bounded update configuration");
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        throw std::runtime_error("Update configuration is not a JSON object");
    const auto object = document.object();
    const QStringList expected{"allowTestKey", "schema", "testFeed"};
    if (object.keys() != expected || !object["schema"].isDouble() || object["schema"].toDouble() != 1 ||
        !object["allowTestKey"].isBool() || !object["allowTestKey"].toBool() ||
        !object["testFeed"].isString() || object["testFeed"].toString().trimmed().isEmpty())
        throw std::runtime_error("Development updates require schema 1, a testFeed, and explicit allowTestKey: true");
    result.feed = object["testFeed"].toString();
    if (result.feed.size() > 8192 || result.feed.contains(QChar(u'\0')))
        throw std::runtime_error("Update source has an invalid length or contains a null character");
    result.configured = true;
    result.information = "Signed development update source. Production updates are not configured.";
    return result;
}
struct Progress {
    std::atomic<bool> cancelled{};
    std::atomic<std::uint64_t> done{}, total{};
};
struct Result {
    std::optional<update::ActiveState> active;
    std::optional<update::CheckResult> checked;
    std::optional<update::InstallResult> installed;
    QString error;
    bool cancelled{};
};
class UpdatePanel final : public QDialog {
public:
    UpdatePanel(QWidget* parent, UpdatePanelHost host, QString directory)
        : QDialog(parent), host_(std::move(host)), directory_(std::move(directory)) {
        setObjectName("updatePanel"); setWindowTitle("Check for Updates");
        setWindowModality(Qt::NonModal); setAttribute(Qt::WA_DeleteOnClose); resize(520, 260);
        auto* layout = new QVBoxLayout(this);
        information_ = new QLabel; information_->setObjectName("updateSourceInformation"); information_->setWordWrap(true); layout->addWidget(information_);
        versions_ = new QLabel; versions_->setObjectName("updateVersions"); versions_->setWordWrap(true); layout->addWidget(versions_);
        status_ = new QLabel("Preparing update check…"); status_->setObjectName("updateStatus"); status_->setAccessibleName("Update status"); status_->setWordWrap(true); layout->addWidget(status_);
        progressBar_ = new QProgressBar; progressBar_->setObjectName("updateProgress"); progressBar_->setAccessibleName("Update progress"); layout->addWidget(progressBar_); progressBar_->hide();
        retained_ = new QLabel; retained_->setObjectName("updateRetainedVersion"); retained_->setWordWrap(true); layout->addWidget(retained_);
        auto* row = new QHBoxLayout; layout->addLayout(row);
        check_ = button(row, "Check again", "updateCheck"); install_ = button(row, "Install Update", "updateInstall");
        cancel_ = button(row, "Cancel", "updateCancel"); restart_ = button(row, "Restart Compositor", "updateRestart");
        row->addStretch(); close_ = button(row, "Close", "updateClose");
        connect(check_, &QPushButton::clicked, this, [this] { begin(false); });
        connect(install_, &QPushButton::clicked, this, [this] { begin(true); });
        connect(cancel_, &QPushButton::clicked, this, [this] { requestCancel(); });
        connect(close_, &QPushButton::clicked, this, &QDialog::reject);
        connect(restart_, &QPushButton::clicked, this, [this] { restart(); });
        connect(&watcher_, &QFutureWatcher<Result>::finished, this, [this] { finished(); });
        timer_.setInterval(40); connect(&timer_, &QTimer::timeout, this, [this] { paintProgress(); });
        controls(); QTimer::singleShot(0, this, [this] { begin(false); });
    }
    ~UpdatePanel() override {
        progress_->cancelled.store(true); watcher_.disconnect(this);
        try { watcher_.waitForFinished(); } catch (...) { /* A worker result cannot escape QWidget teardown. */ }
    }
    void reject() override {
        if (busy_) { closeWhenSettled_ = true; requestCancel(); return; }
        QDialog::reject();
    }
private:
    static QPushButton* button(QHBoxLayout* row, const char* text, const char* name) {
        auto* result = new QPushButton(QString::fromUtf8(text)); result->setObjectName(name); row->addWidget(result); return result;
    }
    void controls() {
        check_->setEnabled(!busy_); install_->setEnabled(!busy_ && available_);
        cancel_->setVisible(busy_); cancel_->setEnabled(busy_ && !progress_->cancelled.load());
        restart_->setVisible(installed_); restart_->setEnabled(!busy_ && installed_ && bool(host_.restart));
        close_->setEnabled(!busy_);
    }
    void begin(bool install) {
        if (busy_ || (install && !available_)) return;
        available_ = false; installed_ = false; retained_->clear();
        try {
            configuration_ = configuration(directory_); information_->setText(configuration_.information);
            versions_->setText(configuration_.root.empty() ? QString{} : "Running version: " + configuration_.runningVersion);
            if (!configuration_.configured) { status_->setText(configuration_.information); setProperty("updateState", "unconfigured"); controls(); return; }
            progress_ = std::make_shared<Progress>(); busy_ = true; installing_ = install;
            setProperty("updateState", install ? "installing" : "checking");
            status_->setText(install ? "Installing and verifying the signed update…" : "Checking for a signed update…");
            progressBar_->setRange(0, 0); progressBar_->show(); controls(); timer_.start();
            const auto config = configuration_; const auto progress = progress_;
            watcher_.setFuture(QtConcurrent::run([config, progress, install] {
                Result result;
                try {
                    update::Updater updater(config.root); result.active = updater.state();
                    update::Options options; options.allowTestKey = true;
                    options.cancelled = [progress] { return progress->cancelled.load(); };
                    options.progress = [progress](auto done, auto total) { progress->total.store(total); progress->done.store(done); };
                    if (install) result.installed = updater.install(config.feed, options);
                    else result.checked = updater.check(config.feed, options);
                    result.active = updater.state();
                } catch (const std::exception& error) {
                    result.error = QString::fromUtf8(error.what()); result.cancelled = progress->cancelled.load();
                    try { result.active = update::Updater(config.root).state(); } catch (...) {}
                }
                return result;
            }));
        } catch (const std::exception& error) {
            busy_ = false; timer_.stop(); progressBar_->hide(); status_->setText(QString::fromUtf8(error.what()));
            setProperty("updateState", "error"); controls();
        }
    }
    void requestCancel() {
        if (!busy_) return;
        progress_->cancelled.store(true); status_->setText("Cancelling the update operation…"); controls();
    }
    void paintProgress() {
        const auto total = progress_->total.load(), done = progress_->done.load();
        if (!total) return;
        progressBar_->setRange(0, 1000); progressBar_->setValue(int(std::min<long double>(1000, static_cast<long double>(done) * 1000 / total)));
        if (installing_ && done >= total && !progress_->cancelled.load()) status_->setText("Verifying files and testing the installed version…");
    }
    void finished() {
        timer_.stop(); paintProgress(); busy_ = false; progressBar_->hide();
        try {
            const auto result = watcher_.result();
            versions_->setText(QString("Running version: %1\nActive installed version: %2").arg(configuration_.runningVersion,
                result.active ? result.active->current.text() : "unavailable"));
            retained_->setText(result.active && !result.active->previous.isEmpty() ? "Retained previous version: " + result.active->previous : QString{});
            installed_ = result.active && !result.active->pending && result.active->current.text() != configuration_.runningVersion;
            if (!result.error.isEmpty()) {
                status_->setText(result.cancelled ? (result.active
                    ? "Update cancelled. Active installed version: " + result.active->current.text() + "."
                    : "Update cancelled. The active installed version could not be read. " + result.error) : result.error);
                setProperty("updateState", result.cancelled ? "cancelled" : "error"); available_ = false;
            } else if (result.installed) {
                status_->setText(installed_ ? "Update installed. Restart Compositor when you are ready." : "This version is already installed.");
                setProperty("updateState", installed_ ? "installed" : "current"); available_ = false;
            } else if (result.checked) {
                available_ = result.checked->available;
                status_->setText(available_ ? "Update " + result.checked->manifest.version.text() + " is available."
                                           : installed_ ? "The installed update is ready. Restart Compositor when you are ready." : "No newer update is available.");
                setProperty("updateState", available_ ? "available" : installed_ ? "installed" : "current");
            }
        } catch (const std::exception& error) { status_->setText(QString::fromUtf8(error.what())); setProperty("updateState", "error"); }
        controls(); if (closeWhenSettled_) QDialog::reject();
    }
    void restart() {
        if (busy_ || !installed_ || !host_.restart) return;
        QPointer<UpdatePanel> alive(this);
        try {
            const bool accepted = host_.restart(configuration_.root);
            if (!alive) return;
            if (accepted) QDialog::accept();
            else status_->setText("Restart cancelled. The installed update remains ready for the next launch.");
        } catch (const std::exception& error) { if (alive) status_->setText(QString::fromUtf8(error.what())); }
    }
    UpdatePanelHost host_;
    QString directory_;
    Configuration configuration_;
    std::shared_ptr<Progress> progress_{std::make_shared<Progress>()};
    QFutureWatcher<Result> watcher_;
    QTimer timer_;
    QLabel *information_{}, *versions_{}, *status_{}, *retained_{};
    QProgressBar* progressBar_{};
    QPushButton *check_{}, *install_{}, *cancel_{}, *restart_{}, *close_{};
    bool busy_{}, installing_{}, available_{}, installed_{}, closeWhenSettled_{};
};
}
QDialog* openUpdatePanel(QWidget* parent, UpdatePanelHost host, QString applicationDirectory) {
    if (parent) for (auto* dialog : parent->findChildren<QDialog*>("updatePanel"))
        if (auto* existing = dynamic_cast<UpdatePanel*>(dialog)) { existing->show(); existing->raise(); existing->activateWindow(); return existing; }
    if (applicationDirectory.isEmpty()) applicationDirectory = QCoreApplication::applicationDirPath();
    auto* panel = new UpdatePanel(parent, std::move(host), std::move(applicationDirectory)); panel->show(); return panel;
}
}
