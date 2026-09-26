// Compile the unchanged before source in a separate namespace. Include shared
// core headers first so namespace substitution never changes Document's types.
#include "core/Document.h"
#include "graphics/RasterSampling.h"
#include "graphics/DownsampleKernel.h"
#include "graphics/ParallelBatch.h"
namespace compositor::reference_graphics {
using graphics::sampleRasterPixels;
using graphics::sampleMaskPixels;
using graphics::halvingWeights;
using graphics::runParallelBatch;
}
#define graphics reference_graphics
#include "reference42/SamplingSource.cpp"
#undef graphics
