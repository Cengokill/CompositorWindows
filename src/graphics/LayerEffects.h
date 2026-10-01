#pragma once
#include "core/Document.h"
#include <functional>

namespace compositor::graphics {
struct EffectSample {
    Pixel content{};
    Pixel under{};
    Blend underBlend{Blend::Multiply};
    bool hasUnder{};
    Pixel glow{};
    Blend glowBlend{Blend::Screen};
    bool hasGlow{};
};
// `shaped` is the layer silhouette with its mask and without layer opacity.
// `coverage` already includes layer opacity and any folder or clipping factor.
EffectSample shadeLayer(const Layer& layer,Point documentPoint,Pixel content,double coverage,const std::function<Pixel(Point)>& shaped);
}
