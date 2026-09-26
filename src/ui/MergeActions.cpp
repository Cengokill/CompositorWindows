#include "MainWindow.h"
#include "LayerMergeJob.h"
#include <QStatusBar>
#include <stdexcept>
namespace compositor {
void MainWindow::mergeLayers(){
    auto* owner=current();
    if(!owner||!owner->document||owner->projectBusy||ui::LayerMergeJob::find(this))return;
    const auto selected=layerSelection();
    if(!layers::mergePlan(*owner->document,selected))return;
    const auto before=owner->document;const auto beforeActive=owner->active;
    QPointer<NativeCanvas> token=owner->canvas;
    ui::LayerMergeJob::Host host;
    host.settled=[this,owner,token]{if(!token)return;owner->projectBusy=false;refresh(false,false);};
    host.error=[this](const QString& error){statusBar()->showMessage(error);};
    host.commit=[this,owner,token,before,beforeActive](layers::EditResult result){
        if(!token)return;
        if(owner->document!=before||owner->active!=beforeActive)
            throw std::runtime_error("The source layers changed; the prepared merge was not applied");
        if(!result.changed)return;
        std::optional<Document> document=std::move(result.document);
        auto active=std::move(result.selection.primary);
        auto selected=std::move(result.selection.ids);
        owner->history.begin(result.action,owner->document,owner->active);
        try{
            owner->document=std::move(document);owner->active=std::move(active);
            owner->history.end(owner->document,owner->active);
        }catch(...){
            if(auto snapshot=owner->history.cancel()){
                owner->document=std::move(snapshot->document);owner->active=std::move(snapshot->activeLayer);
            }
            throw;
        }
        owner->selected=std::move(selected);owner->maskSelected=false;
        if(current()==owner)refresh();
    };
    owner->projectBusy=true;refresh(false,false);
    try{new ui::LayerMergeJob(*before,selected,std::move(host),this);}
    catch(...){owner->projectBusy=false;refresh(false,false);throw;}
}
}
