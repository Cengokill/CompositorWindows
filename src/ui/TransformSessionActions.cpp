#include "MainWindow.h"
#include "PropertyControls.h"
#include "graphics/MaskSampling.h"
#include "editing/Shapes.h"
#include <QAction>
#include <QSignalBlocker>
#include <QStatusBar>
#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace compositor {
namespace {
Layer* layerIn(Document& document,const std::string& id){for(auto& layer:document.layers)if(layer.id==id)return &layer;return nullptr;}
bool visibleIn(const Document& document,const std::string& id){const auto all=layers::entries(document);return std::any_of(all.begin(),all.end(),[&](const layers::Entry& entry){return entry.id==id&&entry.visible;});}
std::vector<std::string> visibleMembers(const Document& document,const std::vector<std::string>& selected){
    std::unordered_set<std::string> ids(selected.begin(),selected.end());for(const auto& id:selected){auto children=layers::descendants(document,id);ids.insert(children.begin(),children.end());}
    std::vector<std::string> result;const auto visible=editing_transform::visiblePlacements(document);
    for(const auto& entry:visible)if(ids.contains(entry.id))result.push_back(entry.id);
    return result;
}
}

void MainWindow::changeTransformDraft(const std::function<void(Transform&)>& change) {
    auto* project=current();
    if(refreshing_||!project||!project->document||project->importing||project->projectBusy||
       (transformSession_&&transformSession_->corners))return;
    try {
        if(!transformSession_&&!startTransformSession(true))return;
        auto next=transformSession_->draft;change(next);
        if(!next.valid())return;
        transformSession_->draft=next;publishTransformSession(false);refresh(true,false);
    }catch(const std::exception& error){cancelTransformSession();statusBar()->showMessage(error.what());}
}
void MainWindow::editTransformGeometry(int field,double value) {
    if(field<0||field>4||!std::isfinite(value)||((field==2||field==3)&&value<1))return;
    changeTransformDraft([&](Transform& t){
        if(field==0)t.x=value;else if(field==1)t.y=value;
        else if(field==2){if(lockRatio_)t.height*=value/t.width;t.width=value;}
        else if(field==3){if(lockRatio_)t.width*=value/t.height;t.height=value;}
        else t.rotation=std::fmod(value,360.);
    });
}
Point MainWindow::transformScalePixelSize() {
    if(transformSession_){const auto& state=*transformSession_;
        if(state.group)return {state.originalBox.width,state.originalBox.height};
        if(state.pixels){const auto& original=state.pixels->originalFloatingTransform();return {original.width,original.height};}
        const auto layer=std::find_if(state.original.layers.begin(),state.original.layers.end(),[&](const Layer& item){return item.id==state.target;});
        if(layer!=state.original.layers.end()){
            // TransformInspector.pixelSize falls back to activeLayer.size for
            // an unlinked mask, not to the mask bitmap or its placed dimensions.
            if(state.maskOnly||!layer->raster)return {layer->transform.width,layer->transform.height};
            return {double(layer->raster->width),double(layer->raster->height)};
        }
    }
    auto* layer=active();auto* project=current();auto box=selectedTransform();
    if(!layer||!project||!box)return {1,1};
    if(layer->group||layerSelection().ids.size()>1)return {box->width,box->height};
    if((project->maskSelected&&layer->mask&&!layer->mask->linked)||!layer->raster)return {layer->transform.width,layer->transform.height};
    return {double(layer->raster->width),double(layer->raster->height)};
}

