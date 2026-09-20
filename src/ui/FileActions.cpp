#include "MainWindow.h"
#include "persistence/ProjectStore.h"
#include "imaging/wic_codec.h"
#include "imaging/heif_codec.h"
#include "core/DocumentExport.h"
#include "ImportActions.h"
#include <QFileDialog>
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
#include <atomic>
#include <cmath>

namespace compositor {
static std::filesystem::path nativePath(const QString&s){return std::filesystem::path(s.toStdWString());}
ui::ImportQueue* MainWindow::ensureImportQueue(){
    if(importQueue_)return importQueue_;
    auto project=[this](QObject* target)->EditorProject*{for(auto& p:projects_)if(p->canvas==target)return p.get();return nullptr;};
    ui::ImportQueue::Host host;
    host.snapshot=[project](QObject* target)->std::optional<ui::ImportState>{auto*p=project(target);if(!p)return {};return ui::ImportState{p->document,p->active};};
    host.blocked=[this,project](QObject* target){auto*p=project(target);return !p||p->projectBusy||!canSwitchProjects()||stroke_||retouch_||transformSession_||drawingOriginal_||cropDraft_||movingSelection_||selectionGesture_.active();};
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
        if(ui::ImportState{p->document,p->active}!=result.before)throw std::runtime_error("The import destination changed; the prepared images were not applied");
        const bool first=!p->document;p->history.begin("Import Images",p->document,p->active);p->document=result.after.document;p->active=result.after.active;p->selected={p->active};p->maskSelected=false;
        for(const auto& id:result.expandedGroups)p->collapsedGroups.erase(id);
        p->history.end(p->document,p->active);p->composite.reset();refresh();if(first&&p->document){p->canvas->setDocumentSize(p->document->width,p->document->height);p->canvas->fit();}
    };
    host.completed=[this](QObject* target,const ui::ImportResult& result){
        if(target&&!result.errors.empty()){auto errors=target->property("imageImportErrors").toStringList();errors.append(result.errors);target->setProperty("imageImportErrors",errors);}
        if(result.cancelled)statusBar()->showMessage("Image import cancelled",5000);
        else if(result.imported)statusBar()->showMessage(QString("Imported %1 image(s)").arg(result.imported),5000);
        QTimer::singleShot(0,this,[this]{if(!importQueue_||!importQueue_->idle())return;QStringList errors;for(const auto&p:projects_){errors.append(p->canvas->property("imageImportErrors").toStringList());p->canvas->setProperty("imageImportErrors",{});}if(errors.isEmpty())return;auto*message=new QMessageBox(QMessageBox::Warning,"Import Images","Some images could not be imported.",QMessageBox::Ok,this);message->setObjectName("imageImportErrors");message->setDetailedText(errors.join("\n\n"));message->setAttribute(Qt::WA_DeleteOnClose);message->setWindowModality(Qt::WindowModal);message->show();});
    };
    importQueue_=new ui::ImportQueue(std::move(host),this);return importQueue_;
}
void MainWindow::queueImageImports(const QStringList& paths,EditorProject* target,std::optional<Point> point){
    if(paths.isEmpty())return;
    finishOpacityEdit();applyGradient();
    if(stroke_){updateBrush(lastBrushPoint_.value_or(press_),true);pointerOwner_=nullptr;}
    if(transformSession_)applyTransformSession();
    if(!retouch_&&!colorPicker_)pointerCancel();
    if(!target)target=&addEmptyProject();
    ui::ImportBatch batch;batch.point=point;for(const auto& path:paths)batch.files.push_back(nativePath(path));
    ensureImportQueue()->enqueue(target->canvas,std::move(batch));
}
void MainWindow::dragEnterEvent(QDragEnterEvent*event){
    if(event->mimeData()->hasImage()){event->acceptProposedAction();return;}
    for(const auto&url:event->mimeData()->urls())if(url.isLocalFile()){event->acceptProposedAction();return;}
}
void MainWindow::dropEvent(QDropEvent*event){
    try{
        auto* destination=current();std::optional<Point> point;
        if(destination&&destination->document&&destination->canvas){const auto local=destination->canvas->mapFrom(this,event->position().toPoint());if(destination->canvas->rect().contains(local)){const auto p=destination->canvas->documentPoint(local);point=Point{p.x(),p.y()};}else destination=nullptr;}
        else if(destination&&destination->page&&!destination->page->rect().contains(destination->page->mapFrom(this,event->position().toPoint())))destination=nullptr;
        QStringList images;
        bool localItems=false;
        auto flush=[&]{if(images.isEmpty())return;if(destination)queueImageImports(images,destination,point);else for(const auto& path:images)openPath(path);images.clear();};
        for(const auto&url:event->mimeData()->urls())if(url.isLocalFile()){localItems=true;const auto path=url.toLocalFile();if(QFileInfo(path).isDir()){flush();openPath(path);}else images.append(path);}
        flush();
        if(!localItems&&event->mimeData()->hasImage()){
            auto temp=std::make_shared<QTemporaryDir>();if(!temp->isValid())throw std::runtime_error("Cannot create dropped-image temporary storage");
            const auto image=qvariant_cast<QImage>(event->mimeData()->imageData());const auto path=temp->filePath("Dropped Image.png");if(image.isNull()||!image.save(path,"PNG"))throw std::runtime_error("Cannot copy dropped image data");
            finishOpacityEdit();applyGradient();if(transformSession_)applyTransformSession();if(!destination)destination=&addEmptyProject();
            ui::ImportBatch batch;batch.files={nativePath(path)};batch.point=point;batch.lifetime=temp;ensureImportQueue()->enqueue(destination->canvas,std::move(batch));
        }
        event->acceptProposedAction();
    }catch(const std::exception&e){QMessageBox::critical(this,"Import Images",e.what());}
}
void MainWindow::openProjectDialog(){auto path=QFileDialog::getExistingDirectory(this,"Open Compositor Project — choose a .comp directory");if(!path.isEmpty())openPath(path);}
void MainWindow::openPath(const QString&path){
    if(QFileInfo(path).isDir()){
        // ProjectWorkspace.swift:59,77-80: guard before any preparation and
        // select an already-open resolved project without reloading its state.
        if(!canSwitchProjects())return;
        const auto supplied=nativePath(path);
        for(size_t index=0;index<projects_.size();++index){
            const auto& project=projects_[index];if(project->path.isEmpty())continue;
            std::error_code error;
            if(std::filesystem::equivalent(supplied,nativePath(project->path),error)&&!error){
                tabs_->setCurrentIndex(int(index));return;
            }
        }
        if(transformSession_&&transformSession_->persistent)applyTransformSession();
        ProjectStore store(makeWicProjectCodec());auto opened=store.load(supplied);
        auto&project=addProject(std::move(opened.document),QFileInfo(path).fileName());
        project.path=path;project.active=opened.activeLayer;project.history.reset();refresh(false);return;
    }
    applyGradient();if(transformSession_&&transformSession_->persistent)applyTransformSession();
    const bool reuse=!current()||!importQueue_||!importQueue_->contains(current()->canvas);
    auto& destination=addEmptyProject(reuse);queueImageImports({path},&destination,{});
}
void MainWindow::importImage(){auto* target=current();auto paths=QFileDialog::getOpenFileNames(this,"Import Images",{},"Images (*.png *.jpg *.jpeg *.tif *.tiff *.heic *.heif);;All files (*)");if(!paths.isEmpty())queueImageImports(paths,target,{});}
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
    QSlider quality(Qt::Horizontal);quality.setObjectName("exportQuality");quality.setRange(0,100);quality.setValue(int(std::round(std::clamp(remembered,0.,1.)*100)));layout.addRow("JPEG quality",&quality);
    QPushButton matte("White");matte.setObjectName("exportMatte");QColor matteColor=Qt::white;layout.addRow("JPEG background",&matte);
    QLabel preview;preview.setObjectName("exportPreview");preview.setMinimumSize(520,320);preview.setAlignment(Qt::AlignCenter);layout.addRow(&preview);
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
        preview.setPixmap(QPixmap::fromImage(shown.copy()));readyPath=prepared.path;
        bytesLabel.setText(QString::number(prepared.result.encodedBytes)+" bytes");buttons.button(QDialogButtonBox::Save)->setEnabled(true);
        dialog.setProperty("readyExportPath",readyPath);dialog.setProperty("readyExportGeneration",generation);
    });
    connect(&debounce,&QTimer::timeout,&dialog,start);
    connect(&progress,&QTimer::timeout,&dialog,[&]{if(running&&runningGeneration==generation)bytesLabel.setText(QString("Encoding %1%").arg(100ULL*completed->load()/uint32_t(snapshot.height)));});
    connect(&format,&QComboBox::currentIndexChanged,&dialog,[&]{quality.setEnabled(format.currentIndex()==1);matte.setEnabled(format.currentIndex()==1);changed();});
    connect(&quality,&QSlider::valueChanged,&dialog,changed);
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
