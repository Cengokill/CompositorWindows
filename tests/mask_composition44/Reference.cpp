#include "core/Document.h"
#include "graphics/BrushSession.h"
#include "graphics/RasterSampling.h"
#include "graphics/SamplingSource.h"
namespace compositor::reference_growth {
using graphics::BrushSessionSettings;
using graphics::BrushSessionGeometry;
using graphics::BrushSessionMetrics;
using graphics::BrushSegment;
using graphics::BrushTile;
using graphics::BrushTileRender;
using graphics::BrushUniforms;
using graphics::D3D11BrushCoverage;
using graphics::SamplingSource;
using graphics::renderBrushCpuBatch;
using graphics::brushContinuousCurve;
using graphics::sampleRasterPixels;
using graphics::sampleMaskPixels;
}
#define graphics reference_growth
#ifdef MASK_REFERENCE43
#include "reference43/GrowingBrushSession.cpp"
#else
#include "reference42/GrowingBrushSession.cpp"
#endif
#undef graphics
