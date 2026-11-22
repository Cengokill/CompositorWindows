#pragma once
#include <QIcon>
#include <QString>
#include <QTimer>
#include <QToolButton>
#include <functional>

class QMenu;

namespace compositor::ui {
// A tool rail button that, like a Photoshop tool group, offers its alternate tool in a small popup when it is pressed and held.
// A small triangle in the bottom-right corner marks a button that has one.
class ToolFlyoutButton final : public QToolButton {
public:
    struct Entry { QIcon icon; QString text; };
    ToolFlyoutButton();
    // describe() names the alternate tool and is asked every time the popup opens; activate() switches to it.
    void setFlyout(std::function<Entry()> describe, std::function<void()> activate);
    bool hasFlyout() const { return bool(describe_); }
    // Opens the popup now, as a long press does. Returns the popup so a caller can inspect it.
    QMenu* showFlyout();
protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
private:
    std::function<Entry()> describe_;
    std::function<void()> activate_;
    QMenu* menu_{};
    QAction* entry_{};
    QTimer hold_;
};
}
