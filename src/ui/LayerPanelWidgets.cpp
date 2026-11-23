#include "LayerPanelWidgets.h"
#include "LayerPanelTokens.h"
#include <QAbstractItemView>
#include <QApplication>
#include <QFile>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPixmap>
#include <QProxyStyle>
#include <QScreen>
#include <QRegularExpression>
#include <QStyleFactory>
#include <QStyleOptionSlider>
#include <QStyledItemDelegate>
#include <QXmlStreamReader>
#include <map>
#include <vector>

static void initializeLayerPanelIcons() { Q_INIT_RESOURCE(layer_panel_icons); }

namespace compositor::ui {
namespace {
namespace t = layer_panel;
constexpr int glassPopupRadius = 18;
constexpr int glassBlurReduction = 5;

QFont panelFont(int pixels, QFont::Weight weight = QFont::Normal) {
    static const QString family = QFontDatabase::families().contains("Segoe UI Variable")
        ? "Segoe UI Variable" : "Segoe UI";
    QFont font(family);
    font.setPixelSize(pixels);
    font.setWeight(weight);
    return font;
}

class BlendGroupDelegate final : public QStyledItemDelegate {
public:
    BlendGroupDelegate(QComboBox* owner, QObject* parent) : QStyledItemDelegate(parent), owner_(owner) {}
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        auto size = QStyledItemDelegate::sizeHint(option, index);
        if (index.data(Qt::UserRole + 1).toBool()) size.rheight() += t::blendGroupGap;
        return size;
    }
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        auto item = option;
        if (index.data(Qt::UserRole + 1).toBool()) {
            painter->save();
            painter->setPen(QColor(t::separator));
            painter->drawLine(option.rect.left(), option.rect.top() + t::blendGroupGap / 2,
                              option.rect.right(), option.rect.top() + t::blendGroupGap / 2);
            painter->restore();
            item.rect.adjust(0, t::blendGroupGap, 0, 0);
        }
        if (owner_ && owner_->property("glassPopup").toBool()) {
            // Glass list: the highlighted row is a full capsule in the application blue, and the current choice carries a check mark.
            const bool enabled = index.flags().testFlag(Qt::ItemIsEnabled);
            const bool hot = enabled && (item.state & (QStyle::State_Selected | QStyle::State_MouseOver));
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            if (hot) {
                const QRectF pill = QRectF(item.rect).adjusted(0, 1, 0, -1);
                painter->setPen(Qt::NoPen);
                painter->setBrush(QColor(t::accent));
                painter->drawRoundedRect(pill, pill.height() / 2, pill.height() / 2);
            }
            if (enabled && index.row() == owner_->currentIndex()) {
                painter->setPen(QPen(Qt::white, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                const auto x = item.rect.left() + t::blendMenuCheckInset + 4;
                const auto y = item.rect.center().y();
                painter->drawLine(QPoint(x, y), QPoint(x + 3, y + 3));
                painter->drawLine(QPoint(x + 3, y + 3), QPoint(x + 8, y - 4));
            }
            painter->restore();
            if (enabled) item.rect.adjust(t::blendMenuTextInset + 8, 0, 0, 0);
            item.state &= ~(QStyle::State_Selected | QStyle::State_MouseOver);
            if (hot) item.palette.setColor(QPalette::Text, Qt::white);
        } else if (owner_ && index.row() == owner_->currentIndex()) {
            painter->save();
            painter->setBrush(QColor(t::accent));
            painter->setPen(Qt::NoPen);
            painter->drawRoundedRect(item.rect, t::fieldRadius, t::fieldRadius);
            painter->setPen(QPen(Qt::white, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            const auto x = item.rect.left() + t::blendMenuCheckInset;
            const auto y = item.rect.center().y();
            painter->drawLine(QPoint(x, y), QPoint(x + 3, y + 3));
            painter->drawLine(QPoint(x + 3, y + 3), QPoint(x + 8, y - 4));
            painter->restore();
            item.rect.adjust(t::blendMenuTextInset, 0, 0, 0);
            item.state &= ~(QStyle::State_Selected | QStyle::State_MouseOver);
            item.palette.setColor(QPalette::Text, Qt::white);
        }
        item.palette.setColor(QPalette::Highlight, QColor(t::accent));
        item.palette.setColor(QPalette::HighlightedText, Qt::white);
        QStyledItemDelegate::paint(painter, item, index);
    }
private:
    QComboBox* owner_{};
};

class SliderStyle final : public QProxyStyle {
public:
    struct Metrics { int track, knobWidth, knobHeight; qreal knobRadius; const char* trackColor; const char* knobColor; };
    explicit SliderStyle(Metrics metrics = {t::sliderTrack, t::sliderKnobWidth, t::sliderKnobHeight, t::sliderKnobRadius, t::track, t::knob}) : QProxyStyle(QStyleFactory::create("Fusion")), m(metrics) {}
    QRect subControlRect(ComplexControl control, const QStyleOptionComplex* option,
                         SubControl part, const QWidget* widget) const override {
        if (control != CC_Slider) return QProxyStyle::subControlRect(control, option, part, widget);
        const auto* slider = qstyleoption_cast<const QStyleOptionSlider*>(option);
        if (!slider || slider->orientation != Qt::Horizontal)
            return QProxyStyle::subControlRect(control, option, part, widget);
        const auto area = option->rect;
        if (part == SC_SliderGroove)
            return {area.x(), area.y() + (area.height() - m.track) / 2, area.width(), m.track};
        if (part == SC_SliderHandle) {
            const int travel = qMax(0, area.width() - m.knobWidth);
            const int position = QStyle::sliderPositionFromValue(slider->minimum, slider->maximum,
                                                                  slider->sliderPosition, travel, slider->upsideDown);
            return {area.x() + position, area.y() + (area.height() - m.knobHeight) / 2,
                    m.knobWidth, m.knobHeight};
        }
        return QProxyStyle::subControlRect(control, option, part, widget);
    }
    void drawComplexControl(ComplexControl control, const QStyleOptionComplex* option,
                            QPainter* painter, const QWidget* widget) const override {
        if (control != CC_Slider) { QProxyStyle::drawComplexControl(control, option, painter, widget); return; }
        const auto* slider = qstyleoption_cast<const QStyleOptionSlider*>(option);
        if (!slider || slider->orientation != Qt::Horizontal) {
            QProxyStyle::drawComplexControl(control, option, painter, widget);
            return;
        }
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        if (!(option->state & State_Enabled)) painter->setOpacity(0.45);
        const auto groove = subControlRect(control, option, SC_SliderGroove, widget);
        const auto knob = subControlRect(control, option, SC_SliderHandle, widget);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(m.trackColor));
        painter->drawRoundedRect(QRectF(groove), m.track / 2.0, m.track / 2.0);
        const qreal fraction = slider->maximum > slider->minimum
            ? qreal(slider->sliderPosition - slider->minimum) / (slider->maximum - slider->minimum) : 0;
        const qreal travel = qMax(0, groove.width() - m.knobWidth);
        const qreal position = fraction * travel;
        const qreal ramp = qMin(qreal(m.knobWidth / 2 + 1), travel / 2);
        qreal fill = position + m.knobWidth / 2.0;
        if (ramp > 0 && position < ramp) fill = position / ramp * (ramp + m.knobWidth / 2.0);
        else if (ramp > 0 && travel - position < ramp)
            fill = groove.width() - (travel - position) / ramp * (ramp + m.knobWidth / 2.0);
        painter->setBrush(QColor(t::accent));
        painter->drawRoundedRect(QRectF(groove.x(), groove.y(), fill, groove.height()),
                                 m.track / 2.0, m.track / 2.0);
        painter->setBrush(QColor(0, 0, 0, 90));
        painter->drawRoundedRect(QRectF(knob).translated(0, 1), m.knobRadius, m.knobRadius);
        painter->setBrush(option->state & State_Sunken ? QColor(Qt::white) : QColor(m.knobColor));
        painter->drawRoundedRect(QRectF(knob), m.knobRadius, m.knobRadius);
        painter->restore();
    }
private:
    Metrics m;
};

// An icon's primitives, parsed once from its bundled SVG on the UI thread.
struct ParsedIcon {
    struct Shape { QPainterPath path; bool filled{}; };
    qreal stroke{};
    std::vector<Shape> shapes;
};

const ParsedIcon& parsedIcon(const QString& symbol) {
    static std::map<QString, ParsedIcon> cache; // Node-based: returned references stay valid.
    if (const auto found = cache.find(symbol); found != cache.end()) return found->second;
    ParsedIcon icon;
    QFile file(":/assets/icons/lucide/" + symbol + ".svg");
    if (!file.open(QIODevice::ReadOnly)) qWarning("Layers panel icon %s is missing", qPrintable(symbol));
    QXmlStreamReader xml(&file);
    auto number = [](const QXmlStreamAttributes& attributes, const char* key) {
        return attributes.value(QLatin1StringView(key)).toDouble();
    };
    while (file.isOpen() && !xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement()) continue;
        const auto name = xml.name();
        const auto a = xml.attributes();
        if (name == u"svg") { icon.stroke = a.value(QLatin1StringView("stroke-width")).toDouble(); continue; }
        ParsedIcon::Shape shape;
        shape.filled = a.value(QLatin1StringView("fill")) == u"currentColor";
        if (name == u"line") {
            shape.path.moveTo(number(a, "x1"), number(a, "y1"));
            shape.path.lineTo(number(a, "x2"), number(a, "y2"));
        } else if (name == u"rect") {
            shape.path.addRoundedRect(QRectF(number(a, "x"), number(a, "y"), number(a, "width"), number(a, "height")),
                                      number(a, "rx"), number(a, "rx"));
        } else if (name == u"circle") {
            shape.path.addEllipse(QPointF(number(a, "cx"), number(a, "cy")), number(a, "r"), number(a, "r"));
        } else if (name == u"polygon" || name == u"polyline") {
            const auto values = a.value(QLatin1StringView("points")).toString().split(QRegularExpression("[,\\s]+"), Qt::SkipEmptyParts);
            QPolygonF points;
            for (int i = 0; i + 1 < values.size(); i += 2) points << QPointF(values[i].toDouble(), values[i + 1].toDouble());
            shape.path.addPolygon(points);
            if (name == u"polygon") shape.path.closeSubpath();
        } else {
            // The painter supports only these SVG primitives; report anything it would drop.
            qWarning("Layers panel icon %s uses unsupported SVG element <%s>", qPrintable(symbol),
                     qPrintable(name.toString()));
            continue;
        }
        icon.shapes.push_back(std::move(shape));
    }
    return cache.emplace(symbol, std::move(icon)).first->second;
}

void paintSymbol(QPainter& painter, const QString& symbol, QRectF bounds, const QColor& color) {
    static const bool resourcesReady = [] { initializeLayerPanelIcons(); return true; }();
    Q_UNUSED(resourcesReady);
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    const qreal side = qMin(bounds.width(), bounds.height());
    painter.translate(bounds.x() + (bounds.width() - side) / 2, bounds.y() + (bounds.height() - side) / 2);
    painter.scale(side / 24, side / 24);
    painter.setPen(QPen(color, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    if (symbol == "checker-circle") {
        painter.drawEllipse(QRectF(2, 2, 20, 20));
        QPainterPath disc;
        disc.addEllipse(QRectF(5.8, 5.8, 12.4, 12.4));
        painter.setClipPath(disc);
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        constexpr qreal square = 12.4 / 4;
        for (int row = 0; row < 4; ++row)
            for (int column = 0; column < 4; ++column)
                if ((row + column) % 2 == 0)
                    painter.drawRect(QRectF(5.8 + column * square, 5.8 + row * square, square, square));
        painter.restore();
        return;
    }
    if (symbol == "circle") {
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawPie(QRectF(2, 2, 20, 20), 90 * 16, 180 * 16);
        painter.setPen(QPen(color, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
    }
    const auto& icon = parsedIcon(symbol);
    if (icon.stroke > 0) painter.setPen(QPen(color, icon.stroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    for (const auto& shape : icon.shapes) {
        painter.setBrush(shape.filled ? QBrush(color) : QBrush(Qt::NoBrush));
        painter.drawPath(shape.path);
    }
    painter.restore();
}
}

QFont layerPanelFont(int pixels, QFont::Weight weight) { return panelFont(pixels, weight); }

IconButton::IconButton(const QString& symbol, QWidget* parent) : QToolButton(parent), symbol_(symbol), iconSize_(t::footerIconSize) {
    setFixedSize(t::headerIcon, t::headerIcon);
    setProperty("active", false);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setAutoRaise(true);
}
void IconButton::setSymbol(const QString& symbol) { symbol_ = symbol; update(); }
void IconButton::setActive(bool active) { if (active_ != active) { active_ = active; setProperty("active", active); update(); } }
void IconButton::changeEvent(QEvent* event) { QToolButton::changeEvent(event); update(); }
QColor IconButton::inkColor() const {
    if (!isEnabled()) return QColor(t::disabled);
    if (active_) return QColor(t::accent);
    return QColor(property("footer").toBool() ? t::primary : t::secondary);
}
void IconButton::paintSurface(QPainter& painter) const {
    painter.setRenderHint(QPainter::Antialiasing);
    if (isEnabled() && (isDown() || underMouse())) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(isDown() ? t::pressed : t::hover));
        painter.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 5, 5);
    }
    if (hasFocus()) {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(t::accent), 1));
        painter.drawRoundedRect(rect().adjusted(1, 1, -2, -2), 5, 5);
    }
}
void IconButton::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    paintSurface(painter);
    paintSymbol(painter, symbol_, QRectF((width() - iconSize_) / 2.0, (height() - iconSize_) / 2.0,
                                        iconSize_, iconSize_), inkColor());
}

SearchField::SearchField(QWidget* parent) : QWidget(parent), edit_(new QLineEdit(this)) {
    setFixedHeight(t::searchHeight);
    setMinimumWidth(0);
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(t::searchLeading + t::searchIconFrame + t::searchGap, 0, t::searchTrailing, 0);
    edit_->setObjectName("layerSearch");
    edit_->setAccessibleName("Search layers by name");
    edit_->setPlaceholderText("Search Layers");
    edit_->setFrame(false);
    edit_->setFont(panelFont(t::searchFont));
    edit_->setStyleSheet(QString("QLineEdit { background: transparent; border: 0; padding: 0; color: %1;"
                                 "font-family: '%3'; font-size: %4px; }"
                                 "QLineEdit:disabled { color: %2; }")
                             .arg(t::primary, t::disabled, panelFont(t::searchFont).family()).arg(t::searchFont));
    layout->addWidget(edit_);
}
void SearchField::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(t::accent), t::separatorWidth));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), t::searchRadius, t::searchRadius);
    paintSymbol(painter, "search", QRectF(t::searchLeading, (height() - t::searchIconFrame) / 2.0,
                                          t::searchIconFrame, t::searchIconFrame), QColor(t::secondary));
}

