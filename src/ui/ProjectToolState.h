#pragma once
#include "graphics/BrushSession.h"
#include "retouch/RetouchSession.h"
#include "editing/PixelEdits.h"
#include "editing/Shapes.h"
#include "editing/SelectionGesture.h"
#include "editing/Snapping.h"
#include "filters/PixelFilters.h"
#include "imaging/subject_matte.h"
#include <QColor>
#include <QJsonObject>
#include <optional>

namespace compositor {
// Values preserve the existing tool action/property order.
enum class ProjectTool { Move,Hand,Brush,Eraser,Marquee,Lasso,Polygon,Wand,Gradient,Shape,Crop,CloneStamp,SpotHealing,Blur,Eyedropper,Zoom,Text };
enum class ProjectBrushMode { Paint,Erase };

// Filters.swift33-83: one remembered bundle per EditorSession. Adjustment JSON
// contains the same typed settings used by the dialog/persistent live layers;
// the bundle itself is neither document content nor undo state.
struct ProjectFilterSettings {
    filters::Settings pixels;
    filters::CameraRawSettings cameraRaw;
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
    std::optional<filters::Kind> lastFilterKind;
    bool ellipse{},selectionAntialias{true};
    int wandTolerance{32},wandSampleRadius{};
    bool wandContiguous{true},wandAllLayers{};
    bool maskPaintWhite{},showSampleRing{true},showPixelGrid{true};
    bool lockRatio{true},autoSelectLayers{true},transformControls{true};
    editing::SnapPolicy snapPolicy;
    editing::LayoutGrid layoutGrid;
    // Continuity for a future Shift-click, not an in-flight pointer gesture.
    std::optional<Point> lastBrushPoint;
    std::string lastBrushLayer;
    bool lastBrushMask{};
    ProjectBrushMode brushMode{ProjectBrushMode::Paint};
};
// Remembered defaults for newly opened projects. Layout grid visibility is
// deliberately excluded; every project starts with its grid hidden.
void applyRememberedSnapAndGrid(editing::SnapPolicy&,editing::LayoutGrid&);
void rememberSnapAndGrid(const editing::SnapPolicy&,const editing::LayoutGrid&);
}
