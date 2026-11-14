#pragma once
#include <string>

class QTreeWidget;
namespace compositor::ui {
void installLayerAccessibility(QTreeWidget* tree);
void updateLayerAccessibility(QTreeWidget* tree);
void updateLayerVisibilityAccessibility(QTreeWidget* tree, const std::string& id);
}
