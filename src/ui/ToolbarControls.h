#pragma once
#include <QString>
#include <QStringList>

class QAction;
class QCheckBox;
class QComboBox;
class QLabel;
class QSpinBox;
class QToolBar;
class QWidget;

namespace compositor::ui {
// A toolbar re-shows a hidden widget when it lays out, so visibility goes through the widget's action.
void setToolbarControlVisible(QToolBar* bar, QWidget* widget, bool visible);
// A bold tool name at the start of an options bar.
QLabel* addToolbarTitle(QToolBar* bar, const QString& text, const char* objectName);
// A quiet caption for the control that follows it.
QLabel* addToolbarCaption(QToolBar* bar, const QString& text, const char* objectName);
// The style sheet of the options-bar controls below and of SegmentedControl, appended to the application style sheet.
QString optionsBarStyleSheet();
// Draws the tool button of an action as a capsule button.
void markPillButton(QToolBar* bar, QAction* action);
// A small right-aligned number field without spin arrows, followed by an optional unit such as "px". Returns the field.
QSpinBox* addNumberField(QToolBar* bar, const char* objectName, const QString& accessibleName, int minimum, int maximum, int value, const QString& unit = {});
// A capsule dropdown, the same control as the blend-mode selector of the Layers panel.
QComboBox* addDropdown(QToolBar* bar, const char* objectName, const QString& accessibleName, const QStringList& items = {});
// A check box with an accent-blue box.
QCheckBox* addOptionCheck(QToolBar* bar, const QString& text, const char* objectName, bool checked = false);
}
