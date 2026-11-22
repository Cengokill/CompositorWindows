#pragma once
#include <functional>

class QAbstractSpinBox;
class QLabel;
class QMainWindow;

namespace compositor::ui {
// Makes the label a drag target for the value of its number field, as in Photoshop: dragging right raises the value and dragging left lowers it.
// Shift drags in fine steps and Ctrl in coarse ones, Escape restores the value from before the drag, and a click without movement
// focuses the field. The field is a QSpinBox or a QDoubleSpinBox; its own valueChanged and editingFinished signals carry the edit.
void attachScrub(QLabel* label, QAbstractSpinBox* field);
// What a cancelled drag needs from the document: how many undo steps exist, and a way to undo one. A field that edits the document live
// (a shape style, say) leaves a step behind, and Escape takes it back so the drag leaves no trace.
struct ScrubHistory { std::function<int()> depth; std::function<void()> undo; };
// Pairs every caption of the options bars with the number field that follows it (and the Zoom title with its field) and attaches the scrub.
void installNumberScrubbing(QMainWindow* window, ScrubHistory history = {});
}
