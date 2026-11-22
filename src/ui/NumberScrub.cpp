#include "NumberScrub.h"
#include "LayerPanelTokens.h"
#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSpinBox>
#include <QStyle>
#include <QToolBar>
#include <algorithm>
#include <cmath>

namespace compositor::ui {
namespace {
ScrubHistory& history() { static ScrubHistory value; return value; }
constexpr int dragThreshold = 3;
constexpr double fineFactor = .1, coarseFactor = 10;

class Scrubber final : public QObject {
public:
    Scrubber(QLabel* label, QAbstractSpinBox* field) : QObject(label), label_(label), field_(field) {
        label->installEventFilter(this);
        label->setCursor(Qt::SplitHCursor);
    }
protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched == label_) return labelEvent(event);
        // While a drag runs, Escape anywhere cancels it.
        if (dragging_ && event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
            cancel();
            return true;
        }
        return false;
    }
private:
    bool usable() const { return field_ && field_->isEnabled() && !field_->isReadOnly(); }
    double value() const {
        if (auto* real = qobject_cast<QDoubleSpinBox*>(field_)) return real->value();
        return static_cast<QSpinBox*>(field_)->value();
    }
    void setValue(double next) {
        if (auto* real = qobject_cast<QDoubleSpinBox*>(field_)) real->setValue(next);
        else static_cast<QSpinBox*>(field_)->setValue(int(std::lround(next)));
    }
    int decimals() const { auto* real = qobject_cast<QDoubleSpinBox*>(field_); return real ? real->decimals() : 0; }
    void setHover(bool on) {
        if (label_->property("scrubHover").toBool() == on) return;
        label_->setProperty("scrubHover", on);
        label_->style()->unpolish(label_);
        label_->style()->polish(label_);
        label_->update();
    }
    // Per pixel dragged: 1 for whole numbers, the next-to-last decimal for the others; Shift divides by 10 (not below the last decimal), Ctrl multiplies by 10.
    double pixelStep(Qt::KeyboardModifiers modifiers) const {
        const double unit = std::pow(10., -decimals());
        double step = decimals() == 0 ? 1. : unit * 10;
        if (modifiers.testFlag(Qt::ShiftModifier)) step = std::max(unit, step * fineFactor);
        else if (modifiers.testFlag(Qt::ControlModifier)) step *= coarseFactor;
        return step;
    }
    void drag(QMouseEvent* event) {
        const double x = event->globalPosition().x();
        if (!dragging_) {
            if (std::abs(x - pressX_) < dragThreshold) return;
            dragging_ = true;
            depthBefore_ = history().depth ? history().depth() : -1;
            qApp->installEventFilter(this);
        }
        // Changing the modifier mid-drag rescales only from the current position, so the value never jumps.
        if (event->modifiers() != lastModifiers_) { startValue_ = value(); rebaseX_ = x; lastModifiers_ = event->modifiers(); }
        setValue(startValue_ + (x - rebaseX_) * pixelStep(event->modifiers()));
    }
    bool labelEvent(QEvent* event) {
        switch (event->type()) {
        case QEvent::Enter:
            if (usable()) { setHover(true); label_->setCursor(Qt::SplitHCursor); }
            else label_->unsetCursor();
            break;
        case QEvent::Leave:
            if (!pressed_) setHover(false);
            break;
        case QEvent::MouseButtonPress: {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() != Qt::LeftButton || !usable()) return false;
            pressed_ = true; dragging_ = false;
            pressX_ = rebaseX_ = mouse->globalPosition().x();
            startValue_ = originalValue_ = value();
            lastModifiers_ = mouse->modifiers();
            return true;
        }
        case QEvent::MouseMove:
            if (!pressed_) return false;
            drag(static_cast<QMouseEvent*>(event));
            return true;
        case QEvent::MouseButtonRelease: {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() != Qt::LeftButton || !pressed_) return false;
            const bool moved = dragging_;
            finish();
            if (moved) {
                // The field commits on editingFinished, so a drag is one edit, like typing a value and pressing Enter.
                emit field_->editingFinished();
            } else {
                field_->setFocus(Qt::MouseFocusReason);
                if (auto* edit = field_->findChild<QLineEdit*>()) edit->selectAll();
            }
            setHover(label_->underMouse());
            return true;
        }
        case QEvent::Paint:
            return paint(event);
        default:
            break;
        }
        return false;
    }
    void finish() {
        pressed_ = false;
        if (dragging_) { dragging_ = false; qApp->removeEventFilter(this); }
    }
    void cancel() {
        finish();
        if (value() != originalValue_) setValue(originalValue_);
        // Closes whatever edit the live values opened, now that the value is back where it was.
        emit field_->editingFinished();
        if (history().undo && depthBefore_ >= 0 && history().depth() > depthBefore_) history().undo();
        setHover(label_->underMouse());
    }
    // The label paints itself, then a flat double arrow sits under the text while the pointer is over it.
    bool paint(QEvent* event) {
        static_cast<QObject*>(label_)->event(event);
        if (!label_->property("scrubHover").toBool()) return true;
        QPainter painter(label_);
        painter.setRenderHint(QPainter::Antialiasing);
        const QColor color(layer_panel::primary);
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        const double cx = label_->width() / 2., y = label_->height() - 3.5, half = 5, head = 2.5;
        QPainterPath arrows;
        arrows.moveTo(cx - half, y); arrows.lineTo(cx - half + head, y - head); arrows.lineTo(cx - half + head, y + head); arrows.closeSubpath();
        arrows.moveTo(cx + half, y); arrows.lineTo(cx + half - head, y - head); arrows.lineTo(cx + half - head, y + head); arrows.closeSubpath();
        painter.drawPath(arrows);
        painter.setPen(QPen(color, 1));
        painter.drawLine(QPointF(cx - half + head, y), QPointF(cx + half - head, y));
        return true;
    }
    QLabel* label_;
    QAbstractSpinBox* field_;
    bool pressed_{}, dragging_{};
    double pressX_{}, rebaseX_{}, startValue_{}, originalValue_{};
    int depthBefore_{-1};
    Qt::KeyboardModifiers lastModifiers_;
};

