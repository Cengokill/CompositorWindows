#include "RawImportDialog.h"
#include "PropertyControls.h"
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <cmath>

namespace compositor::ui {
namespace {
constexpr std::uint32_t kPreviewSide = 1200;
constexpr int kEditDelayMs = 120;
constexpr int kExposureRange = int(imaging::kRawExposureLimitEv * 100); // slider steps of 0.01 EV, symmetric about 0
constexpr int kTintMin = -int(imaging::kRawTintLimit), kTintMax = int(imaging::kRawTintLimit);
constexpr int kNoiseMin = 0, kNoiseMax = int(imaging::kRawNoiseLimit);
}

RawImportDialog::RawImportDialog(const std::filesystem::path& file, Developer developer, QWidget* parent)
    : QDialog(parent), file_(file), developer_(std::move(developer)), cancelled_(std::make_shared<std::atomic_bool>(false)) {
    setWindowTitle("RAW Development – " + QString::fromStdWString(file_.filename().wstring()));
    setObjectName("rawImportDialog");
    setModal(true);
    resize(1280, 820);
    build();
    connect(&watcher_, &QFutureWatcher<Result>::finished, this, [this] { finishPreview(); });
    startPreview();
}

RawImportDialog::~RawImportDialog() {
    cancelled_->store(true);
}

void RawImportDialog::reject() {
    cancelled_->store(true);
    QDialog::reject();
}

void RawImportDialog::build() {
    auto* root = new QHBoxLayout(this);
    preview_ = new QLabel("Rendering preview…");
    preview_->setObjectName("rawPreview");
    preview_->setAccessibleName("RAW preview");
    preview_->setAlignment(Qt::AlignCenter);
    preview_->setFrameShape(QFrame::StyledPanel);
    preview_->setMinimumSize(640, 420);
    root->addWidget(preview_, 1);

    auto* panel = new QVBoxLayout;
    root->addLayout(panel);
    auto* form = new QFormLayout;
    panel->addLayout(form);

    auto row = [&](const char* name, const char* label, TrackSlider*& slider, QLabel*& value) {
        slider = new TrackSlider(Qt::Horizontal);
        slider->setObjectName(name);
        slider->setAccessibleName(label);
        slider->setEnabled(false);
        slider->setMinimumWidth(200);
        value = new QLabel;
        value->setMinimumWidth(80);
        auto* line = new QHBoxLayout;
        line->addWidget(slider, 1);
        line->addWidget(value);
        form->addRow(label, line);
    };
    row("rawExposureSlider", "Exposure", exposure_, exposureValue_);
    row("rawKelvinSlider", "Temperature", kelvin_, kelvinValue_);
    row("rawTintSlider", "Tint", tint_, tintValue_);
    row("rawNoiseSlider", "Noise reduction", noise_, noiseValue_);
    exposure_->setRange(-kExposureRange, kExposureRange);
    tint_->setRange(kTintMin, kTintMax);
    noise_->setRange(kNoiseMin, kNoiseMax);

    lensCorrection_ = new QCheckBox("Lens correction");
    lensCorrection_->setObjectName("rawLensCorrection");
    lensCorrection_->setEnabled(false);
    lensCorrection_->setToolTip("Unavailable: the LibRaw decoder used here applies no lens profile.");
    form->addRow(lensCorrection_);

    explanation_ = new QLabel;
    explanation_->setObjectName("rawExplanation");
    explanation_->setWordWrap(true);
    panel->addWidget(explanation_);
    status_ = new QLabel;
    status_->setObjectName("rawStatus");
    status_->setWordWrap(true);
    panel->addWidget(status_);
    panel->addStretch(1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    importButton_ = new QPushButton("Import");
    importButton_->setObjectName("rawImportButton");
    importButton_->setEnabled(false);
    importButton_->setDefault(true);
    buttons->addButton(importButton_, QDialogButtonBox::AcceptRole);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    panel->addWidget(buttons);

    editTimer_ = new QTimer(this);
    editTimer_->setSingleShot(true);
    editTimer_->setInterval(kEditDelayMs);
    connect(editTimer_, &QTimer::timeout, this, [this] { startPreview(); });

    connect(exposure_, &QSlider::valueChanged, this, [this](int v) {
        adjustments_.exposure = v / 100.0;
        exposureValue_->setText(QString::number(v / 100.0, 'f', 2) + " EV");
        edited();
    });
    connect(kelvin_, &QSlider::valueChanged, this, [this](int v) {
        adjustments_.kelvin = std::uint32_t(v);
        kelvinValue_->setText(QString::number(v) + " K");
        edited();
    });
    connect(tint_, &QSlider::valueChanged, this, [this](int v) {
        adjustments_.tint = double(v);
        tintValue_->setText(QString::number(v));
        edited();
    });
    connect(noise_, &QSlider::valueChanged, this, [this](int v) {
        adjustments_.noiseReduction = double(v);
        noiseValue_->setText(QString::number(v));
        edited();
    });
}

// Any edit supersedes in-flight work: finishPreview discards results from older generations.
void RawImportDialog::edited() {
    ++generation_;
    editTimer_->start();
}

void RawImportDialog::startPreview() {
    if (running_) return;
    running_ = true;
    imaging::ImportOptions options;
    options.raw = adjustments_;
    options.cancelled = [cancelled = cancelled_] { return cancelled->load(); };
    setStatus("Rendering preview…", false);
    watcher_.setFuture(QtConcurrent::run([developer = developer_, path = file_, options, generation = generation_]() -> Result {
        Result result;
        result.generation = generation;
        try {
            result.developed = developer(path, options, kPreviewSide);
        } catch (const std::exception& error) {
            result.error = QString::fromUtf8(error.what());
        }
        return result;
    }));
}

void RawImportDialog::finishPreview() {
    running_ = false;
    if (cancelled_->load()) return;
    auto result = watcher_.result();
    // Obsolete results are dropped and replaced by a preview of the current settings.
    if (result.generation != generation_) {
        startPreview();
        return;
    }
    if (!result.developed) {
        importButton_->setEnabled(false);
        setStatus(result.error, true);
        return;
    }
    auto& developed = *result.developed;
    const auto& image = developed.image.image;
    QImage picture(image.pixels.data(), int(image.width), int(image.height), int(image.stride), QImage::Format_RGBA8888_Premultiplied);
    preview_->setPixmap(QPixmap::fromImage(picture.copy()).scaled(preview_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    if (!initialized_) initializeControls(developed);
    importButton_->setEnabled(true);
    setStatus(QString(), false);
}

void RawImportDialog::initializeControls(const imaging::RawDevelopment& developed) {
    initialized_ = true;
    const auto& caps = developed.capabilities;
    const auto& asShot = developed.asShot;
    // The as-shot value is the initial value and the double-click reset target.
    auto setup = [](TrackSlider* slider, bool enabled, int value) {
        QSignalBlocker blocked(slider);
        slider->setEnabled(enabled);
        slider->setValue(value);
        slider->setDefaultValue(value);
    };
    // Exposure and noise reduction start at zero: the decoders report no as-shot value for them.
    setup(exposure_, true, 0);
    {
        QSignalBlocker blocked(kelvin_);
        kelvin_->setRange(int(caps.minKelvin), int(caps.maxKelvin));
    }
    setup(kelvin_, caps.temperature, int(asShot.kelvin));
    setup(tint_, caps.tint, int(std::lround(asShot.tint)));
    setup(noise_, true, 0);
    // Unset adjustments keep the as-shot values; only a slider edit records a setting.
    exposureValue_->setText("0.00 EV");
    kelvinValue_->setText(QString::number(asShot.kelvin) + " K");
    tintValue_->setText(QString::number(std::lround(asShot.tint)));
    noiseValue_->setText("0");
    showExplanation(caps);
}

void RawImportDialog::showExplanation(const imaging::RawCapabilities& caps) {
    QStringList reasons;
    if (!caps.temperature) reasons << "Temperature is read-only for this decoder.";
    if (!caps.tint) reasons << "Tint is read-only for this decoder.";
    reasons << "Lens correction is unavailable: the LibRaw decoder used here applies no lens profile.";
    explanation_->setText(reasons.join("\n"));
}

void RawImportDialog::setStatus(const QString& text, bool failed) {
    status_->setText(text);
    status_->setStyleSheet(failed ? "color: #c42b1c;" : QString());
}

bool reviewRawImports(QWidget* parent, const std::vector<std::filesystem::path>& files,
                      std::map<std::filesystem::path, imaging::RawAdjustments>& settings, RawImportDialog::Developer developer) {
    for (const auto& file : files) {
        if (!imaging::isRawFileExtension(file)) continue;
        RawImportDialog dialog(file, developer, parent);
        if (dialog.exec() != QDialog::Accepted) return false;
        settings[file] = dialog.settings();
    }
    return true;
}
}
