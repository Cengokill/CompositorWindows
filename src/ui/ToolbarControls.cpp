#include "ToolbarControls.h"
#include "LayerPanelTokens.h"
#include "LayerPanelWidgets.h"
#include "PropertyControls.h"
#include <QCheckBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
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
}
void paintCheckBoxIndicator(QPainter* painter, const QRectF& rect, bool checked, bool enabled, bool hovered) {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    const QRectF box = rect.adjusted(.5, .5, -.5, -.5);
    if (checked) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(enabled ? QColor(t::accent) : QColor(t::controlDisabled));
        painter->drawRoundedRect(box, t::checkBoxRadius, t::checkBoxRadius);
        QPainterPath mark;
        mark.moveTo(box.left() + box.width() * .26, box.top() + box.height() * .54);
        mark.lineTo(box.left() + box.width() * .43, box.top() + box.height() * .70);
        mark.lineTo(box.left() + box.width() * .75, box.top() + box.height() * .30);
        painter->setPen(QPen(enabled ? QColor(Qt::white) : QColor(t::controlTextDisabled), 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(mark);
    } else {
        painter->setPen(QPen(QColor(hovered && enabled ? t::disabled : t::separator), 1));
        painter->setBrush(enabled ? QColor(t::field) : QColor(t::fieldDisabled));
        painter->drawRoundedRect(box, t::checkBoxRadius, t::checkBoxRadius);
    }
    painter->restore();
}
void setToolbarControlVisible(QToolBar* bar, QWidget* widget, bool visible) {
    if (!widget) return;
    if (auto* action = toolbarActionFor(bar, widget)) action->setVisible(visible);
    widget->setVisible(visible);
    // The toolbar re-applies its own enabled state when it shows a widget, so a command pill is brought back in line.
    if (auto* command = qobject_cast<QAction*>(widget->property("commandAction").value<QObject*>())) widget->setEnabled(command->isEnabled());
}
QLabel* addToolbarTitle(QToolBar* bar, const QString& text, const char* objectName) {
    auto* label = makeTitle(text, objectName);
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
        QPushButton[pill="true"]:checked, QPushButton[pill="true"]:checked:hover, QPushButton[pill="true"]:checked:pressed { background: %16; color: white; }
        QPushButton[pill="true"]:checked:disabled { background: %8; color: %9; }
        QPushButton[dialogPill="true"] { border: 1px solid transparent; min-height: %19px; max-height: %19px; }
        QPushButton[dialogPill="true"]:focus { border-color: %2; }
        QToolButton[segment="true"] { background: transparent; color: %2; border: 0; border-radius: %10px; padding: 0 %11px; min-height: %12px; max-height: %12px; }
        QToolButton[segment="true"]:hover:!checked { background: %6; }
        QToolButton[segment="true"]:checked { background: transparent; color: white; }
        QToolButton[segment="true"]:disabled { background: transparent; color: %9; }
        QToolButton[segment="true"]:checked:disabled { color: %2; }
        QAbstractSpinBox[numberField="true"], QLineEdit[numberField="true"] { background: %13; color: %2; border: 1px solid %14; border-radius: %15px; padding: 0 6px; min-height: %5px; max-height: %5px; }
        QAbstractSpinBox[numberField="true"]:focus, QLineEdit[numberField="true"]:focus { border-color: %16; background: %13; }
        QAbstractSpinBox[numberField="true"]:disabled, QLineEdit[numberField="true"]:disabled { background: %17; color: %9; border-color: %14; }
        QLabel[optionCaption="true"] { color: %18; background: transparent; }
        QLabel[scrubHover="true"] { color: %2; }
        QToolBar::separator { background: %14; width: 1px; margin: 4px 6px; }
        )")
        .arg(t::control, t::primary).arg(t::controlHeight / 2).arg(t::pillPadding).arg(t::controlHeight)
        .arg(t::pressed, t::controlPressed, t::controlDisabled, t::controlTextDisabled)
        .arg((t::controlHeight - 2 * t::segmentInset) / 2).arg(t::segmentPadding).arg(t::controlHeight - 2 * t::segmentInset)
        .arg(t::field, t::separator).arg(t::fieldRadius).arg(t::accent, t::fieldDisabled, t::secondary).arg(t::controlHeight - 2);
}
void styleAsNumberField(QAbstractSpinBox* field) {
    field->setButtonSymbols(QAbstractSpinBox::NoButtons);
    field->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    field->setProperty("numberField", true);
    field->setFixedSize(t::numberFieldWidth, t::controlHeight);
}
void styleAsPanelNumberField(QAbstractSpinBox* field) {
    styleAsNumberField(field);
    field->setFixedWidth(t::panelNumberFieldWidth);
}
void markDialogPill(QPushButton* button, bool primary) {
    markPill(button);
    button->setProperty("dialogPill", true);
    if (primary) button->setProperty("primary", true);
    button->setFocusPolicy(Qt::TabFocus);
}
void styleAsNumberField(QLineEdit* field) {
    field->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    field->setProperty("numberField", true);
    field->setFixedSize(t::numberFieldWidth, t::controlHeight);
}
QLabel* makeCaption(const QString& text) {
    auto* label = new QLabel(text);
    label->setProperty("optionCaption", true);
    return label;
}
QCheckBox* makeOptionCheck(const QString& text) { return new QCheckBox(text); }
QLabel* makeTitle(const QString& text, const char* objectName) {
    auto* label = new QLabel(text);
    label->setObjectName(objectName);
    auto font = label->font();
    font.setBold(true);
    label->setFont(font);
    return label;
}
PropertyNumber* makeNumberField(double minimum, double maximum, double value, const QString& accessibleName, int decimals) {
    auto* field = new PropertyNumber;
    field->setDecimals(decimals);
    field->setRange(minimum, maximum);
    field->setValue(value);
    styleAsNumberField(field);
    if (!accessibleName.isEmpty()) field->setAccessibleName(accessibleName);
    return field;
}
OptionSlider* makeOptionSlider(const char* objectName, const QString& accessibleName, int minimum, int maximum) {
    auto* slider = new OptionSlider(Qt::Horizontal);
    slider->setObjectName(objectName);
    slider->setAccessibleName(accessibleName);
    slider->setRange(minimum, maximum);
    slider->setDefaultValue(maximum);
    slider->setFixedWidth(t::optionSliderWidth);
    return slider;
}
void markPill(QWidget* button) { button->setProperty("pill", true); }
GradientSwatch::GradientSwatch(QWidget* parent) : QWidget(parent) { setFixedSize(56, 18); }
void GradientSwatch::setColors(const QColor& from, const QColor& to) { if (from == from_ && to == to_) return; from_ = from; to_ = to; update(); }
QSize GradientSwatch::sizeHint() const { return {56, 18}; }
void GradientSwatch::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF box = QRectF(rect()).adjusted(.5, .5, -.5, -.5);
    QPainterPath shape;
    shape.addRoundedRect(box, 3, 3);
    painter.save();
    painter.setClipPath(shape);
    painter.fillRect(rect(), Qt::white);
    constexpr int tile = 4;
    for (int row = 0; row * tile < height(); ++row)
        for (int column = 0; column * tile < width(); ++column)
            if ((row + column) % 2 == 0) painter.fillRect(column * tile, row * tile, tile, tile, QColor(128, 128, 128, 115));
    QLinearGradient fill(box.left(), 0, box.right(), 0);
    fill.setColorAt(0, from_);
    fill.setColorAt(1, to_);
    painter.fillRect(rect(), fill);
    painter.restore();
    painter.setPen(QPen(isEnabled() ? QColor(t::separator) : QColor(t::controlTextDisabled), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(shape);
}
ColorWell::ColorWell(QWidget* parent) : QAbstractButton(parent) { setCursor(Qt::PointingHandCursor); setFocusPolicy(Qt::StrongFocus); }
void ColorWell::setColor(const QColor& color) { if (color == color_) return; color_ = color; update(); }
QSize ColorWell::sizeHint() const { return {t::colorWellWidth, t::colorWellHeight}; }
void ColorWell::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        event->accept();
        if (!event->isAutoRepeat()) click();
        return;
    }
    QAbstractButton::keyPressEvent(event);
}
void ColorWell::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF box = QRectF(rect()).adjusted(.75, .75, -.75, -.75);
    const auto border = !isEnabled() ? t::controlTextDisabled : hasFocus() ? t::accent : t::primary;
    painter.setPen(QPen(QColor(border), 1.5));
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
QWidget* addToolbarGroup(QToolBar* bar, std::initializer_list<QWidget*> widgets, const char* objectName) {
    auto* host = new QWidget;
    host->setObjectName(objectName);
    auto* row = new QHBoxLayout(host);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(t::checkBoxGap);
    for (auto* widget : widgets) row->addWidget(widget);
    // A toolbar hands spare width to expanding widgets; a group keeps the size of its controls.
    host->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    bar->addWidget(host);
    return host;
}
QPushButton* addCommandPill(QToolBar* bar, const QString& text, const char* objectName, QAction* command, bool primary) {
    auto* button = new QPushButton(text);
    button->setProperty("pill", true);
    button->setProperty("primary", primary);
    button->setFixedHeight(t::controlHeight);
    button->setObjectName(objectName);
    button->setAccessibleName(command->text());
    button->setProperty("commandAction", QVariant::fromValue<QObject*>(command));
    QObject::connect(button, &QPushButton::clicked, command, &QAction::trigger);
    QObject::connect(command, &QAction::changed, button, [button, command] { button->setEnabled(command->isEnabled()); });
    button->setEnabled(command->isEnabled());
    bar->addWidget(button);
    return button;
}
CapsuleDropdown* addDropdown(QToolBar* bar, const char* objectName, const QString& accessibleName, const QStringList& items) {
    auto* dropdown = new CapsuleDropdown;
    dropdown->setObjectName(objectName);
    dropdown->setAccessibleName(accessibleName);
    dropdown->addItems(items);
    bar->addWidget(dropdown);
    return dropdown;
}
QCheckBox* addOptionCheck(QToolBar* bar, const QString& text, const char* objectName, bool checked) {
    auto* check = makeOptionCheck(text);
    check->setObjectName(objectName);
    check->setChecked(checked);
    bar->addWidget(check);
    return check;
}
}
