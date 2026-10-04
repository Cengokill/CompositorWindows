#include "CameraRawPanel.h"
#include "PropertyControls.h"
#include <QApplication>
#include <QAbstractButton>
#include <QButtonGroup>
#include <QComboBox>
#include <QCheckBox>
#include <QContextMenuEvent>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace compositor::ui {
namespace {
class ResetLabel final : public QLabel {
public:
    std::function<void()> reset;
    using QLabel::QLabel;
protected:
    void mouseDoubleClickEvent(QMouseEvent* event) override {
        if (reset) reset();
        QLabel::mouseDoubleClickEvent(event);
    }
};
class ScopeView final : public QWidget {
public:
    filters::CameraRawScope scope{};
    bool ready{};
    bool vectorscope{};
    explicit ScopeView(QWidget* parent = nullptr) : QWidget(parent) {}
protected:
    void contextMenuEvent(QContextMenuEvent* event) override {
        QMenu menu;
        auto* histogram = menu.addAction("Histogram");
        auto* vectors = menu.addAction("Vectorscope");
        if (auto* chosen = menu.exec(event->globalPos())) {
            vectorscope = chosen == vectors;
            Q_UNUSED(histogram);
            update();
        }
    }
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(), QColor("#1c1d20"));
        painter.setRenderHint(QPainter::Antialiasing);
        if (!ready) return;
        if (vectorscope) {
            const int side = 64;
            double peak = 0;
            for (double value : scope.vectorscope) peak = std::max(peak, value);
            QImage image(side, side, QImage::Format_ARGB32);
            image.fill(QColor("#1c1d20"));
            if (peak > 0) {
                for (int y = 0; y < side; ++y) for (int x = 0; x < side; ++x) {
                    const double value = scope.vectorscope[size_t(y * side + x)];
                    if (value <= 0) continue;
                    const double dx = x - 31.5, dy = y - 31.5;
                    double hue = std::atan2(dy, dx) * 180 / std::numbers::pi;
                    if (hue < 0) hue += 360;
                    auto color = QColor::fromHsv(int(hue) % 360, 210, 230);
                    color.setAlpha(int(std::clamp(40 + value / peak * 215, 0., 255.)));
                    image.setPixelColor(x, y, color);
                }
            }
            const int fitted = std::min(width(), height()) - 8;
            painter.drawImage(QRect((width() - fitted) / 2, (height() - fitted) / 2, fitted, fitted), image);
            return;
        }
        const double peak = std::max(scope.peak(), 1e-6);
        const QColor colors[]{QColor(214, 72, 72, 170), QColor(72, 176, 96, 170), QColor(86, 140, 214, 170)};
        const std::array<double, 256>* channels[]{&scope.red, &scope.green, &scope.blue};
        for (int channel = 0; channel < 3; ++channel) {
            QPainterPath path;
            for (int i = 0; i < 256; ++i) {
                const double x = i / 255. * (width() - 1);
                const double y = height() - 4 - (*channels[channel])[size_t(i)] / peak * (height() - 8);
                if (i == 0) path.moveTo(x, y);
                else path.lineTo(x, y);
            }
            painter.setPen(QPen(colors[channel], 1.2));
            painter.drawPath(path);
        }
    }
};
class CurveGraph final : public QWidget {
public:
    filters::CameraRawSettings* settings{};
    bool* parametric{};
    int* channel{};
    std::function<void()> onEdit;
    explicit CurveGraph(QWidget* parent = nullptr) : QWidget(parent) { setMinimumHeight(150); setMouseTracking(true); }
protected:
    std::vector<Point>& points() { return channelPoints(*settings, *channel); }
    const std::vector<Point>& points() const { return channelPoints(*settings, *channel); }
    static std::vector<Point>& channelPoints(filters::CameraRawSettings& settings, int channel) {
        switch (channel) {
        case 1: return settings.curve.red;
        case 2: return settings.curve.green;
        case 3: return settings.curve.blue;
        default: return settings.curve.rgb;
        }
    }
    QPointF toView(Point point) const { return {point.x * (width() - 1), (1 - point.y) * (height() - 1)}; }
    Point toCurve(QPointF point) const {
        return {std::clamp(point.x() / std::max(1, width() - 1), 0., 1.), std::clamp(1 - point.y() / std::max(1, height() - 1), 0., 1.)};
    }
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(), QColor("#1c1d20"));
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor("#3a3c42"), 1));
        painter.drawLine(0, height() - 1, width(), 0);
        if (*parametric) {
            const auto table = settings->curve.toneTable();
            QPainterPath path;
            for (int i = 0; i < 256; ++i) {
                const QPointF at(i / 255. * (width() - 1), (1 - table[size_t(i)]) * (height() - 1));
                if (i == 0) path.moveTo(at);
                else path.lineTo(at);
            }
            painter.setPen(QPen(QColor("#e4e5e7"), 1.4));
            painter.drawPath(path);
            const double splits[]{settings->curve.shadowSplit, settings->curve.darkSplit, settings->curve.lightSplit};
            painter.setPen(QPen(QColor("#8d9098"), 1));
            for (double split : splits) painter.drawLine(QPointF(split / 100 * (width() - 1), 0), QPointF(split / 100 * (width() - 1), height()));
            return;
        }
        const auto table = settings->curve.channelTable(points());
        QPainterPath path;
        for (int i = 0; i < 256; ++i) {
            const QPointF at(i / 255. * (width() - 1), (1 - table[size_t(i)]) * (height() - 1));
            if (i == 0) path.moveTo(at);
            else path.lineTo(at);
        }
        painter.setPen(QPen(QColor("#e4e5e7"), 1.4));
        painter.drawPath(path);
        painter.setBrush(QColor("#f2f2f2"));
        painter.setPen(Qt::NoPen);
        for (const auto& point : points()) painter.drawEllipse(toView(point), 3.5, 3.5);
    }
    int dividerAt(QPointF at) const {
        const double splits[]{settings->curve.shadowSplit, settings->curve.darkSplit, settings->curve.lightSplit};
        for (int i = 0; i < 3; ++i) if (std::abs(at.x() - splits[i] / 100 * (width() - 1)) <= 8) return i;
        return -1;
    }
    int pointAt(QPointF at) const {
        const auto& curve = points();
        for (int i = 0; i < int(curve.size()); ++i) if (QLineF(toView(curve[size_t(i)]), at).length() <= 8) return i;
        return -1;
    }
    void mousePressEvent(QMouseEvent* event) override {
        if (*parametric) { drag_ = dividerAt(event->position()); return; }
        drag_ = pointAt(event->position());
        if (drag_ >= 0) return;
        auto placed = toCurve(event->position());
        points().insert(points().end() - 1, placed);
        settings->curve = settings->curve.normalized();
        drag_ = pointAt(event->position());
        if (onEdit) onEdit();
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if (drag_ < 0 || !(event->buttons() & Qt::LeftButton)) return;
        if (*parametric) {
            const double split = std::clamp(event->position().x() / std::max(1, width() - 1) * 100, 0., 100.);
            if (drag_ == 0) settings->curve.shadowSplit = split;
            else if (drag_ == 1) settings->curve.darkSplit = split;
            else settings->curve.lightSplit = split;
            settings->curve = settings->curve.normalized();
        } else if (drag_ < int(points().size())) {
            auto placed = toCurve(event->position());
            if (drag_ == 0) placed.x = 0;
            else if (drag_ + 1 == int(points().size())) placed.x = 1;
            points()[size_t(drag_)] = placed;
        }
        if (onEdit) onEdit();
    }
    void mouseReleaseEvent(QMouseEvent*) override {
        if (drag_ >= 0 && !*parametric) settings->curve = settings->curve.normalized();
        drag_ = -1;
        if (onEdit) onEdit();
    }
    void mouseDoubleClickEvent(QMouseEvent* event) override {
        if (*parametric) return;
        const int index = pointAt(event->position());
        if (index <= 0 || index + 1 >= int(points().size())) return;
        points().erase(points().begin() + index);
        settings->curve = settings->curve.normalized();
        drag_ = -1;
        if (onEdit) onEdit();
    }
    int drag_{-1};
};
void paintGlassDisc(QPainter& painter, const QRectF& bounds, bool lens) {
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, lens ? 70 : 80));
    painter.drawEllipse(bounds.translated(0, 1.2));
    painter.setBrush(lens ? QColor(255, 255, 255, 30) : QColor(12, 14, 18, 170));
    painter.drawEllipse(bounds);
    QLinearGradient fill(bounds.topLeft(), bounds.bottomLeft());
    if (lens) {
        fill.setColorAt(0, QColor(255, 255, 255, 248));
        fill.setColorAt(0.48, QColor(246, 247, 250, 242));
        fill.setColorAt(1, QColor(220, 224, 232, 236));
    } else {
        fill.setColorAt(0, QColor(255, 255, 255, 96));
        fill.setColorAt(0.42, QColor(255, 255, 255, 28));
        fill.setColorAt(1, QColor(255, 255, 255, 10));
    }
    painter.setBrush(fill);
    painter.setPen(QPen(QColor(255, 255, 255, lens ? 220 : 150), 1));
    painter.drawEllipse(bounds.adjusted(0.5, 0.5, -0.5, -0.5));
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 255, 255, lens ? 160 : 110));
    painter.drawEllipse(bounds.adjusted(bounds.width() * 0.26, bounds.height() * 0.12, -bounds.width() * 0.26, -bounds.height() * 0.58));
    painter.restore();
}
class GlassSlider final : public QSlider {
public:
    explicit GlassSlider(Qt::Orientation orientation, QWidget* parent = nullptr) : QSlider(orientation, parent) {
        setFixedHeight(28);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
    }
    void setTrackColors(QColor left, QColor right) { left_ = left; right_ = right; }
protected:
    static constexpr int knob = 20;
    QRectF groove() const {
        constexpr double thickness = 7;
        return {knob / 2.0, (height() - thickness) / 2.0, std::max(0.0, width() - double(knob)), thickness};
    }
    double ratio() const {
        return maximum() == minimum() ? 0 : double(value() - minimum()) / double(maximum() - minimum());
    }
    QPointF knobCenter() const {
        const auto track = groove();
        return {track.left() + ratio() * track.width(), height() / 2.0};
    }
    int valueFrom(double x) const {
        const auto track = groove();
        if (track.width() <= 0 || maximum() == minimum()) return value();
        const double placed = std::clamp((x - track.left()) / track.width(), 0., 1.);
        return int(std::lround(minimum() + placed * (maximum() - minimum())));
    }
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const auto track = groove();
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(255, 255, 255, 28));
        painter.drawRoundedRect(track, track.height() / 2, track.height() / 2);
        const QRectF active(track.left(), track.top(), std::max(track.height(), knobCenter().x() - track.left()), track.height());
        QLinearGradient fill(active.topLeft(), active.topRight());
        if (left_.isValid() && right_.isValid()) {
            fill.setColorAt(0, left_);
            fill.setColorAt(1, right_);
        } else {
            fill.setColorAt(0, QColor(255, 255, 255, 70));
            fill.setColorAt(1, QColor(255, 255, 255, 150));
        }
        painter.setBrush(fill);
        painter.drawRoundedRect(active, track.height() / 2, track.height() / 2);
        painter.setPen(QPen(QColor(255, 255, 255, 90), 1));
        painter.drawLine(QPointF(track.left() + 4, track.top() + 1), QPointF(track.right() - 4, track.top() + 1));
        const QPointF center = knobCenter();
        paintGlassDisc(painter, QRectF(center.x() - knob / 2.0, center.y() - knob / 2.0, knob, knob), true);
    }
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton || !isEnabled()) return;
        setSliderDown(true);
        setValue(valueFrom(event->position().x()));
        event->accept();
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if (!(event->buttons() & Qt::LeftButton) || !isSliderDown()) return;
        setValue(valueFrom(event->position().x()));
        event->accept();
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (isSliderDown()) setSliderDown(false);
        event->accept();
    }
