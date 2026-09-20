#pragma once
#include "Selection.h"
#include <string>

namespace compositor::editing {
// Selection.swift and EditorCanvas.swift at the pinned upstream revision.
enum class LassoKind { Freehand, Polygonal, Rectangle, Ellipse };
struct LassoDraft {
    std::vector<Point> points;
    std::optional<Point> cursor;
    SelectionMode mode{SelectionMode::Replace};
    LassoKind kind{LassoKind::Freehand};
    std::optional<Point> anchor;
};
struct SelectionGestureResult {
    std::optional<SelectionOutline> selection;
    std::string historyName;
    // False for a degenerate Add/Subtract or subtracting from absent selection.
    // A host still suppresses an edit whose resulting selection equals its input.
    bool requestsEdit{};
};
enum class PolygonPressResult { Ignored, Extended, FinishRequested };
class SelectionGesture {
public:
    void begin(Point, LassoKind, SelectionMode, bool shiftAtPress = false);
    const std::optional<LassoDraft>& draft() const { return draft_; }
    bool active() const { return draft_.has_value(); }
    bool marquee() const;
    SelectionMode cursorMode(bool shift, bool option, SelectionMode choice) const;
    // Drag events: freehand extends, polygon updates only its rubber endpoint,
    // marquee uses the fresh-Shift latch and always fromCenter=false.
    void move(Point, bool shift = false);
    void moveCursor(std::optional<Point>);
    void modifiersChanged(bool shift);
    // Use VIEW-space distance from the mapped first point, in logical points.
    // A closing click is not appended; call finish() for FinishRequested.
    PolygonPressResult polygonPress(Point, double viewDistanceFromFirst, int clickCount);
    bool extend(Point);
    // Direct session operation preserves the source's programmatic centered box.
    // Canvas code calls move() instead because Option means Subtract there.
    void dragMarquee(Point, bool square, bool fromCenter);
    void removeLast();
    void cancel();
    // Focus loss keeps polygonal drafts; cancels freehand and marquee drafts.
    void interrupt();
    SelectionOutline outline(bool antialiased = true) const;
    // Pure geometry until this operation; neither this class nor finish allocates
    // canvas coverage, mutates a Document, or opens a History transaction.
    std::optional<SelectionGestureResult> finish(const std::optional<SelectionOutline>& current,
        int canvasWidth, int canvasHeight, bool antialiased = true);
private:
    std::optional<LassoDraft> draft_;
    std::optional<Point> marqueeDragPoint_;
    bool constrainArmed_{};
};
// Source dragSelection: Shift chooses the largest axis, choosing X on ties.
// Final whole-pixel rounding belongs to moveSelectionCoverage/outline.moved.
Point selectionMoveOffset(Point start, Point current, bool shift);
// Source autoscroll: 12-point margin, 2+0.4*overshoot, cap40 points/frame.
// Host calls at60Hz, pans, then re-maps its stationary VIEW pointer to document.
Point selectionAutoscrollDelta(Rect visibleView, Point pointerInView);
}
