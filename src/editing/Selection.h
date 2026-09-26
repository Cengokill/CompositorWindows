#pragma once
#include "core/Document.h"
#include <optional>
#include <span>

struct ID2D1SimplifiedGeometrySink;

namespace compositor::editing {
struct Rect {
    double x{}, y{}, width{}, height{};
    bool empty() const { return width <= 0 || height <= 0; }
    bool operator==(const Rect&) const = default;
};
enum class SelectionMode { Replace, Add, Subtract, Intersect };
Rect dragBox(Point anchor, Point point, bool square = false, bool fromCenter = false);
SelectionMode selectionMode(bool shift, bool option, SelectionMode choice = SelectionMode::Replace);
// The caller keeps this immutable outline across a move gesture. Rasterizing the
// preview never discards geometry outside the canvas, so moving back restores it.
class SelectionOutline {
public:
    struct Impl;
    SelectionOutline();
    static SelectionOutline rectangle(Rect, bool antialiased = true);
    static SelectionOutline roundedRectangle(Rect, double radius, bool antialiased = true);
    static SelectionOutline ellipse(Rect, bool antialiased = true);
    static SelectionOutline polygon(std::span<const Point>, bool antialiased = true);
    // Exact pinned wand_trace: every NONZERO pixel is selected; holes retain
    // opposite winding. Soft coverage becomes its binary geometric silhouette.
    static SelectionOutline fromCoverage(const GrayRaster&, bool antialiased = true);
    bool empty() const;
    bool antialiased() const;
    Rect bounds() const;
    bool contains(Point) const;
    // Native no-op criterion: same AA and exact bounds, then empty D2D XOR.
    // Uses the fixed D2D default flattening tolerance0.25 source pixels; this
    // compares filled geometry, not CoreGraphics's structural path equality.
    bool geometricallyEquals(const SelectionOutline&) const;
    SelectionOutline combined(const SelectionOutline&, SelectionMode, bool antialiased) const;
    SelectionOutline clipped(int canvasWidth, int canvasHeight) const;
    SelectionOutline moved(Point offset) const; // rounds offset, never clips
    // Maps source pixel coordinates through the normalized layer/mask transform;
    // preserves vector holes and off-canvas geometry without rasterizing.
    SelectionOutline transformed(const Transform&, int sourceWidth, int sourceHeight) const;
    SelectionOutline resized(double delta, int canvasWidth, int canvasHeight) const;
    SelectionOutline mirrored(bool horizontally, double axis) const;
    SelectionOutline affineMapped(std::array<double,6> matrix)const;
    SelectionOutline projected(const std::function<Point(Point)>&)const;
    std::shared_ptr<const GrayRaster> rasterize(int width, int height) const;
    // Dense bounded region, using the same global Direct2D tile phase.
    std::shared_ptr<const GrayRaster> rasterizeRegion(int x,int y,int width,int height)const;
    std::vector<std::pair<Point,Point>> boundarySegments()const;
    // Retains cubic curves and contour boundaries; the caller owns the sink
    // and closes it. Coordinates are transformed into the caller's view space.
    void writeGeometry(ID2D1SimplifiedGeometrySink*,const std::array<float,6>& documentToView)const;
private:
    std::shared_ptr<const Impl> impl_;
    explicit SelectionOutline(std::shared_ptr<const Impl>);
};
// Shapes are clipped before combination. Subtracting/intersecting no selection
// leaves it absent. An empty result is a present, all-zero selection.
std::optional<SelectionOutline> applySelection(const std::optional<SelectionOutline>& current,
    const SelectionOutline& shape, SelectionMode, int width, int height, bool antialiased = true);
// A degenerate draft deselects in Replace mode and leaves other modes unchanged.
std::optional<SelectionOutline> finishSelection(const std::optional<SelectionOutline>& current,
    const SelectionOutline& draft, SelectionMode, int width, int height, bool antialiased = true);
std::optional<SelectionOutline> inverseSelection(const std::optional<SelectionOutline>& current,
    int width, int height);
std::optional<Selection> rasterSelection(const std::optional<SelectionOutline>&, int width, int height);
bool appendLassoPoint(std::vector<Point>&, Point); // source quarter-pixel filter
Rect coverageBounds(const GrayRaster&); // integer nonzero-pixel bounds
// Preserves and moves Selection.outline when present. Coverage-only callers pass
// the ORIGINAL snapshot on every preview so off-canvas pixels can return intact.
std::optional<Selection> moveSelectionCoverage(const std::optional<Selection>& original, Point offset);
enum class CoverageSampling { Nearest, Linear };
// Maps document coverage to source pixel centers. Absent selection returns null
// unless clipCanvas is requested. Linear interpolation is an explicit native
// candidate; Core Graphics transformed-mask byte equality needs Mac fixtures.
std::shared_ptr<const GrayRaster> mappedCoverage(const Document&, const Transform&, int sourceWidth,
    int sourceHeight, bool clipCanvas = false, CoverageSampling = CoverageSampling::Linear);
}