bool MainWindow::startTransformSession(bool persistent,bool distort) {
    if(transformSession_) {
        if(transformOwner_!=current())return false;
        if(distort&&!transformSession_->corners){transformSession_->corners=editing_transform::corners(transformSession_->draft);publishTransformSession(false);refresh();}
        return true;
    }
    if(!canEditLayers()||stroke_||retouch_||drawingOriginal_||!shapeDraftId_.empty()||selectionBefore_)return false;
    auto* project=current();auto* layer=active();if(!project||!project->document||!layer)return false;
    finishOpacityEdit();
    const auto selected=layerSelection();auto box=selectedTransform();if(!box)return false;
    auto state=std::make_unique<editing_transform::TransformSessionState>();
    state->original=*project->document;state->originalActive=project->active;state->originalSelected=project->selected;state->originalMaskSelected=project->maskSelected;
    state->target=layer->id;state->persistent=persistent;state->originalBox=*box;state->draft=*box;
    const bool group=selected.ids.size()>1||layer->group;
    state->group=group;
    if(group)state->ids=visibleMembers(*project->document,selected.ids);
    else if(layer->raster&&visibleIn(*project->document,layer->id))state->ids={layer->id};
    if(state->ids.empty())return false;
    state->maskOnly=!group&&project->maskSelected&&layer->mask&&!layer->mask->linked;
    const auto& selection=project->document->selection;
    if(persistent&&!group&&!project->maskSelected&&selection&&selection->coverage&&
       selection->coverage->hasCoverage()) {
        state->pixels=std::make_unique<editing_transform::PixelTransformSession>(*layer,selection->coverage,editing_transform::PixelTransformKind::Affine);
        if(!state->pixels->begin())return false;
        state->draft=state->originalBox=state->pixels->originalFloatingTransform();
    }
    if(distort)state->corners=editing_transform::corners(state->draft);
    // Do this before installing state: selectTool resolves any older gesture.
    selectTool(Tool::Move);
    project->history.begin(state->pixels?"Transform Selection":state->maskOnly?(distort?"Distort Layer Mask":"Transform Layer Mask"):distort?(group?"Distort Layers":"Distort"):group?"Transform Layers":"Transform Layer",project->document,project->active);
    transformOwner_=project;transformSession_=std::move(state);
    try{publishTransformSession(false);refresh();}
    catch(...){cancelTransformSession();throw;}
    return true;
}

void MainWindow::publishTransformSession(bool finish) {
    if(!transformSession_||!transformOwner_)return;
    auto& state=*transformSession_;auto* project=transformOwner_;Document result=state.original;
    if(state.pixels) {
        if(state.pixelMove)state.pixels->move(state.moveOffset);else state.pixels->update(state.draft,state.corners);
        if(state.pixelMove||finish) {
            const auto output=finish?state.pixels->apply():state.pixels->preview();
            auto* layer=layerIn(result,state.target);if(!layer)throw std::runtime_error("The selected pixel target was removed");
            *layer=output.layer;
            if(layer->raster!=layerIn(state.original,state.target)->raster)layer->shapeJson.clear();
            result.selection=Selection{output.selection};
            if(state.pixelMove&&state.original.selection&&state.original.selection->outline)
                result.selection=editing::moveSelectionCoverage(state.original.selection,state.moveOffset);
            // An unchanged affine apply restores the exact original selection,
            // including its vector outline and soft coverage pointer.
            if(!state.pixelMove&&state.draft==state.originalBox&&!state.corners)result.selection=state.original.selection;
            project->active=state.originalActive;project->selected=state.originalSelected;
        } else {
            const auto display=state.pixels->floatingPreview();
            auto where=std::find_if(result.layers.begin(),result.layers.end(),[&](const Layer& layer){return layer.id==state.target;});
            if(where==result.layers.end())throw std::runtime_error("The selected pixel target was removed");
            *where=display.clearedLayer;result.layers.insert(where+1,display.floatingLayer);
            project->active=display.floatingLayer.id;project->selected={project->active};
            const auto selection=state.pixels->selectionPreview({},true);
            result.selection=state.draft==state.originalBox?state.original.selection:std::optional(Selection{selection});
        }
    } else {
        for(const auto& id:state.ids) {
            auto* layer=layerIn(result,id);if(!layer)continue;const Layer original=*layer;
            if(state.maskOnly) {
                if(!layer->mask)throw std::runtime_error("The transform mask was removed");
                if(state.corners) {
                    const auto warped=editing_transform::warpGray(original.mask->raster,state.draft,*state.corners,graphics::cachedMaskBackground(original.mask->raster),{finish?0:2048,false,{}});
                    layer->mask->raster=warped.raster;layer->mask->placement=editing_transform::samePlacement(warped.transform,original.transform)?std::nullopt:std::optional(warped.transform);
                } else layer->mask->placement=editing_transform::samePlacement(state.draft,original.transform)?std::nullopt:std::optional(state.draft);
                continue;
            }
            const auto placement=state.group?editing_transform::following(original.transform,state.originalBox,state.draft):state.draft;
            if(state.corners) {
                const auto shape=state.group?editing_transform::carriedCorners(placement,state.draft,*state.corners):*state.corners;
                *layer=editing_transform::distortLayer(original,placement,shape,{finish?0:2048,finish,{}});
                layer->shapeJson.clear();
            } else {
                layer->transform=placement;
                if(original.mask) {
                    if(original.mask->raster->width==1&&original.mask->raster->height==1)layer->mask->placement.reset();
                    else {
                        if(original.mask->linked&&original.mask->placement)layer->mask->placement=editing_transform::following(*original.mask->placement,original.transform,placement);
                        else if(!original.mask->linked)layer->mask->placement=original.mask->placement.value_or(original.transform);
                        if(layer->mask->placement&&editing_transform::samePlacement(*layer->mask->placement,placement))layer->mask->placement.reset();
                    }
                }
                if(!layer->shapeJson.empty()){
                    if(finish)*layer=editing::redrawShape(*layer);
                    else if(auto preview=editing::shapeTransformPreview(original,placement))layer->raster=std::move(preview);
                }
            }
        }
        project->active=state.duplicated?state.target:state.originalActive;project->selected=state.duplicated?std::vector<std::string>{state.target}:state.originalSelected;
    }
    validateDocument(result);project->document=std::move(result);
    project->maskSelected=state.originalMaskSelected&&!state.duplicated;
}

