#pragma once
#include "Selection.h"
#include <string_view>
#include <vector>

namespace compositor::editing {
enum class ShapeKind { Rectangle, Ellipse, Line };
enum class ShapeLineStyle { Solid, Dashed, LongDash, DashDot, LongDashDot, Dotted };
enum class ShapeLineCap { Round, Square };
struct ShapeStyle {
    ShapeKind kind{ShapeKind::Rectangle};
    double red{},green{},blue{},cornerRadius{};
    double lineWidth{4},startX{},startY{},endX{1},endY{1};
    // Rectangle and ellipse only. The stroke is drawn inside the layer bounds, so it never extends past the raster.
    bool strokeEnabled{};
    double strokeWidth{2},strokeRed{},strokeGreen{},strokeBlue{};
    bool smoothing{};
    ShapeLineStyle lineStyle{ShapeLineStyle::Solid};
    ShapeLineCap lineCap{ShapeLineCap::Round};
    bool operator==(const ShapeStyle&) const = default;
};
std::vector<double> shapeLinePattern(ShapeLineStyle);
ShapeStyle decodeShapeStyle(std::string_view);
std::string encodeShapeStyle(const ShapeStyle&);
SelectionOutline rectangleShapeOutline(Rect, const ShapeStyle&, double inset = 0);
std::shared_ptr<const Raster> shapeRaster(const ShapeStyle&,int width,int height);
std::optional<Layer> createShapeLayer(Rect,const ShapeStyle&,std::string name);
std::string nextShapeName(const Document&,ShapeKind);
// Redraw a live shape after updating its transform, preserving its document-pixel
// corner radius. Attached masks receive explicit placement before the grid changes.
Layer redrawShape(const Layer&);
Layer restyleShape(const Layer&,const ShapeStyle&);
std::shared_ptr<const Raster> shapeTransformPreview(const Layer&,const Transform&);
}
