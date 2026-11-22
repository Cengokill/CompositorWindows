#include "ToolFlyoutButton.h"
#include <QAction>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

namespace compositor::ui {
namespace {
constexpr int holdMilliseconds = 400;
}
ToolFlyoutButton::ToolFlyoutButton() {
    hold_.setSingleShot(true);
    hold_.setInterval(holdMilliseconds);
    connect(&hold_, &QTimer::timeout, this, [this] { if (isDown()) showFlyout(); });
}
void ToolFlyoutButton::setFlyout(std::function<Entry()> describe, std::function<void()> activate) {
    describe_ = std::move(describe);
    activate_ = std::move(activate);
    if (menu_) return;
    menu_ = new QMenu(this);
    entry_ = menu_->addAction(QString());
    connect(entry_, &QAction::triggered, this, [this] { if (activate_) activate_(); });
}
QMenu* ToolFlyoutButton::showFlyout() {
    if (!describe_ || !menu_) return nullptr;
    const auto entry = describe_();
    entry_->setIcon(entry.icon);
    entry_->setText(entry.text);
    // The press that opened the popup must not also click the tool.
    hold_.stop();
    setDown(false);
    menu_->popup(mapToGlobal(QPoint(width() + 2, 0)));
    return menu_;
}
void ToolFlyoutButton::mousePressEvent(QMouseEvent* event) {
    if (describe_ && event->button() == Qt::LeftButton) hold_.start();
    QToolButton::mousePressEvent(event);
}
void ToolFlyoutButton::mouseReleaseEvent(QMouseEvent* event) {
    hold_.stop();
    QToolButton::mouseReleaseEvent(event);
}
void ToolFlyoutButton::paintEvent(QPaintEvent* event) {
    QToolButton::paintEvent(event);
    if (!describe_) return;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const auto color = palette().color(isEnabled() ? QPalette::Active : QPalette::Disabled, QPalette::ButtonText);
    QPainterPath triangle;
    const QPointF corner(width() - 4.5, height() - 4.5);
    triangle.moveTo(corner);
    triangle.lineTo(corner + QPointF(-4.5, 0));
    triangle.lineTo(corner + QPointF(0, -4.5));
    triangle.closeSubpath();
    painter.fillPath(triangle, color);
}
}
