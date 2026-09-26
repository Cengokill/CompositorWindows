#include "MainWindow.h"
#include "EditPanelSession.h"
#include <QDialog>
#include <QStatusBar>

namespace compositor {
namespace {
struct EditViewState {
    Layer original;
    bool live{},livePending{};
    std::shared_ptr<const Document> rendered,cached;
    std::optional<Document> canonical;
    std::shared_ptr<const Document> view(const Document& current){
        if(!rendered)return {};
        if(canonical&&*canonical==current)return cached;
        auto target=std::find_if(current.layers.begin(),current.layers.end(),[&](const Layer& l){return l.id==original.id;});
        auto result=std::find_if(rendered->layers.begin(),rendered->layers.end(),[&](const Layer& l){return l.id==original.id;});
        if(target==current.layers.end()||result==rendered->layers.end()){canonical=current;cached.reset();return {};}
        Document shown=current;auto& layer=shown.layers[size_t(target-current.layers.begin())];
        if(live)layer.adjustmentJson=result->adjustmentJson;
        else {
            layer.raster=result->raster;
            // Filters have a fixed grown preview placement; Hue and ordinary
            // ungrown previews follow the current layer transform after Undo.
            if(result->transform!=original.transform){
                layer.transform=result->transform;
                if(layer.mask&&!layer.mask->placement)layer.mask->placement=target->transform;
            }
        }
        auto made=std::make_shared<const Document>(std::move(shown));
        canonical=current;cached=std::move(made);return cached;
    }
};
}
std::shared_ptr<const Document> MainWindow::editPanelPreview(EditorProject& project)const{
    project.effectPreview=editPanel_&&editPanel_->canvas()==project.canvas?editPanel_->previewDocument():nullptr;
    return project.effectPreview;
}
ui::EditPanelHost MainWindow::makeEditPanelHost(EditorProject& project,const Document& document,std::string action){
    auto* owner=&project;QPointer<NativeCanvas> token=project.canvas;
    auto original=std::find_if(document.layers.begin(),document.layers.end(),[&](const Layer& layer){return layer.id==project.active;});
    if(original==document.layers.end())throw std::runtime_error("The edit target no longer exists");
    auto state=std::make_shared<EditViewState>();state->original=*original;state->live=!original->adjustmentJson.empty();
    auto present=[this,owner,token]{return token&&std::any_of(projects_.begin(),projects_.end(),[owner](const auto& item){return item.get()==owner;});};
    ui::EditPanelHost host;host.canvas=project.canvas;
    // The source keeps Hue/Filter editors through permitted history/imports.
    // Input identity is validated only when the completed pixels are applied.
    host.valid=[this,owner,present]{return present()&&current()==owner;};
    host.view=[owner,present,state]{return present()&&owner->document?state->view(*owner->document):nullptr;};
    host.currentDocument=[owner,present]{return present()?owner->document:std::optional<Document>{};};
    host.openColor=[this](effects_tools::PaletteColor color,const QString& title,std::function<void(effects_tools::PaletteColor)> changed){openEditPanelColor(color,title,std::move(changed));};
    host.closeColor=[this](bool commit){closeEditPanelColor(commit);};
    host.stateChanged=[this,owner,present]{if(present()&&current()==owner)refresh(false,false);};
    host.sampleComposite=[owner,present,state](Point point)->std::optional<effects_tools::PaletteColor>{
        if(!present()||!owner->document)return {};
        auto shown=state->view(*owner->document);
        return effects_tools::sampleCompositeColor(shown?*shown:*owner->document,point,SoftwareRenderer());
    };
    host.preview=[this,owner,present,state](std::shared_ptr<const Document> preview){
        if(!present())return;state->rendered=std::move(preview);state->cached.reset();state->canonical.reset();
        owner->effectPreview=owner->document?state->view(*owner->document):nullptr;
        if(current()==owner)refresh(false,false);
    };
    host.commit=[this,owner,present,state,action](ui::EditPanelResult value){
        if(!present()||current()!=owner||!owner->document)return;
        auto target=std::find_if(owner->document->layers.begin(),owner->document->layers.end(),[&](const Layer& l){return l.id==state->original.id;});
        auto rendered=std::find_if(value.document.layers.begin(),value.document.layers.end(),[&](const Layer& l){return l.id==state->original.id;});
        if(target==owner->document->layers.end()||rendered==value.document.layers.end())return;
        if(!state->live&&(target->raster!=state->original.raster||
            (editPanel_&&editPanel_->kind()!=ui::EditPanelSession::Kind::Hue&&target->transform!=state->original.transform)))return;
        Document prepared=*owner->document;auto& changed=prepared.layers[size_t(target-owner->document->layers.begin())];
        if(state->live)changed.adjustmentJson=rendered->adjustmentJson;
        else if(value.mergeLayer)changed=value.mergeLayer(*target);
        else {changed.raster=rendered->raster;changed.shapeJson=rendered->shapeJson;}
        validateDocument(prepared);
        if(!state->live)owner->history.begin(action,owner->document,owner->active);
        try{owner->document=std::move(prepared);owner->history.end(owner->document,owner->active);state->livePending=false;if(value.maskSelected)owner->maskSelected=true;}
        catch(...){if(auto snapshot=owner->history.cancel()){owner->document=std::move(snapshot->document);owner->active=std::move(snapshot->activeLayer);}state->livePending=false;throw;}
    };
    host.closed=[this,owner,present,state]{
        editPanel_=nullptr;
        if(present()){
            if(state->livePending){
                // Live settings never replaced canonical metadata during preview.
                // Cancel still ends the source transaction so independently
                // accepted imports survive; only a failed end rolls it back.
                try{owner->history.end(owner->document,owner->active);}
                catch(const std::exception& error){if(auto snapshot=owner->history.cancel()){owner->document=std::move(snapshot->document);owner->active=std::move(snapshot->activeLayer);}statusBar()->showMessage(QString::fromUtf8(error.what()));}
                state->livePending=false;
            }
            owner->effectPreview.reset();if(current()==owner){refresh();owner->canvas->setFocus();}
        }
    };
    host.error=[this](const QString& message){statusBar()->showMessage(message);};
    if(state->live){owner->history.begin(action,owner->document,owner->active);state->livePending=true;}
    return host;
}
void MainWindow::populateEditPanelCommandState(ui::CommandState& state,EditorProject* owner)const{
    if(!editPanel_||!owner||editPanel_->canvas()!=owner->canvas)return;
    state.levels=editPanel_->kind()==ui::EditPanelSession::Kind::Levels;
    state.hueSaturation=editPanel_->kind()==ui::EditPanelSession::Kind::Hue;
    state.filterEdit=editPanel_->kind()==ui::EditPanelSession::Kind::Filter;
    state.adjustmentEditing=editPanel_->live();
    state.projectBusy=state.projectBusy||editPanel_->committing();
}
bool MainWindow::beginEditPanelPointer(Point point,Qt::KeyboardModifiers modifiers){
    auto* owner=current();if(!editPanel_||!owner||editPanel_->canvas()!=owner->canvas)return false;
    if(editPanel_->committing())return true;
    if(tool_==Tool::Hand||tool_==Tool::Zoom)return false;
    const auto view=owner->canvas->viewMapping().toView(point);
    if(editPanel_->samplePress(point,view.x,modifiers))return true;
    // EditorCanvas1177-1181 gives Levels sampling priority, then prevents
    // ordinary pointer edits while its panel remains open.
    if(editPanel_->kind()==ui::EditPanelSession::Kind::Levels)return true;
    const bool temporaryPalette=modifiers.testFlag(Qt::AltModifier)&&
        (tool_==Tool::Brush||tool_==Tool::Eraser||tool_==Tool::SpotHealing||tool_==Tool::Gradient);
    if(colorPicker_||tool_==Tool::Eyedropper||temporaryPalette)return false;
    return true;
}
bool MainWindow::updateEditPanelPointer(Point point,Qt::KeyboardModifiers modifiers,bool finish){
    auto* owner=current();if(!editPanel_||!owner||editPanel_->canvas()!=owner->canvas)return false;
    const auto view=owner->canvas->viewMapping().toView(point);
    if(editPanel_->sampleMove(point,view.x,modifiers,finish))return true;
    if(editPanel_->committing())return true;
    return tool_!=Tool::Hand&&tool_!=Tool::Zoom&&!samplingPalette_;
}
void MainWindow::cancelEditPanel(){if(editPanel_)editPanel_->cancel();}
}
