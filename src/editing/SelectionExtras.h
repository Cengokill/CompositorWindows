#pragma once
#include "Selection.h"
#include <memory>

namespace compositor::editing {
Selection featherSelection(const Selection& selection,double radius);
std::shared_ptr<const GrayRaster> colorRangeMask(const Raster& source,int x,int y,int tolerance);
std::shared_ptr<const GrayRaster> connectedComponent(const GrayRaster& mask,int x,int y,int threshold=1);
}
