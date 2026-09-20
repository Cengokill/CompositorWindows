#pragma once
#include "image_types.h"
#include <filesystem>
#include <optional>
class QWidget;
namespace compositor::imaging {
std::optional<GrayMask> showSubjectDialog(QWidget* parent,const RgbaImage& source,const GrayMask* existing,const std::filesystem::path& modelPath);
}
