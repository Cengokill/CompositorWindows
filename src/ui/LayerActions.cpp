#include "MainWindow.h"
#include "LayerPanel.h"
#include "LayerCopyCommit.h"
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QInputDialog>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QTimer>

namespace compositor {
layers::SelectionState MainWindow::layerSelection()const{
    int index=tabs_->currentIndex();if(index<0||index>=int(projects_.size())||!projects_[index]->document)return {};
    const auto&p=*projects_[index];return layers::normalizeSelection(*p.document,{p.selected.empty()?std::vector<std::string>{p.active}:p.selected,p.active});
}
void MainWindow::applyLayerEdit(layers::EditResult result){
    if(!result.changed)return;edit(result.action.c_str(),[&](Document&d){d=std::move(result.document);current()->active=result.selection.primary;current()->selected=result.selection.ids;});
}
void MainWindow::newBlankLayer(){
    if(!canEditLayers())return;
    edit("New Blank Layer",[this](Document& document){
        Layer layer;layer.id=newId();layer.transform={0,0,double(document.width),double(document.height)};
        int number=1;while(std::any_of(document.layers.begin(),document.layers.end(),[&](const Layer& item){return item.name=="Layer "+std::to_string(number);}))++number;
        layer.name="Layer "+std::to_string(number);
        auto selected=std::find_if(document.layers.begin(),document.layers.end(),[&](const Layer& item){return item.id==current()->active;});
        size_t insertion=document.layers.size();
        if(selected!=document.layers.end()){
            insertion=size_t(selected-document.layers.begin())+1;layer.parentId=selected->group?selected->id:selected->parentId;
            if(selected->group){const auto children=layers::descendants(document,selected->id);for(size_t i=0;i<document.layers.size();++i)if(children.contains(document.layers[i].id))insertion=std::max(insertion,i+1);}
        }
        if(!layer.parentId.empty())current()->collapsedGroups.erase(layer.parentId);
        current()->active=layer.id;current()->selected={layer.id};current()->maskSelected=false;
        document.layers.insert(document.layers.begin()+insertion,std::move(layer));
    });
}
void MainWindow::finishVisibilitySwipe(){
    auto* owner=visibilityOwner_;visibilityOwner_=nullptr;
    if(owner){
        try{owner->history.end(owner->document,owner->active);}
        catch(...){if(auto snapshot=owner->history.cancel()){owner->document=std::move(snapshot->document);owner->active=std::move(snapshot->activeLayer);}throw;}
        if(owner==current())refresh();
    }
}
void MainWindow::layerCommand(int command){
    applyGradient();if(transformSession_&&transformSession_->persistent)applyTransformSession();pointerCancel();
    auto*p=current();if(!p||!p->document)return;const auto&d=*p->document;auto selection=layerSelection();
    switch(command){
        case 0:applyLayerEdit(layers::addGroup(d,selection));break;
        case 1:applyLayerEdit(layers::group(d,selection));break;
        case 2:applyLayerEdit(layers::moveOut(d,selection));break;
        case 3:applyLayerEdit(layers::duplicateLayer(d,selection,p->active));break;
        case 4:applyLayerEdit(layers::moveSibling(d,selection,1));break;
        case 5:applyLayerEdit(layers::moveSibling(d,selection,-1));break;
        case 6:{auto plan=layers::deletionPlan(d,selection);auto mode=layers::DeleteMode::Bake;
            if(!plan.dependents.empty()){QMessageBox box(QMessageBox::Question,"Delete Layers","Other layers use the selected layers as clipping sources.",QMessageBox::Cancel,this);auto*bake=box.addButton("Bake Appearance",QMessageBox::AcceptRole);auto*release=box.addButton("Remove Clipping Links",QMessageBox::DestructiveRole);box.exec();if(box.clickedButton()==bake)mode=layers::DeleteMode::Bake;else if(box.clickedButton()==release)mode=layers::DeleteMode::RemoveLinks;else return;}
            applyLayerEdit(layers::erase(d,selection,mode));break;}
        case 7:applyLayerEdit(layers::toggleClipping(d,selection,p->active));break;
        case 8:mergeLayers();break;
        case 9:{
            QStringList titles{"New Project"};std::vector<QPointer<QObject>> identities{nullptr};
            for(int i=0;i<int(projects_.size());++i)if(projects_[i].get()!=p&&!projects_[i]->importing&&!projects_[i]->projectBusy){identities.push_back(projects_[i]->canvas);titles.append(tabs_->tabText(i));}
            QInputDialog dialog(this);dialog.setWindowTitle("Copy Layer to Project");dialog.setLabelText("Destination project");dialog.setComboBoxItems(titles);dialog.setComboBoxEditable(false);
            auto* choices=dialog.findChild<QComboBox*>();if(!choices)throw std::runtime_error("Copy destination control is unavailable");
            for(int i=0;i<int(identities.size());++i)choices->setItemData(i,QVariant::fromValue(identities[size_t(i)].data()));
            if(dialog.exec()!=QDialog::Accepted)return;
            auto* identity=choices->currentData().value<QObject*>();EditorProject* target=nullptr;
            if(identity){if(choices->currentIndex()<1||identities[size_t(choices->currentIndex())].data()!=identity)return;for(auto& project:projects_)if(project->canvas==identity){target=project.get();break;}if(!target||target->importing||target->projectBusy)return;}
            else{if(choices->currentIndex()!=0)return;target=&addEmptyProject(false);}
            const bool first=!target->document;Document destination;
            if(target->document)destination=*target->document;
            else{destination.id=newId();destination.width=d.width;destination.height=d.height;Layer blank;blank.id=newId();blank.name="Layer 1";blank.transform={0,0,double(d.width),double(d.height)};destination.layers.push_back(blank);}
            auto result=layers::copySubtree(d,p->active,destination);
            ui::commitPreparedLayerCopy(*target,target->document,target->active,result.edit);
            for(int i=0;i<int(projects_.size());++i)if(projects_[i].get()==target)tabs_->setCurrentIndex(i);refresh();if(first)target->canvas->fit();break;
        }
        case 10:QTimer::singleShot(0,this,[this]{if(auto*item=layers_->currentItem())layers_->editItem(item,0);});break;
        case 11:if(active())edit("Layer Visibility",[this](Document&){active()->visible=!active()->visible;});break;
        case 12:applyLayerEdit(layers::releaseClipping(d,selection,p->active));break;
    }
}
void MainWindow::setupLayerActions(){
    QMenu*menu=nullptr;for(auto*a:menuBar()->actions())if(a->text()=="&Layer")menu=a->menu();if(!menu)return;
    action(menu,"New Group",{},[this]{layerCommand(0);});action(menu,"Group Selected",QKeySequence("Ctrl+G"),[this]{layerCommand(1);});action(menu,"Move Out of Group",{},[this]{layerCommand(2);});
    action(menu,"Create / Release Clipping Mask",QKeySequence("Ctrl+Alt+G"),[this]{layerCommand(7);});action(menu,"Merge Layers",QKeySequence("Ctrl+E"),[this]{layerCommand(8);});action(menu,"Copy Layer to Project…",{},[this]{layerCommand(9);});
    ui::LayerPanelController::Host host;
    host.dragOwner=[this]()->QObject*{auto*p=current();return p?p->canvas:nullptr;};
    host.prepare=[this]{if(refreshing_)return;applyGradient();if(transformSession_&&transformSession_->persistent)applyTransformSession();finishOpacityEdit();pointerCancel();};
    host.state=[this]{auto*p=current();return ui::PanelState{p&&p->document?&*p->document:nullptr,layerSelection(),p&&p->maskSelected,p?p->collapsedGroups:std::unordered_set<std::string>{}};};
    host.select=[this](layers::SelectionState selection,bool mask){auto*p=current();if(refreshing_||!p||!p->document||p->importing||p->projectBusy||selection.ids.empty())return;selection=layers::normalizeSelection(*p->document,std::move(selection));p->active=selection.primary;p->selected=selection.ids;p->maskSelected=mask&&selection.ids.size()==1&&active()&&active()->mask.has_value();refresh(false,false);if(auto*c=ui::LayerPanelController::find(layers_))c->updateSelection();};
    host.commit=[this](layers::EditResult result,bool mask){if(auto*p=current()){p->maskSelected=mask;applyLayerEdit(std::move(result));}};
    host.collapse=[this](const std::string& id,bool collapsed){auto*p=current();if(!p||!p->document)return;if(collapsed){auto children=layers::descendants(*p->document,id);if(children.contains(p->active)){p->active=id;p->selected={id};p->maskSelected=false;}p->collapsedGroups.insert(id);}else p->collapsedGroups.erase(id);if(auto*c=ui::LayerPanelController::find(layers_))c->updateSelection();};
    host.layerCommand=[this](int command){layerCommand(command);};host.maskCommand=[this](int command){maskCommand(command);};host.loadSelection=[this](const std::string&id,bool mask,Qt::KeyboardModifiers modifiers){loadLayerSelection(id,mask,modifiers);};host.error=[this](const QString& error){statusBar()->showMessage(error);};
    host.canEdit=[this]{return canEditLayers();};
    host.targetEnabled=[this]{auto* project=current();if(!project||!project->document)return false;const auto state=commandState(project);return !state.modalDialog&&!state.projectBusy&&!state.importing;};
    host.selectTarget=[this](const std::string& id,bool mask){selectLayerTarget(id,mask);};
    host.beginVisibilitySwipe=[this](const std::string& id)->std::optional<bool>{
        if(!canEditLayers())return {};finishVisibilitySwipe();finishOpacityEdit();
        auto* project=current();auto& document=*project->document;
        auto found=std::find_if(document.layers.begin(),document.layers.end(),[&](const Layer& layer){return layer.id==id;});if(found==document.layers.end())return {};
        const bool visible=!found->visible;visibilityOwner_=project;project->history.begin(visible?"Show Layer":"Hide Layer",project->document,project->active);
        found->visible=visible;refresh(false,false);return visible;
    };
    host.setVisibilityInSwipe=[this](const std::string& id,bool visible){
        if(!visibilityOwner_||!visibilityOwner_->document)return;
        for(auto& layer:visibilityOwner_->document->layers)if(layer.id==id){layer.visible=visible;break;}
        if(visibilityOwner_==current())refresh(false,false);
    };
    host.endVisibilitySwipe=[this]{finishVisibilitySwipe();};
    new ui::LayerPanelController(layers_,std::move(host));
    disconnect(layers_,&QTreeWidget::itemChanged,this,nullptr);
    connect(layers_,&QTreeWidget::itemChanged,this,[this](QTreeWidgetItem* item,int column){
        auto* owner=current();if(refreshing_||column!=0||!item||!owner||!owner->document||!canEditLayers())return;
        const auto id=item->data(0,Qt::UserRole).toString().toStdString();
        const auto name=item->text(0).trimmed().toStdString();const bool visible=item->checkState(0)==Qt::Checked;
        const auto documentId=owner->document->id;QPointer<NativeCanvas> token=owner->canvas;
        // A refresh rebuilds the tree. Qt still accesses the item after emitting
        // itemChanged from setData, so retain values and finish that call first.
        QTimer::singleShot(0,this,[this,owner,token,documentId,id,name,visible]{
            if(!token||current()!=owner||!owner->document||owner->document->id!=documentId||!canEditLayers())return;
            try{
                auto found=std::find_if(owner->document->layers.begin(),owner->document->layers.end(),[&](const Layer& layer){return layer.id==id;});
                if(found==owner->document->layers.end())return;
                const bool renamed=!name.empty()&&name!=found->name;if(!renamed&&visible==found->visible)return;
                applyGradient();if(transformSession_&&transformSession_->persistent)applyTransformSession();pointerCancel();
                edit(renamed?"Rename Layer":"Layer Visibility",[&](Document& document){for(auto& layer:document.layers)if(layer.id==id){layer.visible=visible;if(!name.empty())layer.name=name;}});
            }catch(const std::exception& error){statusBar()->showMessage(QString::fromUtf8(error.what()));}
        });
    });
    connect(layers_,&QTreeWidget::itemDoubleClicked,this,[this](QTreeWidgetItem*item,int column){if(!item||column!=0)return;const auto id=item->data(0,Qt::UserRole).toString().toStdString();selectLayerTarget(id,false);if(active()&&!active()->adjustmentJson.empty())adjust({},true,true);else if(auto* current=layers_->currentItem())layers_->editItem(current,0);});
    auto*masks=menu->addMenu("Mask");masks->setObjectName("layerMaskMenu");
    struct Entry{const char* text;const char* name;ui::MaskCommand command;};
    for(const auto& entry:std::vector<Entry>{{"Add White Mask (Hide Selection)","maskAddReveal",ui::MaskCommand::AddReveal},{"Add Black Mask (Reveal Selection)","maskAddHide",ui::MaskCommand::AddHide},{"Reveal All","maskRevealAll",ui::MaskCommand::RevealAll},{"Hide All","maskHideAll",ui::MaskCommand::HideAll},{"Enable / Disable Mask","maskToggleEnabled",ui::MaskCommand::ToggleEnabled},{"Link / Unlink Mask","maskToggleLink",ui::MaskCommand::ToggleLink},{"Delete Mask","maskDelete",ui::MaskCommand::Delete},{"Edit Image","maskEditImage",ui::MaskCommand::EditImage},{"Edit Mask","maskEditMask",ui::MaskCommand::EditMask},{"Select Image Alpha","maskLoadAlpha",ui::MaskCommand::LoadAlpha},{"Select Mask Black Areas","maskLoadBlack",ui::MaskCommand::LoadBlack}}){auto*a=action(masks,entry.text,{},[this,command=entry.command]{maskCommand(int(command));});a->setObjectName(entry.name);a->setProperty("maskCommand",int(entry.command));}

    auto*parent=layers_->parentWidget();auto*layout=qobject_cast<QVBoxLayout*>(parent->layout());if(layout){auto*editTarget=new QComboBox(parent);editTarget->setObjectName("layerEditTarget");editTarget->setAccessibleName("Layer editing target");editTarget->addItems({"Edit image","Edit mask"});layout->addWidget(editTarget);editTarget->hide();connect(editTarget,&QComboBox::currentIndexChanged,this,[this](int value){if(!refreshing_)maskCommand(int(value?ui::MaskCommand::EditMask:ui::MaskCommand::EditImage));});auto*footer=new QHBoxLayout;auto addButton=[&](QString text,QString name,std::function<void()> fn){auto*button=new QPushButton(text,parent);button->setObjectName(name);button->setAccessibleName(text);connect(button,&QPushButton::clicked,this,[this,fn]{try{fn();}catch(const std::exception&e){statusBar()->showMessage(e.what());}});footer->addWidget(button);};addButton("Group","layerGroupButton",[this]{layerCommand(1);});addButton("Add Mask","layerAddMaskButton",[this]{maskCommand(int(ui::MaskCommand::AddReveal));});addButton("Delete","layerDeleteButton",[this]{auto*p=current();if(p&&p->maskSelected&&layerSelection().ids.size()==1)maskCommand(int(ui::MaskCommand::Delete));else layerCommand(6);});layout->addLayout(footer);}
}
void MainWindow::refreshLayerPanel(){if(auto* controller=ui::LayerPanelController::find(layers_))controller->rebuild();}
}
