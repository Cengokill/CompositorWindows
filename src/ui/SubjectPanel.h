#pragma once
#include "EditPanelSession.h"
#include "imaging/SubjectDialog.h"
namespace compositor {
ui::EditPanelSession* openSubjectPanel(QWidget*,const Document&,const Layer&,
    const std::filesystem::path&,const imaging::SubjectDialogOptions&,ui::EditPanelHost);
}