namespace {
// Liquid Glass for a popup window: when it is about to appear, the screen area it will cover is captured and blurred
// lightly (shrinking with area averaging and enlarging smoothly keeps the shapes and text behind it readable). Its
// paint becomes that blur under a translucent neutral gray tint (the #1f1f1f surfaces), a soft highlight along the top
// and a light rim, clipped to a rounded panel; outside the panel the window stays transparent. The view behind is
// taken once per opening.
class GlassPopupPainter final : public QObject {
public:
    explicit GlassPopupPainter(QWidget* popup) : QObject(popup), popup_(popup) { popup->installEventFilter(this); }
protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched != popup_) return false;
        if (event->type() == QEvent::Show) capture();
        else if (event->type() == QEvent::Paint) {
            paint();
            // The popup's own paint (for a combo box list, Qt's opaque menu panel) would cover the glass.
            return true;
        }
        return false;
    }
private:
    void capture() {
        backdrop_ = {};
        const QRect area = popup_->geometry();
        auto* screen = QGuiApplication::screenAt(area.center());
        if (!screen) screen = popup_->screen();
        if (!screen || area.isEmpty()) return;
        const QRect local = area.translated(-screen->geometry().topLeft());
        const QImage behind = screen->grabWindow(0, local.x(), local.y(), local.width(), local.height()).toImage();
        if (behind.isNull()) return;
        const QSize reduced = (behind.size() / glassBlurReduction).expandedTo(QSize(1, 1));
        backdrop_ = QPixmap::fromImage(behind.scaled(reduced, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                                           .scaled(behind.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    }
    void paint() {
        QPainter painter(popup_);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        const QRectF panel = QRectF(popup_->rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath shape;
        shape.addRoundedRect(panel, glassPopupRadius - 0.5, glassPopupRadius - 0.5);
        painter.save();
        painter.setClipPath(shape);
        if (!backdrop_.isNull()) painter.drawPixmap(popup_->rect(), backdrop_);
        QLinearGradient tint(panel.topLeft(), panel.bottomLeft());
        tint.setColorAt(0, QColor(90, 90, 90, 120));
        tint.setColorAt(0.25, QColor(44, 44, 44, 105));
        tint.setColorAt(1, QColor(31, 31, 31, 125));
        painter.fillPath(shape, tint);
        painter.restore();
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(255, 255, 255, 46), 1));
        painter.drawPath(shape);
    }
    QWidget* popup_;
    QPixmap backdrop_;
};
}

