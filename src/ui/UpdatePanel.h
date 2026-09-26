#pragma once
#include <QString>
#include <filesystem>
#include <functional>
class QDialog;
class QWidget;

namespace compositor::ui {
struct UpdatePanelHost {
    // The window must accept its normal close/save flow before launching.
    // False means the user cancelled; the installed version remains retained.
    std::function<bool(const std::filesystem::path&)> restart;
};
QDialog* openUpdatePanel(QWidget* parent, UpdatePanelHost host = {},
                         QString applicationDirectory = {});
}
