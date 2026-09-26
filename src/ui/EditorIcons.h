#pragma once
#include <QIcon>

namespace compositor::ui {
enum class EditorIcon {
    Move, Marquee, EllipseMarquee, Lasso, Polygon, Wand, Crop, Brush, Eraser,
    Heal, Clone, Retouch, Gradient, Shape, Eyedropper, Hand, Zoom, Group, Mask, Delete,
    Close, Minus, Maximize, Plus, Folder, File, ChevronDown, ChevronUp, ChevronLeft, ChevronRight, Link
};
QIcon editorIcon(EditorIcon icon);
}
