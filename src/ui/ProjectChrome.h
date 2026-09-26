#pragma once
#include <QStringList>
class QMainWindow;
class QTabWidget;
class QDockWidget;
class QTreeWidget;

namespace compositor::ui {
// Install after the layer panel has created its controls. Width is an app
// preference, matching ContentView's AppStorage, not project file metadata.
void installProjectChrome(QMainWindow*, QTabWidget*, QDockWidget*, QTreeWidget*);
// Titles omit the native dirty marker and retain literal title characters.
void refreshProjectTabs(QTabWidget*, bool canClose, const QStringList& titles);
}
