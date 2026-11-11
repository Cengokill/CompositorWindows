#pragma once
#include "layers/LayerOperations.h"
namespace compositor::editing_transform {
enum class Arrange { Left, CenterX, Right, Top, CenterY, Bottom, CentersX, CentersY, SpacingX, SpacingY };
size_t arrangeItemCount(const Document&, const layers::SelectionState&);
layers::EditResult arrange(const Document&, layers::SelectionState, Arrange);
}
