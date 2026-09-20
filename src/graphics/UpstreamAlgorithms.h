#pragma once
// The copied algorithms are MIT licensed; see upstream/LICENSE and
// docs/graphics-feasibility.md for the narrowly scoped Windows ABI changes.
// Prefer PixelAlgorithms.h, which validates allocation and parameter contracts.
extern "C" {
#include "upstream/AdjustPixels.h"
#include "upstream/BrushPixels.h"
#include "upstream/ContentFill.h"
#include "upstream/HealPixels.h"
#include "upstream/LensPixels.h"
#include "upstream/LevelsPixels.h"
#include "upstream/NoisePixels.h"
#include "upstream/WandPixels.h"
}
