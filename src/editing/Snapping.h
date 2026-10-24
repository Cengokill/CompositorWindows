#pragma once
#include "Selection.h"
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace compositor::editing {
// Each target is independent. The master Snap switch is `enabled`; when it is
// false the builders below return no lines.
struct SnapPolicy {
    bool enabled{true};
    bool grid{};
    bool guides{true};
    bool layerEdges{true};
    bool layerCenters{true};
    bool documentEdges{true};
    bool documentCenter{true};
    bool operator==(const SnapPolicy&) const = default;
};

// Layout grid in document pixels. Snapping uses every line, major and minor,
// so its step is spacing / subdivisions. Visibility is not part of snapping.
struct LayoutGrid {
    bool visible{};
    double spacing{100};
    int subdivisions{4};
    std::array<uint8_t,3> color{128,128,128};
    double opacity{.35};
    bool operator==(const LayoutGrid&) const = default;
    bool valid() const;
    double step() const { return spacing / subdivisions; }
};

// Snap lines for one document-space frame. A zero grid step disables grid lines on that axis.
struct SnapLines {
    std::vector<double> xs,ys;
    double gridX{},gridY{};
};
struct PointSnap {
    Point point;
    std::optional<double> x,y; // Matched lines, for guide feedback.
};

SnapLines documentSnapLines(Point documentSize, std::span<const Guide> guides, const SnapPolicy&, const LayoutGrid&);
// Adds edges and centers of an axis-aligned box in the order used by the transform snap.
void addBoxSnapLines(SnapLines&, double x, double y, double width, double height, const SnapPolicy&);
// Nearest explicit line or grid line within tolerance. An explicit line wins equal distances;
// otherwise the first listed line wins.
std::optional<double> nearestSnapLine(double value, std::span<const double> lines, double gridStep, double tolerance);
PointSnap snapPoint(Point, const SnapLines&, double tolerance);
}