void makeGlassPopup(QWidget* popup) {
    popup->setWindowFlags(popup->windowFlags() | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    popup->setAttribute(Qt::WA_TranslucentBackground);
    new GlassPopupPainter(popup);
}

CapsuleDropdown::CapsuleDropdown(QWidget* parent) : QComboBox(parent) {
    setMinimumWidth(t::blendMinimumWidth);
    setFixedHeight(t::blendHeight);
    setFont(panelFont(t::searchFont));
    view()->setItemDelegate(new BlendGroupDelegate(this, view()));
    setStyleSheet(QString(
        "QComboBox { background: %1; border: 0; border-radius: %2px; padding-left: %8px; padding-right: %9px; color: %3;"
        "font-family: '%11'; font-size: %12px; }"
        "QComboBox:hover { background: %4; } QComboBox:disabled { color: %5; }"
        "QComboBox::drop-down { border: 0; width: %10px; } QComboBox::down-arrow { image: none; }"
        "QComboBox QAbstractItemView { background: %6; color: %3; border: 1px solid %7; padding: 4px; }"
        "QComboBox QAbstractItemView::item { min-height: 24px; padding: 3px 8px; }")
        .arg(t::control).arg(t::blendHeight / 2).arg(t::primary).arg(t::pressed).arg(t::disabled).arg(t::popup).arg(t::separator)
        .arg(t::blendLeading).arg(t::blendTrailing).arg(t::blendArrowWidth)
        .arg(panelFont(t::searchFont).family()).arg(t::searchFont));
    connect(this, &QComboBox::highlighted, this, [this](int index) { if (open_ && preview) preview(index); });
}
void CapsuleDropdown::useGlassPopup() {
    setProperty("glassPopup", true);
    makeGlassPopup(view()->window());
    view()->viewport()->setAutoFillBackground(false);
    setStyleSheet(styleSheet() +
        "QComboBox QAbstractItemView { background: transparent; border: 0; padding: 6px; outline: 0; }"
        "QComboBox QAbstractItemView::item { min-height: 28px; padding: 3px 8px; }");
}
void CapsuleDropdown::dismiss() { if (!open_) return; open_ = false; if (dismissed) dismissed(); }
bool CapsuleDropdown::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::Hide && watched == view()->window()) dismiss();
    return QComboBox::eventFilter(watched, event);
}
void CapsuleDropdown::showPopup() { view()->window()->installEventFilter(this); open_ = true; QComboBox::showPopup(); }
void CapsuleDropdown::hidePopup() { QComboBox::hidePopup(); dismiss(); }
void CapsuleDropdown::paintEvent(QPaintEvent* event) {
    QComboBox::paintEvent(event);
    QPainter painter(this);
    const auto color = isEnabled() ? QColor(t::primary) : QColor(t::disabled);
    const qreal x = width() - t::blendChevronTrailing;
    const qreal size = t::blendChevronSize;
    painter.save();
    painter.translate(x, t::blendChevronTop + size / 2);
    painter.rotate(180);
    paintSymbol(painter, "chevron-down", QRectF(-size / 2, -size / 2, size, size), color);
    painter.restore();
    paintSymbol(painter, "chevron-down", QRectF(x - size / 2, t::blendChevronBottom, size, size), color);
}

