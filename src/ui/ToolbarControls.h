#pragma once
#include <QAbstractButton>
#include <QColor>
#include <QString>
#include <QStringList>

class QAbstractSpinBox;
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
// Pieces for bars that lay their controls out themselves (the Brush bar) instead of adding them to the toolbar.
void styleAsNumberField(QAbstractSpinBox* field);
QLabel* makeCaption(const QString& text);
QCheckBox* makeOptionCheck(const QString& text);
// A capsule button; a checkable one fills with the accent blue while checked.
void markPill(QWidget* button);
// The current color as a rounded well with a light border; clicking it is up to the owner.
class ColorWell final : public QAbstractButton {
public:
    explicit ColorWell(QWidget* parent = nullptr);
    void setColor(const QColor& color);
    QColor color() const { return color_; }
    QSize sizeHint() const override;
protected:
    void paintEvent(QPaintEvent*) override;
private:
    QColor color_{Qt::black};
};
}
