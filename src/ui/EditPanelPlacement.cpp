#include "EditPanelPlacement.h"
#include <QEvent>
#include <QGuiApplication>
#include <QPointer>
#include <QScreen>
#include <QTimer>
#include <QVariant>
#include <QWidget>
#include <QWindow>
#include <algorithm>

namespace compositor::ui {
namespace {
std::optional<EditPanelCorner> lastCorner;
QRect screenFor(QRect frame,const QList<QRect>& screens,QRect fallbackScreen){
    QRect best=fallbackScreen;qint64 bestArea=0;
    for(const auto& screen:screens){
        const auto shared=screen.intersected(frame);
        const auto area=shared.isEmpty()?qint64(0):qint64(shared.width())*shared.height();
        if(area>bestArea){best=screen;bestArea=area;}
    }
    return best;
}
QList<QRect> availableScreens(){
    QList<QRect> result;
    for(const auto* screen:QGuiApplication::screens())result.append(screen->availableGeometry());
    return result;
}
QRect primaryAvailable(){
    const auto* screen=QGuiApplication::primaryScreen();
    return screen?screen->availableGeometry():QRect();
}
// A window the Mac-style chrome made frameless keeps its former title bar in QWidget's frame
// margins, so move() and frameGeometry() are only right for windows with a native frame.
bool frameless(const QWidget* panel){return panel->windowFlags().testFlag(Qt::FramelessWindowHint);}
QRect frameOf(const QWidget* panel){return frameless(panel)?panel->geometry():panel->frameGeometry();}
constexpr auto openedAt="compositorEditPanelOpenedAt";
void moveToSharedPosition(QWidget* panel,const QWidget* canvas){
    // Qt reports the native margins reliably only while the window is shown.
    const auto margins=frameless(panel)?QMargins():panel->windowHandle()->frameMargins();
    const auto frame=panel->size().grownBy(margins);
    std::optional<QRect> area;
    auto fallback=primaryAvailable();
    if(canvas){
        area=QRect(canvas->mapToGlobal(QPoint(0,0)),canvas->size());
        if(const auto* screen=QGuiApplication::screenAt(area->center()))fallback=screen->availableGeometry();
    }
    const auto position=editPanelFrame(frame,lastCorner,area,availableScreens(),fallback).topLeft();
    // move() places the frame; it avoids a rounding pass through the client origin at fractional scaling.
    if(frameless(panel))panel->setGeometry({position,panel->size()});
    else panel->move(position);
}
// At the first Show event Qt has sized the panel and created its native window, before it is on
// screen. Sizing it any earlier collapses the dialogs the application chrome wraps.
class PlaceOnShow final:public QObject {
    QPointer<const QWidget> canvas_;
public:
    PlaceOnShow(QWidget* panel,const QWidget* canvas):QObject(panel),canvas_(canvas){panel->installEventFilter(this);}
    bool eventFilter(QObject* watched,QEvent* event)override{
        if(event->type()!=QEvent::Show)return false;
        watched->removeEventFilter(this);
        auto* panel=static_cast<QWidget*>(watched);
        moveToSharedPosition(panel,canvas_);
        // Read once show() has returned: fractional scaling can settle a pixel or two away.
        QTimer::singleShot(0,panel,[panel]{if(panel->isVisible())panel->setProperty(openedAt,frameOf(panel).topLeft());});
        deleteLater();
        return false;
    }
};
}
QRect editPanelFrame(QSize frame,const std::optional<EditPanelCorner>& saved,const std::optional<QRect>& canvas,const QList<QRect>& screens,QRect fallbackScreen){
    QPoint position;
    if(saved)position={saved->right?saved->corner.x()-frame.width():saved->corner.x(),saved->corner.y()};
    // The first panel straddles the canvas's right edge, so the middle of the picture stays visible.
    else if(canvas)position={canvas->x()+canvas->width()-frame.width()/2,canvas->y()+(canvas->height()-frame.height())/2};
    else position={fallbackScreen.x()+(fallbackScreen.width()-frame.width())/2,fallbackScreen.y()+(fallbackScreen.height()-frame.height())/2};
    const auto screen=screenFor({position,frame},screens,fallbackScreen);
    // The lower bound is applied last: a panel larger than the screen keeps its title bar reachable.
    position.setX(std::max(screen.x(),std::min(position.x(),screen.x()+screen.width()-frame.width())));
    position.setY(std::max(screen.y(),std::min(position.y(),screen.y()+screen.height()-frame.height())));
    return {position,frame};
}
EditPanelCorner editPanelCorner(QRect frame,const QList<QRect>& screens,QRect fallbackScreen){
    const auto screen=screenFor(frame,screens,fallbackScreen);
    const bool right=2*frame.x()+frame.width()>2*screen.x()+screen.width();
    return {{right?frame.x()+frame.width():frame.x(),frame.y()},right};
}
void placeEditPanel(QWidget* panel,const QWidget* canvas){
    new PlaceOnShow(panel,canvas);
}
void rememberEditPanel(const QWidget* panel){
    const auto frame=frameOf(panel);
    // Only a move changes the shared position. Otherwise scaling round-off would make it creep from
    // one panel to the next, and a panel pushed back on screen would move all the following ones.
    const auto opened=panel->property(openedAt);
    if(lastCorner&&opened.isValid()&&opened.toPoint()==frame.topLeft())return;
    lastCorner=editPanelCorner(frame,availableScreens(),primaryAvailable());
}
}
