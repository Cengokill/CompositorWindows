#pragma once
#include "graphics/BrushSession.h"
#include "retouch/RetouchSession.h"
#include "editing/PixelEdits.h"
#include "editing/Shapes.h"
#include <QColor>

namespace compositor {
// Values preserve the existing tool action/property order.
enum class ProjectTool { Move,Hand,Brush,Eraser,Marquee,Lasso,Polygon,Wand,Gradient,Shape,Crop,CloneStamp,SpotHealing,Blur,Eyedropper,Zoom };

// Session choices for implemented controls. Kept by EditorProject, never in a
// saved Document or History snapshot. Clone alignment, selected layers, group
// collapse and viewport already have project-owned storage in EditorProject.
struct ProjectToolState {
    ProjectTool tool{ProjectTool::Move};
    QColor foreground{Qt::black},background{Qt::white};
    graphics::BrushSessionSettings brushSettings;
    retouch::Settings cloneSettings,blurSettings{retouch::Mode::Liquify};
    retouch::Mode healingMode{retouch::Mode::HealContentAware};
    editing::GradientSettings gradientSettings;
    editing::ShapeStyle shapeStyle;
    editing::SelectionMode selectionMode{editing::SelectionMode::Replace};
    bool ellipse{},selectionAntialias{true};
    int wandTolerance{32},wandSampleRadius{};
    bool wandContiguous{true},wandAllLayers{};
    bool maskPaintWhite{},showSampleRing{true},showPixelGrid{true};
    bool lockRatio{true},autoSelectLayers{},transformControls{true},snapping{true};
    // Continuity for a future Shift-click, not an in-flight pointer gesture.
    std::optional<Point> lastBrushPoint;
    std::string lastBrushLayer;
    bool lastBrushMask{};
};
}
