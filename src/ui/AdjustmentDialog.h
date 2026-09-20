#pragma once
#include "core/Document.h"
#include <QString>
#include <optional>
class QWidget;
namespace compositor {
struct AdjustmentDialogResult { Document document; std::string active; };
std::optional<AdjustmentDialogResult> showAdjustmentDialog(QWidget*,const Document&,
    const std::string& active,const QString& kind,bool live,bool existing);
}
