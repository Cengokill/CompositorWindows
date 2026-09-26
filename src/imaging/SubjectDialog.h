#pragma once
#include "image_types.h"
#include "subject_matte.h"
#include <filesystem>
#include <functional>
#include <optional>
#include <memory>
class QWidget;
class QDialog;
namespace compositor::imaging {
struct SubjectDialogOptions {
    MatteSettings initial;
    std::function<void(const MatteSettings&)> onApply;
};
struct SubjectDialogCallbacks {
    std::function<void(std::shared_ptr<const RgbaImage>)> preview;
    std::function<std::shared_ptr<const GrayMask>()> existingAtApply;
    std::function<void(bool)> committingChanged;
    std::function<void(std::optional<GrayMask>)> finished;
    // Worker-only hooks. Capture immutable data; never widgets or UI state.
    std::function<void(RgbaImage&,const RgbaImage&,const ImportOptions&)> processPreview;
    std::function<void(GrayMask&,const GrayMask*,const ImportOptions&)> processMask;
};
// The nonmodal dialog owns its worker through cancellation and deletes itself
// after closing and receiving the final worker completion.
QDialog* openSubjectDialog(QWidget*,const RgbaImage&,const std::filesystem::path&,const SubjectDialogOptions&,SubjectDialogCallbacks);
std::optional<GrayMask> showSubjectDialog(QWidget* parent,const RgbaImage& source,const GrayMask* existing,const std::filesystem::path& modelPath,const SubjectDialogOptions& options={});
}
