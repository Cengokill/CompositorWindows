#pragma once
#include <QColor>
#include <QWidget>
#include <array>
class QAction;
class QToolButton;

namespace compositor::ui {
// ColorPaletteControls.swift: two24-point swatches with12-point overlap.
// Existing registered actions retain command gating, shortcuts and callbacks.
class PaletteSwatches final:public QWidget {
    std::array<QToolButton*,4> buttons_{};
public:
    explicit PaletteSwatches(const std::array<QAction*,4>& actions,QWidget* parent=nullptr);
    void setColors(QColor foreground,QColor background);
    QWidget* swatch(bool background)const;
};
}
