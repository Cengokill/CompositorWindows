#include "MainWindow.h"
#include <QDockWidget>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QPixmap>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStatusBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace compositor {
namespace {
QIcon frameIcon(const std::shared_ptr<const Raster>& raster) {
    if (!raster || raster->width <= 0 || raster->height <= 0) return {};
    const double scale = std::min(48.0 / raster->width, 48.0 / raster->height);
    const int width = std::max(1, int(raster->width * scale));
    const int height = std::max(1, int(raster->height * scale));
    QImage image(width, height, QImage::Format_RGBA8888_Premultiplied);
    for (int y = 0; y < height; ++y) {
        auto* row = image.scanLine(y);
        for (int x = 0; x < width; ++x) {
            const auto pixel = raster->pixel(std::min(raster->width - 1, x * raster->width / width),
                                             std::min(raster->height - 1, y * raster->height / height));
            row[x * 4] = pixel.r; row[x * 4 + 1] = pixel.g;
            row[x * 4 + 2] = pixel.b; row[x * 4 + 3] = pixel.a;
        }
    }
    return QIcon(QPixmap::fromImage(image));
}
bool canDuplicateFrame(const Document& document, const Layer& target) {
    if (!target.raster || !target.animation || target.animation->frames.size() >= 4096) return false;
    uint64_t pixels = 0;
    const auto add = [&pixels](const std::shared_ptr<const Raster>& raster) {
        if (raster) pixels += uint64_t(raster->width) * uint64_t(raster->height);
    };
    for (const auto& layer : document.layers) {
        add(layer.raster);
        if (layer.animation) for (size_t i = 0; i < layer.animation->frames.size(); ++i)
            if (i != layer.animation->selectedFrame) add(layer.animation->frames[i].raster);
    }
    return pixels + uint64_t(target.raster->width) * uint64_t(target.raster->height) <= 100000000;
}
}

void MainWindow::setupGifTimeline() {
    gifTimer_ = new QTimer(this);
    gifTimer_->setSingleShot(true);
    gifTimeline_ = new QDockWidget("GIF frames", this);
    gifTimeline_->setObjectName("gifTimelineDock");
    gifTimeline_->setFeatures(QDockWidget::NoDockWidgetFeatures);
    auto* contents = new QWidget(gifTimeline_);
    auto* layout = new QVBoxLayout(contents);
    layout->setContentsMargins(8, 4, 8, 6);
    auto* controls = new QHBoxLayout;
    auto button = [&](const char* text, const char* name, const char* accessible) {
        auto* result = new QToolButton(contents);
        result->setText(text); result->setObjectName(name); result->setAccessibleName(accessible);
        controls->addWidget(result); return result;
    };
    gifPlay_ = button("Play", "gifPlay", "Play or pause GIF");
    gifDuplicate_ = button("Duplicate", "gifDuplicate", "Duplicate selected GIF frame");
    gifDelete_ = button("Delete", "gifDelete", "Delete selected GIF frame");
    controls->addWidget(new QLabel("Frame delay:", contents));
    gifDelay_ = new QSpinBox(contents); gifDelay_->setObjectName("gifFrameDelay");
    gifDelay_->setAccessibleName("Selected GIF frame delay");
    gifDelay_->setRange(0, 655350); gifDelay_->setSuffix(" ms");
    gifDelay_->setKeyboardTracking(false); controls->addWidget(gifDelay_); controls->addStretch();
    layout->addLayout(controls);
    gifFrames_ = new QListWidget(contents); gifFrames_->setObjectName("gifFrameList");
    gifFrames_->setAccessibleName("GIF frames"); gifFrames_->setViewMode(QListView::IconMode);
    gifFrames_->setFlow(QListView::LeftToRight); gifFrames_->setWrapping(false);
    gifFrames_->setMovement(QListView::Static); gifFrames_->setIconSize({48, 48});
    gifFrames_->setResizeMode(QListView::Adjust); gifFrames_->setFixedHeight(92);
    layout->addWidget(gifFrames_); gifTimeline_->setWidget(contents);
    addDockWidget(Qt::BottomDockWidgetArea, gifTimeline_); gifTimeline_->hide();

    connect(gifFrames_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0 && !refreshing_) { stopGifPlayback(); selectGifFrame(size_t(row)); }
    });
    connect(gifPlay_, &QToolButton::clicked, this, [this] {
        if (gifPlaybackProject_) { stopGifPlayback(); refreshGifTimeline(); return; }
        auto* project = current(); auto* layer = active();
        if (!project || !layer || !layer->animation || layer->animation->frames.size() < 2 || !canEditLayers()) return;
        gifPlaybackProject_ = project; gifPlaybackLayer_ = layer->id;
        gifTimer_->start(std::max(20, layer->animation->frames[layer->animation->selectedFrame].delayMs));
        refreshGifTimeline();
    });
    connect(gifTimer_, &QTimer::timeout, this, [this] {
        auto* project = current(); auto* layer = active();
        if (!project || project != gifPlaybackProject_ || !layer || layer->id != gifPlaybackLayer_ ||
            !layer->animation || layer->animation->frames.size() < 2 || !canEditLayers()) {
            stopGifPlayback(); refreshGifTimeline(); return;
        }
        const auto index = (layer->animation->selectedFrame + 1) % layer->animation->frames.size();
        selectGifFrame(index);
        layer = active();
        if (gifPlaybackProject_ && layer && layer->animation)
            gifTimer_->start(std::max(20, layer->animation->frames[layer->animation->selectedFrame].delayMs));
    });
    connect(gifDuplicate_, &QToolButton::clicked, this, [this] {
        stopGifPlayback(); auto* layer = active();
        if (!canEditLayers() || !layer || !layer->animation || !current() || !current()->document) return;
        if (!canDuplicateFrame(*current()->document, *layer)) {
            statusBar()->showMessage("Duplicating this frame would exceed the project image limit.", 5000); return;
        }
        edit("Duplicate GIF Frame", [this](Document&) {
            auto* l = active(); auto& animation = *l->animation; const auto index = animation.selectedFrame;
            animation.frames[index].raster = l->raster;
            const auto copy = animation.frames[index];
            animation.frames.insert(animation.frames.begin() + index + 1, copy);
            animation.selectedFrame = index + 1; l->raster = animation.frames[animation.selectedFrame].raster;
        });
    });
    connect(gifDelete_, &QToolButton::clicked, this, [this] {
        stopGifPlayback(); auto* layer = active();
        if (!canEditLayers() || !layer || !layer->animation || layer->animation->frames.size() < 2) return;
        edit("Delete GIF Frame", [this](Document&) {
            auto* l = active(); auto& animation = *l->animation; const auto index = animation.selectedFrame;
            animation.frames.erase(animation.frames.begin() + index);
            animation.selectedFrame = std::min(index, animation.frames.size() - 1);
            l->raster = animation.frames[animation.selectedFrame].raster;
        });
    });
    connect(gifDelay_, &QSpinBox::editingFinished, this, [this] {
        auto* layer = active();
        if (!canEditLayers() || !layer || !layer->animation || layer->animation->frames.empty()) return;
        const int value = gifDelay_->value(); const auto index = layer->animation->selectedFrame;
        if (layer->animation->frames[index].delayMs == value) return;
        stopGifPlayback(); edit("GIF Frame Delay", [this, value](Document&) {
            auto& animation = *active()->animation; animation.frames[animation.selectedFrame].delayMs = value;
        });
    });
}

