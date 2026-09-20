#pragma once
#include "image_types.h"
#include "subject_matte.h"
#include <filesystem>
#include <functional>
#include <optional>
class QWidget;
namespace compositor::imaging {
struct SubjectDialogOptions {
    MatteSettings initial;
    std::function<void(const MatteSettings&)> onApply;
};
std::optional<GrayMask> showSubjectDialog(QWidget* parent,const RgbaImage& source,const GrayMask* existing,const std::filesystem::path& modelPath,const SubjectDialogOptions& options={});
}
