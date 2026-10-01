#include "MainWindow.h"
#include "LayerPanel.h"
#include <QApplication>
#include <QSignalBlocker>
#include <cmath>

namespace compositor::ui {
namespace {
const Layer* layerOf(const Document& d,const std::string& id){for(const auto& layer:d.layers)if(layer.id==id)return &layer;return nullptr;}
std::optional<editing::SelectionOutline> outlineOf(const std::optional<Selection>& selection){if(!selection)return {};if(selection->outline)return *selection->outline;if(selection->coverage)return editing::SelectionOutline::fromCoverage(*selection->coverage);return editing::SelectionOutline();}
}
layers::EditResult editMask(const Document& document,layers::SelectionState selection,const std::string& id,MaskCommand command){
    validateDocument(document);selection=layers::normalizeSelection(document,std::move(selection));const auto* original=layerOf(document,id);
    if(!original||(command!=MaskCommand::ToggleLink&&(selection.ids.size()!=1||selection.primary!=id)))return {document,selection,false,"Layer Mask"};
    Document next=document;auto& layer=*std::find_if(next.layers.begin(),next.layers.end(),[&](const Layer& value){return value.id==id;});std::string name;
    if(command==MaskCommand::AddReveal||command==MaskCommand::AddHide||command==MaskCommand::RevealAll||command==MaskCommand::HideAll){
        if(layer.mask)return {document,selection,false,"Add Layer Mask"};const bool reveal=command==MaskCommand::AddReveal||command==MaskCommand::RevealAll;
        const bool useSelection=(command==MaskCommand::AddReveal||command==MaskCommand::AddHide)&&next.selection.has_value();
        auto raster=std::make_shared<GrayRaster>();
        if(useSelection){raster->width=layer.raster?layer.raster->width:int(std::lround(layer.transform.width));raster->height=layer.raster?layer.raster->height:int(std::lround(layer.transform.height));
            if(raster->width<1||raster->height<1||raster->width>30000||raster->height>30000||uint64_t(raster->width)*raster->height>100000000)throw std::runtime_error("Mask exceeds image limits");
            const auto coverage=editing::mappedCoverage(next,layer.transform,raster->width,raster->height);if(!coverage)throw std::runtime_error("Selection coverage is unavailable");raster->pixels.resize(size_t(raster->width)*raster->height);for(int y=0;y<raster->height;++y)for(int x=0;x<raster->width;++x)raster->pixels[size_t(y)*raster->width+x]=coverage->pixel(x,y);if(!reveal)for(auto& byte:raster->pixels)byte=uint8_t(255-byte);next.selection.reset();name="Add Mask from Selection";
        }else{raster->width=raster->height=1;raster->pixels={uint8_t(reveal?255:0)};name=reveal?"Add Reveal-All Mask":"Add Hide-All Mask";}
        layer.mask=Mask{raster};
    }else if(command==MaskCommand::ToggleEnabled&&layer.mask){name=layer.mask->enabled?"Disable Layer Mask":"Enable Layer Mask";layer.mask->enabled=!layer.mask->enabled;}
    else if(command==MaskCommand::ToggleLink&&layer.mask){name=layer.mask->linked?"Unlink Layer Mask":"Link Layer Mask";layer.mask->linked=!layer.mask->linked;}
    else if(command==MaskCommand::Delete&&layer.mask){name="Delete Layer Mask";layer.mask.reset();}
    else return {document,selection,false,"Layer Mask"};
    validateDocument(next);const bool changed=next!=document;return {std::move(next),std::move(selection),changed,name};
}
layers::EditResult copyMask(const Document& d,layers::SelectionState selection,const std::string& source,const std::string& target){
    const auto* from=layerOf(d,source);const auto* to=layerOf(d,target);if(!from||!from->mask||!to||to->group||source==target)return {d,std::move(selection),false,"Copy Layer Mask"};
    Document next=d;auto& layer=*std::find_if(next.layers.begin(),next.layers.end(),[&](const Layer& l){return l.id==target;});auto mask=*from->mask;mask.placement=mask.placement.value_or(from->transform);const auto action=layer.mask?"Replace Layer Mask":"Copy Layer Mask";layer.mask=std::move(mask);validateDocument(next);return {std::move(next),{{target},target},true,action};
}
std::optional<Selection> loadedSelection(const Document& d,const std::string& id,bool mask,editing::SelectionMode mode){
    const auto* layer=layerOf(d,id);if(!layer)return d.selection;
    if(mask?(!layer->mask||!layer->mask->raster):(layer->group||!layer->raster))return d.selection;
    GrayRaster binary;binary.width=mask?layer->mask->raster->width:layer->raster->width;binary.height=mask?layer->mask->raster->height:layer->raster->height;binary.pixels.resize(size_t(binary.width)*binary.height);bool any=false;
    for(int y=0;y<binary.height;++y)for(int x=0;x<binary.width;++x){const bool selected=mask?layer->mask->raster->pixel(x,y)<128:layer->raster->pixel(x,y).a>=128;binary.pixels[size_t(y)*binary.width+x]=selected?255:0;any|=selected;}
    if(!any)return d.selection;
    const auto transform=mask?layer->mask->placement.value_or(layer->transform):layer->transform;
    const auto traced=editing::SelectionOutline::fromCoverage(binary).transformed(transform,binary.width,binary.height);
    return editing::rasterSelection(editing::applySelection(outlineOf(d.selection),traced,mode,d.width,d.height,true),d.width,d.height);
}
}

