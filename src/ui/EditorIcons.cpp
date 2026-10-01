#include "EditorIcons.h"
#include <QApplication>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>

namespace compositor::ui {
namespace {
class EditorIconEngine final : public QIconEngine {
public:
    explicit EditorIconEngine(EditorIcon icon) : icon_(icon) {}
    QIconEngine* clone() const override { return new EditorIconEngine(icon_); }
    void paint(QPainter* p, const QRect& rect, QIcon::Mode mode, QIcon::State) override {
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        const auto side = qMin(rect.width(), rect.height());
        p->translate(rect.center().x() - side / 2.0, rect.center().y() - side / 2.0);
        p->scale(side / 24.0, side / 24.0);
        const auto color = QApplication::palette().color(mode == QIcon::Disabled ? QPalette::Disabled : QPalette::Active, QPalette::ButtonText);
        p->setPen(QPen(color, 1.65, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p->setBrush(Qt::NoBrush);
        auto line = [&](double x1, double y1, double x2, double y2) { p->drawLine(QPointF(x1,y1), QPointF(x2,y2)); };
        auto path = [&](std::initializer_list<QPointF> points) {
            QPainterPath shape; bool first = true;
            for (const auto& point : points) { if (first) shape.moveTo(point); else shape.lineTo(point); first = false; }
            p->drawPath(shape);
        };
        switch (icon_) {
        case EditorIcon::Move:
            path({{4,10},{4,4},{10,4}}); line(4,4,10,10);
            path({{14,20},{20,20},{20,14}}); line(14,14,20,20); break;
        case EditorIcon::Marquee:
        case EditorIcon::EllipseMarquee: {
            auto pen = p->pen(); pen.setDashPattern({2,2}); p->setPen(pen);
            if (icon_ == EditorIcon::Marquee) p->drawRoundedRect(QRectF(3,4,18,16),1,1);
            else p->drawEllipse(QRectF(3,3,18,18));
            break;
        }
        case EditorIcon::Lasso: {
            QPainterPath shape; shape.moveTo(16,17); shape.cubicTo(25,11,19,3,11,4);
            shape.cubicTo(2,5,1,13,6,16); shape.cubicTo(10,19,17,16,15,14);
            shape.cubicTo(13,12,11,17,15,19); shape.cubicTo(18,21,18,22,16,22);
            p->drawPath(shape); break;
        }
        case EditorIcon::Polygon:
            path({{5,5},{20,8},{15,20},{4,16},{5,5}});
            for (const auto& point : {QPointF(5,5),QPointF(20,8),QPointF(15,20),QPointF(4,16)}) {
                p->setBrush(QApplication::palette().color(QPalette::Window)); p->drawRect(QRectF(point-QPointF(1.5,1.5),QSizeF(3,3)));
            } break;
        case EditorIcon::Wand:
            path({{5,10},{8,7},{21,20},{18,23},{5,10}}); line(10,9,7,12);
            line(5,2,5,6); line(3,4,7,4); line(16,2,16,6); line(14,4,18,4);
            line(3,15,3,19); line(1,17,5,17); break;
        case EditorIcon::Crop:
            path({{6,2},{6,18},{22,18}}); path({{2,6},{18,6},{18,22}}); line(9,15,21,3); break;
        case EditorIcon::Brush: {
            QPainterPath shape; shape.moveTo(9,14); shape.cubicTo(10,10,18,3,21,3);
            shape.cubicTo(21,6,15,14,12,16); shape.closeSubpath(); p->drawPath(shape);
            shape = {}; shape.moveTo(10,15); shape.cubicTo(3,12,7,20,2,21);
            shape.cubicTo(8,23,13,20,12,17); p->drawPath(shape); line(11,12,15,15); break;
        }
        case EditorIcon::Eraser:
            path({{3,14},{13,4},{21,12},{13,20},{9,20},{3,14}}); line(8,9,16,17); line(13,20,21,20); break;
        case EditorIcon::Heal:
            p->translate(12,12); p->rotate(-45); p->drawRoundedRect(QRectF(-4,-10,8,20),3,3);
            p->drawRect(QRectF(-4,-3.5,8,7)); p->setBrush(color);
            for (double y : {-6.5,6.5}) for (double x : {-1.5,1.5}) p->drawEllipse(QPointF(x,y),.35,.35);
            break;
        case EditorIcon::Clone: {
            QPainterPath shape; shape.moveTo(7,15); shape.lineTo(9,13); shape.lineTo(9,9);
            shape.cubicTo(5,3,18,3,15,9); shape.lineTo(15,13); shape.lineTo(17,15);
            shape.closeSubpath(); p->drawPath(shape); p->drawRoundedRect(QRectF(4,15,16,4),1,1); line(4,22,20,22); break;
        }
        case EditorIcon::Retouch: {
            QPainterPath shape; shape.moveTo(12,3); shape.cubicTo(10,7,6,11,6,15);
            shape.cubicTo(6,23,18,23,18,15); shape.cubicTo(18,11,14,7,12,3);
            p->drawPath(shape); break;
        }
        case EditorIcon::Gradient: {
            QLinearGradient gradient(4,0,20,0); auto transparent=color; transparent.setAlpha(0);
            gradient.setColorAt(0,color); gradient.setColorAt(1,transparent);
            p->setBrush(gradient); p->drawRoundedRect(QRectF(4,4,16,16),2,2); break;
        }
        case EditorIcon::Shape:
            p->drawEllipse(QRectF(3,3,13,13)); p->setBrush(QApplication::palette().color(QPalette::Window));
            p->drawRoundedRect(QRectF(9,9,12,12),1.5,1.5); break;
        case EditorIcon::Eyedropper:
            path({{15,5},{19,9},{9,19},{5,19},{5,15},{15,5}});
            line(4,20,6,18); line(13,4,20,11); p->setBrush(color);
            path({{16,5},{19,2},{22,5},{19,8},{16,5}}); break;
        case EditorIcon::Hand: {
            QPainterPath shape; shape.moveTo(7,13); shape.lineTo(7,6); shape.cubicTo(7,3,10,3,10,6);
            shape.lineTo(10,11); shape.lineTo(10,4); shape.cubicTo(10,1,13,1,13,4);
            shape.lineTo(13,11); shape.lineTo(13,5); shape.cubicTo(13,2,16,2,16,5);
            shape.lineTo(16,12); shape.lineTo(16,8); shape.cubicTo(16,5,19,5,19,8);
            shape.lineTo(19,15); shape.cubicTo(19,24,10,23,7,19); shape.lineTo(3,13);
            shape.cubicTo(1,10,4,9,7,13); p->drawPath(shape); break;
        }
        case EditorIcon::Zoom:
            p->drawEllipse(QRectF(3,3,13,13)); line(14,14,21,21); break;
        case EditorIcon::Type:
            line(6,5,18,5); line(12,5,12,19); break;
        case EditorIcon::Group:
            path({{3,20},{3,5},{10,5},{12,8},{21,8},{21,20},{3,20}});
            line(12,11,12,17); line(9,14,15,14); break;
        case EditorIcon::Mask: {
            QPainterPath shape; shape.setFillRule(Qt::OddEvenFill); shape.addRoundedRect(QRectF(3,5,18,14),2,2);
            shape.addEllipse(QRectF(8,8,8,8)); p->setBrush(color); p->setPen(Qt::NoPen); p->drawPath(shape); break;
        }
        case EditorIcon::Delete:
            path({{5,7},{6,21},{18,21},{19,7}}); line(3,5,21,5);
            path({{9,5},{9,2},{15,2},{15,5}}); line(10,9,10,17); line(14,9,14,17); break;
        case EditorIcon::Close: line(7,7,17,17); line(7,17,17,7); break;
        case EditorIcon::Minus: line(6,12,18,12); break;
        case EditorIcon::Maximize: p->drawRoundedRect(QRectF(5,5,14,14),2,2); break;
        case EditorIcon::Plus: line(5,12,19,12);line(12,5,12,19);break;
        case EditorIcon::Folder: path({{3,20},{3,5},{10,5},{12,8},{21,8},{21,20},{3,20}}); break;
        case EditorIcon::File: path({{5,3},{14,3},{19,8},{19,21},{5,21},{5,3}});path({{14,3},{14,8},{19,8}});break;
        case EditorIcon::ChevronDown: path({{7,10},{12,15},{17,10}});break;
        case EditorIcon::ChevronUp: path({{7,14},{12,9},{17,14}});break;
        case EditorIcon::ChevronLeft: path({{14,7},{9,12},{14,17}});break;
        case EditorIcon::ChevronRight: path({{10,7},{15,12},{10,17}});break;
        case EditorIcon::Link:
            p->translate(12,12);p->rotate(-35);p->drawRoundedRect(QRectF(-4,-10,8,12),4,4);
            p->drawRoundedRect(QRectF(-4,-2,8,12),4,4);break;
        }
        p->restore();
    }
    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override {
        QPixmap result(size); result.fill(Qt::transparent); QPainter painter(&result);
        paint(&painter, QRect(QPoint{},size), mode, state); return result;
    }
private:
    EditorIcon icon_;
};
}
QIcon editorIcon(EditorIcon icon) { return QIcon(new EditorIconEngine(icon)); }
}