CapsuleButton::CapsuleButton(QWidget* parent) : QToolButton(parent) {
    setMinimumWidth(t::blendMinimumWidth);
    setFixedHeight(t::blendHeight);
    setFont(panelFont(t::searchFont));
    setToolButtonStyle(Qt::ToolButtonTextOnly);
}
void CapsuleButton::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const bool hot = isEnabled() && (underMouse() || isDown());
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(hot ? t::pressed : t::control));
    painter.drawRoundedRect(QRectF(rect()), t::blendHeight / 2.0, t::blendHeight / 2.0);
    const auto color = isEnabled() ? QColor(t::primary) : QColor(t::disabled);
    painter.setPen(color);
    painter.setFont(font());
    const QRect textArea(t::blendLeading, 0, width() - t::blendLeading - t::blendTrailing, height());
    painter.drawText(textArea, Qt::AlignLeft | Qt::AlignVCenter, fontMetrics().elidedText(text(), Qt::ElideRight, textArea.width()));
    const qreal x = width() - t::blendChevronTrailing;
    const qreal size = t::blendChevronSize;
    painter.save();
    painter.translate(x, t::blendChevronTop + size / 2);
    painter.rotate(180);
    paintSymbol(painter, "chevron-down", QRectF(-size / 2, -size / 2, size, size), color);
    painter.restore();
    paintSymbol(painter, "chevron-down", QRectF(x - size / 2, t::blendChevronBottom, size, size), color);
}