namespace compositor {
void MainWindow::selectLayerTarget(const std::string& id,bool mask){
    auto* project=current();if(!project||!project->document)return;
    const auto state=commandState(project);
    if(state.projectBusy||state.importing||state.brushStroke||state.modalDialog)return;
    if(std::none_of(project->document->layers.begin(),project->document->layers.end(),[&](const Layer& layer){return layer.id==id;}))return;
    // LayerMask.swift222 first resolves the gradient, then selectLayer277
    // commits a transform only when the selected layer ID actually changes.
    // Crop/polygon drafts and captured color/filter editors remain attached.
    applyGradient();
    if(!state.warpStroke&&!state.levels){
        if(project->active!=id){
            project->blendPreview.reset();finishOpacityEdit();
            if(transformSession_)applyTransformSession();
        }
        project->active=id;project->selected={id};
    }
    const auto found=std::find_if(project->document->layers.begin(),project->document->layers.end(),[&](const Layer& layer){return layer.id==project->active;});
    project->maskSelected=mask&&found!=project->document->layers.end()&&found->mask.has_value();
    refresh(false,false);if(auto* controller=ui::LayerPanelController::find(layers_))controller->updateSelection();
}
void MainWindow::loadLayerSelection(const std::string& id,bool mask,Qt::KeyboardModifiers modifiers){
    applyGradient();if(transformSession_&&transformSession_->persistent)applyTransformSession();pointerCancel();auto* p=current();if(!p||!p->document)return;
    const auto mode=modifiers.testFlag(Qt::AltModifier)?editing::SelectionMode::Subtract:modifiers.testFlag(Qt::ShiftModifier)?editing::SelectionMode::Add:editing::SelectionMode::Replace;
    auto selection=ui::loadedSelection(*p->document,id,mask,mode);if(selection==p->document->selection){QApplication::beep();return;}
    edit(mask?"Load Mask Selection":"Load Layer Selection",[&](Document& d){d.selection=std::move(selection);});
}
void MainWindow::maskCommand(int value){
    const auto command=ui::MaskCommand(value);
    if(command==ui::MaskCommand::EditImage||command==ui::MaskCommand::EditMask){
        // Alternate target controls follow LayerMask.swift222 directly. They
        // must not cancel crop/lasso or commit a same-layer transform first.
        if(auto* project=current();project&&project->document&&active())selectLayerTarget(project->active,command==ui::MaskCommand::EditMask);
        // A stale/programmatic combo signal can arrive while its target is
        // guarded. Restore the actual choice without emitting another edit.
        if(auto* controller=ui::LayerPanelController::find(layers_))controller->updateSelection();
        return;
    }
    applyGradient();if(transformSession_&&transformSession_->persistent)applyTransformSession();pointerCancel();auto* p=current();if(!p||!p->document||!active())return;
    if(command==ui::MaskCommand::LoadAlpha||command==ui::MaskCommand::LoadBlack){loadLayerSelection(p->active,command==ui::MaskCommand::LoadBlack,Qt::NoModifier);return;}
    auto result=ui::editMask(*p->document,layerSelection(),p->active,command);if(!result.changed)return;
    if(command==ui::MaskCommand::Delete)p->maskSelected=false;
    else if(command==ui::MaskCommand::AddReveal||command==ui::MaskCommand::AddHide||command==ui::MaskCommand::RevealAll||command==ui::MaskCommand::HideAll)p->maskSelected=true;
    applyLayerEdit(std::move(result));
}
}
