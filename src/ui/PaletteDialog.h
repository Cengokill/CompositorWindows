#pragma once
#include "effects_tools/Color.h"
#include <QDialog>
#include <functional>
#include <memory>
namespace compositor {
class PaletteDialog final:public QDialog {
    struct Impl;std::unique_ptr<Impl> impl_;
public:
    PaletteDialog(effects_tools::PaletteColor original,const QString& title,QWidget*parent=nullptr);
    ~PaletteDialog()override;
    effects_tools::PaletteColor color()const;
    void sample(effects_tools::PaletteColor);
    std::function<void(effects_tools::PaletteColor)> onPreview;
};
}
