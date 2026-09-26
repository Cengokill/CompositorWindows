#pragma once
class QMainWindow;
namespace compositor::ui {
void installVisualStyle();
void styleWorkspace(QMainWindow* window);
bool macTitleBarEnabled();
void setMacTitleBarEnabled(bool enabled);
}