void MainWindow::stopGifPlayback() {
    if (gifTimer_) gifTimer_->stop();
    gifPlaybackProject_ = nullptr; gifPlaybackLayer_.clear();
    if (gifPlay_) gifPlay_->setText("Play");
}

void MainWindow::selectGifFrame(size_t index) {
    auto* layer = active();
    if (!layer || !layer->animation || !canEditLayers()) return;
    auto& animation = *layer->animation;
    if (index >= animation.frames.size() || animation.selectedFrame >= animation.frames.size()) return;
    animation.frames[animation.selectedFrame].raster = layer->raster;
    animation.selectedFrame = index; layer->raster = animation.frames[index].raster;
    refresh(true, false);
}

void MainWindow::refreshGifTimeline() {
    if (!gifTimeline_) return;
    auto* project = current(); auto* layer = active();
    const bool available = project && layer && layer->animation && !layer->animation->frames.empty() &&
        layer->animation->selectedFrame < layer->animation->frames.size() && project->selected.size() == 1;
    if (!available || project != gifPlaybackProject_ || (layer && layer->id != gifPlaybackLayer_)) {
        if (gifPlaybackProject_ && (!available || project != gifPlaybackProject_ || layer->id != gifPlaybackLayer_)) stopGifPlayback();
    }
    gifTimeline_->setVisible(available && !canvasOnly_);
    if (!available) { gifShownLayer_.clear(); gifShownFrames_.clear(); return; }
    auto& animation = *layer->animation;
    bool rebuild = gifShownLayer_ != layer->id || gifShownFrames_ != animation.frames;
    if (rebuild) {
        QSignalBlocker block(gifFrames_); gifFrames_->clear();
        for (size_t i = 0; i < animation.frames.size(); ++i) {
            auto* item = new QListWidgetItem(frameIcon(animation.frames[i].raster), QString::number(i + 1), gifFrames_);
            item->setToolTip(QString("Frame %1 · %2 ms").arg(i + 1).arg(animation.frames[i].delayMs));
        }
        gifShownLayer_ = layer->id; gifShownFrames_ = animation.frames;
    }
    { QSignalBlocker block(gifFrames_); gifFrames_->setCurrentRow(int(animation.selectedFrame)); }
    { QSignalBlocker block(gifDelay_); gifDelay_->setValue(animation.frames[animation.selectedFrame].delayMs); }
    const bool editable = canEditLayers();
    gifFrames_->setEnabled(editable); gifDelay_->setEnabled(editable);
    gifDuplicate_->setEnabled(editable && canDuplicateFrame(*project->document, *layer));
    gifDelete_->setEnabled(editable && animation.frames.size() > 1);
    gifPlay_->setEnabled(editable && animation.frames.size() > 1);
    gifPlay_->setText(gifPlaybackProject_ ? "Pause" : "Play");
}
}
