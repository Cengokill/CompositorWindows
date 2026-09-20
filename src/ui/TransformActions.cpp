#include "MainWindow.h"
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
            transformOwner_=p;transformSession_=std::move(state);transformDrag_=editing_transform::Drag{transformSession_->draft,point,{}};
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
        refresh(false,false);
    }catch(const std::exception& error){cancelTransformSession();statusBar()->showMessage(error.what());}
    return true;
}
void MainWindow::updateTransform(Point point,Qt::KeyboardModifiers modifiers,bool finish){
    auto*p=current();if(!p||!p->document||!transformDrag_||!transformSession_)return;
    if(p!=transformOwner_){cancelTransformSession();return;}
    try {
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
            auto visible=editing_transform::visiblePlacements(state.original);auto targets=editing_transform::collectSnapTargets({double(p->document->width),double(p->document->height)},visible,state.ids);
            auto preview=editing_transform::previewDrag(*transformDrag_,point,lockRatio_,{modifiers.testFlag(Qt::ShiftModifier),modifiers.testFlag(Qt::AltModifier),modifiers.testFlag(Qt::ControlModifier),!snapping_},targets,canvas()->pointsPerPixel());
            if(preview.accepted){state.draft=preview.transform;state.corners=preview.distortion;}
        }
        if(finish&&!state.persistent){applyTransformSession();return;}
        publishTransformSession(false);
        if(finish)transformDrag_.reset();
        refresh();
    }catch(const std::exception& error){cancelTransformSession();statusBar()->showMessage(error.what());}
}
void MainWindow::setupTransformActions(){
    auto*bar=addToolBar("Transform Options");bar->setObjectName("transformOptions");
    auto*autoSelect=new QCheckBox("Auto-select layer");autoSelect->setObjectName("transformAutoSelect");bar->addWidget(autoSelect);connect(autoSelect,&QCheckBox::toggled,this,[this](bool value){autoSelectLayers_=value;});
    auto*controls=new QCheckBox("Show controls");controls->setObjectName("transformShowControls");controls->setChecked(true);bar->addWidget(controls);connect(controls,&QCheckBox::toggled,this,[this](bool value){transformControls_=value;refresh(false,false);});
    auto*ratio=new QCheckBox("Lock aspect ratio");ratio->setObjectName("transformLockRatio");bar->addWidget(ratio);connect(ratio,&QCheckBox::toggled,this,[this](bool v){lockRatio_=v;});
    auto*snap=new QCheckBox("Snap to edges and centers");snap->setObjectName("transformSnapping");snap->setChecked(true);bar->addWidget(snap);connect(snap,&QCheckBox::toggled,this,[this](bool v){snapping_=v;});
    auto*sampling=new QComboBox;sampling->setObjectName("transformSampling");sampling->addItems({"Nearest","Smooth","High quality"});sampling->setCurrentIndex(2);sampling->setAccessibleName("Transform sampling");bar->addWidget(sampling);connect(sampling,&QComboBox::currentIndexChanged,this,[this](int i){if(transformSession_){transformSession_->draft.sampling=Transform::Sampling(i);publishTransformSession(false);refresh();}else if(active())edit("Transform Sampling",[&](Document&){active()->transform.sampling=Transform::Sampling(i);});});
    for(bool horizontal:{true,false}){auto*flip=bar->addAction(horizontal?"Flip H":"Flip V");flip->setObjectName(horizontal?"transformFlipH":"transformFlipV");bindCommand(flip,"Transform Options",horizontal?"Flip H":"Flip V",[this,horizontal]{try{const bool pending=bool(transformSession_);if(!pending&&!startTransformSession(false))return;transformSession_->draft=editing_transform::flippedLocal(transformSession_->draft,horizontal);publishTransformSession(false);if(pending)refresh();else applyTransformSession();}catch(const std::exception& error){cancelTransformSession();statusBar()->showMessage(error.what());}});}
    auto*menu=menuBar()->addMenu("&Transform");
    auto*free=menu->addAction("Free Transform");free->setObjectName("freeTransform");free->setShortcut(QKeySequence("Ctrl+T"));bindCommand(free,"Transform","Free Transform",[this]{startTransformSession(true);});
    auto*distort=menu->addAction("Distort");distort->setObjectName("distortTransform");bindCommand(distort,"Transform","Distort",[this]{startTransformSession(true,true);});
    auto*apply=menu->addAction("Apply Transform");apply->setObjectName("applyTransform");bar->addAction(apply);bindCommand(apply,"Transform","Apply Transform",[this]{applyTransformSession();});
    auto*cancel=menu->addAction("Cancel Transform");cancel->setObjectName("cancelTransform");bar->addAction(cancel);bindCommand(cancel,"Transform","Cancel Transform",[this]{cancelTransformSession();});
    updateTransformActions();
    action(menu,"Flip Layer Horizontally",{},[this]{if(current()&&current()->document)edit("Flip Horizontal",[&](Document&d){d=editing::flipLayers(d,layerSelection().ids,true);});});
    action(menu,"Flip Layer Vertically",{},[this]{if(current()&&current()->document)edit("Flip Vertical",[&](Document&d){d=editing::flipLayers(d,layerSelection().ids,false);});});
    action(menu,"Scale…",{},[this]{if(!active()||!active()->raster)return;bool ok;double value=QInputDialog::getDouble(this,"Scale Layer","Scale (%)",editing_transform::scalePercent(active()->transform,{double(active()->raster->width),double(active()->raster->height)}),.01,100000,2,&ok);if(ok)edit("Scale Layer",[&](Document&){auto*l=active();l->transform=editing_transform::scaledPercent(l->transform,value,{double(l->raster->width),double(l->raster->height)});if(!l->shapeJson.empty())*l=editing::redrawShape(*l);});});
}
}