void MainWindow::applyTransformSession() {
    if(!transformSession_||!transformOwner_)return;
    auto* owner=transformOwner_;
    owner->canvas->setSnapGuides();
    try {
        if(transformSession_->corners&&!transformSession_->pixels) {
            const auto* name=transformSession_->maskOnly?"Distort Layer Mask":transformSession_->group?"Distort Layers":"Distort";
            if(auto before=owner->history.cancel())owner->history.begin(name,before->document,before->activeLayer);
        }
        publishTransformSession(true);
        owner->history.end(owner->document,owner->active);
        transformSession_.reset();transformOwner_=nullptr;transformDrag_.reset();transformBefore_.reset();transformIds_.clear();
        refresh();
    } catch(const std::exception& error){cancelTransformSession();statusBar()->showMessage(error.what());}
}

void MainWindow::cancelTransformSession() {
    if(!transformSession_)return;
    auto* owner=transformOwner_;auto activeId=std::move(transformSession_->originalActive);auto selected=std::move(transformSession_->originalSelected);const bool mask=transformSession_->originalMaskSelected;
    if(owner)owner->canvas->setSnapGuides();
    transformSession_.reset();transformOwner_=nullptr;transformDrag_.reset();transformBefore_.reset();transformIds_.clear();
    if(owner) {
        if(auto snapshot=owner->history.cancel()){owner->document=std::move(snapshot->document);owner->active=std::move(snapshot->activeLayer);}
        else owner->active=std::move(activeId);
        owner->selected=std::move(selected);owner->maskSelected=mask;
    }
    refresh();
}

void MainWindow::updateTransformActions() {
    for(const auto* name:{"applyTransform","cancelTransform"})if(auto* item=findChild<QAction*>(name))item->setEnabled(bool(transformSession_));
    const auto draft=selectedTransform();auto* project=current();auto* layer=active();
    const bool editable=ui::commandEnabled(ui::CommandGate::TransformDraft,commandState())&&project&&project->document&&!project->importing&&!project->projectBusy&&draft&&layer&&
        (layer->raster||layer->group||transformSession_)&&(!transformSession_||!transformSession_->corners);
    if(auto* sampling=findChild<QComboBox*>("transformSampling")){QSignalBlocker block(sampling);sampling->setEnabled(editable);if(draft)sampling->setCurrentIndex(int(draft->sampling));}
    if(auto* scale=findChild<QDoubleSpinBox*>("transformScale")){QSignalBlocker block(scale);scale->setEnabled(editable);if(draft)ui::synchronizeNumber(scale,editing_transform::scalePercent(*draft,transformScalePixelSize()));}
    for(const auto* name:{"transformFlipH","transformFlipV"})if(auto* item=findChild<QAction*>(name))item->setEnabled(editable);
}
}