Slider::Slider(Qt::Orientation orientation, QWidget* parent) : TrackSlider(orientation, parent) {
    setFixedHeight(t::sliderHeight);
    setMinimumWidth(t::sliderMinimumWidth);
    // QWidget::setStyle does not take ownership.
    auto* style = new SliderStyle;
    style->setParent(this);
    setStyle(style);
    setStyleSheet(QString("QSlider::sub-page:horizontal { background: %1; }").arg(t::accent));
}

OptionSlider::OptionSlider(Qt::Orientation orientation, QWidget* parent) : TrackSlider(orientation, parent) {
    setFixedHeight(t::optionSliderHeight);
    setMinimumWidth(t::sliderMinimumWidth);
    auto* style = new SliderStyle({t::optionSliderTrack, t::optionKnobWidth, t::optionKnobHeight, t::optionKnobHeight / 2.0, t::optionTrack, t::primary});
    style->setParent(this);
    setStyle(style);
    setStyleSheet(QString("QSlider::sub-page:horizontal { background: %1; }").arg(t::accent));
}

PercentField::PercentField(QWidget* parent) : QLineEdit(parent) {
    setFixedWidth(t::percentWidth);
    setFixedHeight(t::percentHeight);
    setFont(panelFont(t::searchFont));
    setAlignment(Qt::AlignCenter);
    setStyleSheet(QString(
        "QLineEdit { background: %1; color: %2; border: 1px solid %3; border-radius: %6px; padding: 2px;"
        "font-family: '%7'; font-size: %8px; }"
        "QLineEdit:focus { border-color: %4; } QLineEdit:disabled { color: %5; }")
        .arg(t::field, t::primary, t::separator, t::accent, t::disabled).arg(t::fieldRadius)
        .arg(panelFont(t::searchFont).family()).arg(t::searchFont));
}

