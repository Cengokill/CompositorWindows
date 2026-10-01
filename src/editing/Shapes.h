#pragma once
#include "Selection.h"
#include <string_view>

namespace compositor::editing {
enum class ShapeKind { Rectangle, Ellipse, Line };
struct ShapeStyle {
    ShapeKind kind{ShapeKind::Rectangle};
    double red{},green{},blue{},cornerRadius{};
    double lineWidth{4},startX{},startY{},endX{1},endY{1};
    bool operator==(const ShapeStyle&) const = default;
};
ShapeStyle decodeShapeStyle(std::string_view);
std::string encodeShapeStyle(const ShapeStyle&);
std::shared_ptr<const Raster> shapeRaster(const ShapeStyle&,int width,int height);
std::optional<Layer> createShapeLayer(Rect,const ShapeStyle&,std::string name);
std::string nextShapeName(const Document&,ShapeKind);
// Redraw a live shape after updating its transform, preserving its document-pixel
// corner radius. Attached masks receive explicit placement before the grid changes.
Layer redrawShape(const Layer&);
Layer restyleShape(const Layer&,const ShapeStyle&);
std::shared_ptr<const Raster> shapeTransformPreview(const Layer&,const Transform&);
}
