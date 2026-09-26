#include "SelectionCursors.h"
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QThread>
#include <array>
#include <cmath>
#include <list>
#include <stdexcept>

namespace compositor::ui {
namespace {
constexpr QPoint hotSpot{7,7};
void outlined(QPainter& painter,const QPainterPath& path,qreal whiteWidth,qreal blackWidth,bool dashed=false){
    for(const auto& stroke:std::array<std::pair<QColor,qreal>,2>{{{Qt::white,whiteWidth},{Qt::black,blackWidth}}}){
        QPen pen(stroke.first,stroke.second,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin);
        if(dashed){pen.setCapStyle(Qt::FlatCap);pen.setDashPattern({2/stroke.second,1.5/stroke.second});}
        painter.strokePath(path,pen);
    }
}
void toolIcon(QPainter& painter,editing::LassoKind kind,const QRectF& box){
    QPainterPath path;
    if(kind==editing::LassoKind::Polygonal){
        const qreal unit=box.width()/18;
        const auto point=[&](qreal x,qreal y){return QPointF(box.x()+x*unit,box.y()+y*unit);};
        path.moveTo(point(1.2,7));for(const auto p:std::array<QPointF,5>{{{4,2.4},{11.8,1.8},{16.8,5.2},{15.6,10.4},{7,11.6}}})path.lineTo(point(p.x(),p.y()));path.closeSubpath();
        path.moveTo(point(8.9,10.9));path.lineTo(point(13.3,10.5));path.lineTo(point(11.6,14.5));path.closeSubpath();
        path.moveTo(point(11.6,14.5));path.lineTo(point(12.9,17.3));
        outlined(painter,path,1.4*unit+2,1.4*unit);return;
    }
    if(kind==editing::LassoKind::Rectangle){path.addRect(QRectF(box.x(),box.y()+1.5,box.width(),box.height()-3));outlined(painter,path,3.2,1.2,true);return;}
    if(kind==editing::LassoKind::Ellipse){path.addEllipse(box);outlined(painter,path,3.2,1.2,true);return;}
    // Local lasso symbol: an irregular closed loop and its crossing loose tail.
    // SF Symbols are not shipped in the Windows build, so no glyph-pixel claim is made.
    path.moveTo(box.x()+1,box.y()+5);
    path.cubicTo(box.x()+.5,box.y()+1,box.x()+10,box.y(),box.x()+11,box.y()+4);
    path.cubicTo(box.x()+13,box.y()+8,box.x()+6,box.y()+10,box.x()+2,box.y()+8);
    path.cubicTo(box.x()+.5,box.y()+7,box.x()+.5,box.y()+6,box.x()+1,box.y()+5);
    path.moveTo(box.x()+6,box.y()+7);path.cubicTo(box.x()+4,box.y()+8,box.x()+6,box.y()+10,box.x()+8,box.y()+9);
    path.cubicTo(box.x()+10,box.y()+8,box.x()+8,box.y()+6,box.x()+7,box.y()+8);
    path.cubicTo(box.x()+6,box.y()+10,box.x()+9,box.y()+11,box.x()+8,box.y()+12);
    outlined(painter,path,3.2,1.2);
}
QCursor makeCursor(editing::LassoKind kind,editing::SelectionMode mode,qreal scale){
    QImage image(qRound(44*scale),qRound(36*scale),QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);
    QPainter painter(&image);painter.setRenderHint(QPainter::Antialiasing);painter.scale(scale,scale);
    // A Windows vector crosshair with a fixed logical hot spot. Its white outline
    // remains visible over dark pixels; system crosshair bitmaps differ by platform.
    QPainterPath cross;
    cross.moveTo(0,7);cross.lineTo(5,7);cross.moveTo(9,7);cross.lineTo(14,7);
    cross.moveTo(7,0);cross.lineTo(7,5);cross.moveTo(7,9);cross.lineTo(7,14);
    outlined(painter,cross,3.2,1.2);
    const QRectF box(hotSpot.x()+7,hotSpot.y()+7,12,12);toolIcon(painter,kind,box);
    if(mode!=editing::SelectionMode::Replace){
        const QPointF center(box.right()+5,box.center().y());QPainterPath badge;
        badge.moveTo(center.x()-3,center.y());badge.lineTo(center.x()+3,center.y());
        if(mode==editing::SelectionMode::Add){badge.moveTo(center.x(),center.y()-3);badge.lineTo(center.x(),center.y()+3);}
        outlined(painter,badge,3.2,1.2);
    }
    painter.end();auto pixels=QPixmap::fromImage(image);pixels.setDevicePixelRatio(scale);return QCursor(pixels,hotSpot.x(),hotSpot.y());
}
int iconIndex(editing::LassoKind kind){switch(kind){case editing::LassoKind::Freehand:return 0;case editing::LassoKind::Polygonal:return 1;case editing::LassoKind::Rectangle:return 2;case editing::LassoKind::Ellipse:return 3;}throw std::invalid_argument("Unknown selection cursor tool");}
struct ScaleCache {qreal scale;std::array<QCursor,12> cursors;};
}
QCursor selectionToolCursor(editing::LassoKind kind,editing::SelectionMode mode,qreal deviceScale){
    if(!QGuiApplication::instance()||QThread::currentThread()!=QGuiApplication::instance()->thread())throw std::logic_error("Selection cursor requires the GUI thread");
    if(!std::isfinite(deviceScale)||deviceScale<.5||deviceScale>8)throw std::invalid_argument("Invalid selection cursor scale");
    const int icon=iconIndex(kind);const int selected=int(mode);if(selected<0||selected>2)throw std::invalid_argument("Unknown selection cursor mode");
    static std::list<ScaleCache> cache;
    for(auto it=cache.begin();it!=cache.end();++it)if(it->scale==deviceScale){cache.splice(cache.begin(),cache,it);return cache.front().cursors[size_t(icon)*3+size_t(selected)];}
    ScaleCache entry{deviceScale,{}};const std::array<editing::LassoKind,4> kinds{editing::LassoKind::Freehand,editing::LassoKind::Polygonal,editing::LassoKind::Rectangle,editing::LassoKind::Ellipse};
    for(size_t k=0;k<4;++k)for(int m=0;m<3;++m)entry.cursors[k*3+size_t(m)]=makeCursor(kinds[k],editing::SelectionMode(m),deviceScale);
    cache.push_front(std::move(entry));if(cache.size()>8)cache.pop_back();return cache.front().cursors[size_t(icon)*3+size_t(selected)];
}
}
