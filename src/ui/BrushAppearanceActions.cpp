#include "MainWindow.h"
#include "LayerOpacityField.h"
#include "LayerPanelTokens.h"
#include <QHBoxLayout>
#include <QLabel>

namespace compositor {
QWidget* MainWindow::createLayerOpacityControl(){
    namespace t=ui::layer_panel;
    auto* control=new QWidget;auto* layout=new QHBoxLayout(control);layout->setContentsMargins(0,0,0,0);layout->setSpacing(t::opacitySpacing);
    auto* label=new QLabel("Opacity",control);label->setObjectName("layerOpacityLabel");label->setFont(ui::layerPanelFont(t::captionFont));label->setStyleSheet(QString("QLabel { color: %1; font-family: '%2'; font-size: %3px; }").arg(t::primary,ui::layerPanelFont(t::captionFont).family()).arg(t::captionFont));layout->addWidget(label);
    opacityPercent_=new ui::LayerOpacityField;opacityPercent_->setObjectName("layerOpacityPercent");opacityPercent_->setAccessibleName("Opacity percent");
    opacityPercent_->target=[this]()->std::optional<ui::LayerOpacityField::Target>{
        auto* project=current();auto* layer=active();
        if(!project||!layer)return {};return ui::LayerOpacityField::Target{project->page,layer->id,layer->opacity};
    };
    opacityPercent_->apply=[this](const ui::LayerOpacityField::Target& target,double opacity){
        auto* project=current();if(!project||project->page!=target.owner||!canEditAppearance()||project->active!=target.layer)return;
        edit("Layer Opacity",[&](Document&){active()->opacity=opacity;});
    };
    opacityPercent_->releaseFocus=[this]{if(canvas())canvas()->setFocus();};
    // Segoe's visible glyphs sit low in their line box; align the track optically with the label.
    layout->addWidget(opacity_,1,Qt::AlignBottom);auto*unit=new QWidget(control);auto*unitLayout=new QHBoxLayout(unit);unitLayout->setContentsMargins(0,0,0,0);unitLayout->setSpacing(t::percentSpacing);unitLayout->addWidget(opacityPercent_);auto*percent=new QLabel("%",unit);percent->setFont(ui::layerPanelFont(t::captionFont));percent->setStyleSheet(QString("QLabel { color: %1; font-family: '%2'; font-size: %3px; }").arg(t::primary,ui::layerPanelFont(t::captionFont).family()).arg(t::captionFont));unitLayout->addWidget(percent);layout->addWidget(unit);return control;
}
void MainWindow::refreshOpacityPercent(){if(opacityPercent_){opacityPercent_->setEnabled(canEditAppearance());opacityPercent_->synchronize();}}
void MainWindow::detachOpacityPercent(){if(opacityPercent_)opacityPercent_->detach();}
}
