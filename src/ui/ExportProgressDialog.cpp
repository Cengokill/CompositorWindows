#include "ExportProgressDialog.h"
#include "LayerPanelTokens.h"
#include "ToolbarControls.h"
#include <QElapsedTimer>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace compositor::ui {
namespace t = layer_panel;
namespace {
constexpr int dialogWidth = 380;
constexpr int stripHeight = 44;
constexpr qint64 fillMs = 520, checkDelayMs = 340, checkMs = 260;
constexpr int denseTiles = 10;
// Past this many files the tiles would be narrower than a pixel in the strip, so a bar that fills is drawn instead.
constexpr int maxTiles = 84;
// The last tile needs this long to fill and draw its check before the window goes.
constexpr int settleMs = int(checkDelayMs + checkMs) + 120;
double easeOut(double x) { x = std::clamp(x, 0., 1.); return 1 - std::pow(1 - x, 3); }
QColor white(int alpha) { return QColor(255, 255, 255, alpha); }
}

// Paints the tiles, or the sliding bar. One 60 Hz timer runs only while something moves.
class ExportProgressStrip final : public QWidget {
public:
    ExportProgressStrip(int count, QWidget* parent) : QWidget(parent), count_(count), doneAt_(size_t(std::max(count, 0)), -1), skipped_(size_t(std::max(count, 0)), false) {
        setObjectName("exportProgressStrip");
        setFixedHeight(stripHeight);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        clock_.start();
        connect(&timer_, &QTimer::timeout, this, qOverload<>(&QWidget::update));
        timer_.setInterval(16);
    }
    void setCompleted(int completed) {
        completed = std::clamp(completed, 0, std::max(count_, 0));
        for (int i = completed_; i < completed; ++i) doneAt_[size_t(i)] = clock_.elapsed();
        completed_ = completed;
        update();
    }
    int completed() const { return completed_; }
    // A file that was handled without a PNG (nothing to export, or a failure) is not shown as a success.
    void markSkipped(int index) { if (index >= 0 && index < count_) { skipped_[size_t(index)] = true; update(); } }
protected:
    void showEvent(QShowEvent* event) override { timer_.start(); QWidget::showEvent(event); }
    void hideEvent(QHideEvent* event) override { timer_.stop(); QWidget::hideEvent(event); }
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const auto now = clock_.elapsed();
        if (count_ < 2) paintBar(painter, now);
        else if (count_ > maxTiles) paintFill(painter);
        else paintTiles(painter, now);
    }
