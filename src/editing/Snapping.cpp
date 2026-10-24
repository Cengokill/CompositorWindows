#include "Snapping.h"
#include <cmath>

namespace compositor::editing {
bool LayoutGrid::valid() const {
    return std::isfinite(spacing) && spacing >= 1 && spacing <= 100000 && subdivisions >= 1 && subdivisions <= 64 && std::isfinite(opacity) && opacity >= 0 && opacity <= 1;
}

SnapLines documentSnapLines(Point size, std::span<const Guide> guides, const SnapPolicy& policy, const LayoutGrid& grid) {
    SnapLines lines;
    if (!policy.enabled) return lines;
    if (policy.documentEdges) lines.xs.push_back(0);
    if (policy.documentCenter) lines.xs.push_back(size.x / 2);
    if (policy.documentEdges) lines.xs.push_back(size.x);
    if (policy.documentEdges) lines.ys.push_back(0);
    if (policy.documentCenter) lines.ys.push_back(size.y / 2);
    if (policy.documentEdges) lines.ys.push_back(size.y);
    if (policy.guides)
        for (const auto& guide : guides) (guide.horizontal ? lines.ys : lines.xs).push_back(guide.position);
    if (policy.grid && grid.valid()) {
        lines.gridX = grid.step();
        lines.gridY = grid.step();
    }
    return lines;
}

void addBoxSnapLines(SnapLines& lines, double x, double y, double width, double height, const SnapPolicy& policy) {
    if (!policy.enabled) return;
    const auto add = [&](std::vector<double>& target, double start, double length) {
        if (policy.layerEdges) target.push_back(std::round(start));
        if (policy.layerCenters) target.push_back(std::round(start + length / 2));
        if (policy.layerEdges) target.push_back(std::round(start + length));
    };
    add(lines.xs, x, width);
    add(lines.ys, y, height);
}

std::optional<double> nearestSnapLine(double value, std::span<const double> lines, double gridStep, double tolerance) {
    if (!std::isfinite(value) || !std::isfinite(tolerance) || tolerance < 0) return {};
    std::optional<double> best;
    double distance = 0;
    for (double line : lines) {
        if (!std::isfinite(line)) continue;
        const double candidate = std::abs(line - value);
        if (candidate <= tolerance && (!best || candidate < distance)) {
            best = line;
            distance = candidate;
        }
    }
    // Grid candidates are computed directly rather than enumerated, so dense grids stay cheap.
    if (std::isfinite(gridStep) && gridStep > 0) {
        const double line = std::round(value / gridStep) * gridStep;
        const double candidate = std::abs(line - value);
        if (candidate <= tolerance && (!best || candidate < distance)) best = line;
    }
    return best;
}

PointSnap snapPoint(Point point, const SnapLines& lines, double tolerance) {
    PointSnap result{point, {}, {}};
    result.x = nearestSnapLine(point.x, lines.xs, lines.gridX, tolerance);
    result.y = nearestSnapLine(point.y, lines.ys, lines.gridY, tolerance);
    if (result.x) result.point.x = *result.x;
    if (result.y) result.point.y = *result.y;
    return result;
}
}
