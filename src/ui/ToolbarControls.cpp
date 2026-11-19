#include "ToolbarControls.h"
#include "LayerPanelTokens.h"
#include "LayerPanelWidgets.h"
#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QSpinBox>
#include <QToolBar>
#include <QWidgetAction>

namespace compositor::ui {
namespace t = layer_panel;
namespace {
QAction* toolbarActionFor(QToolBar* bar, QWidget* widget) {
    if (!bar || !widget) return nullptr;
    for (auto* action : bar->actions()) {
        if (bar->widgetForAction(action) == widget) return action;
        if (auto* hosted = qobject_cast<QWidgetAction*>(action); hosted && hosted->defaultWidget() == widget) return action;
    }
    return nullptr;
}
// QCheckBox draws its box through the style; the options bar draws its own so the checked box is the application blue.
class OptionCheckBox final : public QCheckBox {
public:
    using QCheckBox::QCheckBox;
    QSize sizeHint() const override { return {t::checkBoxSize + t::checkBoxGap + fontMetrics().horizontalAdvance(text()), t::controlHeight}; }
    QSize minimumSizeHint() const override { return sizeHint(); }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF box(0.5, (height() - t::checkBoxSize) / 2. + .5, t::checkBoxSize - 1, t::checkBoxSize - 1);
        const bool on = isEnabled();
        if (isChecked()) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(on ? QColor(t::accent) : QColor(t::controlDisabled));
            painter.drawRoundedRect(box, t::checkBoxRadius, t::checkBoxRadius);
            QPainterPath mark;
            mark.moveTo(box.left() + box.width() * .26, box.top() + box.height() * .54);
            mark.lineTo(box.left() + box.width() * .43, box.top() + box.height() * .70);
            mark.lineTo(box.left() + box.width() * .75, box.top() + box.height() * .30);
            painter.setPen(QPen(on ? QColor(Qt::white) : QColor(t::controlTextDisabled), 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(mark);
        } else {
            painter.setPen(QPen(QColor(underMouse() && on ? t::disabled : t::separator), 1));
            painter.setBrush(on ? QColor(t::field) : QColor(t::fieldDisabled));
            painter.drawRoundedRect(box, t::checkBoxRadius, t::checkBoxRadius);
        }
        painter.setPen(on ? QColor(t::primary) : QColor(t::controlTextDisabled));
        painter.drawText(QRect(t::checkBoxSize + t::checkBoxGap, 0, width() - t::checkBoxSize - t::checkBoxGap, height()), Qt::AlignVCenter | Qt::AlignLeft, text());
    }
};
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
QLabel* addToolbarCaption(QToolBar* bar, const QString& text, const char* objectName) {
    auto* label = new QLabel(text);
    label->setObjectName(objectName);
    label->setProperty("optionCaption", true);
    bar->addWidget(label);
    return label;
}
QString optionsBarStyleSheet() {
    return QString(R"(
        QToolButton[pill="true"], QPushButton[pill="true"] { background: %1; color: %2; border: 0; border-radius: %3px; padding: 0 %4px; min-height: %5px; max-height: %5px; }
        QToolButton[pill="true"]:hover, QPushButton[pill="true"]:hover { background: %6; }
        QToolButton[pill="true"]:pressed, QPushButton[pill="true"]:pressed { background: %7; }
        QToolButton[pill="true"]:disabled, QPushButton[pill="true"]:disabled { background: %8; color: %9; }
        QToolButton[pill="true"]:checked { background: %16; color: white; }
        QPushButton[pill="true"][primary="true"] { background: %16; color: white; }
        QPushButton[pill="true"][primary="true"]:hover { background: %16; }
        QPushButton[pill="true"][primary="true"]:disabled { background: %8; color: %9; }
        QToolButton[segment="true"] { background: transparent; color: %2; border: 0; border-radius: %10px; padding: 0 %11px; min-height: %12px; max-height: %12px; }
        QToolButton[segment="true"]:hover:!checked { background: %6; }
        QToolButton[segment="true"]:checked { background: transparent; color: white; }
        QToolButton[segment="true"]:disabled { background: transparent; color: %9; }
        QToolButton[segment="true"]:checked:disabled { color: %2; }
        QAbstractSpinBox[numberField="true"] { background: %13; color: %2; border: 1px solid %14; border-radius: %15px; padding: 0 6px; min-height: %5px; max-height: %5px; }
        QAbstractSpinBox[numberField="true"]:focus { border-color: %16; background: %13; }
        QAbstractSpinBox[numberField="true"]:disabled { background: %17; color: %9; border-color: %14; }
        QLabel[optionCaption="true"] { color: %18; background: transparent; }
        QToolBar::separator { background: %14; width: 1px; margin: 4px 6px; }
        )")
        .arg(t::control, t::primary).arg(t::controlHeight / 2).arg(t::pillPadding).arg(t::controlHeight)
        .arg(t::pressed, t::controlPressed, t::controlDisabled, t::controlTextDisabled)
        .arg((t::controlHeight - 2 * t::segmentInset) / 2).arg(t::segmentPadding).arg(t::controlHeight - 2 * t::segmentInset)
        .arg(t::field, t::separator).arg(t::fieldRadius).arg(t::accent, t::fieldDisabled, t::secondary);
}
void styleAsNumberField(QAbstractSpinBox* field) {
    field->setButtonSymbols(QAbstractSpinBox::NoButtons);
    field->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    field->setProperty("numberField", true);
    field->setFixedSize(t::numberFieldWidth, t::controlHeight);
}
QLabel* makeCaption(const QString& text) {
    auto* label = new QLabel(text);
    label->setProperty("optionCaption", true);
    return label;
}
QCheckBox* makeOptionCheck(const QString& text) { return new OptionCheckBox(text); }
void markPill(QWidget* button) { button->setProperty("pill", true); }
ColorWell::ColorWell(QWidget* parent) : QAbstractButton(parent) { setCursor(Qt::PointingHandCursor); setFocusPolicy(Qt::StrongFocus); }
void ColorWell::setColor(const QColor& color) { if (color == color_) return; color_ = color; update(); }
QSize ColorWell::sizeHint() const { return {t::colorWellWidth, t::colorWellHeight}; }
void ColorWell::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF box = QRectF(rect()).adjusted(.75, .75, -.75, -.75);
    painter.setPen(QPen(isEnabled() ? QColor(t::primary) : QColor(t::controlTextDisabled), 1.5));
    painter.setBrush(isEnabled() ? color_ : color_.darker(160));
    painter.drawRoundedRect(box, t::colorWellRadius, t::colorWellRadius);
}
void markPillButton(QToolBar* bar, QAction* action) {
    if (auto* button = bar ? bar->widgetForAction(action) : nullptr) button->setProperty("pill", true);
}
QSpinBox* addNumberField(QToolBar* bar, const char* objectName, const QString& accessibleName, int minimum, int maximum, int value, const QString& unit) {
    auto* host = new QWidget;
    auto* row = new QHBoxLayout(host);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(t::checkBoxGap);
    auto* field = new QSpinBox;
    field->setObjectName(objectName);
    field->setAccessibleName(accessibleName);
    field->setRange(minimum, maximum);
    field->setValue(value);
    styleAsNumberField(field);
    row->addWidget(field);
    if (!unit.isEmpty()) {
        row->addWidget(makeCaption(unit));
    }
    bar->addWidget(host);
    return field;
}
QComboBox* addDropdown(QToolBar* bar, const char* objectName, const QString& accessibleName, const QStringList& items) {
    auto* dropdown = new CapsuleDropdown;
    dropdown->setObjectName(objectName);
    dropdown->setAccessibleName(accessibleName);
    dropdown->addItems(items);
    bar->addWidget(dropdown);
    return dropdown;
}
QCheckBox* addOptionCheck(QToolBar* bar, const QString& text, const char* objectName, bool checked) {
    auto* check = new OptionCheckBox(text);
    check->setObjectName(objectName);
    check->setChecked(checked);
    bar->addWidget(check);
    return check;
}
}