private:
    // A glass body: faint white fill, a light edge and a gloss on the upper half.
    static void glass(QPainter& painter, const QRectF& box, double radius) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(white(26));
        painter.drawRoundedRect(box, radius, radius);
        QPainterPath clip;
        clip.addRoundedRect(box, radius, radius);
        painter.save();
        painter.setClipPath(clip);
        QLinearGradient gloss(box.topLeft(), QPointF(box.left(), box.center().y()));
        gloss.setColorAt(0, white(46));
        gloss.setColorAt(1, white(0));
        painter.fillRect(box, gloss);
        painter.restore();
        painter.setPen(QPen(white(64), 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(box.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
    }
    // Accent-blue liquid up to `level` of the box, with a wave on its surface that calms as it fills.
    void liquid(QPainter& painter, const QRectF& box, double radius, double level, double wave, qint64 now) const {
        if (level <= 0) return;
        QPainterPath clip;
        clip.addRoundedRect(box, radius, radius);
        painter.save();
        painter.setClipPath(clip);
        const double surfaceY = box.bottom() - level * box.height();
        QPainterPath body;
        body.moveTo(box.left(), box.bottom() + 1);
        const double phase = double(now) * 0.006;
        for (double x = box.left(); x <= box.right() + 2; x += 2)
            body.lineTo(x, surfaceY - wave * std::sin(phase + (x - box.left()) / std::max(box.width(), 1.) * 2 * std::numbers::pi * 1.4));
        body.lineTo(box.right() + 1, box.bottom() + 1);
        body.closeSubpath();
        const QColor accent(t::accent);
        QLinearGradient fill(box.topLeft(), box.bottomLeft());
        fill.setColorAt(0, accent.lighter(135));
        fill.setColorAt(0.55, accent);
        fill.setColorAt(1, accent.darker(135));
        painter.fillPath(body, fill);
        if (wave > 0.2) {
            painter.setPen(QPen(white(110), 1.2));
            painter.setBrush(Qt::NoBrush);
            QPainterPath meniscus;
            bool first = true;
            for (double x = box.left(); x <= box.right() + 2; x += 2) {
                const double y = surfaceY - wave * std::sin(phase + (x - box.left()) / std::max(box.width(), 1.) * 2 * std::numbers::pi * 1.4);
                if (first) { meniscus.moveTo(x, y); first = false; } else meniscus.lineTo(x, y);
            }
            painter.drawPath(meniscus);
        }
        // The specular highlight that makes the liquid read as glass rather than paint.
        QLinearGradient shine(box.topLeft(), QPointF(box.left(), box.center().y()));
        shine.setColorAt(0, white(88));
        shine.setColorAt(1, white(0));
        painter.fillRect(QRectF(box.left(), std::max(box.top(), surfaceY), box.width(), box.height() / 2), shine);
        painter.restore();
    }
    static void check(QPainter& painter, const QRectF& box, double progress) {
        if (progress <= 0) return;
        const double side = std::min(box.width(), box.height());
        const QPointF centre = box.center();
        const double scale = 0.85 + 0.15 * easeOut(progress);
        const auto at = [&](double x, double y) { return QPointF(centre.x() + x * side * scale, centre.y() + y * side * scale); };
        const QPointF a = at(-0.22, 0.02), b = at(-0.06, 0.18), c = at(0.24, -0.16);
        const double first = std::hypot(b.x() - a.x(), b.y() - a.y()), second = std::hypot(c.x() - b.x(), c.y() - b.y());
        const double drawn = easeOut(progress) * (first + second);
        QPainterPath path;
        path.moveTo(a);
        if (drawn <= first) path.lineTo(a + (b - a) * (drawn / first));
        else { path.lineTo(b); path.lineTo(b + (c - b) * ((drawn - first) / second)); }
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(Qt::white, std::max(1.6, side * 0.075), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(path);
    }
    void paintTiles(QPainter& painter, qint64 now) const {
        const bool dense = count_ > denseTiles;
        const double gap = dense ? 3. : 8.;
        const double width = dense ? std::max(1., (this->width() - gap * (count_ - 1)) / count_) : std::min<double>(stripHeight, (this->width() - gap * (count_ - 1)) / count_);
        const double height = dense ? 20. : stripHeight;
        const double top = (this->height() - height) / 2;
        const double radius = std::min(dense ? 6. : 11., width / 2);
        for (int i = 0; i < count_; ++i) {
            const QRectF box(i * (width + gap), top, width, height);
            glass(painter, box, radius);
            double level = 0, wave = 0, checkProgress = 0;
            if (doneAt_[size_t(i)] >= 0 && skipped_[size_t(i)]) {
                // Handled, but no file: a quiet glass tile with a dash, never the liquid and the check of a success.
                if (width >= 18) {
                    painter.setPen(QPen(white(150), std::max(1.6, std::min(box.width(), box.height()) * 0.075), Qt::SolidLine, Qt::RoundCap));
                    painter.drawLine(QPointF(box.center().x() - box.width() * 0.2, box.center().y()), QPointF(box.center().x() + box.width() * 0.2, box.center().y()));
                }
                continue;
            }
            if (doneAt_[size_t(i)] >= 0) {
                const auto elapsed = now - doneAt_[size_t(i)];
                const double rise = easeOut(double(elapsed) / fillMs);
                level = ambient + (1 - ambient) * rise;
                wave = waveHeight * (1 - rise);
                checkProgress = std::clamp(double(elapsed - checkDelayMs) / checkMs, 0., 1.);
            } else if (i == completed_) {
                // The tile being written: a low swell and a ring that breathes.
                level = ambient;
                wave = waveHeight;
                const double breath = 0.5 + 0.5 * std::sin(double(now) * 0.006);
                QColor ring(t::accent);
                ring.setAlphaF(0.45 + 0.55 * breath);
                liquid(painter, box, radius, level, wave, now);
                painter.setPen(QPen(ring, 1.6));
                painter.setBrush(Qt::NoBrush);
                painter.drawRoundedRect(box.adjusted(0.8, 0.8, -0.8, -0.8), radius, radius);
                continue;
            }
            liquid(painter, box, radius, level, wave, now);
            if (width >= 18 && level > 0) check(painter, box, checkProgress);
            if (level > 0) {
                painter.setPen(QPen(white(70), 1));
                painter.setBrush(Qt::NoBrush);
                painter.drawRoundedRect(box.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
            }
        }
    }
    void paintBar(QPainter& painter, qint64 now) const {
        const double height = 10;
        const QRectF track(0, (this->height() - height) / 2, this->width(), height);
        glass(painter, track, height / 2);
        const double span = track.width() * 0.35;
        // Ping-pong with ease in and out, so the capsule slows at each end like a liquid settling.
        const double sweep = 0.5 - 0.5 * std::cos(double(now) / 1300. * std::numbers::pi);
        const QRectF capsule(track.left() + sweep * (track.width() - span), track.top(), span, track.height());
        accentCapsule(painter, capsule);
    }
    // The bar of a long export: the share of files written so far.
    void paintFill(QPainter& painter) const {
        const double height = 10;
        const QRectF track(0, (this->height() - height) / 2, this->width(), height);
        glass(painter, track, height / 2);
        if (completed_ <= 0) return;
        accentCapsule(painter, QRectF(track.left(), track.top(), std::max(height, track.width() * completed_ / count_), height));
    }
    static void accentCapsule(QPainter& painter, const QRectF& capsule) {
        const double height = capsule.height();
        QPainterPath clip;
        clip.addRoundedRect(capsule, height / 2, height / 2);
        const QColor accent(t::accent);
        QLinearGradient fill(capsule.topLeft(), capsule.bottomLeft());
        fill.setColorAt(0, accent.lighter(135));
        fill.setColorAt(0.55, accent);
        fill.setColorAt(1, accent.darker(135));
        painter.fillPath(clip, fill);
        painter.save();
        painter.setClipPath(clip);
        QLinearGradient shine(capsule.topLeft(), QPointF(capsule.left(), capsule.center().y()));
        shine.setColorAt(0, white(96));
        shine.setColorAt(1, white(0));
        painter.fillRect(capsule, shine);
        painter.restore();
        painter.setPen(QPen(white(80), 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(capsule.adjusted(0.5, 0.5, -0.5, -0.5), height / 2, height / 2);
    }
    static constexpr double ambient = 0.2, waveHeight = 2.4;
    const int count_;
    int completed_{};
    std::vector<qint64> doneAt_;
    std::vector<bool> skipped_;
    QElapsedTimer clock_;
    QTimer timer_;
};

ExportProgressDialog::ExportProgressDialog(QWidget* parent, const QString& title, const QString& heading, int count)
    : QDialog(parent), count_(count) {
    setWindowTitle(title);
    setModal(true);
    setWindowModality(Qt::ApplicationModal);
    // The application chrome wraps dialogs that lack this in a title area and a content widget, which hides the
    // children of a dialog that positions them itself. This one lays out its own, with the native title bar.
    setProperty("studioChrome", true);
    setFixedWidth(dialogWidth);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(22, 20, 22, 18);
    layout->setSpacing(4);
    headingLabel_ = new QLabel(count >= 2 ? QString("Exporting layer 1 of %1").arg(count) : heading, this);
    headingLabel_->setObjectName("exportProgressHeading");
    currentLabel_ = new QLabel(this);
    currentLabel_->setObjectName("exportProgressCurrent");
    currentLabel_->setTextFormat(Qt::PlainText);
    strip_ = new ExportProgressStrip(count, this);
    countLabel_ = new QLabel(count >= 2 ? QString("0 of %1 done").arg(count) : QString("Writing the file"), this);
    countLabel_->setObjectName("exportProgressCount");
    cancel_ = new QPushButton("Cancel", this);
    cancel_->setObjectName("exportProgressCancel");
    cancel_->setAutoDefault(false);
    markPill(cancel_);
    setStyleSheet(QString(
        "QLabel#exportProgressHeading { color: %1; font-size: 15px; font-weight: 600; }"
        "QLabel#exportProgressCurrent { color: %2; font-size: 12px; }"
        "QLabel#exportProgressCount { color: %2; font-size: 12px; }").arg(t::primary, t::secondary));
    layout->addWidget(headingLabel_);
    layout->addWidget(currentLabel_);
    layout->addSpacing(12);
    layout->addWidget(strip_);
    layout->addSpacing(14);
    auto* footer = new QHBoxLayout;
    footer->setContentsMargins(0, 0, 0, 0);
    footer->addWidget(countLabel_);
    footer->addStretch();
    footer->addWidget(cancel_);
    layout->addLayout(footer);
    connect(cancel_, &QPushButton::clicked, this, [this] { requestCancel(); });
    setAccessibleName(title);
    strip_->setAccessibleName(headingLabel_->text());
}
void ExportProgressDialog::setProgress(int completed, const QString& current) {
    completed = std::clamp(completed, 0, std::max(count_, 0));
    strip_->setCompleted(completed);
    if (count_ >= 2) {
        headingLabel_->setText(QString("Exporting layer %1 of %2").arg(std::min(completed + 1, count_)).arg(count_));
        countLabel_->setText(QString("%1 of %2 done").arg(completed).arg(count_));
    }
    currentLabel_->setText(QFontMetrics(currentLabel_->font()).elidedText(current, Qt::ElideMiddle, dialogWidth - 44));
    currentLabel_->setToolTip(current);
    strip_->setAccessibleName(headingLabel_->text());
}
void ExportProgressDialog::markSkipped(int index) { strip_->markSkipped(index); }
void ExportProgressDialog::finish(int completed) {
    if (completed < 0 || count_ < 2 || cancelRequested_) { accept(); return; }
    setProgress(completed, {});
    QTimer::singleShot(settleMs, this, &QDialog::accept);
}
void ExportProgressDialog::requestCancel() {
    if (cancelRequested_) return;
    cancelRequested_ = true;
    cancel_->setEnabled(false);
    cancel_->setText("Cancelling…");
    if (onCancel_) onCancel_();
}
void ExportProgressDialog::reject() { requestCancel(); }
void ExportProgressDialog::paintEvent(QPaintEvent*) {
    // Opaque on purpose: the surface of the glass dialogs without their translucent window.
    QPainter painter(this);
    QLinearGradient surface(0, 0, 0, height());
    surface.setColorAt(0, QColor(t::control));
    surface.setColorAt(0.22, QColor(t::background));
    surface.setColorAt(1, QColor(t::canvasSurround));
    painter.fillRect(rect(), surface);
    painter.setPen(QColor(t::surfaceBorder));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
}
}
