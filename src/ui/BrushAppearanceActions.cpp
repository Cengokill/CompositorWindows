#include "MainWindow.h"
#include "LayerOpacityField.h"
#include <QHBoxLayout>
#include <QLabel>

namespace compositor {
QWidget* MainWindow::createLayerOpacityControl(){
    auto* control=new QWidget;auto* layout=new QHBoxLayout(control);layout->setContentsMargins(0,0,0,0);layout->setSpacing(4);
    opacityPercent_=new ui::LayerOpacityField;opacityPercent_->setObjectName("layerOpacityPercent");opacityPercent_->setAccessibleName("Opacity percent");opacityPercent_->setFixedWidth(48);
    opacityPercent_->target=[this]()->std::optional<ui::LayerOpacityField::Target>{
        auto* project=current();auto* layer=active();
        if(!project||!layer)return {};return ui::LayerOpacityField::Target{project->page,layer->id,layer->opacity};
    };
    opacityPercent_->apply=[this](const ui::LayerOpacityField::Target& target,double opacity){
        auto* project=current();if(!project||project->page!=target.owner||!canEditAppearance()||project->active!=target.layer)return;
        edit("Layer Opacity",[&](Document&){active()->opacity=opacity;});
    };
    opacityPercent_->releaseFocus=[this]{if(canvas())canvas()->setFocus();};
    layout->addWidget(opacity_,1);layout->addWidget(opacityPercent_);layout->addWidget(new QLabel("%"));return control;
}
void MainWindow::refreshOpacityPercent(){if(opacityPercent_){opacityPercent_->setEnabled(canEditAppearance());opacityPercent_->synchronize();}}
void MainWindow::detachOpacityPercent(){if(opacityPercent_)opacityPercent_->detach();}
}