Divider::Divider(QWidget* parent) : QFrame(parent) {
    setFixedHeight(t::separatorWidth);
    setFrameShape(QFrame::NoFrame);
    setStyleSheet(QString("background: %1; border: 0;").arg(t::separator));
}

FooterIconButton::FooterIconButton(const QString& symbol, QWidget* parent) : IconButton(symbol, parent) {
    setProperty("footer", true);
    setFixedSize(t::footerIconSize + 2 * t::footerIconHorizontal,
                 t::footerIconSize + 2 * t::footerIconVertical);
}
FooterMenuButton::FooterMenuButton(const QString& symbol, QWidget* parent) : IconButton(symbol, parent) {
    setProperty("footer", true);
    setFixedSize(t::footerIconSize + t::footerChevronGap + t::footerChevronSize + 2 * t::footerMenuHorizontal,
                 t::footerIconSize + 2 * t::footerMenuVertical);
    connect(this, &QToolButton::clicked, this, [this] {
        if (auto* popup = menu()) {
            popup->adjustSize();
            popup->popup(mapToGlobal(QPoint(t::footerMenuHorizontal, -popup->height() - t::footerMenuGapAbove)));
        }
    });
}
void FooterMenuButton::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    paintSurface(painter);
    const auto color = inkColor();
    paintSymbol(painter, symbol_, QRectF(t::footerMenuHorizontal, (height() - t::footerIconSize) / 2.0,
                                        t::footerIconSize, t::footerIconSize), color);
    paintSymbol(painter, "chevron-down",
                QRectF(t::footerMenuHorizontal + t::footerIconSize + t::footerChevronGap,
                       (height() - t::footerChevronSize) / 2.0, t::footerChevronSize, t::footerChevronSize), color);
}

EmptyState::EmptyState(QWidget* parent) : QWidget(parent) {
    setDetail("Create a canvas or import a file.");
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}
void EmptyState::setDetail(const QString& detail) { detail_ = detail; update(); }
void EmptyState::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const int iconTop = height() / 2 - 34;
    paintSymbol(painter, "layers", QRectF((width() - t::emptyIcon) / 2.0, iconTop, t::emptyIcon, t::emptyIcon), QColor(t::secondary));
    painter.setPen(QColor(t::secondary));
    painter.setFont(panelFont(t::calloutFont, QFont::Medium));
    painter.drawText(QRect(t::emptyPadding, iconTop + t::emptyIcon + t::emptySpacing,
                           width() - 2 * t::emptyPadding, 18), Qt::AlignCenter, "No layers yet");
    painter.setFont(panelFont(t::captionFont));
    painter.drawText(QRect(t::emptyPadding, iconTop + t::emptyIcon + t::emptySpacing + 24,
                           width() - 2 * t::emptyPadding, 40), Qt::AlignHCenter | Qt::TextWordWrap, detail_);
}
}
