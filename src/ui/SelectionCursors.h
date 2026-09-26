#pragma once
#include "editing/SelectionGesture.h"
#include <QCursor>
namespace compositor::ui {
// Windows vector equivalents of EditorCanvas.selectionCursors (four tools x three modes).
// The system-specific SF Symbols are replaced by recognizable local vector artwork;
// badge geometry and polygon coordinates preserve the pinned source definition.
QCursor selectionToolCursor(editing::LassoKind kind,editing::SelectionMode mode,qreal deviceScale);
}