bool isNumberBox(QWidget* widget) { return qobject_cast<QSpinBox*>(widget) || qobject_cast<QDoubleSpinBox*>(widget); }
// The number field a widget is or holds (the host made by addNumberField wraps one).
QAbstractSpinBox* fieldIn(QWidget* widget) {
    if (!widget) return nullptr;
    if (isNumberBox(widget)) return static_cast<QAbstractSpinBox*>(widget);
    for (auto* child : widget->children())
        if (auto* box = qobject_cast<QWidget*>(child); box && isNumberBox(box)) return static_cast<QAbstractSpinBox*>(box);
    return nullptr;
}
bool isCaption(QWidget* widget) {
    auto* label = qobject_cast<QLabel*>(widget);
    return label && label->property("optionCaption").toBool() && !label->text().isEmpty() && label->text().front().isLetter();
}
void pair(const QList<QWidget*>& siblings) {
    for (int i = 0; i < siblings.size(); ++i) {
        if (!isCaption(siblings[i])) continue;
        // A slider may sit between a caption and its field; anything else means the caption belongs to another control.
        for (int j = i + 1; j < siblings.size(); ++j) {
            if (auto* box = fieldIn(siblings[j])) { attachScrub(static_cast<QLabel*>(siblings[i]), box); break; }
            if (!qobject_cast<QAbstractSlider*>(siblings[j])) break;
        }
    }
}
}

void attachScrub(QLabel* label, QAbstractSpinBox* field) {
    if (!label || !field || label->findChild<QObject*>("numberScrub")) return;
    auto* scrubber = new Scrubber(label, field);
    scrubber->setObjectName("numberScrub");
}

void installNumberScrubbing(QMainWindow* window, ScrubHistory hooks) {
    history() = std::move(hooks);
    for (auto* bar : window->findChildren<QToolBar*>()) {
        // Captions placed straight on the bar, then those inside the groups the bar holds.
        QList<QWidget*> direct;
        for (auto* action : bar->actions())
            if (auto* widget = bar->widgetForAction(action)) direct << widget;
        pair(direct);
        for (auto* host : bar->findChildren<QWidget*>()) {
            auto* row = qobject_cast<QHBoxLayout*>(host->layout());
            if (!row) continue;
            QList<QWidget*> items;
            for (int i = 0; i < row->count(); ++i)
                if (auto* widget = row->itemAt(i)->widget()) items << widget;
            pair(items);
        }
    }
    // The Zoom bar has no caption: its bold title is the handle.
    if (auto* title = window->findChild<QLabel*>("zoomTitle"))
        if (auto* field = window->findChild<QDoubleSpinBox*>("zoomPercent")) attachScrub(title, field);
}
}
