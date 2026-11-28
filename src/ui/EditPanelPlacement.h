#pragma once
#include <QList>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <optional>
class QWidget;

namespace compositor::ui {
// Every edit panel reopens where any of them was last left, for the application session.
// The stored point is the frame's top corner on the side of its screen it was left on, so a
// panel of another width grows away from that screen edge instead of past it.
struct EditPanelCorner {
    QPoint corner;
    bool right{};
};
// Frame rectangles are global and screens are available geometries. fallbackScreen is used
// when a rectangle overlaps none of them, such as after a monitor is removed.
QRect editPanelFrame(QSize frame,const std::optional<EditPanelCorner>& saved,const std::optional<QRect>& canvas,const QList<QRect>& screens,QRect fallbackScreen);
EditPanelCorner editPanelCorner(QRect frame,const QList<QRect>& screens,QRect fallbackScreen);
// Call placeEditPanel before the panel is shown and rememberEditPanel when it closes. A panel
// closed where it opened leaves the shared position unchanged.
void placeEditPanel(QWidget* panel,const QWidget* canvas);
void rememberEditPanel(const QWidget* panel);
}
