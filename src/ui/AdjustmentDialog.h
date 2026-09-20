#pragma once
#include "core/Document.h"
#include <QString>
#include <optional>
#include <functional>
class QWidget;
namespace compositor {
struct AdjustmentDialogResult { Document document; std::string active; };
struct AdjustmentDialogOptions {
    std::string initialAdjustmentJson;
    std::function<void(const std::string&)> onApply;
};
std::optional<AdjustmentDialogResult> showAdjustmentDialog(QWidget*,const Document&,
    const std::string& active,const QString& kind,bool live,bool existing,const AdjustmentDialogOptions& options={});
}
