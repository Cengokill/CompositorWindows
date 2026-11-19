#include "ToolbarControls.h"
#include <QLabel>
#include <QToolBar>
#include <QWidgetAction>

namespace compositor::ui {
namespace {
QAction* toolbarActionFor(QToolBar* bar, QWidget* widget) {
    if (!bar || !widget) return nullptr;
    for (auto* action : bar->actions()) {
        if (bar->widgetForAction(action) == widget) return action;
        if (auto* hosted = qobject_cast<QWidgetAction*>(action); hosted && hosted->defaultWidget() == widget) return action;
    }
    return nullptr;
}
}
void setToolbarControlVisible(QToolBar* bar, QWidget* widget, bool visible) {
    if (!widget) return;
    if (auto* action = toolbarActionFor(bar, widget)) action->setVisible(visible);
    widget->setVisible(visible);
}
QLabel* addToolbarTitle(QToolBar* bar, const QString& text, const char* objectName) {
    auto* label = new QLabel(text);
    label->setObjectName(objectName);
    auto font = label->font();
    font.setBold(true);
    label->setFont(font);
    bar->addWidget(label);
    return label;
}
}
