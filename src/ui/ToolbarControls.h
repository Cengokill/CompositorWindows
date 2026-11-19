#pragma once
#include <QString>

class QLabel;
class QToolBar;
class QWidget;

namespace compositor::ui {
// A toolbar re-shows a hidden widget when it lays out, so visibility goes through the widget's action.
void setToolbarControlVisible(QToolBar* bar, QWidget* widget, bool visible);
// A bold tool name at the start of an options bar.
QLabel* addToolbarTitle(QToolBar* bar, const QString& text, const char* objectName);
}
