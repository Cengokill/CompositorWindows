#include "MainWindow.h"
#include "FocusStackJob.h"
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QStatusBar>
#include <cmath>
#include <stdexcept>
namespace compositor {
namespace {
enum class FocusStackOutput { Cancel, NewLayer, NewDocument };
void showFocusStackError(QWidget* parent,const QString& text){
    auto* message=new QMessageBox(QMessageBox::Warning,"Focus Stack",text,QMessageBox::Ok,parent);
    message->setObjectName("focusStackError");message->setAttribute(Qt::WA_DeleteOnClose);
    message->setWindowModality(Qt::WindowModal);message->show();
}
// Tests inject "layer", "document" or "cancel" instead of answering the dialog.
FocusStackOutput chooseFocusStackOutput(QWidget* parent){
    const auto injected=parent->property("testFocusStackOutput");
    if(injected.isValid()){const auto value=injected.toString();
        return value=="layer"?FocusStackOutput::NewLayer:value=="document"?FocusStackOutput::NewDocument:FocusStackOutput::Cancel;}
    QMessageBox message(QMessageBox::Question,"Focus Stack","Add the focus-stacked result as a new layer in this document, or open it as a new document?",QMessageBox::NoButton,parent);
    message.setObjectName("focusStackOutput");
    auto* layer=message.addButton("New Layer",QMessageBox::AcceptRole);layer->setObjectName("focusStackNewLayer");
    auto* document=message.addButton("New Document",QMessageBox::AcceptRole);document->setObjectName("focusStackNewDocument");
    message.addButton(QMessageBox::Cancel);
    message.setDefaultButton(layer);message.exec();
    return message.clickedButton()==layer?FocusStackOutput::NewLayer:message.clickedButton()==document?FocusStackOutput::NewDocument:FocusStackOutput::Cancel;
}
}
void MainWindow::focusStackSelectedImages(const QStringList& paths){
    if(paths.isEmpty())return;
    if(ui::FocusStackJob::find(this)){statusBar()->showMessage("A focus stack is already running",5000);return;}
    if(paths.size()<2){showFocusStackError(this,"Select at least two images to focus stack.");return;}
    std::vector<std::filesystem::path> files;for(const auto& path:paths)files.push_back(std::filesystem::path(path.toStdWString()));
    auto* owner=current();
    const bool hasDocument=owner&&owner->document;
    // Every choice, including Cancel, precedes any busy state, tab or history change.
    const auto output=hasDocument?chooseFocusStackOutput(this):FocusStackOutput::NewDocument;
    if(output==FocusStackOutput::Cancel)return;
    ui::FocusStackJob::Host host;
    host.error=[this](const QString& error){showFocusStackError(this,error);};
    if(output==FocusStackOutput::NewDocument){
        host.settled=[this]{refresh(false,false);};
        host.commit=[this](std::shared_ptr<const Raster> raster){
            Document document;document.id=newId();document.width=raster->width;document.height=raster->height;
            Layer layer;layer.id=newId();layer.name="Focus Stack";layer.transform={0,0,double(raster->width),double(raster->height)};layer.raster=std::move(raster);
            document.layers.push_back(std::move(layer));
            auto& project=addProject(std::move(document),"Focus Stack",false);
            refresh();if(project.canvas)project.canvas->fit();
        };
        new ui::FocusStackJob(std::move(files),std::move(host),this);
        return;
    }
    if(owner->projectBusy||owner->importing)return;
    const auto before=owner->document;const auto beforeActive=owner->active;
    QPointer<NativeCanvas> token=owner->canvas;
    host.settled=[this,owner,token]{if(!token)return;owner->projectBusy=false;refresh(false,false);};
    host.commit=[this,owner,token,before,beforeActive](std::shared_ptr<const Raster> raster){
        if(!token)return;
        if(owner->document!=before||owner->active!=beforeActive)
            throw std::runtime_error("The document changed while focus stacking; the result was not applied");
        auto document=*owner->document;
        Layer layer;layer.id=newId();layer.name="Focus Stack";
        layer.transform={std::floor(document.width/2.-raster->width/2.),std::floor(document.height/2.-raster->height/2.),double(raster->width),double(raster->height)};
        layer.raster=std::move(raster);
        for(const auto& active:document.layers)if(active.id==owner->active){layer.parentId=active.group?active.id:active.parentId;break;}
        const auto id=layer.id,parent=layer.parentId;
        document.layers.push_back(std::move(layer));
        validateDocument(document);
        owner->history.begin("Focus Stack",owner->document,owner->active);
        try{
            owner->document=std::move(document);owner->active=id;
            owner->history.end(owner->document,owner->active);
        }catch(...){
            if(auto snapshot=owner->history.cancel()){
                owner->document=std::move(snapshot->document);owner->active=std::move(snapshot->activeLayer);
            }
            throw;
        }
        owner->selected={id};owner->maskSelected=false;
        if(!parent.empty())owner->collapsedGroups.erase(parent);
        // Focus returns to the window after the progress dialog; keep it on the canvas so Edit > Undo tracks the history, not a text field.
        if(current()==owner){refresh();if(owner->canvas)owner->canvas->setFocus();}
    };
    owner->projectBusy=true;refresh(false,false);
    try{new ui::FocusStackJob(std::move(files),std::move(host),this);}
    catch(...){owner->projectBusy=false;refresh(false,false);throw;}
}
}
