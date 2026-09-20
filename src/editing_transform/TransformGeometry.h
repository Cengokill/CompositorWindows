#pragma once
// Adapted from Compositor, copyright (c) 2026 Wonder Assembly LLC.
// Distributed under the MIT license in LICENSE in this directory.
#include "core/Document.h"
#include <array>
#include <functional>
#include <span>

namespace compositor::editing_transform {
inline constexpr std::array<Point,8> handleUnits{{{0,0},{.5,0},{1,0},{1,.5},{1,1},{.5,1},{0,1},{0,.5}}};
using Corners=std::array<Point,4>;
struct Rect { double x{},y{},width{},height{}; };
// Coordinates here are logical view pixels (DIPs), not physical framebuffer pixels.
struct ViewMapping {
    double pointsPerPixel{1}; Point origin;
    Point toView(Point documentPoint) const;
    Point toDocument(Point viewPoint) const;
};
Point center(const Transform&);
// The outline does not flip when the image flips; core Transform::fromUnit does.
Point outlinePoint(const Transform&,Point unit);
Corners corners(const Transform&);
Rect bounds(const Transform&);
bool contains(const Transform&,Point documentPoint);
Transform roundedTransform(const Transform&);
double scalePercent(const Transform&,Point pixelSize);
Transform scaledPercent(const Transform&,double percent,Point pixelSize);
Transform resizedField(const Transform&,double value,bool width,bool lockRatio);
Transform flippedLocal(const Transform&,bool horizontal);
Transform mirrored(const Transform&,bool horizontal,double documentAxis);
Transform following(const Transform& placement,const Transform& oldParent,const Transform& newParent);
bool samePlacement(Transform,Transform);

enum class ModeKind { Move,Resize,Rotate,Distort };
struct Mode { ModeKind kind{ModeKind::Move}; int handle{-1}; bool operator==(const Mode&) const=default; };
// Semantic source modifiers. The host explicitly maps physical Windows keys.
struct Modifiers { bool shift{},option{},command{},control{}; };
enum class Cursor { Move,Duplicate,Horizontal,DiagonalDown,Vertical,DiagonalUp,Rotate,Distort };
struct OverlayGeometry {
    std::array<Point,8> handles{};
    Corners outline{};
    Point rotationHandle{};
    bool showsRotation{true};
    static OverlayGeometry fromTransform(const Transform&,const ViewMapping&);
    static OverlayGeometry fromCorners(const Corners&,const ViewMapping&);
    std::optional<Mode> hit(Point viewPoint) const;
    Cursor resizeCursor(int handle) const;
    Cursor cursor(Mode,Modifiers={},bool distorted=false) const;
};
struct Drag {
    Transform original;
    Point start; // Document coordinates, retained for the entire drag.
    Mode mode;
    std::optional<Corners> originalCorners;
    // Continuous source math. See previewDrag for source rounding/snap ordering.
    Transform updated(Point,bool lockRatio,Modifiers={}) const;
    std::optional<Corners> movedCorners(Point,bool shift=false) const;
};
bool usableCorners(const Corners&);
struct SnapTargets { std::vector<double> xs,ys; };
struct SnapResult { Point offset; std::optional<double> x,y; };
SnapResult snapOffset(Rect,const SnapTargets&,double documentTolerance);
struct Preview {
    Transform transform;
    std::optional<Corners> distortion;
    SnapResult snap;
    bool accepted{true}; // False: retain the preceding preview (invalid distortion).
};
Preview previewDrag(const Drag&,Point,bool lockRatio,Modifiers,const SnapTargets&,double pointsPerPixel);
// Pass visible bitmap layers in source rendering order; hidden descendants and
// folders are removed by visiblePlacements. Displayed overrides support drafts.
struct Placement { std::string id; Transform transform; };
using DisplayedTransform=std::function<Transform(const Layer&)>;
std::vector<Placement> visiblePlacements(const Document&,DisplayedTransform={});
SnapTargets collectSnapTargets(Point documentSize,std::span<const Placement>,std::span<const std::string> excluded={});
struct PressContext {
    std::string activeId;
    std::optional<Transform> activeTransform,groupBox;
    std::span<const Placement> visibleLayers;
    bool canEdit{true},hasEdit{},autoSelect{},controlsVisible{true},persistent{},hasDistortion{};
};
struct PressIntent {
    std::string layerId;
    Mode mode;
    bool picked{},duplicateOnFirstDrag{};
};
// Resolve selection/drag intent only. History, duplication, commit/cancel and
// distortion pixel resampling are host responsibilities.
std::optional<PressIntent> resolvePress(const PressContext&,Point viewPoint,const ViewMapping&,
                                      const std::optional<OverlayGeometry>&,Modifiers={});
}