private:
    QColor left_, right_;
};
class GlassChevron final : public QToolButton {
public:
    explicit GlassChevron(QWidget* parent = nullptr) : QToolButton(parent) {
        setFixedSize(26, 26);
        setCheckable(true);
        setAutoRaise(true);
        setCursor(Qt::PointingHandCursor);
        setStyleSheet("QToolButton { background: transparent; border: none; padding: 0; min-height: 0; min-width: 0; }");
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        paintGlassDisc(painter, QRectF(rect()).adjusted(2, 2, -2, -2), false);
        painter.setPen(QPen(QColor(255, 255, 255, 230), 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        const QPointF center = rect().center();
        QPainterPath path;
        if (isChecked()) {
            path.moveTo(center.x() - 4.2, center.y() - 1.6);
            path.lineTo(center.x(), center.y() + 2.4);
            path.lineTo(center.x() + 4.2, center.y() - 1.6);
        } else {
            path.moveTo(center.x() - 1.6, center.y() - 4.2);
            path.lineTo(center.x() + 2.4, center.y());
            path.lineTo(center.x() - 1.6, center.y() + 4.2);
        }
        painter.drawPath(path);
    }
};
class GlassEye final : public QToolButton {
public:
    explicit GlassEye(QWidget* parent = nullptr) : QToolButton(parent) {
        setFixedSize(26, 26);
        setCheckable(true);
        setAutoRaise(true);
        setCursor(Qt::PointingHandCursor);
        setStyleSheet("QToolButton { background: transparent; border: none; padding: 0; min-height: 0; min-width: 0; }");
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        paintGlassDisc(painter, QRectF(rect()).adjusted(2, 2, -2, -2), false);
        painter.setPen(QPen(QColor(255, 255, 255, isChecked() ? 235 : 120), 1.3));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(QRectF(7.5, 10.2, 11, 6.4));
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(255, 255, 255, isChecked() ? 235 : 100));
        painter.drawEllipse(QPointF(13, 13.4), 1.8, 1.8);
    }
};
class GradeWheel final : public QWidget {
public:
    filters::CameraRawGradeWheel* wheel{};
    std::function<void()> onEdit;
    explicit GradeWheel(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedSize(104, 104);
        setMouseTracking(true);
        setCursor(Qt::PointingHandCursor);
    }
protected:
    QPointF center() const { return {width() / 2.0, height() / 2.0}; }
    double outerRadius() const { return width() / 2.0 - 3; }
    double travel() const { return outerRadius() - 8; }
    QPointF knobAt() const {
        const double angle = wheel->hue * std::numbers::pi / 180;
        const double radius = wheel->saturation / 100 * travel();
        return {center().x() + std::cos(angle) * radius, center().y() - std::sin(angle) * radius};
    }
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QPointF origin = center();
        const double outer = outerRadius();
        QConicalGradient gradient(origin, 0);
        for (int stop = 0; stop <= 12; ++stop) {
            const int hue = (360 - stop * 30) % 360;
            gradient.setColorAt(stop / 12., QColor::fromHsv(hue, 190, 230));
        }
        painter.setPen(QPen(QColor(255, 255, 255, 150), 1.2));
        painter.setBrush(gradient);
        painter.drawEllipse(origin, outer, outer);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(18, 19, 22, 235));
        painter.drawEllipse(origin, outer * 0.46, outer * 0.46);
        painter.setBrush(QColor(255, 255, 255, 36));
        painter.drawEllipse(QPointF(origin.x(), origin.y() - outer * 0.55), outer * 0.34, outer * 0.12);
        const QPointF knob = knobAt();
        paintGlassDisc(painter, QRectF(knob.x() - 7, knob.y() - 7, 14, 14), true);
    }
    void place(QPointF at) {
        const QPointF origin = center();
        const double dx = at.x() - origin.x(), dy = at.y() - origin.y();
        double hue = std::atan2(-dy, dx) * 180 / std::numbers::pi;
        if (hue < 0) hue += 360;
        wheel->hue = hue;
        wheel->saturation = std::clamp(std::hypot(dx, dy) / travel() * 100, 0., 100.);
        update();
        if (onEdit) onEdit();
    }
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton) return;
        grabMouse();
        place(event->position());
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if (event->buttons() & Qt::LeftButton) place(event->position());
    }
    void mouseReleaseEvent(QMouseEvent*) override { if (mouseGrabber() == this) releaseMouse(); }
    void mouseDoubleClickEvent(QMouseEvent*) override {
        wheel->hue = 0;
        wheel->saturation = 0;
        update();
        if (onEdit) onEdit();
    }
};
void hsvOf(const Pixel& pixel, double& hue, double& saturation, double& value) {
    const double r = pixel.r / 255., g = pixel.g / 255., b = pixel.b / 255.;
    const double max = std::max(r, std::max(g, b)), min = std::min(r, std::min(g, b)), chroma = max - min;
    value = max;
    saturation = max <= 0 ? 0 : chroma / max;
    if (chroma <= 1e-8) { hue = 0; return; }
    double turns = max == r ? (g - b) / chroma : max == g ? 2 + (b - r) / chroma : 4 + (r - g) / chroma;
    hue = turns * 60;
    if (hue < 0) hue += 360;
}
void applyTrackColors(GlassSlider* slider, const QString& stops) {
    QColor colors[2];
    int found = 0;
    for (int index = 0; index < stops.size() && found < 2; ++index) {
        if (stops[index] != QLatin1Char('#') || index + 6 >= stops.size()) continue;
        const QColor color(stops.mid(index, 7));
        if (!color.isValid()) continue;
        colors[found++] = color;
        index += 6;
    }
    if (found == 2) slider->setTrackColors(colors[0], colors[1]);
}
QString hueStops(double center) {
    auto color = [](double hue) { return QColor::fromHsv(int(std::fmod(hue + 360, 360.)), 180, 210).name(); };
    return QString("stop:0 %1, stop:1 %2").arg(color(center - 40), color(center + 40));
}
}
CameraRawPanel::CameraRawPanel(filters::CameraRawSettings settings, QWidget* parent) : QWidget(parent), settings_(std::move(settings)) {
    setObjectName("cameraRawPanel");
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(
        "QWidget#cameraRawPanel { background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 rgba(255,255,255,0.08), stop:0.55 rgba(255,255,255,0.03), stop:1 rgba(255,255,255,0.07)); }"
        "QWidget#cameraRawPanel QLabel { background: transparent; color: rgba(244,246,248,0.96); }"
        "QWidget#cameraRawPanel QComboBox, QWidget#cameraRawPanel QPushButton, QWidget#cameraRawPanel QToolButton { background: rgba(255,255,255,0.14); color: white; border: 1px solid rgba(255,255,255,0.38); border-top-color: rgba(255,255,255,0.68); border-radius: 12px; padding: 4px 10px; min-height: 18px; }"
        "QWidget#cameraRawPanel QPushButton:hover, QWidget#cameraRawPanel QToolButton:hover, QWidget#cameraRawPanel QComboBox:hover { background: rgba(255,255,255,0.22); }"
        "QWidget#cameraRawPanel QToolButton:checked, QWidget#cameraRawPanel QPushButton:checked { background: rgba(255,255,255,0.32); border-color: rgba(255,255,255,0.72); }"
        "QWidget#cameraRawPanel QComboBox::drop-down { border: 0; width: 22px; }"
        "QWidget#cameraRawPanel QCheckBox { background: transparent; spacing: 8px; }"
        "QWidget#cameraRawPanel QScrollArea, QWidget#cameraRawPanel QScrollArea > QWidget > QWidget { background: transparent; border: 0; }");
    build();
    qApp->installEventFilter(this);
}
CameraRawPanel::~CameraRawPanel() { if (qApp) qApp->removeEventFilter(this); }
filters::CameraRawSettings CameraRawPanel::rendered() const { return settings_.applying(eyes_).normalized(); }
filters::CameraRawView CameraRawPanel::previewView() const {
    filters::CameraRawView view;
    if (altDown_ && draggingAlt_ == 1) view.clipping = 1;
    if (altDown_ && draggingAlt_ == 2) view.clipping = 2;
    if (altDown_ && draggingAlt_ == 3) view.sharpenMask = true;
    view.shadowOverlay = shadowOverlay_;
    view.highlightOverlay = highlightOverlay_;
    if (eyes_.mixer && pointIndex_ >= 0 && pointIndex_ < int(settings_.mixer.points.size()) && settings_.mixer.points[size_t(pointIndex_)].visualize)
        view.visualizePointColor = pointIndex_;
    return view;
}
bool CameraRawPanel::whiteBalanceIsAuto() const { return settings_.whiteBalance == filters::CameraRawWhiteBalance::Auto; }
void CameraRawPanel::setScope(const filters::CameraRawScope& scope) { static_cast<ScopeView*>(scopeView_)->scope = scope; static_cast<ScopeView*>(scopeView_)->ready = true; scopeView_->update(); }
void CameraRawPanel::setReadout(std::optional<std::array<int, 3>> rgb) {
    readout_->setText(rgb ? QString("R %1  G %2  B %3").arg((*rgb)[0]).arg((*rgb)[1]).arg((*rgb)[2]) : QString("R —  G —  B —"));
}
void CameraRawPanel::selectAuto() {
    writing_ = true;
    settings_.whiteBalance = filters::CameraRawWhiteBalance::Auto;
    if (whiteBalance_) whiteBalance_->setCurrentIndex(1);
    writing_ = false;
}
void CameraRawPanel::applyAuto(std::optional<std::pair<double, double>> balance) {
    if (!whiteBalanceIsAuto()) return;
    settings_.temperature = balance ? balance->first : 0;
    settings_.tint = balance ? balance->second : 0;
    settings_ = settings_.normalized();
    publish();
}
void CameraRawPanel::sampleOriginal(const Pixel& pixel) {
    if (tool_ == Tool::WhiteBalance) {
        if (!pixel.a) return;
        auto balance = filters::CameraRawSettings::neutralizeStraight(pixel.r / 255., pixel.g / 255., pixel.b / 255.);
        if (!balance) return;
        settings_.temperature = balance->first;
        settings_.tint = balance->second;
        settings_.whiteBalance = filters::CameraRawWhiteBalance::Custom;
        settings_ = settings_.normalized();
        publish();
        return;
    }
    if (tool_ != Tool::Defringe || !pixel.a) return;
    double hue, saturation, value;
    hsvOf(pixel, hue, saturation, value);
    auto& optics = settings_.optics;
    if (std::abs(hue - 290) <= std::abs(hue - 90)) {
        optics.purpleHueLow = hue - 25;
        optics.purpleHueHigh = hue + 25;
        if (optics.purpleAmount == 0) optics.purpleAmount = 50;
    } else {
        optics.greenHueLow = hue - 25;
        optics.greenHueHigh = hue + 25;
        if (optics.greenAmount == 0) optics.greenAmount = 50;
    }
    settings_.optics = settings_.optics.normalized();
    publish();
}
void CameraRawPanel::sampleGraded(const Pixel& pixel) {
    if (tool_ != Tool::PointColor || !pixel.a || settings_.mixer.points.size() >= 8 && (pointIndex_ < 0 || pointIndex_ >= int(settings_.mixer.points.size()))) return;
    double hue, saturation, value;
    hsvOf(pixel, hue, saturation, value);
    if (pointIndex_ < 0 || pointIndex_ >= int(settings_.mixer.points.size())) {
        if (settings_.mixer.points.size() >= 8) return;
        filters::CameraRawPointColor added;
        added.hue = hue;
        added.saturation = saturation;
        added.luminance = value;
        settings_.mixer.points.push_back(added);
        pointIndex_ = int(settings_.mixer.points.size()) - 1;
    } else {
        auto& point = settings_.mixer.points[size_t(pointIndex_)];
        point.hue = hue;
        point.saturation = saturation;
        point.luminance = value;
    }
    settings_.mixer = settings_.mixer.normalized();
    publish();
}
void CameraRawPanel::beginTarget(const Pixel& graded, double viewY) {
    if (!graded.a) return;
    targetStart_ = settings_;
    targetY_ = viewY;
    hsvOf(graded, targetHue_, targetTone_, targetTone_);
    const double r = graded.r / 255., g = graded.g / 255., b = graded.b / 255.;
    targetTone_ = 0.2126 * r + 0.7152 * g + 0.0722 * b;
    targeting_ = true;
}
void CameraRawPanel::dragTarget(double viewY) {
    if (!targeting_) return;
    settings_ = targetStart_;
    const double delta = (targetY_ - viewY) * 0.35;
    if (tool_ == Tool::Curve && curveParametric_) {
        double* field = &settings_.curve.shadows;
        switch (settings_.curve.region(targetTone_)) {
        case 1: field = &settings_.curve.darks; break;
        case 2: field = &settings_.curve.lights; break;
        case 3: field = &settings_.curve.highlights; break;
        default: break;
        }
        *field = std::clamp(*field + delta, -100., 100.);
    } else if (tool_ == Tool::Curve) {
        const auto channel = filters::CameraRawPointChannel(std::clamp(curveChannel_, 0, 3));
        settings_.curve = settings_.curve.nudged(channel, targetTone_, delta / 100);
    } else if (tool_ == Tool::Mixer) {
        const auto weights = filters::CameraRawMixerSettings::weights(targetHue_);
        auto& values = mixerTab_ == 1 ? settings_.mixer.saturation : mixerTab_ == 2 ? settings_.mixer.luminance : settings_.mixer.hue;
        for (size_t i = 0; i < values.size(); ++i) values[i] = std::clamp(values[i] + delta * weights[i], -100., 100.);
    }
    publish();
}
void CameraRawPanel::beginGuide(double x, double y) { draftGuide_ = {x, y, x, y}; guiding_ = true; }
void CameraRawPanel::dragGuide(double x, double y) { if (guiding_) { draftGuide_.endX = x; draftGuide_.endY = y; } }
void CameraRawPanel::endGuide() {
    if (!guiding_) return;
    guiding_ = false;
    if (std::hypot(draftGuide_.endX - draftGuide_.startX, draftGuide_.endY - draftGuide_.startY) <= 0.01) return;
    if (settings_.geometry.guides.size() >= 2) return;
    settings_.geometry.guides.push_back(draftGuide_);
    settings_.geometry.upright = filters::CameraRawUpright::Guided;
    publish();
}
void CameraRawPanel::releaseTool() { arm(Tool::None); }
void CameraRawPanel::publish() { sync(); if (edited) edited(); }
void CameraRawPanel::sync() {
    writing_ = true;
    for (const auto& step : sync_) step();
    if (scopeView_) scopeView_->update();
    writing_ = false;
}
void CameraRawPanel::arm(Tool next) {
    tool_ = tool_ == next ? Tool::None : next;
    targeting_ = false;
    guiding_ = false;
    for (size_t i = 0; i < tools_.size(); ++i) if (tools_[i]) tools_[i]->setChecked(int(i + 1) == int(tool_));
}
bool CameraRawPanel::eventFilter(QObject* watched, QEvent* event) {
    Q_UNUSED(watched);
    if ((event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease)) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Alt && !key->isAutoRepeat()) {
            altDown_ = event->type() == QEvent::KeyPress;
            if (draggingAlt_ && edited) edited();
        }
    }
    return QWidget::eventFilter(watched, event);
}
void CameraRawPanel::build() {
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(8);
    auto* scopeRow = new QHBoxLayout;
    auto* scope = new ScopeView;
    scope->setObjectName("cameraRawHistogram");
    scope->setFixedHeight(110);
    scope->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    scopeView_ = scope;
    scopeRow->addWidget(scope, 1);
    auto* clips = new QVBoxLayout;
    auto* highlights = new QToolButton;
    auto* shadows = new QToolButton;
    highlights->setObjectName("clipHighlights");
    shadows->setObjectName("clipShadows");
    highlights->setCheckable(true);
    shadows->setCheckable(true);
    highlights->setText("▲");
    shadows->setText("▼");
    highlights->setAccessibleName("Highlight clipping");
    shadows->setAccessibleName("Shadow clipping");
    clips->addWidget(highlights);
    clips->addStretch(1);
    clips->addWidget(shadows);
    scopeRow->addLayout(clips);
    column->addLayout(scopeRow);
    readout_ = new QLabel("R —  G —  B —");
    readout_->setObjectName("cameraRawReadout");
    column->addWidget(readout_);
    connect(highlights, &QToolButton::toggled, this, [this](bool on) { highlightOverlay_ = on; if (!writing_ && edited) edited(); });
    connect(shadows, &QToolButton::toggled, this, [this](bool on) { shadowOverlay_ = on; if (!writing_ && edited) edited(); });
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* body = new QWidget;
    auto* sections = new QVBoxLayout(body);
    sections->setContentsMargins(0, 0, 0, 0);
    sections->setSpacing(6);
    scroll->setWidget(body);
    scroll->viewport()->setAutoFillBackground(false);
    body->setAutoFillBackground(false);
    column->addWidget(scroll, 1);

    auto addSlider = [this](QVBoxLayout* layout, const QString& name, double* field, double low, double high, double fallback, int decimals, int alt, const QString& gradient, bool balance, bool stacked = false) {
        auto* row = new QWidget;
        auto* line = stacked ? static_cast<QBoxLayout*>(new QVBoxLayout(row)) : static_cast<QBoxLayout*>(new QHBoxLayout(row));
        line->setContentsMargins(0, stacked ? 2 : 1, 0, stacked ? 2 : 1);
        line->setSpacing(stacked ? 4 : 8);
        auto* label = new ResetLabel(stacked ? name.section(QLatin1Char(' '), -1) : name);
        label->setToolTip(name);
        if (stacked) label->setAlignment(Qt::AlignCenter);
        auto* slider = new GlassSlider(Qt::Horizontal);
        auto* spin = new PropertyNumber;
        spin->setRange(low, high);
        spin->setDecimals(decimals);
        spin->setSingleStep(std::pow(10, -std::min(decimals, 1)));
        spin->setValue(*field);
        spin->setAccessibleName(name);
        spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
        spin->setAlignment(Qt::AlignCenter);
        spin->setFixedWidth(72);
        spin->setStyleSheet("QDoubleSpinBox { background: rgba(255,255,255,0.16); color: white; border: 1px solid rgba(255,255,255,0.42); border-top-color: rgba(255,255,255,0.72); border-radius: 11px; padding: 0 6px; min-height: 22px; }"
                             "QDoubleSpinBox QLineEdit { background: transparent; border: 0; padding: 0; margin: 0; color: white; }");
        slider->setRange(0, 10000);
        slider->setAccessibleName(name + " slider");
        applyTrackColors(slider, gradient);
        const auto position = [low, high](double value) { return int(std::lround((value - low) / (high - low) * 10000)); };
        slider->setValue(position(*field));
        line->addWidget(label);
        line->addWidget(slider, stacked ? 0 : 1);
        line->addWidget(spin, 0, stacked ? Qt::AlignHCenter : Qt::Alignment());
        layout->addWidget(row);
        label->reset = [spin, fallback] { spin->setValue(fallback); };
        connect(slider, &QSlider::valueChanged, spin, [spin, low, high, decimals](int value) {
            const double next = low + (high - low) * value / 10000.;
            const double factor = std::pow(10, decimals);
            spin->setValue(std::round(next * factor) / factor);
        });
        connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), slider, [slider, position](double value) { QSignalBlocker block(slider); slider->setValue(position(value)); });
        connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this, field, balance](double value) {
            if (writing_) return;
            *field = value;
            if (balance) settings_.whiteBalance = filters::CameraRawWhiteBalance::Custom;
            publish();
        });
        connect(slider, &QSlider::sliderPressed, this, [this, alt] { draggingAlt_ = alt; altDown_ = QApplication::keyboardModifiers().testFlag(Qt::AltModifier); if (alt && edited) edited(); });
        connect(slider, &QSlider::sliderReleased, this, [this] { draggingAlt_ = 0; if (edited) edited(); });
        sync_.push_back([spin, slider, field, position] { QSignalBlocker a(*spin), b(slider); spin->setValue(*field); slider->setValue(position(*field)); });
        return spin;
    };
    auto addSection = [this](QVBoxLayout* parent, const QString& title, const QString& id, bool expanded, bool* shown, const std::function<bool()>& adjusts) {
        auto* header = new QWidget;
        auto* row = new QHBoxLayout(header);
        row->setContentsMargins(0, 0, 0, 0);
        auto* disclosure = new GlassChevron;
        disclosure->setObjectName("section." + title);
        disclosure->setChecked(expanded);
        disclosure->setAccessibleName(title);
        auto* label = new QLabel(title);
        auto* eye = new GlassEye;
        eye->setObjectName("eye." + id);
        eye->setAccessibleName("Show " + title);
        eye->setChecked(*shown);
        row->addWidget(disclosure);
        row->addWidget(label, 1);
        row->addWidget(eye);
        auto* content = new QWidget;
        auto* layout = new QVBoxLayout(content);
        layout->setContentsMargins(8, 0, 0, 4);
        content->setVisible(expanded);
        parent->addWidget(header);
        parent->addWidget(content);
        connect(disclosure, &QToolButton::toggled, content, [disclosure, content](bool on) { content->setVisible(on); disclosure->update(); });
        connect(eye, &QToolButton::toggled, this, [this, shown](bool on) { *shown = on; if (!writing_ && edited) edited(); });
        sync_.push_back([eye, shown, adjusts] { eye->setVisible(adjusts()); QSignalBlocker block(eye); eye->setChecked(*shown); });
        return layout;
    };
    auto toolButton = [this](const QString& name, Tool tool) {
        auto* button = new QToolButton;
        button->setText(name);
        button->setCheckable(true);
        button->setAccessibleName(name);
        button->setAutoRaise(true);
        connect(button, &QToolButton::clicked, this, [this, tool] { arm(tool); });
        tools_[size_t(tool) - 1] = button;
        return button;
    };

    auto* light = addSection(sections, "Light", "light", true, &eyes_.light, [this] { return settings_.adjustsLight(); });
    addSlider(light, "Exposure", &settings_.exposure, -5, 5, 0, 2, 1, {}, false);
    addSlider(light, "Contrast", &settings_.contrast, -100, 100, 0, 0, 0, {}, false);
    addSlider(light, "Highlights", &settings_.highlights, -100, 100, 0, 0, 1, {}, false);
    addSlider(light, "Shadows", &settings_.shadows, -100, 100, 0, 0, 2, {}, false);
    addSlider(light, "Whites", &settings_.whites, -100, 100, 0, 0, 1, {}, false);
    addSlider(light, "Blacks", &settings_.blacks, -100, 100, 0, 0, 2, {}, false);

    auto* color = addSection(sections, "Color", "color", true, &eyes_.color, [this] { return settings_.adjustsColor(); });
    whiteBalance_ = new QComboBox;
    whiteBalance_->setObjectName("cameraRawWhiteBalance");
    whiteBalance_->addItems({"Custom", "Auto"});
    whiteBalance_->setAccessibleName("White Balance");
    color->addWidget(whiteBalance_);
    color->addWidget(toolButton("White Balance", Tool::WhiteBalance));
    connect(whiteBalance_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (writing_) return;
        settings_.whiteBalance = index == 1 ? filters::CameraRawWhiteBalance::Auto : filters::CameraRawWhiteBalance::Custom;
        if (index == 1) { if (autoRequested) autoRequested(); }
        else if (edited) edited();
    });
    sync_.push_back([this] { if (whiteBalance_) { QSignalBlocker block(whiteBalance_); whiteBalance_->setCurrentIndex(whiteBalanceIsAuto() ? 1 : 0); } });
    addSlider(color, "Temperature", &settings_.temperature, -100, 100, 0, 0, 0, "stop:0 #3a6ea5, stop:1 #d4a017", true);
    addSlider(color, "Tint", &settings_.tint, -100, 100, 0, 0, 0, "stop:0 #3c9a55, stop:1 #c45b8a", true);
    addSlider(color, "Vibrance", &settings_.vibrance, -100, 100, 0, 0, 0, {}, false);
    addSlider(color, "Saturation", &settings_.saturation, -100, 100, 0, 0, 0, "stop:0 #8a8a8a, stop:1 #d64545", false);

    auto* curve = addSection(sections, "Curve", "curve", false, &eyes_.curve, [this] { return settings_.curve.adjusts(); });
    auto* curvePages = new QWidget;
    auto* curvePageLayout = new QHBoxLayout(curvePages);
    curvePageLayout->setContentsMargins(0, 0, 0, 0);
    auto* parametric = new QPushButton("Parametric");
    auto* point = new QPushButton("Point");
    parametric->setCheckable(true);
    point->setCheckable(true);
    parametric->setChecked(true);
    auto* curveGroup = new QButtonGroup(curvePages);
    curveGroup->addButton(parametric, 0);
    curveGroup->addButton(point, 1);
    curvePageLayout->addWidget(parametric);
    curvePageLayout->addWidget(point);
    curvePageLayout->addWidget(toolButton("Curve Target", Tool::Curve));
    curve->addWidget(curvePages);
    auto* channel = new QComboBox;
    channel->setObjectName("cameraRawCurveChannel");
    channel->addItems({"RGB", "Red", "Green", "Blue"});
    channel->setAccessibleName("Curve Channel");
    curve->addWidget(channel);
    auto* graph = new CurveGraph;
    graph->setObjectName("cameraRawCurve");
    graph->settings = &settings_;
    graph->parametric = &curveParametric_;
    graph->channel = &curveChannel_;
    graph->onEdit = [this] { publish(); };
    curve->addWidget(graph);
    addSlider(curve, "Highlights", &settings_.curve.highlights, -100, 100, 0, 0, 0, {}, false);
    addSlider(curve, "Lights", &settings_.curve.lights, -100, 100, 0, 0, 0, {}, false);
    addSlider(curve, "Darks", &settings_.curve.darks, -100, 100, 0, 0, 0, {}, false);
    addSlider(curve, "Shadows", &settings_.curve.shadows, -100, 100, 0, 0, 0, {}, false);
    auto* refine = addSlider(curve, "Refine Saturation", &settings_.curve.refineSaturation, -100, 100, 0, 0, 0, {}, false);
    auto* presets = new QComboBox;
    presets->setObjectName("cameraRawCurvePreset");
    presets->addItems({"Curve Preset", "Linear", "Medium Contrast", "Strong Contrast"});
    presets->setAccessibleName("Curve Preset");
    curve->addWidget(presets);
    connect(curveGroup, &QButtonGroup::idClicked, this, [this, channel, graph](int id) { curveParametric_ = id == 0; channel->setVisible(id == 1); graph->update(); });
    channel->setVisible(false);
    connect(channel, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, graph](int id) { curveChannel_ = id; graph->update(); });
    connect(presets, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, presets](int id) {
        if (writing_ || id <= 0) return;
        auto& points = curveChannel_ == 1 ? settings_.curve.red : curveChannel_ == 2 ? settings_.curve.green : curveChannel_ == 3 ? settings_.curve.blue : settings_.curve.rgb;
        points = id == 2 ? filters::CameraRawCurveSettings::mediumContrast() : id == 3 ? filters::CameraRawCurveSettings::strongContrast() : filters::CameraRawCurveSettings::linear();
        { QSignalBlocker block(presets); presets->setCurrentIndex(0); }
        publish();
    });
    sync_.push_back([graph, refine, this] { graph->update(); refine->setEnabled(curveChannel_ == 0 || curveParametric_); });

    auto* mixer = addSection(sections, "Mixer", "mixer", false, &eyes_.mixer, [this] { return settings_.mixer.adjusts(); });
    auto* mixerPages = new QHBoxLayout;
    auto* hsl = new QPushButton("HSL");
    auto* colors = new QPushButton("Color");
    auto* points = new QPushButton("Point");
    hsl->setCheckable(true);
    colors->setCheckable(true);
    points->setCheckable(true);
    hsl->setChecked(true);
    auto* mixerGroup = new QButtonGroup(this);
    mixerGroup->addButton(hsl, 0);
    mixerGroup->addButton(colors, 1);
    mixerGroup->addButton(points, 2);
    mixerPages->addWidget(hsl);
    mixerPages->addWidget(colors);
    mixerPages->addWidget(points);
    mixerPages->addWidget(toolButton("Mixer Target", Tool::Mixer));
    mixer->addLayout(mixerPages);
    auto* mixerTabs = new QHBoxLayout;
    auto* hueTab = new QPushButton("Hue");
    auto* satTab = new QPushButton("Saturation");
    auto* lumTab = new QPushButton("Luminance");
    for (auto* tab : {hueTab, satTab, lumTab}) tab->setCheckable(true);
    hueTab->setChecked(true);
    auto* tabGroup = new QButtonGroup(this);
    tabGroup->addButton(hueTab, 0);
    tabGroup->addButton(satTab, 1);
    tabGroup->addButton(lumTab, 2);
    mixerTabs->addWidget(hueTab);
    mixerTabs->addWidget(satTab);
    mixerTabs->addWidget(lumTab);
    auto* tabHost = new QWidget;
    tabHost->setLayout(mixerTabs);
    mixer->addWidget(tabHost);
    std::array<QWidget*, 8> hueRows{}, satRows{}, lumRows{};
    for (int i = 0; i < 8; ++i) {
        const QString family = filters::CameraRawMixerSettings::names[size_t(i)];
        hueRows[size_t(i)] = addSlider(mixer, family, &settings_.mixer.hue[size_t(i)], -100, 100, 0, 0, 0, hueStops(filters::CameraRawMixerSettings::centers[size_t(i)]), false)->parentWidget();
        satRows[size_t(i)] = addSlider(mixer, family + " Saturation", &settings_.mixer.saturation[size_t(i)], -100, 100, 0, 0, 0, "stop:0 #8a8a8a, stop:1 " + QColor::fromHsv(int(filters::CameraRawMixerSettings::centers[size_t(i)]), 180, 210).name(), false)->parentWidget();
        lumRows[size_t(i)] = addSlider(mixer, family + " Luminance", &settings_.mixer.luminance[size_t(i)], -100, 100, 0, 0, 0, {}, false)->parentWidget();
        satRows[size_t(i)]->hide();
        lumRows[size_t(i)]->hide();
    }
    auto* colorHost = new QWidget;
    auto* colorLayout = new QVBoxLayout(colorHost);
    colorLayout->setContentsMargins(0, 0, 0, 0);
    auto* swatches = new QHBoxLayout;
    auto* familyGroup = new QButtonGroup(this);
    for (int i = 0; i < 8; ++i) {
        auto* swatch = new QToolButton;
        swatch->setCheckable(true);
        swatch->setChecked(i == 0);
        swatch->setFixedSize(28, 28);
        swatch->setAccessibleName(filters::CameraRawMixerSettings::names[size_t(i)]);
        const auto swatchColor = QColor::fromHsv(int(filters::CameraRawMixerSettings::centers[size_t(i)]), 180, 210);
        swatch->setStyleSheet(QString("background:%1; border-radius:14px;").arg(swatchColor.name()));
        familyGroup->addButton(swatch, i);
        swatches->addWidget(swatch);
    }
    colorLayout->addLayout(swatches);
    auto* familyHue = addSlider(colorLayout, "Hue", &settings_.mixer.hue[0], -100, 100, 0, 0, 0, {}, false);
    auto* familySat = addSlider(colorLayout, "Saturation", &settings_.mixer.saturation[0], -100, 100, 0, 0, 0, {}, false);
    auto* familyLum = addSlider(colorLayout, "Luminance", &settings_.mixer.luminance[0], -100, 100, 0, 0, 0, {}, false);
    mixer->addWidget(colorHost);
    colorHost->hide();
    auto* pointHost = new QWidget;
    auto* pointLayout = new QVBoxLayout(pointHost);
    pointLayout->setContentsMargins(0, 0, 0, 0);
    pointLayout->addWidget(toolButton("Point Color", Tool::PointColor));
    auto* pointList = new QComboBox;
    pointList->setObjectName("cameraRawPoints");
    pointList->setAccessibleName("Point Color");
    pointLayout->addWidget(pointList);
    auto* pointHue = addSlider(pointLayout, "Hue Shift", &pointScratch_, -100, 100, 0, 0, 0, {}, false);
    auto* pointSat = addSlider(pointLayout, "Saturation Shift", &pointScratch_, -100, 100, 0, 0, 0, {}, false);
    auto* pointLum = addSlider(pointLayout, "Luminance Shift", &pointScratch_, -100, 100, 0, 0, 0, {}, false);
    auto* visualize = new QCheckBox("Visualize");
    visualize->setObjectName("cameraRawVisualize");
    visualize->setAccessibleName("Visualize");
    pointLayout->addWidget(visualize);
    mixer->addWidget(pointHost);
    pointHost->hide();
    connect(tabGroup, &QButtonGroup::idClicked, this, [this, hueRows, satRows, lumRows](int id) {
        mixerTab_ = id;
        for (int i = 0; i < 8; ++i) {
            hueRows[size_t(i)]->setVisible(id == 0);
            satRows[size_t(i)]->setVisible(id == 1);
            lumRows[size_t(i)]->setVisible(id == 2);
        }
    });
    connect(mixerGroup, &QButtonGroup::idClicked, this, [this, tabHost, colorHost, pointHost, hueRows, satRows, lumRows](int id) {
        tabHost->setVisible(id == 0);
        colorHost->setVisible(id == 1);
        pointHost->setVisible(id == 2);
        for (int i = 0; i < 8; ++i) {
            hueRows[size_t(i)]->setVisible(id == 0 && mixerTab_ == 0);
            satRows[size_t(i)]->setVisible(id == 0 && mixerTab_ == 1);
            lumRows[size_t(i)]->setVisible(id == 0 && mixerTab_ == 2);
        }
    });
    connect(familyGroup, &QButtonGroup::idClicked, this, [this, familyHue, familySat, familyLum](int id) {
        mixerFamily_ = id;
        QSignalBlocker a(familyHue), b(familySat), c(familyLum);
        familyHue->setValue(settings_.mixer.hue[size_t(mixerFamily_)]);
        familySat->setValue(settings_.mixer.saturation[size_t(mixerFamily_)]);
        familyLum->setValue(settings_.mixer.luminance[size_t(mixerFamily_)]);
    });
    auto retarget = [this](QDoubleSpinBox* spin, double* base) {
        QObject::disconnect(spin, nullptr, nullptr, nullptr);
        connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this, base](double value) {
            if (writing_) return;
            base[mixerFamily_] = value;
            publish();
        });
    };
    retarget(familyHue, settings_.mixer.hue.data());
    retarget(familySat, settings_.mixer.saturation.data());
    retarget(familyLum, settings_.mixer.luminance.data());
    auto bindPoint = [this](QDoubleSpinBox* spin, double filters::CameraRawPointColor::* member) {
        QObject::disconnect(spin, nullptr, nullptr, nullptr);
        connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this, member](double value) {
            if (writing_ || pointIndex_ < 0 || pointIndex_ >= int(settings_.mixer.points.size())) return;
            settings_.mixer.points[size_t(pointIndex_)].*member = value;
            publish();
        });
    };
    bindPoint(pointHue, &filters::CameraRawPointColor::hueShift);
    bindPoint(pointSat, &filters::CameraRawPointColor::saturationShift);
    bindPoint(pointLum, &filters::CameraRawPointColor::luminanceShift);
    connect(pointList, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) { if (writing_ || index < 0) return; pointIndex_ = index; publish(); });
    connect(visualize, &QCheckBox::toggled, this, [this](bool on) {
        if (writing_ || pointIndex_ < 0 || pointIndex_ >= int(settings_.mixer.points.size())) return;
        settings_.mixer.points[size_t(pointIndex_)].visualize = on;
        if (edited) edited();
    });
    sync_.push_back([this, familyHue, familySat, familyLum, pointList, pointHue, pointSat, pointLum, visualize] {
        QSignalBlocker a(familyHue), b(familySat), c(familyLum);
        familyHue->setValue(settings_.mixer.hue[size_t(mixerFamily_)]);
        familySat->setValue(settings_.mixer.saturation[size_t(mixerFamily_)]);
        familyLum->setValue(settings_.mixer.luminance[size_t(mixerFamily_)]);
        QSignalBlocker block(pointList);
        pointList->clear();
        for (int i = 0; i < int(settings_.mixer.points.size()); ++i) pointList->addItem(QString("Point %1").arg(i + 1));
        const bool any = !settings_.mixer.points.empty();
        pointHue->setEnabled(any);
        pointSat->setEnabled(any);
        pointLum->setEnabled(any);
        visualize->setEnabled(any);
        if (any) {
            pointIndex_ = std::clamp(pointIndex_, 0, int(settings_.mixer.points.size()) - 1);
            pointList->setCurrentIndex(pointIndex_);
            const auto& point = settings_.mixer.points[size_t(pointIndex_)];
            pointHue->setValue(point.hueShift);
            pointSat->setValue(point.saturationShift);
            pointLum->setValue(point.luminanceShift);
            QSignalBlocker blockVisualize(visualize);
            visualize->setChecked(point.visualize);
        }
    });

    auto* grading = addSection(sections, "Color Grading", "grading", true, &eyes_.grading, [this] { return settings_.grading.adjusts(); });
    auto* gradePage = new QComboBox;
    gradePage->setObjectName("cameraRawGradePage");
    gradePage->addItems({"Three-Way", "Shadows", "Midtones", "Highlights", "Global"});
    gradePage->setAccessibleName("Color Grading");
    grading->addWidget(gradePage);
    filters::CameraRawGradeWheel* wheels[]{&settings_.grading.shadows, &settings_.grading.midtones, &settings_.grading.highlights, &settings_.grading.global};
    const char* wheelNames[]{"Shadows", "Midtones", "Highlights", "Global"};
    std::array<QWidget*, 4> columns{};
    auto* wheelRow = new QHBoxLayout;
    for (int i = 0; i < 4; ++i) {
        auto* wheelColumn = new QWidget;
        auto* stack = new QVBoxLayout(wheelColumn);
        stack->setContentsMargins(0, 0, 0, 0);
        auto* title = new QLabel(wheelNames[i]);
        title->setAlignment(Qt::AlignCenter);
        title->setMinimumWidth(title->fontMetrics().horizontalAdvance(title->text()) + 4);
        auto* wheel = new GradeWheel;
        wheel->setObjectName(QString("cameraRawGrade") + wheelNames[i]);
        wheel->wheel = wheels[i];
        wheel->onEdit = [this, wheel] { wheel->update(); publish(); };
        stack->addWidget(title);
        stack->addWidget(wheel, 0, Qt::AlignHCenter);
        addSlider(stack, QString(wheelNames[i]) + " Luminance", &wheels[i]->luminance, -100, 100, 0, 0, 0, {}, false, true);
        columns[size_t(i)] = wheelColumn;
        wheelRow->addWidget(wheelColumn, 1);
    }
    grading->addLayout(wheelRow);
    addSlider(grading, "Blending", &settings_.grading.blending, 0, 100, 50, 0, 0, {}, false);
    addSlider(grading, "Balance", &settings_.grading.balance, -100, 100, 0, 0, 0, {}, false);
    connect(gradePage, qOverload<int>(&QComboBox::currentIndexChanged), this, [columns](int id) {
        for (int i = 0; i < 4; ++i) columns[size_t(i)]->setVisible(id == 0 ? i < 3 : i == id - 1);
    });
    columns[3]->hide();

    auto* effects = addSection(sections, "Effects", "effects", false, &eyes_.effects, [this] { return settings_.adjustsEffects(); });
    addSlider(effects, "Texture", &settings_.texture, -100, 100, 0, 0, 0, {}, false);
    addSlider(effects, "Clarity", &settings_.clarity, -100, 100, 0, 0, 0, {}, false);
    addSlider(effects, "Dehaze", &settings_.dehaze, -100, 100, 0, 0, 0, {}, false);
    auto* glowStyle = new QComboBox;
    glowStyle->setObjectName("cameraRawGlowStyle");
    glowStyle->addItems({"Diffusion", "Bloom", "Halation"});
    glowStyle->setAccessibleName("Glow Style");
    effects->addWidget(glowStyle);
    addSlider(effects, "Glow", &settings_.glow, 0, 100, 0, 0, 0, {}, false);
    addSlider(effects, "Glow Range", &settings_.glowRange, -100, 100, 0, 0, 0, {}, false);
    addSlider(effects, "Glow Spread", &settings_.glowSpread, -100, 100, 0, 0, 0, {}, false);
    addSlider(effects, "Glow Warmth", &settings_.glowWarmth, -100, 100, 0, 0, 0, {}, false);
    auto* vignetteStyle = new QComboBox;
    vignetteStyle->setObjectName("cameraRawVignetteStyle");
    vignetteStyle->addItems({"Highlight Priority", "Color Priority", "Paint Overlay"});
    vignetteStyle->setAccessibleName("Vignette Style");
    effects->addWidget(vignetteStyle);
    addSlider(effects, "Vignette", &settings_.vignetteAmount, -100, 100, 0, 0, 0, {}, false);
    addSlider(effects, "Vignette Midpoint", &settings_.vignetteMidpoint, 0, 100, 50, 0, 0, {}, false);
    addSlider(effects, "Vignette Roundness", &settings_.vignetteRoundness, -100, 100, 0, 0, 0, {}, false);
    addSlider(effects, "Vignette Feather", &settings_.vignetteFeather, 0, 100, 50, 0, 0, {}, false);
    addSlider(effects, "Vignette Highlights", &settings_.vignetteHighlights, 0, 100, 0, 0, 0, {}, false);
    addSlider(effects, "Grain", &settings_.grainAmount, 0, 100, 0, 0, 0, {}, false);
    addSlider(effects, "Grain Size", &settings_.grainSize, 0, 100, 25, 0, 0, {}, false);
    addSlider(effects, "Grain Roughness", &settings_.grainRoughness, 0, 100, 50, 0, 0, {}, false);
    connect(glowStyle, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int id) {
        if (writing_) return;
        settings_.glowStyle = filters::CameraRawGlowStyle(id);
        if (edited) edited();
    });
    connect(vignetteStyle, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int id) {
        if (writing_) return;
        settings_.vignetteStyle = filters::CameraRawVignetteStyle(id);
        if (edited) edited();
    });
    sync_.push_back([this, glowStyle, vignetteStyle] {
        QSignalBlocker a(glowStyle), b(vignetteStyle);
        glowStyle->setCurrentIndex(int(settings_.glowStyle));
        vignetteStyle->setCurrentIndex(int(settings_.vignetteStyle));
    });

    auto* detail = addSection(sections, "Detail", "detail", false, &eyes_.detail, [this] { return settings_.detail.adjusts(); });
    addSlider(detail, "Sharpen", &settings_.detail.sharpenAmount, 0, 150, 0, 0, 0, {}, false);
    addSlider(detail, "Sharpen Radius", &settings_.detail.sharpenRadius, 0, 100, 10, 0, 0, {}, false);
    addSlider(detail, "Sharpen Detail", &settings_.detail.sharpenDetail, 0, 100, 25, 0, 0, {}, false);
    addSlider(detail, "Masking", &settings_.detail.sharpenMasking, 0, 100, 0, 0, 3, {}, false);
    addSlider(detail, "Luminance Noise", &settings_.detail.noiseLuminance, 0, 100, 0, 0, 0, {}, false);
    auto* noiseLumaDetail = addSlider(detail, "Luminance Detail", &settings_.detail.noiseLuminanceDetail, 0, 100, 50, 0, 0, {}, false);
    auto* noiseLumaContrast = addSlider(detail, "Luminance Contrast", &settings_.detail.noiseLuminanceContrast, 0, 100, 0, 0, 0, {}, false);
    addSlider(detail, "Color Noise", &settings_.detail.noiseColor, 0, 100, 0, 0, 0, {}, false);
    auto* noiseColorDetail = addSlider(detail, "Color Detail", &settings_.detail.noiseColorDetail, 0, 100, 50, 0, 0, {}, false);
    auto* noiseColorSmooth = addSlider(detail, "Color Smoothness", &settings_.detail.noiseColorSmoothness, 0, 100, 50, 0, 0, {}, false);
    sync_.push_back([this, noiseLumaDetail, noiseLumaContrast, noiseColorDetail, noiseColorSmooth] {
        const bool luma = settings_.detail.noiseLuminance > 0, chroma = settings_.detail.noiseColor > 0;
        noiseLumaDetail->setEnabled(luma);
        noiseLumaContrast->setEnabled(luma);
        noiseColorDetail->setEnabled(chroma);
        noiseColorSmooth->setEnabled(chroma);
    });

    auto* optics = addSection(sections, "Optics", "optics", false, &eyes_.optics, [this] { return settings_.optics.adjusts(); });
    auto* aberration = new QCheckBox("Remove Chromatic Aberration");
    auto* profile = new QCheckBox("Enable Lens Profile");
    aberration->setAccessibleName("Remove Chromatic Aberration");
    profile->setAccessibleName("Enable Lens Profile");
    optics->addWidget(aberration);
    optics->addWidget(profile);
    optics->addWidget(toolButton("Defringe", Tool::Defringe));
    auto* profileDistortion = addSlider(optics, "Profile Distortion", &settings_.optics.profileDistortion, 0, 100, 100, 0, 0, {}, false);
    auto* profileVignette = addSlider(optics, "Profile Vignetting", &settings_.optics.profileVignetting, 0, 100, 100, 0, 0, {}, false);
    addSlider(optics, "Distortion", &settings_.optics.distortion, -100, 100, 0, 0, 0, {}, false);
    addSlider(optics, "Purple Amount", &settings_.optics.purpleAmount, 0, 100, 0, 0, 0, {}, false);
    addSlider(optics, "Purple Hue Low", &settings_.optics.purpleHueLow, 0, 360, 270, 0, 0, {}, false);
    addSlider(optics, "Purple Hue High", &settings_.optics.purpleHueHigh, 0, 360, 310, 0, 0, {}, false);
    addSlider(optics, "Green Amount", &settings_.optics.greenAmount, 0, 100, 0, 0, 0, {}, false);
    addSlider(optics, "Green Hue Low", &settings_.optics.greenHueLow, 0, 360, 60, 0, 0, {}, false);
    addSlider(optics, "Green Hue High", &settings_.optics.greenHueHigh, 0, 360, 120, 0, 0, {}, false);
    addSlider(optics, "Lens Vignette", &settings_.optics.vignetteAmount, -100, 100, 0, 0, 0, {}, false);
    addSlider(optics, "Lens Vignette Midpoint", &settings_.optics.vignetteMidpoint, 0, 100, 50, 0, 0, {}, false);
    connect(aberration, &QCheckBox::toggled, this, [this](bool on) { if (writing_) return; settings_.optics.removeChromaticAberration = on; if (edited) edited(); });
    connect(profile, &QCheckBox::toggled, this, [this](bool on) { if (writing_) return; settings_.optics.enableLensProfile = on; if (edited) edited(); });
    sync_.push_back([this, aberration, profile, profileDistortion, profileVignette] {
        QSignalBlocker a(aberration), b(profile);
        aberration->setChecked(settings_.optics.removeChromaticAberration);
        profile->setChecked(settings_.optics.enableLensProfile);
        profileDistortion->setEnabled(settings_.optics.enableLensProfile);
        profileVignette->setEnabled(settings_.optics.enableLensProfile);
    });

    auto* geometry = addSection(sections, "Geometry", "geometry", false, &eyes_.geometry, [this] { return settings_.geometry.adjusts(); });
    auto* upright = new QComboBox;
    upright->setObjectName("cameraRawUpright");
    upright->addItems({"Off", "Guided"});
    upright->setAccessibleName("Upright");
    auto* projection = new QComboBox;
    projection->setObjectName("cameraRawProjection");
    projection->addItems({"Perspective", "Rectilinear"});
    projection->setAccessibleName("Projection");
    geometry->addWidget(upright);
    geometry->addWidget(projection);
    geometry->addWidget(toolButton("Draw Guides", Tool::Guide));
    auto* clearGuides = new QPushButton("Clear Guides");
    clearGuides->setAccessibleName("Clear Guides");
    geometry->addWidget(clearGuides);
    addSlider(geometry, "Vertical", &settings_.geometry.vertical, -100, 100, 0, 0, 0, {}, false);
    addSlider(geometry, "Horizontal", &settings_.geometry.horizontal, -100, 100, 0, 0, 0, {}, false);
    addSlider(geometry, "Rotate", &settings_.geometry.rotate, -45, 45, 0, 0, 0, {}, false);
    addSlider(geometry, "Aspect", &settings_.geometry.aspect, -100, 100, 0, 0, 0, {}, false);
    addSlider(geometry, "Scale", &settings_.geometry.scale, -100, 100, 0, 0, 0, {}, false);
    addSlider(geometry, "Offset X", &settings_.geometry.offsetX, -100, 100, 0, 0, 0, {}, false);
    addSlider(geometry, "Offset Y", &settings_.geometry.offsetY, -100, 100, 0, 0, 0, {}, false);
    auto* constrain = new QCheckBox("Constrain Crop");
    constrain->setAccessibleName("Constrain Crop");
    geometry->addWidget(constrain);
    connect(upright, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int id) {
        if (writing_) return;
        settings_.geometry.upright = id == 1 ? filters::CameraRawUpright::Guided : filters::CameraRawUpright::Off;
        if (edited) edited();
    });
    connect(projection, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int id) {
        if (writing_) return;
        settings_.geometry.projection = id == 1 ? filters::CameraRawProjection::Rectilinear : filters::CameraRawProjection::Perspective;
        if (edited) edited();
    });
    connect(constrain, &QCheckBox::toggled, this, [this](bool on) { if (writing_) return; settings_.geometry.constrainCrop = on; if (edited) edited(); });
    connect(clearGuides, &QPushButton::clicked, this, [this] { settings_.geometry.guides.clear(); publish(); });
    sync_.push_back([this, upright, projection, constrain] {
        QSignalBlocker a(upright), b(projection), c(constrain);
        upright->setCurrentIndex(settings_.geometry.upright == filters::CameraRawUpright::Guided ? 1 : 0);
        projection->setCurrentIndex(settings_.geometry.projection == filters::CameraRawProjection::Rectilinear ? 1 : 0);
        constrain->setChecked(settings_.geometry.constrainCrop);
    });

    auto* calibration = addSection(sections, "Calibration", "calibration", false, &eyes_.calibration, [this] { return settings_.calibration.adjusts(); });
    auto* process = new QComboBox;
    process->setObjectName("cameraRawProcess");
    process->addItems({"Version 1", "Version 2", "Version 3", "Version 4", "Version 5", "Version 6"});
    process->setAccessibleName("Process");
    processSummary_ = new QLabel;
    processSummary_->setWordWrap(true);
    processSummary_->setObjectName("cameraRawProcessSummary");
    calibration->addWidget(process);
    calibration->addWidget(processSummary_);
    addSlider(calibration, "Shadow Tint", &settings_.calibration.shadowTint, -100, 100, 0, 0, 0, {}, false);
    addSlider(calibration, "Red Hue", &settings_.calibration.redHue, -100, 100, 0, 0, 0, {}, false);
    addSlider(calibration, "Red Saturation", &settings_.calibration.redSaturation, -100, 100, 0, 0, 0, {}, false);
    addSlider(calibration, "Green Hue", &settings_.calibration.greenHue, -100, 100, 0, 0, 0, {}, false);
    addSlider(calibration, "Green Saturation", &settings_.calibration.greenSaturation, -100, 100, 0, 0, 0, {}, false);
    addSlider(calibration, "Blue Hue", &settings_.calibration.blueHue, -100, 100, 0, 0, 0, {}, false);
    addSlider(calibration, "Blue Saturation", &settings_.calibration.blueSaturation, -100, 100, 0, 0, 0, {}, false);
    connect(process, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int id) {
        if (writing_) return;
        settings_.calibration.process = filters::CameraRawProcess(id + 1);
        if (edited) edited();
    });
    sync_.push_back([this, process] {
        QSignalBlocker block(process);
        process->setCurrentIndex(int(settings_.calibration.process) - 1);
        processSummary_->setText(filters::CameraRawCalibrationSettings::summary(settings_.calibration.process));
    });
    sections->addStretch(1);
    sync();
}
}
