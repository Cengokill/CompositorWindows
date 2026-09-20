#include "MainWindow.h"
#include <QApplication>
#include <QClipboard>
#include <QMessageBox>
#include <QMimeData>
#include <QDebug>
#include <algorithm>

namespace compositor {
ui::CommandState MainWindow::commandState(EditorProject* owner){
    ui::CommandState state;
    auto* project=owner?owner:current();
    state.modalDialog=QApplication::activeModalWidget()!=nullptr;
    state.brushStroke=bool(stroke_);state.warpStroke=bool(retouch_);
    state.gradient=drawingOriginal_.has_value();state.crop=cropDraft_.has_value();
    state.transformEdit=bool(transformSession_);
    state.persistentTransform=transformSession_&&transformSession_->persistent;
    state.pixelMove=transformSession_&&bool(transformSession_->pixels);
    state.colorPicker=!colorPicker_.isNull();state.lassoDraft=selectionGesture_.active();
    state.moveTool=tool_==Tool::Move;
    const auto* mime=QApplication::clipboard()->mimeData();
    state.clipboardImage=mime&&(mime->hasImage()||mime->hasFormat("image/png"));
    if(!project)return state;
    state.projectBusy=project->projectBusy;state.importing=project->importing;
    state.historyUndo=project->history.canUndo();state.historyRedo=project->history.canRedo();
    state.document=project->document.has_value();if(!state.document)return state;
    const auto& document=*project->document;
    state.selection=document.selection.has_value();
    if(document.selection){
        if(document.selection->outline)state.selectionEmpty=document.selection->outline->empty();
        else {const auto& coverage=document.selection->coverage;state.selectionEmpty=!coverage||std::none_of(coverage->pixels.begin(),coverage->pixels.end(),[](uint8_t value){return value!=0;});}
    }
    state.renderHasPixels=std::any_of(document.layers.begin(),document.layers.end(),[](const Layer& layer){return layer.visible&&bool(layer.raster);});
    const auto found=std::find_if(document.layers.begin(),document.layers.end(),[&](const Layer& layer){return layer.id==project->active;});
    const auto selected=layers::normalizeSelection(document,{project->selected.empty()?std::vector<std::string>{project->active}:project->selected,project->active});
    state.singleSelected=selected.ids.size()==1;
    state.canMerge=layers::mergePlan(document,selected).has_value();
    state.hasOtherProject=std::any_of(projects_.begin(),projects_.end(),[&](const auto& item){return item.get()!=project&&!item->importing&&!item->projectBusy;});
    if(found==document.layers.end())return state;
    const auto& layer=*found;state.activeLayer=true;state.group=layer.group;state.asset=bool(layer.raster);
    state.mask=layer.mask.has_value();state.maskEnabled=layer.mask&&layer.mask->enabled;
    state.maskSelected=project->maskSelected&&state.mask;
    state.adjustmentLayer=!layer.adjustmentJson.empty();state.shape=!layer.shapeJson.empty();state.hasParent=!layer.parentId.empty();
    const auto entries=layers::entries(document);
    state.effectiveVisible=std::any_of(entries.begin(),entries.end(),[&](const auto& entry){return entry.id==layer.id&&entry.visible;});
    state.transformable=project==current()&&(state.singleSelected&&!state.group?(state.asset&&state.effectiveVisible):selectedTransform().has_value());
    state.canMoveUp=layers::canMoveSibling(document,layer.id,1);state.canMoveDown=layers::canMoveSibling(document,layer.id,-1);
    state.canToggleClipping=layers::canToggleClipping(document,layer.id);
    return state;
}
void MainWindow::initializeCommands(){
    commands_=new ui::CommandRegistry(this,[this]{return commandState();},[this](ui::CommandPreparation preparation,const QString&){
        if(preparation==ui::CommandPreparation::None)return;
        finishOpacityEdit();
        if(preparation==ui::CommandPreparation::CommitTransformAndGradient){
            applyGradient();if(transformSession_&&transformSession_->persistent)applyTransformSession();
        }else if(preparation==ui::CommandPreparation::ProjectOperation){
            cropDraft_.reset();cropDrag_.reset();
            if(transformSession_)applyTransformSession();
        }
    });
    commands_->setErrorHandler([this](const QString& error){QMessageBox::critical(this,"Compositor",error);});
    connect(QApplication::clipboard(),&QClipboard::dataChanged,this,[this]{if(commands_)commands_->refresh();});
}
void MainWindow::bindCommand(QAction* action,const QString& menu,const QString& label,std::function<void()> invoke){
    const auto* spec=ui::commandSpec(menu,label);
    if(!spec){action->setEnabled(false);action->setProperty("unregisteredCommand",true);qWarning()<<"Unregistered command"<<menu<<label;return;}
    commands_->bind(action,*spec,[this,invoke=std::move(invoke)]{
        try{invoke();}catch(const std::exception& error){QMessageBox::critical(this,"Compositor",error.what());}
    });
}
}
