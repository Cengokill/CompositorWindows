#pragma once
#include "LayerPanelWidgets.h"
#include <QAbstractButton>
#include <QColor>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <initializer_list>

class QAbstractSpinBox;
class QAction;
class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QPainter;
class QSpinBox;
class QToolBar;
class QWidget;

namespace compositor::ui {
class OptionSlider;
class PropertyNumber;
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
CapsuleDropdown* addDropdown(QToolBar* bar, const char* objectName, const QString& accessibleName, const QStringList& items = {});
// A check box. Every check box in the application is drawn by the application style with paintCheckBoxIndicator.
QCheckBox* addOptionCheck(QToolBar* bar, const QString& text, const char* objectName, bool checked = false);
// The one check box box: a field-colored rounded square, filled with the accent blue and a white tick when checked.
// The application style calls it for every QCheckBox, so no check box needs its own painting.
void paintCheckBoxIndicator(QPainter* painter, const QRectF& rect, bool checked, bool enabled, bool hovered);
// Pieces for bars that lay their controls out themselves (the Brush bar) instead of adding them to the toolbar.
void styleAsNumberField(QAbstractSpinBox* field);
// The same field at the wider edit-panel width.
void styleAsPanelNumberField(QAbstractSpinBox* field);
// A capsule button of a dialog or edit panel. A click leaves the focus where it was; Tab reaches the button and rings it.
void markDialogPill(QPushButton* button, bool primary = false);
void styleAsNumberField(QLineEdit* field);
QLabel* makeCaption(const QString& text);
QCheckBox* makeOptionCheck(const QString& text);
// A bold tool name for a bar laid out by a WrappingToolOptions.
QLabel* makeTitle(const QString& text, const char* objectName);
// A right-aligned number field without spin arrows, ready for makeCaption("px") or makeCaption("%") after it.
PropertyNumber* makeNumberField(double minimum, double maximum, double value, const QString& accessibleName, int decimals = 0);
// The options-bar slider at the shared width, in the hundredths of a percent the brush and retouch bars use.
OptionSlider* makeOptionSlider(const char* objectName, const QString& accessibleName, int minimum, int maximum);
// Controls laid out in one row that the toolbar shows or hides as a whole.
QWidget* addToolbarGroup(QToolBar* bar, std::initializer_list<QWidget*> widgets, const char* objectName);
// A capsule button that runs a command and follows its enabled state; primary fills it with the accent blue.
QPushButton* addCommandPill(QToolBar* bar, const QString& text, const char* objectName, QAction* command, bool primary);
// A capsule button; a checkable one fills with the accent blue while checked.
void markPill(QWidget* button);
// A small preview of a two-color gradient over a checkerboard, so a transparent end reads as transparency.
class GradientSwatch final : public QWidget {
public:
    explicit GradientSwatch(QWidget* parent = nullptr);
    void setColors(const QColor& from, const QColor& to);
    QSize sizeHint() const override;
protected:
    void paintEvent(QPaintEvent*) override;
private:
    QColor from_{Qt::black}, to_{Qt::white};
};
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
