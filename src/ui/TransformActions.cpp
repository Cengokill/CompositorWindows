#include "MainWindow.h"
#include "PropertyControls.h"
#include "editing/DocumentGeometry.h"
#include "editing/Shapes.h"
#include <QToolBar>
#include <QCheckBox>
#include <QMenuBar>
#include <QLabel>
#include <QInputDialog>
#include <QStatusBar>
#include <cmath>
#include <algorithm>
#include <unordered_set>

namespace compositor {
namespace {
struct MovePreviewPlan { bool sprite{}; bool covered{}; bool masked{}; std::vector<const Layer*> above; };
MovePreviewPlan planMovePreview(const Document& document,const Layer& moving){
    MovePreviewPlan plan;
    const bool plain=!moving.group&&moving.raster&&moving.blend==Blend::Normal&&!moving.effects.active()&&moving.adjustmentJson.empty()&&moving.maskSourceId.empty()&&moving.shapeJson.empty()&&moving.parentId.empty()&&(!moving.mask||moving.mask->linked)&&std::none_of(document.layers.begin(),document.layers.end(),[&](const Layer& layer){return layer.maskSourceId==moving.id;});
    if(!plain)return plan;
    std::vector<const Layer*> top;for(const auto& layer:document.layers)if(layer.parentId.empty())top.push_back(&layer);
    auto at=std::find_if(top.begin(),top.end(),[&](const Layer* layer){return layer->id==moving.id;});if(at==top.end())return plan;
    for(auto it=std::next(at);it!=top.end();++it){const auto& layer=**it;if(!layer.visible)continue;
        if(layer.group||layer.blend!=Blend::Normal||layer.effects.active()||!layer.adjustmentJson.empty()||!layer.maskSourceId.empty()||!layer.shapeJson.empty())return {};
        if(!layer.raster)continue;plan.above.push_back(*it);}
    auto visible=editing_transform::visiblePlacements(document);
    plan.sprite=!moving.mask&&plan.above.empty()&&!visible.empty()&&visible.back().id==moving.id;
    plan.covered=!plan.above.empty();plan.masked=moving.mask.has_value();
    if(!plan.sprite&&!plan.covered&&!plan.masked)return {};
    return plan;
}
Document hideLayers(Document document,const std::unordered_set<std::string>& ids){for(auto& layer:document.layers)if(ids.contains(layer.id))layer.visible=false;return document;}
}
std::optional<Transform> MainWindow::selectedTransform(){
    if(transformSession_&&transformOwner_==current())return transformSession_->draft;
    auto*l=active();auto*p=current();if(!l||!p||!p->document)return {};
    const auto selected=layerSelection();
    if(selected.ids.size()==1&&!l->group){if(p->maskSelected&&l->mask&&!l->mask->linked)return l->mask->placement.value_or(l->transform);return l->transform;}
    std::unordered_set<std::string> ids(selected.ids.begin(),selected.ids.end());for(const auto&id:selected.ids){auto children=layers::descendants(*p->document,id);ids.insert(children.begin(),children.end());}
    double x0=1e100,y0=1e100,x1=-1e100,y1=-1e100;bool found=false;
    for(const auto&entry:editing_transform::visiblePlacements(*p->document))if(ids.contains(entry.id)){auto box=editing_transform::bounds(entry.transform);x0=std::min(x0,box.x);y0=std::min(y0,box.y);x1=std::max(x1,box.x+box.width);y1=std::max(y1,box.y+box.height);found=true;}
    if(!found)return {};return Transform{x0,y0,x1-x0,y1-y0};
}
bool MainWindow::beginTransform(Point point,Qt::KeyboardModifiers modifiers){
    const bool selectionTool=tool_==Tool::Marquee||tool_==Tool::Lasso||tool_==Tool::Polygon||tool_==Tool::Wand;
    auto*p=current();if(!p||!p->document)return tool_==Tool::Move;
    if(selectionTool&&modifiers.testFlag(Qt::ControlModifier)&&!transformSession_&&!selectionGesture_.active()&&canEditLayers()) {
        auto*l=active();const auto& selection=p->document->selection;
        if(!selection||!selection->coverage||selection->coverage->pixel(int(std::floor(point.x)),int(std::floor(point.y)))==0)return false;
        if(!l||!l->raster||l->group||p->maskSelected||layerSelection().ids.size()!=1)return true;
        const auto visible=editing_transform::visiblePlacements(*p->document);if(std::none_of(visible.begin(),visible.end(),[&](const auto&entry){return entry.id==l->id;}))return true;
        try {
            auto state=std::make_unique<editing_transform::TransformSessionState>();state->original=*p->document;state->originalActive=p->active;state->originalSelected=p->selected;state->originalMaskSelected=p->maskSelected;
            state->target=l->id;state->ids={l->id};state->pixelMove=true;
            state->pixels=std::make_unique<editing_transform::PixelTransformSession>(*l,selection->coverage,editing_transform::PixelTransformKind::Move,modifiers.testFlag(Qt::AltModifier));
            if(!state->pixels->begin())return true;
            state->draft=state->originalBox=state->pixels->originalFloatingTransform();
            finishOpacityEdit();p->history.begin(modifiers.testFlag(Qt::AltModifier)?"Duplicate Pixels":"Move Pixels",p->document,p->active);
            transformOwner_=p;transformSession_=std::move(state);beginPreviewGesture(p->canvas);transformDrag_=editing_transform::Drag{transformSession_->draft,point,{}};
        }catch(const std::exception& error){cancelTransformSession();statusBar()->showMessage(error.what());}
        return true;
    }
    if(tool_!=Tool::Move)return false;
    try {
        auto transform=selectedTransform();auto mapping=canvas()->viewMapping();
        std::optional<editing_transform::OverlayGeometry> overlay;
        if(transform)overlay=transformSession_&&transformSession_->corners?editing_transform::OverlayGeometry::fromCorners(*transformSession_->corners,mapping):editing_transform::OverlayGeometry::fromTransform(*transform,mapping);
        auto visible=editing_transform::visiblePlacements(*p->document);
        editing_transform::PressContext context;context.activeId=p->active;context.activeTransform=transform;context.visibleLayers=visible;context.autoSelect=autoSelectLayers_;context.controlsVisible=transformControls_;
        context.hasEdit=bool(transformSession_);context.persistent=transformSession_&&transformSession_->persistent;context.hasDistortion=transformSession_&&transformSession_->corners.has_value();
        if(layerSelection().ids.size()>1||(active()&&active()->group))context.groupBox=transform;
        const auto intent=editing_transform::resolvePress(context,mapping.toView(point),mapping,overlay,{modifiers.testFlag(Qt::ShiftModifier),modifiers.testFlag(Qt::AltModifier),modifiers.testFlag(Qt::ControlModifier),false});
        if(!intent)return true;
        if(intent->picked){p->active=intent->layerId;p->selected={p->active};p->maskSelected=false;}
        if(!transformSession_&&!startTransformSession(false))return true;
        auto& state=*transformSession_;
        if(intent->mode.kind==editing_transform::ModeKind::Distort&&!state.corners)state.corners=editing_transform::corners(state.draft);
        state.duplicateOnDrag=intent->duplicateOnFirstDrag&&!state.persistent&&!state.pixels&&!state.maskOnly&&state.ids.size()==1&&!active()->group;
        if(state.duplicateOnDrag){p->history.cancel();p->history.begin("Duplicate Layer",state.original,state.originalActive);}
        transformDrag_=editing_transform::Drag{state.draft,point,intent->mode,state.corners};
        beginPreviewGesture(p->canvas);if(canvas())canvas()->setCompositeHeld(true);refresh(false,false);
    }catch(const std::exception& error){cancelTransformSession();statusBar()->showMessage(error.what());}
    return true;
}
void MainWindow::updateTransform(Point point,Qt::KeyboardModifiers modifiers,bool finish){
    auto*p=current();if(!p||!p->document||!transformDrag_||!transformSession_)return;
    if(p!=transformOwner_){cancelTransformSession();return;}
    try {
        canvas()->setSnapGuides();
        auto& state=*transformSession_;
        if(state.pixelMove) {
            state.moveOffset={std::round(point.x-transformDrag_->start.x),std::round(point.y-transformDrag_->start.y)};
            state.draft=state.originalBox;state.draft.x+=state.moveOffset.x;state.draft.y+=state.moveOffset.y;
        } else {
            if(state.duplicateOnDrag&&!state.duplicated&&point!=transformDrag_->start) {
                auto source=std::find_if(state.original.layers.begin(),state.original.layers.end(),[&](const Layer& layer){return layer.id==state.target;});
                if(source==state.original.layers.end())throw std::runtime_error("The duplicate target was removed");
                Layer copy=*source;copy.id=newId();copy.name+=" Copy";state.target=copy.id;state.ids={copy.id};state.original.layers.insert(source+1,std::move(copy));state.duplicated=true;
            }
            auto visible=editing_transform::visiblePlacements(state.original);auto targets=editing_transform::collectSnapTargets({double(p->document->width),double(p->document->height)},visible,state.ids);if(snapToGuides_)for(const auto&guide:p->document->guides)(guide.horizontal?targets.ys:targets.xs).push_back(guide.position);
            // Windows Ctrl retains handle distortion and temporarily suppresses
            // move snapping. Source Command and Control are context-mapped here.
            auto preview=editing_transform::previewDrag(*transformDrag_,point,lockRatio_,{modifiers.testFlag(Qt::ShiftModifier),modifiers.testFlag(Qt::AltModifier),modifiers.testFlag(Qt::ControlModifier),!snapping_||modifiers.testFlag(Qt::ControlModifier)},targets,canvas()->pointsPerPixel());
            if(preview.accepted){state.draft=preview.transform;state.corners=preview.distortion;if(!finish)canvas()->setSnapGuides(preview.snap.x,preview.snap.y);}
        }
        const bool plainMove=!state.pixels&&!state.corners&&!state.maskOnly&&!state.group&&state.ids.size()==1&&transformDrag_->mode.kind==editing_transform::ModeKind::Move;
        const Layer* moving=nullptr;if(plainMove)for(const auto& layer:state.original.layers)if(layer.id==state.ids.front())moving=&layer;
        const auto preview=moving?planMovePreview(state.original,*moving):MovePreviewPlan{};
        const bool fast=moving&&(preview.sprite||preview.covered||preview.masked);
        if(fast){previewDraft_=state.draft;previewLayerId_=moving->id;}
        if(finish&&!state.persistent){applyTransformSession();return;}
        if(!finish&&fast&&canvas()&&canvas()->viewportBounds().valid&&movePreviewJob_){
            try{
                if(canvas()->previewMatches(moving->id,previewGesture_))canvas()->setMovePreviewPlacement(state.draft);
                else if(!movePreviewJob_->covers(previewGesture_)){
                    const auto bounds=canvas()->viewportBounds();
                    ui::MovePreviewJob::Request request;
                    request.gesture=previewGesture_;request.layerId=moving->id;request.opacity=preview.masked?1:moving->opacity;request.anchorX=moving->transform.x;request.anchorY=moving->transform.y;
                    request.viewX=bounds.x;request.viewY=bounds.y;request.viewWidth=bounds.width;request.viewHeight=bounds.height;request.units=bounds.units;
                    request.pixelsWide=std::max(1,int(std::ceil(bounds.width/bounds.units)));request.pixelsHigh=std::max(1,int(std::ceil(bounds.height/bounds.units)));
                    if(!preview.masked)request.sprite=moving->raster;
                    std::unordered_set<std::string> above;for(const auto* layer:preview.above)above.insert(layer->id);
                    auto backIds=above;backIds.insert(moving->id);request.back=hideLayers(state.original,backIds);
                    if(!above.empty()){request.wantFront=true;std::unordered_set<std::string> hidden;for(const auto& layer:state.original.layers)if(!above.contains(layer.id))hidden.insert(layer.id);request.front=hideLayers(state.original,hidden);}
                    if(preview.masked){const auto plate=ui::movingLayerPlateBounds(*moving,bounds.units);if(!plate.valid)throw std::runtime_error("The moving layer does not fit a preview plate");
                        request.wantPlate=true;request.plateX=plate.x;request.plateY=plate.y;request.plateWidth=plate.width;request.plateHeight=plate.height;request.platePixelsWide=plate.pixelsWide;request.platePixelsHigh=plate.pixelsHigh;
                        std::unordered_set<std::string> hidden;for(const auto& layer:state.original.layers)if(layer.id!=moving->id)hidden.insert(layer.id);request.plate=hideLayers(state.original,hidden);}
                    movePreviewJob_->submit(std::move(request));
                }
                refreshing_=true;canvas()->setNavigatorDeferred(true);canvas()->setTransformOverlay(transformControls_||transformSession_?std::optional(state.draft):std::nullopt);
                const double values[]{state.draft.x,state.draft.y,state.draft.width,state.draft.height,state.draft.rotation};for(int i=0;i<5;++i)if(geometry_[i])ui::synchronizeNumber(geometry_[i],values[i]);refreshing_=false;return;
            }catch(const std::exception&){canvas()->clearMovePreview();}
        }
        if(finish&&canvas()&&canvas()->hasMovePreview()){
            publishTransformSession(false);transformDrag_.reset();
            if(canvas()->viewportBounds().valid){scheduleViewportComposite(*p);refresh(false);}
            else{canvas()->clearMovePreview();refresh();}
            return;
        }
        publishTransformSession(false);
        if(finish)transformDrag_.reset();
        if(canvas()&&canvas()->viewportBounds().valid){canvas()->setNavigatorDeferred(true);canvas()->setCompositeHeld(true);scheduleViewportComposite(*p);refresh(false);}
        else{if(canvas())canvas()->setCompositeHeld(false);refresh();}
    }catch(const std::exception& error){cancelTransformSession();statusBar()->showMessage(error.what());}
}
void MainWindow::setupTransformActions(){
    auto*bar=addToolBar("Transform Options");bar->setObjectName("transformOptions");
    auto*autoSelect=new QCheckBox("Auto-select layer");autoSelect->setObjectName("transformAutoSelect");bar->addWidget(autoSelect);connect(autoSelect,&QCheckBox::toggled,this,[this](bool value){autoSelectLayers_=value;});
    auto*controls=new QCheckBox("Show controls");controls->setObjectName("transformShowControls");controls->setChecked(true);bar->addWidget(controls);connect(controls,&QCheckBox::toggled,this,[this](bool value){transformControls_=value;refresh(false,false);});
    auto*ratio=new QCheckBox("Lock aspect ratio");ratio->setObjectName("transformLockRatio");bar->addWidget(ratio);connect(ratio,&QCheckBox::toggled,this,[this](bool v){lockRatio_=v;});
    auto*snap=new QCheckBox("Snap to edges and centers");snap->setObjectName("transformSnapping");snap->setChecked(true);bar->addWidget(snap);connect(snap,&QCheckBox::toggled,this,[this](bool v){snapping_=v;});
    auto*scale=new ui::PropertyNumber;scale->releaseFocus=[this]{if(canvas())canvas()->setFocus();};scale->setObjectName("transformScale");scale->setAccessibleName("Transform scale percent");scale->setRange(.01,100000);scale->setDecimals(2);scale->setSuffix(" %");scale->setValue(100);bar->addWidget(scale);connect(scale,&QDoubleSpinBox::valueChanged,this,[this](double value){changeTransformDraft([&](Transform& draft){draft=editing_transform::scaledPercent(draft,value,transformScalePixelSize());});});
    auto*sampling=new QComboBox;sampling->setObjectName("transformSampling");sampling->addItems({"Nearest","Smooth","High quality"});sampling->setCurrentIndex(2);sampling->setAccessibleName("Transform sampling");bar->addWidget(sampling);connect(sampling,&QComboBox::currentIndexChanged,this,[this](int i){if(i>=0&&i<=2)changeTransformDraft([&](Transform& draft){draft.sampling=Transform::Sampling(i);});});
    for(bool horizontal:{true,false}){auto*flip=bar->addAction(horizontal?"Flip H":"Flip V");flip->setObjectName(horizontal?"transformFlipH":"transformFlipV");bindCommand(flip,"Transform Options",horizontal?"Flip H":"Flip V",[this,horizontal]{changeTransformDraft([&](Transform& draft){draft=editing_transform::flippedLocal(draft,horizontal);});});}
    auto*menu=menuBar()->addMenu("&Transform");
    auto*free=menu->addAction("Free Transform");free->setObjectName("freeTransform");free->setShortcut(QKeySequence("Ctrl+T"));bindCommand(free,"Transform","Free Transform",[this]{startTransformSession(true);});
    auto*distort=menu->addAction("Distort");distort->setObjectName("distortTransform");bindCommand(distort,"Transform","Distort",[this]{startTransformSession(true,true);});
    auto*apply=menu->addAction("Apply Transform");apply->setObjectName("applyTransform");bar->addAction(apply);bindCommand(apply,"Transform","Apply Transform",[this]{applyTransformSession();});
    auto*cancel=menu->addAction("Cancel Transform");cancel->setObjectName("cancelTransform");bar->addAction(cancel);bindCommand(cancel,"Transform","Cancel Transform",[this]{cancelTransformSession();});
    updateTransformActions();
    action(menu,"Flip Layer Horizontally",{},[this]{if(current()&&current()->document)edit("Flip Horizontal",[&](Document&d){d=editing::flipLayers(d,layerSelection().ids,true);});});
    action(menu,"Flip Layer Vertically",{},[this]{if(current()&&current()->document)edit("Flip Vertical",[&](Document&d){d=editing::flipLayers(d,layerSelection().ids,false);});});
    action(menu,"Scale…",{},[this]{auto transform=selectedTransform();if(!transform||(transformSession_&&transformSession_->corners))return;bool ok;const double value=QInputDialog::getDouble(this,"Scale","Scale (%)",editing_transform::scalePercent(*transform,transformScalePixelSize()),.01,100000,2,&ok);if(ok)changeTransformDraft([&](Transform& draft){draft=editing_transform::scaledPercent(draft,value,transformScalePixelSize());});});
}
}
