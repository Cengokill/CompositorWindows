#pragma once
#include "PixelTransform.h"

namespace compositor::editing_transform {
// Host-owned immutable source state. The native UI publishes a fresh preview
// from original on every change, then closes exactly one history transaction.
struct TransformSessionState {
    Document original;
    std::string originalActive,target;
    std::vector<std::string> originalSelected,ids;
    bool originalMaskSelected{},persistent{},maskOnly{},pixelMove{},group{};
    bool duplicateOnDrag{},duplicated{};
    Transform originalBox,draft;
    std::optional<Corners> corners;
    Point moveOffset;
    std::unique_ptr<PixelTransformSession> pixels;
};
}
