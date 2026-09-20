#include "CanvasAccessibility.h"
#include "NativeCanvas.h"
#include <QAccessibleWidget>
#include <mutex>

namespace compositor::ui {
namespace {
QAccessibleInterface* canvasInterface(const QString&,QObject* object){
    // NativeCanvas intentionally has no Q_OBJECT, so its Qt class name is QWidget.
    // RTTI confines the factory to actual canvases without capturing other widgets.
    if(auto* canvas=dynamic_cast<NativeCanvas*>(object))
        return new QAccessibleWidget(canvas,QAccessible::Graphic);
    return nullptr;
}
}
void installCanvasAccessibility(){
    static std::once_flag installed;
    std::call_once(installed,[]{QAccessible::installFactory(canvasInterface);});
}
}
