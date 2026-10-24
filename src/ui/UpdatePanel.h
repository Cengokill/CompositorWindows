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
    // Stable-release notification check. The callback receives the status text
    // on the UI thread; installation stays manual.
    std::function<void(std::function<void(const QString&)>)> checkRelease;
};
QDialog* openUpdatePanel(QWidget* parent, UpdatePanelHost host = {},
                         QString applicationDirectory = {});
}
