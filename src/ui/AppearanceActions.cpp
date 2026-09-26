#include "MainWindow.h"

namespace compositor {
void MainWindow::previewBlendMode(std::optional<Blend> mode) {
    for (auto& project : projects_) {
        if (project->blendPreview) {
            project->blendPreview.reset();
            project->canvas->update();
        }
    }
    if (!mode || !canEditAppearance()) return;
    auto* project=current();
    project->blendPreview=std::pair{project->active,*mode};
    project->canvas->update();
}
void MainWindow::setLayerBlendMode(Blend mode) {
    previewBlendMode({});
    if (!canEditAppearance()) return;
    finishOpacityEdit();
    edit("Layer Blend Mode",[&](Document&){active()->blend=mode;});
}
}
