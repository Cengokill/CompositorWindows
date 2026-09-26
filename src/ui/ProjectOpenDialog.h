#pragma once
#include <QStringList>
#include <optional>
class QWidget;
struct IFileOpenDialog;
struct IShellItem;

namespace compositor::ui {
// Themed project picker. nullopt means Cancel; packages remain atomic folders.
std::optional<QStringList> chooseProjectDirectories(QWidget* owner);

// The native folder-picker API cannot use SetFileTypes. These shared rules
// constrain the picker only; ProjectStore remains responsible for file safety.
bool isProjectPackageDirectory(const QString& path);
bool canNavigateProjectFolder(const QString& path);
bool canNavigateProjectItem(IShellItem* item);
void configureProjectOpenDialog(IFileOpenDialog* dialog);
}
