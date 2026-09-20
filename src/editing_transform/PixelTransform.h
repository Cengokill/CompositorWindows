#pragma once
#include "Distortion.h"

namespace compositor::editing_transform {
enum class PixelTransformKind { Move,Affine };
struct PixelTransformResult {
    Layer layer;
    std::shared_ptr<const GrayRaster> selection;
    // Unclipped geometric bounds; dense selection storage uses the canvas grid.
    Rect selectionBounds;
};
struct FloatingPixelPreview {Layer clearedLayer,floatingLayer;};
class PixelTransformSession {
public:
    PixelTransformSession(Layer original,std::shared_ptr<const GrayRaster> documentSelection,
        PixelTransformKind kind=PixelTransformKind::Move,bool duplicate=false);
    bool begin(CancelCheck={}); // False for empty/no intersecting selection.
    bool move(Point documentOffset); // Whole document pixels, from original.
    bool update(Transform draft,std::optional<Corners> distortion={}); // Affine kind.
    PixelTransformResult preview(CancelCheck={});
    // Source floating-selection canvas stack before merge: the cleared source
    // retains its mask; the temporary layer inherits opacity/blend/parent only.
    FloatingPixelPreview floatingPreview(CancelCheck={});
    // Source live floating outline follows the affine draft until Apply, even
    // during distortion. Final selection coverage also includes the homography.
    std::shared_ptr<const GrayRaster> selectionPreview(CancelCheck={},bool affineOnly=false)const;
    PixelTransformResult apply(CancelCheck={});
    PixelTransformResult cancel();
    const Transform& originalFloatingTransform()const;
    const Transform& draft()const;
    Point mapSelectionPoint(Point originalDocumentPoint)const;
    ~PixelTransformSession();
    PixelTransformSession(const PixelTransformSession&)=delete;
    PixelTransformSession& operator=(const PixelTransformSession&)=delete;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
