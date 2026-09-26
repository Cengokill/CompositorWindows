#pragma once
#include "graphics/BrushSession.h"
#include "retouch/RetouchSession.h"
#include "editing/PixelEdits.h"
#include "editing/Shapes.h"
#include "editing/SelectionGesture.h"
#include "filters/PixelFilters.h"
#include "imaging/subject_matte.h"
#include <QColor>
#include <QJsonObject>

namespace compositor {
// Values preserve the existing tool action/property order.
enum class ProjectTool { Move,Hand,Brush,Eraser,Marquee,Lasso,Polygon,Wand,Gradient,Shape,Crop,CloneStamp,SpotHealing,Blur,Eyedropper,Zoom };
enum class ProjectBrushMode { Paint,Erase };

// Filters.swift33-83: one remembered bundle per EditorSession. Adjustment JSON
// contains the same typed settings used by the dialog/persistent live layers;
// the bundle itself is neither document content nor undo state.
struct ProjectFilterSettings {
    filters::Settings pixels;
    imaging::MatteSettings background;
    QJsonObject curves,exposure,gradientMap,grain;
    ProjectFilterSettings();
    std::string beginAdjustment(const QString& kind,QColor foreground,QColor backgroundColor) const;
    void rememberAdjustment(const std::string& json);
};

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
    QString cropRatioChoice{"Free"};
    editing::LassoKind lassoKind{editing::LassoKind::Freehand};
    int selectionExpandAmount{1},selectionContractAmount{1};
    ProjectFilterSettings filterSettings;
    bool ellipse{},selectionAntialias{true};
    int wandTolerance{32},wandSampleRadius{};
    bool wandContiguous{true},wandAllLayers{};
    bool maskPaintWhite{},showSampleRing{true},showPixelGrid{true};
    bool lockRatio{true},autoSelectLayers{},transformControls{true},snapping{true};
    // Continuity for a future Shift-click, not an in-flight pointer gesture.
    std::optional<Point> lastBrushPoint;
    std::string lastBrushLayer;
    bool lastBrushMask{};
    ProjectBrushMode brushMode{ProjectBrushMode::Paint};
};
}
