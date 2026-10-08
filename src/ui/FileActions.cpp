#include "MainWindow.h"
#include "AppLogging.h"
#include "PropertyControls.h"
#include "persistence/ProjectStore.h"
#include "imaging/wic_codec.h"
#include "imaging/heif_codec.h"
#include "imaging/psd/PsdDocument.h"
#include <QSlider>
#include "core/DocumentExport.h"
#include "ImportActions.h"
#include "ImportCommit.h"
#include "ProjectOpenDialog.h"
#include "WorkspaceDropQueue.h"
#include "VirtualImageDrop.h"
#include "ProjectLayerDrag.h"
#include "ProjectLayerCopyJob.h"
#include "LayerCopyCommit.h"
#include <QFileDialog>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QColorDialog>
#include <QBuffer>
#include <QTemporaryDir>
#include <QSaveFile>
#include <QFile>
#include <QApplication>
#include <QTimer>
#include <QPushButton>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QProgressDialog>
#include <QStatusBar>
#include <QSignalBlocker>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <QSettings>
#include <QScopedValueRollback>
#include <QScopeGuard>
#include <QTabBar>
#include <QMenuBar>
#include <QThread>
#include <commdlg.h>
#include <knownfolders.h>
#include <shlobj.h>
#include <atomic>
#include <cmath>
#include <memory>
#include <string>

namespace compositor {
#pragma comment(lib, "comdlg32.lib")
static std::filesystem::path nativePath(const QString&s){return std::filesystem::path(s.toStdWString());}
namespace {
template<class Load> bool loadProjectBatch(QWidget* owner,const QStringList& paths,Load&& load){
    bool opened=false;
    for(const auto& path:paths){
        try{opened=load(path)||opened;}
        catch(const std::exception& error){QMessageBox message(QMessageBox::Critical,"Open Compositor Project",QString("Could not open %1.\n\n%2").arg(QDir::toNativeSeparators(path),QString::fromUtf8(error.what())),QMessageBox::Ok,owner);message.setObjectName("projectOpenError");message.exec();}
    }
    return opened;
}
QStringList nativeImportPaths(HWND owner,const QString& directory){
    // This function runs outside Qt's GUI thread. Keeping the native modal
    // loop away from Qt prevents qwindows/comdlg re-entry from corrupting
    // Qt's object tree.
    static const wchar_t filter[]=
        L"Images\0*.png;*.jpg;*.jpeg;*.tif;*.tiff;*.heic;*.heif;*.psd;*.psb;*.svg;*.cr2;*.nef;*.arw;*.dng;*.raw\0"
        L"All files\0*.*\0";
    std::wstring buffer(32768,L'\0');
    const auto initial=directory.toStdWString();
    OPENFILENAMEW dialog{};
    dialog.lStructSize=sizeof(dialog);
    dialog.hwndOwner=owner;
    dialog.lpstrFilter=filter;
    dialog.lpstrFile=buffer.data();
    dialog.nMaxFile=DWORD(buffer.size());
    dialog.lpstrInitialDir=initial.empty()?nullptr:initial.c_str();
    dialog.lpstrTitle=L"Import Images";
    dialog.Flags=OFN_EXPLORER|OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_ALLOWMULTISELECT|OFN_NOCHANGEDIR;
    logging::trace(QString("native picker entered owner=0x%1 directory=%2")
        .arg(quintptr(owner),0,16).arg(QDir::toNativeSeparators(directory)));
    if(!GetOpenFileNameW(&dialog)){
        logging::trace(QString("native picker cancelled error=%1").arg(CommDlgExtendedError()));
        return {};
    }
    QStringList paths;
    const wchar_t* cursor=buffer.c_str();
    const QString first=QString::fromWCharArray(cursor);
    cursor+=first.size()+1;
    if(!*cursor)paths.append(first);
    else while(*cursor){const QString name=QString::fromWCharArray(cursor);paths.append(QDir(first).filePath(name));cursor+=name.size()+1;}
    logging::trace(QString("native picker returned %1 path(s): %2")
        .arg(paths.size()).arg(paths.join(" | ")));
    return paths;
}
}
ui::ImportQueue* MainWindow::ensureImportQueue(){
    if(importQueue_)return importQueue_;
    auto project=[this](QObject* target)->EditorProject*{for(auto& p:projects_)if(p->canvas==target)return p.get();return nullptr;};
    ui::ImportQueue::Host host;
    host.snapshot=[project](QObject* target)->std::optional<ui::ImportState>{auto*p=project(target);if(!p)return {};return ui::ImportState{p->document,p->active};};
    host.blocked=[this,project](QObject* target){auto*p=project(target);if(!p||p->projectBusy)return true;if(workspaceDrops_&&workspaceDrops_->waitingFor(target))return false;return !canSwitchProjects()||stroke_||retouch_||transformSession_||drawingOriginal_||cropDraft_||movingSelection_||selectionGesture_.active();};
    host.blockedRequest=[project,deferred=host.blocked](QObject* target,const ui::ImportBatch& batch){
        if(batch.origin==ui::ImportBatch::Origin::Explicit){auto*p=project(target);return !p||p->projectBusy;}
        return deferred(target);
    };
    host.busy=[this,project](QObject* target,bool busy){
        auto*p=project(target);if(!p)return;
        if(busy&&current()!=p)for(size_t i=0;i<projects_.size();++i)if(projects_[i].get()==p){tabs_->setCurrentIndex(int(i));break;}
        p->importing=busy;
        if(busy){auto*progress=new QProgressDialog("Importing images…","Cancel",0,0,this);progress->setObjectName("imageImportProgress");progress->setWindowModality(Qt::NonModal);progress->setMinimumDuration(200);progress->setAutoClose(false);progress->setAutoReset(false);connect(progress,&QProgressDialog::canceled,this,[this,target=QPointer<QObject>(target)]{if(importQueue_&&target)importQueue_->cancel(target);});progress->setValue(0);}
        else for(auto*progress:findChildren<QProgressDialog*>("imageImportProgress")){QSignalBlocker blocked(progress);progress->close();progress->deleteLater();}
        refresh(false,false);
    };
    host.commit=[this,project](QObject* target,const ui::ImportResult& result){
        auto*p=project(target);if(!p)throw std::runtime_error("The import destination was closed");
        const bool first=ui::commitPreparedImport(*p,result);
        p->composite.reset();refresh();if(first&&p->document){p->canvas->setDocumentSize(p->document->width,p->document->height);p->canvas->fit();}
    };
    host.completed=[this](QObject* target,const ui::ImportResult& result){
        if(target&&!result.errors.empty()){auto errors=target->property("imageImportErrors").toStringList();errors.append(result.errors);target->setProperty("imageImportErrors",errors);}
        if(result.cancelled)statusBar()->showMessage("Image import cancelled",5000);
        else if(result.imported)statusBar()->showMessage(QString("Imported %1 image(s)").arg(result.imported),5000);
        QTimer::singleShot(0,this,[this]{if(!importQueue_||!importQueue_->idle())return;QStringList errors;for(const auto&p:projects_){errors.append(p->canvas->property("imageImportErrors").toStringList());p->canvas->setProperty("imageImportErrors",{});}if(errors.isEmpty())return;auto*message=new QMessageBox(QMessageBox::Warning,"Import Images","Some images could not be imported.",QMessageBox::Ok,this);message->setObjectName("imageImportErrors");message->setDetailedText(errors.join("\n\n"));message->setAttribute(Qt::WA_DeleteOnClose);message->setWindowModality(Qt::WindowModal);message->show();});
        if(workspaceDrops_)workspaceDrops_->imageFinished(target,result.cancelled);
    };
    importQueue_=new ui::ImportQueue(std::move(host),this);return importQueue_;
}
ui::WorkspaceDropQueue* MainWindow::ensureWorkspaceDropQueue(){
    if(workspaceDrops_)return workspaceDrops_;
    auto project=[this](QObject* target)->EditorProject*{for(auto& item:projects_)if(item->canvas==target)return item.get();return nullptr;};
    ui::WorkspaceDropQueue::Host host;
    host.canBegin=[this]{return !closingWindow_&&canSwitchProjects()&&(!importQueue_||importQueue_->idle());};
    host.canAdvance=[this]{return !QApplication::activeModalWidget()&&(!importQueue_||importQueue_->idle());};
    host.managing=[this](bool active){managingProjectOpen_=active;refresh(false,false);};
    host.project=[this](const QString& path){loadProjectDirectory(path);};
    host.imageTarget=[this,project](QObject* captured,bool hasDestination)->QObject*{
        QScopedValueRollback<bool> selecting(selectingProjectForOpen_,true);
        auto* target=hasDestination?project(captured):&addEmptyProject();if(!target)return nullptr;
        for(size_t i=0;i<projects_.size();++i)if(projects_[i].get()==target){tabs_->setCurrentIndex(int(i));break;}
        return target->canvas;
    };
    host.image=[this,project](QObject* canvas,const QString& path,std::optional<Point> point,std::shared_ptr<void> lifetime){
        auto* target=project(canvas);if(!target)throw std::runtime_error("The drop destination was closed");
        finishOpacityEdit();cropDraft_.reset();cropDrag_.reset();if(transformSession_)applyTransformSession();
        ui::ImportBatch batch;batch.files={nativePath(path)};batch.point=point;batch.lifetime=std::move(lifetime);
        ensureImportQueue()->enqueue(target->canvas,std::move(batch));
    };
    host.cancelImage=[this](QObject* target){if(importQueue_)importQueue_->cancel(target);};
    host.error=[this](const QString& path,const QString& error){QMessageBox message(QMessageBox::Critical,"Receive Dropped Files",QString("Could not open %1.\n\n%2").arg(QDir::toNativeSeparators(path),error),QMessageBox::Ok,this);message.setObjectName("workspaceDropError");message.exec();};
    workspaceDrops_=new ui::WorkspaceDropQueue(std::move(host),this);return workspaceDrops_;
}
void MainWindow::receiveDropPaths(const QStringList& paths,NativeCanvas* destination,std::optional<Point> point){
    ui::WorkspaceDropRequest request;request.paths=paths;request.destination=destination;request.hasDestination=destination!=nullptr;request.point=point;
    ensureWorkspaceDropQueue()->enqueue(std::move(request));
}
void MainWindow::queueImageImports(const QStringList& paths,EditorProject* target,std::optional<Point> point){
    if(paths.isEmpty())return;
    // EditorSession.importImages finishes an existing stroke, cancels crop and
    // commits transform. Gradient and polygon drafts keep their captured target.
    // Picker focus loss is handled separately by NativeCanvas::pointerInterrupted.
    finishOpacityEdit();
    if(stroke_){updateBrush(lastBrushPoint_.value_or(press_),true);pointerOwner_=nullptr;}
    cropDraft_.reset();cropDrag_.reset();cropSnap_.reset();refreshCropControls();
    if(transformSession_)applyTransformSession();
    if(!target)target=&addEmptyProject();
    ui::ImportBatch batch;batch.origin=ui::ImportBatch::Origin::Explicit;batch.point=point;for(const auto& path:paths)batch.files.push_back(nativePath(path));
    ensureImportQueue()->enqueue(target->canvas,std::move(batch));
}
EditorProject* MainWindow::dropDestinationAt(QPoint location,std::optional<Point>& point){
    point.reset();auto* destination=current();
    if(auto* corner=tabs_->cornerWidget(Qt::TopRightCorner);corner&&corner->rect().contains(corner->mapFrom(this,location)))return nullptr;
    auto* bar=tabs_->tabBar();const auto tabPoint=bar->mapFrom(this,location);const auto local=tabs_->mapFrom(this,location);
    if(local.x()>=0&&local.x()<tabs_->width()&&local.y()>=bar->geometry().top()&&local.y()<=bar->geometry().bottom()){
        const auto index=bar->tabAt(tabPoint);return index>=0&&size_t(index)<projects_.size()?projects_[size_t(index)].get():nullptr;
    }
    // The native menu/new-project chrome is outside ContentView. Editor tools,
    // docks and other content keep the current session with an absent point.
    if(menuBar()->rect().contains(menuBar()->mapFrom(this,location)))return nullptr;
    if(destination&&destination->document&&destination->canvas){const auto canvasPoint=destination->canvas->mapFrom(this,location);if(destination->canvas->rect().contains(canvasPoint)){const auto p=destination->canvas->documentPoint(canvasPoint);point=Point{p.x(),p.y()};}}
    return destination;
}
bool MainWindow::canReceiveLayerDrop(const QMimeData* mime,QPoint location,Qt::KeyboardModifiers modifiers){
    if(!canSwitchProjects()||modifiers.testFlag(Qt::AltModifier))return false;
    const auto payload=ui::projectLayerPayload(mime);if(!payload)return false;
    auto found=std::find_if(projects_.begin(),projects_.end(),[&](const auto& project){return ui::projectLayerIdentity(project->canvas)==payload->session&&(!payload->scoped||payload->owner==project->canvas);});
    if(found==projects_.end())return false;auto* source=found->get();if(!ui::canEditLayers(commandState(source)))return false;
    for(const auto& id:payload->ids)if(std::none_of(source->document->layers.begin(),source->document->layers.end(),[&](const Layer& layer){return layer.id==id;}))return false;
    std::optional<Point> point;auto* target=dropDestinationAt(location,point);if(target==source)return false;
    return !target||(ui::canStartProjectOperation(commandState(target))&&(!target->document||ui::canEditLayers(commandState(target))));
}
bool MainWindow::receiveLayerDrop(const QMimeData* mime,QPoint location,Qt::KeyboardModifiers modifiers){
    if(!canReceiveLayerDrop(mime,location,modifiers))return false;
    const auto payload=*ui::projectLayerPayload(mime);std::optional<Point> point;auto* destination=dropDestinationAt(location,point);
    auto source=std::find_if(projects_.begin(),projects_.end(),[&](const auto& project){return ui::projectLayerIdentity(project->canvas)==payload.session;});
    QPointer<QObject> sourceOwner=(*source)->canvas,targetOwner=destination?destination->canvas:nullptr;const bool captured=destination!=nullptr;
    auto project=[this](QObject* owner)->EditorProject*{for(auto& item:projects_)if(item->canvas==owner)return item.get();return nullptr;};
    ui::ProjectLayerCopyJob::Host host;
    host.prepare=[this,project,sourceOwner,targetOwner,captured,point](const std::string& id)->std::optional<ui::LayerCopyWork>{
        auto* sourceProject=project(sourceOwner);if(!sourceProject||!sourceProject->document)return {};
        auto* target=captured?project(targetOwner):nullptr;if(captured&&!target)return {};
        if(!target){QScopedValueRollback<bool> selecting(selectingProjectForOpen_,true);target=&addEmptyProject(false);}
        ui::LayerCopyWork work;work.source=*sourceProject->document;work.root=id;work.point=point;work.target=target->canvas;work.before={target->document,target->active};
        if(target->document)work.destination=*target->document;
        else{work.destination.id=newId();work.destination.width=work.source.width;work.destination.height=work.source.height;Layer blank;blank.id=newId();blank.name="Layer 1";blank.transform={0,0,double(work.source.width),double(work.source.height)};work.destination.layers.push_back(std::move(blank));}
        target->projectBusy=true;try{refresh(false,false);}catch(...){target->projectBusy=false;throw;}return work;
    };
    host.commit=[this,project](const ui::LayerCopyWork& work,layers::CopyResult copy){
        auto* target=project(work.target);if(!target)return;
        const bool first=ui::commitPreparedLayerCopy(*target,work.before.document,work.before.active,copy.edit);target->composite.reset();
        {QScopedValueRollback<bool> selecting(selectingProjectForOpen_,true);for(size_t index=0;index<projects_.size();++index)if(projects_[index].get()==target){tabs_->setCurrentIndex(int(index));break;}}
        refresh();if(first){target->canvas->setDocumentSize(target->document->width,target->document->height);target->canvas->fit();}
    };
    host.settled=[this,project](const ui::LayerCopyWork& work){if(auto* target=project(work.target))target->projectBusy=false;refresh(false,false);};
    host.error=[this](const QString& error){QMessageBox message(QMessageBox::Warning,"Copy Layers from Project",error,QMessageBox::Ok,this);message.setObjectName("projectLayerDropError");message.exec();};
    host.finished=[this,project,sourceOwner]{if(auto* owner=project(sourceOwner))owner->projectBusy=false;managingProjectOpen_=false;refresh();};
    // Construct the worker owner before publishing busy state; it starts via a
    // queued event, so failed allocation cannot leave the workspace locked.
    new ui::ProjectLayerCopyJob(payload.ids,std::move(host),this);
    (*source)->projectBusy=true;managingProjectOpen_=true;refresh(false,false);return true;
}
void MainWindow::dragEnterEvent(QDragEnterEvent*event){
    if(!canSwitchProjects())return;
    if(event->mimeData()->hasFormat(ui::projectLayerMime)){if(canReceiveLayerDrop(event->mimeData(),event->position().toPoint(),event->modifiers())){event->setDropAction(Qt::CopyAction);event->accept();}return;}
    if(ui::VirtualDropJob::hasVirtualFiles(*event->mimeData())){event->setDropAction(Qt::CopyAction);event->accept();return;}
    if(event->mimeData()->hasImage()){event->acceptProposedAction();return;}
    for(const auto&url:event->mimeData()->urls())if(url.isLocalFile()){event->acceptProposedAction();return;}
}
void MainWindow::dropEvent(QDropEvent*event){
    // ContentView.swift:102 and ProjectTabs.swift:163 reject pre-blocked UI
    // drops. Already-accepted provider/OS requests use receiveDropPaths and wait.
    if(!canSwitchProjects())return;
    try{
        if(event->mimeData()->hasFormat(ui::projectLayerMime)){if(receiveLayerDrop(event->mimeData(),event->position().toPoint(),event->modifiers())){event->setDropAction(Qt::CopyAction);event->accept();}return;}
        std::optional<Point> point;auto* destination=dropDestinationAt(event->position().toPoint(),point);
        ui::WorkspaceDropRequest request;request.destination=destination?destination->canvas:nullptr;request.hasDestination=destination!=nullptr;request.point=point;
        for(const auto&url:event->mimeData()->urls())if(url.isLocalFile())request.paths.append(url.toLocalFile());
        if(request.paths.isEmpty()&&ui::VirtualDropJob::hasVirtualFiles(*event->mimeData())){
            // Capture the native provider while Drop owns QMimeData. The job
            // marshals an async provider, or copies a synchronous provider here.
            // A foreign blocking GetData/Read cannot be given a cancel deadline.
            QPointer<MainWindow> owner(this);
            QPointer<QProgressDialog> progress=new QProgressDialog("Receiving dropped images…","Cancel",0,0,this);
            progress->setObjectName("virtualImageDropProgress");progress->setWindowModality(Qt::NonModal);
            progress->setAutoClose(false);progress->setAutoReset(false);progress->setMinimumDuration(200);progress->setValue(0);
            try{
                auto* job=ui::VirtualDropJob::start(*event->mimeData(),std::move(request),this,
                    [owner,progress](ui::VirtualDropResult result){
                        if(progress){QSignalBlocker blocked(progress);progress->close();progress->deleteLater();}
                        if(!owner||owner->closingWindow_||result.cancelled)return;
                        const auto error=result.error;const auto captured=result.request.destination;const bool hadDestination=result.request.hasDestination;
                        result.request.completed=[owner,captured,hadDestination,error](bool cancelled){
                            if(cancelled||error.isEmpty()||!owner||owner->closingWindow_||(hadDestination&&!captured))return;
                            auto* message=new QMessageBox(QMessageBox::Warning,"Receive Dropped Images","Some dropped items could not be read.",QMessageBox::Ok,owner);
                            message->setObjectName("virtualImageDropErrors");message->setTextFormat(Qt::PlainText);message->setDetailedText(error);
                            message->setAttribute(Qt::WA_DeleteOnClose);message->setWindowModality(Qt::WindowModal);message->show();
                        };
                        owner->ensureWorkspaceDropQueue()->enqueue(std::move(result.request));
                    },[progress](uint64_t bytes,bool timedOut){
                        if(progress)progress->setLabelText(timedOut?QStringLiteral("Waiting for the image provider to finish cancelling…"):QString("Receiving dropped images… %1 bytes").arg(qulonglong(bytes)));
                    });
                connect(progress,&QProgressDialog::canceled,job,&ui::VirtualDropJob::cancel);
            }catch(...){if(progress){QSignalBlocker blocked(progress);progress->close();progress->deleteLater();}throw;}
            event->setDropAction(Qt::CopyAction);event->accept();return;
        }
        if(request.paths.isEmpty()&&event->mimeData()->hasImage()){
            auto temp=std::make_shared<QTemporaryDir>();if(!temp->isValid())throw std::runtime_error("Cannot create dropped-image temporary storage");
            const auto image=qvariant_cast<QImage>(event->mimeData()->imageData());const auto path=temp->filePath("Dropped Image.png");if(image.isNull()||!image.save(path,"PNG"))throw std::runtime_error("Cannot copy dropped image data");
            request.paths={path};request.lifetime=std::move(temp);
        }
        if(request.paths.isEmpty())return;
        ensureWorkspaceDropQueue()->enqueue(std::move(request));
        event->acceptProposedAction();
    }catch(const std::exception&e){QMessageBox::critical(this,"Import Images",e.what());}
}
void MainWindow::openProjectDialog(){
    if(!canSwitchProjects())return;
    // Destruction order clears the managing flag before refreshing commands.
    const auto update=qScopeGuard([this]{refresh(false,false);});
    QScopedValueRollback<bool> managing(managingProjectOpen_,true);refresh(false,false);
    try{if(const auto paths=ui::chooseProjectDirectories(this))loadProjectBatch(this,*paths,[this](const QString& path){return loadProjectDirectory(path);});}
    catch(const std::exception& error){QMessageBox::critical(this,"Open Compositor Projects",QString::fromUtf8(error.what()));}
}
bool MainWindow::openProjectPaths(const QStringList& paths){
    if(!canSwitchProjects()||paths.isEmpty())return false;
    const auto update=qScopeGuard([this]{refresh(false,false);});
    QScopedValueRollback<bool> managing(managingProjectOpen_,true);refresh(false,false);
    return loadProjectBatch(this,paths,[this](const QString& path){return loadProjectDirectory(path);});
}
bool MainWindow::loadProjectDirectory(const QString& path){
        if(!QFileInfo(path).isDir())throw std::runtime_error("The project path is not an existing directory");
        // ProjectWorkspace.swift:77-80: select an existing resolved project
        // without reloading state; the caller owns the workspace guard.
        const auto supplied=nativePath(path);
        for(size_t index=0;index<projects_.size();++index){
            const auto& project=projects_[index];if(project->path.isEmpty())continue;
            std::error_code error;
            if(std::filesystem::equivalent(supplied,nativePath(project->path),error)&&!error){
                QScopedValueRollback<bool> selecting(selectingProjectForOpen_,true);
                tabs_->setCurrentIndex(int(index));return true;
            }
        }
        if(transformSession_&&transformSession_->persistent)applyTransformSession();
        ProjectStore store(makeWicProjectCodec());auto opened=store.load(supplied);
        EditorProject* project{};
        {QScopedValueRollback<bool> selecting(selectingProjectForOpen_,true);project=&addProject(std::move(opened.document),QFileInfo(path).fileName(),false);}
        project->path=path;project->active=opened.activeLayer;project->selected={opened.activeLayer};project->history.reset();refresh(false);return true;
}
void MainWindow::openPath(const QString&path){
    if(QFileInfo(path).isDir()){
        // Direct callers retain exception reporting and the same guard before
        // any preparation or filesystem lookup as the workspace Open command.
        if(!canSwitchProjects())return;
        const auto update=qScopeGuard([this]{refresh(false,false);});
        QScopedValueRollback<bool> managing(managingProjectOpen_,true);refresh(false,false);
        loadProjectDirectory(path);return;
    }
    applyGradient();if(transformSession_&&transformSession_->persistent)applyTransformSession();
    const auto extension=QFileInfo(path).suffix().toLower();
    if(extension=="psd"||extension=="psb"){importSelectedImages({path});return;}
    const bool reuse=!current()||!importQueue_||!importQueue_->contains(current()->canvas);
    auto& destination=addEmptyProject(reuse);queueImageImports({path},&destination,{});
}
QString MainWindow::userDownloadsDirectory(){
    PWSTR known=nullptr;
    QString path;
    if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads,KF_FLAG_DEFAULT,nullptr,&known))&&known){
        path=QDir::cleanPath(QString::fromWCharArray(known));
        CoTaskMemFree(known);
    }
    if(path.isEmpty()||!QDir(path).exists())path=QDir::cleanPath(QDir::homePath());
    return path;
}
void MainWindow::importSelectedImages(const QStringList& paths){
    logging::trace(QString("importSelectedImages begin count=%1 paths=%2")
        .arg(paths.size()).arg(paths.join(" | ")));
    auto* target=current();
    QStringList images;
    for(const auto& path:paths){
        auto ext=QFileInfo(path).suffix().toLower();
        if(ext=="psd"||ext=="psb"){
            try{
                logging::trace(QString("PSD read begin path=%1").arg(QDir::toNativeSeparators(path)));
                auto imported=imaging::readPsd(nativePath(path));
                logging::trace(QString("PSD read complete path=%1 report=%2")
                    .arg(QDir::toNativeSeparators(path),QString::fromStdString(imported.report)));
                logging::trace(QString("PSD import accepted automatically path=%1")
                    .arg(QDir::toNativeSeparators(path)));
                auto& project=addProject(std::move(imported.document),QFileInfo(path).fileName(),false);
                // addProject can replace the empty welcome project, invalidating
                // the pointer captured before the PSD was opened.
                target=&project;
                if(project.document){project.active=project.document->layers.back().id;project.selected={project.active};}
                refresh();project.canvas->fit();
                logging::trace(QString("PSD project installed path=%1").arg(QDir::toNativeSeparators(path)));
            }catch(const std::exception& error){
                logging::trace(QString("PSD import exception path=%1 error=%2")
                    .arg(QDir::toNativeSeparators(path),QString::fromUtf8(error.what())));
                QMessageBox::critical(this,"Import PSD",error.what());
            }
        }else images.append(path);
    }
    if(!images.isEmpty())queueImageImports(images,target,{});
}
namespace {
void deferSelectedImageImport(MainWindow* window,QStringList paths){
    if(paths.isEmpty())return;
    QPointer<MainWindow> guard(window);
    QTimer::singleShot(0,window,[guard,paths=std::move(paths)]{
        if(guard)guard->importSelectedImages(paths);
    });
}
void startNativeImport(MainWindow* window,const QString& directory){
    if(window->property("nativeImportInProgress").toBool()){
        logging::trace("native picker request ignored because another picker is active");
        return;
    }
    window->setProperty("nativeImportInProgress",true);
    const auto owner=reinterpret_cast<HWND>(window->winId());
    auto paths=std::make_shared<QStringList>();
    const auto initial=directory;
    logging::trace(QString("native picker request queued directory=%1").arg(QDir::toNativeSeparators(directory)));
    auto* thread=QThread::create([paths,owner,initial]{
        const HRESULT apartment=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
        struct Release{HRESULT hr;~Release(){if(SUCCEEDED(hr))CoUninitialize();}} release{apartment};
        logging::trace(QString("native picker worker started apartment=0x%1").arg(quint32(apartment),0,16));
        *paths=nativeImportPaths(owner,initial);
        logging::trace(QString("native picker worker finished count=%1").arg(paths->size()));
    });
    QObject::connect(thread,&QThread::finished,thread,&QObject::deleteLater);
    QPointer<MainWindow> guard(window);
    QObject::connect(thread,&QThread::finished,window,[guard,paths]{
        if(!guard)return;
        guard->setProperty("nativeImportInProgress",false);
        logging::trace(QString("native picker result delivered count=%1").arg(paths->size()));
        deferSelectedImageImport(guard.data(),std::move(*paths));
    },Qt::QueuedConnection);
    thread->start();
}
}
void MainWindow::importWelcomeImage(){startNativeImport(this,userDownloadsDirectory());}
void MainWindow::importImage(){startNativeImport(this,userDownloadsDirectory());}
bool MainWindow::saveProject(bool saveAs){cropDraft_.reset();cropDrag_.reset();if(transformSession_&&transformSession_->persistent)applyTransformSession();auto*p=current();if(!p||!p->document||p->importing||p->projectBusy)return false;auto path=p->path;if(path.isEmpty()||saveAs){path=QFileDialog::getSaveFileName(this,"Save Compositor Project",path.isEmpty()?"Untitled.comp":path,"Compositor project directory (*.comp)");if(path.isEmpty())return false;if(!path.endsWith(".comp",Qt::CaseInsensitive))path+=".comp";}ProjectStore store(makeWicProjectCodec());store.save(nativePath(path),*p->document,p->active);p->path=path;p->history.markSaved();refresh(false);return true;}
void MainWindow::exportImage(){
    auto*p=current();if(!p||!p->document||p->importing||p->projectBusy)return;
    imaging::validateExportExtent(uint32_t(p->document->width),uint32_t(p->document->height));
    const Document snapshot=*p->document;
    p->projectBusy=true;refresh(false,false);
    struct Restore {std::function<void()> action;~Restore(){action();}} restore{[&]{p->projectBusy=false;refresh(false,false);}};
    QTemporaryDir temp;if(!temp.isValid())throw std::runtime_error("Cannot create image export storage");
    QDialog dialog(this);dialog.setObjectName("imageExportDialog");dialog.setWindowTitle("Export Image");QFormLayout layout(&dialog);
    QComboBox format;format.setObjectName("exportFormat");format.addItems({"PNG","JPEG"});layout.addRow("Format",&format);
    QSettings settings;double remembered=settings.value("jpegExportQuality",.85).toDouble();if(!std::isfinite(remembered))remembered=.85;
    ui::TrackSlider quality(Qt::Horizontal);quality.setObjectName("exportQuality");quality.setRange(0,100);quality.setValue(int(std::round(std::clamp(remembered,0.,1.)*100)));layout.addRow("JPEG quality",&quality);
    QPushButton matte("White");matte.setObjectName("exportMatte");QColor matteColor=Qt::white;layout.addRow("JPEG background",&matte);
    QLabel preview;preview.setObjectName("exportPreview");preview.setMinimumSize(520,320);preview.setAlignment(Qt::AlignCenter);layout.addRow(&preview);
    QSlider previewZoom(Qt::Horizontal);previewZoom.setObjectName("exportPreviewZoom");previewZoom.setRange(25,400);previewZoom.setValue(100);layout.addRow("Preview zoom",&previewZoom);QPixmap previewSource;auto showPreview=[&]{if(previewSource.isNull())return;preview.setPixmap(previewSource.scaled(previewSource.size()*previewZoom.value()/100.,Qt::KeepAspectRatio,Qt::SmoothTransformation));};
    QLabel bytesLabel;bytesLabel.setObjectName("exportEncodedSize");layout.addRow("Encoded size",&bytesLabel);
    QDialogButtonBox buttons(QDialogButtonBox::Save|QDialogButtonBox::Cancel);buttons.setObjectName("exportButtons");layout.addRow(&buttons);
    struct Prepared {imaging::StreamingExportResult result;QString path,error;bool cancelled{};};
    QFutureWatcher<Prepared> watcher;QTimer debounce,progress;debounce.setSingleShot(true);debounce.setInterval(200);progress.setInterval(80);
    auto cancel=std::make_shared<std::atomic_bool>(false);auto completed=std::make_shared<std::atomic_uint32_t>(0);
    quint64 generation=0,runningGeneration=0;bool running=false,closed=false;QString readyPath;
    std::function<void()> start;
    auto changed=[&]{++generation;cancel->store(true);readyPath.clear();buttons.button(QDialogButtonBox::Save)->setEnabled(false);bytesLabel.setText("Updating preview…");debounce.start();};
    start=[&]{
        if(running||closed)return;
        running=true;runningGeneration=generation;cancel=std::make_shared<std::atomic_bool>(false);completed->store(0);
        imaging::ExportOptions options;options.format=format.currentIndex()==0?imaging::ImageFormat::Png:imaging::ImageFormat::Jpeg;options.jpegQuality=quality.value()/100.;
        options.matteR=uint8_t(matteColor.red());options.matteG=uint8_t(matteColor.green());options.matteB=uint8_t(matteColor.blue());options.cancelled=[flag=cancel]{return flag->load();};
        const auto path=temp.filePath(QString("preview-%1.%2").arg(generation).arg(format.currentIndex()==0?"png":"jpg"));
        watcher.setFuture(QtConcurrent::run([snapshot,path,options,completed]{Prepared out;out.path=path;try{out.result=exportDocumentAtomic(snapshot,nativePath(path),options,{},[completed](uint32_t rows,uint32_t){completed->store(rows);});}catch(const imaging::ExportCancelled&){out.cancelled=true;}catch(const std::exception&e){out.error=QString::fromUtf8(e.what());}return out;}));
    };
    connect(&watcher,&QFutureWatcher<Prepared>::finished,&dialog,[&]{
        running=false;if(closed)return;auto prepared=watcher.result();
        if(runningGeneration!=generation){if(!debounce.isActive())debounce.start(0);return;}
        if(prepared.cancelled)return;
        if(!prepared.error.isEmpty()){bytesLabel.setText(prepared.error);return;}
        const auto&image=prepared.result.preview;
        QImage shown(image.pixels.data(),int(image.width),int(image.height),qsizetype(image.stride),QImage::Format_RGBA8888_Premultiplied);
        if(shown.isNull()){bytesLabel.setText("Cannot display export preview");return;}
        previewSource=QPixmap::fromImage(shown.copy());showPreview();readyPath=prepared.path;
        bytesLabel.setText(QString::number(prepared.result.encodedBytes)+" bytes");buttons.button(QDialogButtonBox::Save)->setEnabled(true);
        dialog.setProperty("readyExportPath",readyPath);dialog.setProperty("readyExportGeneration",generation);
    });
    connect(&debounce,&QTimer::timeout,&dialog,start);
    connect(&progress,&QTimer::timeout,&dialog,[&]{if(running&&runningGeneration==generation)bytesLabel.setText(QString("Encoding %1%").arg(100ULL*completed->load()/uint32_t(snapshot.height)));});
    connect(&format,&QComboBox::currentIndexChanged,&dialog,[&]{quality.setEnabled(format.currentIndex()==1);matte.setEnabled(format.currentIndex()==1);changed();});
    connect(&quality,&QSlider::valueChanged,&dialog,changed);
    connect(&previewZoom,&QSlider::valueChanged,&dialog,showPreview);
    connect(&matte,&QPushButton::clicked,&dialog,[&]{auto color=QColorDialog::getColor(matteColor,&dialog,"JPEG background");if(color.isValid()){matteColor=color;matte.setText(color.name());changed();}});
    connect(&buttons,&QDialogButtonBox::accepted,&dialog,[&]{if(!readyPath.isEmpty()&&!running)dialog.accept();});
    connect(&buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    quality.setEnabled(false);matte.setEnabled(false);changed();progress.start();
    const auto response=dialog.exec();closed=true;debounce.stop();progress.stop();cancel->store(true);watcher.waitForFinished();
    if(response!=QDialog::Accepted||readyPath.isEmpty())return;
    if(format.currentIndex()==1)settings.setValue("jpegExportQuality",quality.value()/100.);
    const auto destination=QFileDialog::getSaveFileName(this,"Export Image",{},format.currentIndex()==0?"PNG (*.png)":"JPEG (*.jpg)");if(destination.isEmpty())return;
    QProgressDialog saving("Saving image…","Cancel",0,0,this);saving.setObjectName("imageExportSaving");saving.setWindowModality(Qt::ApplicationModal);saving.setMinimumDuration(0);
    auto copyCancelled=std::make_shared<std::atomic_bool>(false);QFutureWatcher<QString> copy;
    connect(&saving,&QProgressDialog::canceled,&saving,[copyCancelled]{copyCancelled->store(true);});
    connect(&copy,&QFutureWatcher<QString>::finished,&saving,&QDialog::accept);
    copy.setFuture(QtConcurrent::run([readyPath,destination,copyCancelled]{try{imaging::copyEncodedAtomic(nativePath(readyPath),nativePath(destination),[copyCancelled]{return copyCancelled->load();});return QString();}catch(const imaging::ExportCancelled&){return QString();}catch(const std::exception&e){return QString::fromUtf8(e.what());}}));
    saving.exec();copy.waitForFinished();if(!copy.result().isEmpty())throw std::runtime_error(copy.result().toStdString());
    // The file that produced the encoded preview is copied exactly. No project/history mutation.
}
}
