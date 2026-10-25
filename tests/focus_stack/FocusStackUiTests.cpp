// File > Focus Stack Images… through MainWindow, offscreen.
#include "ui/MainWindow.h"
#include "ui/FocusStackJob.h"
#include "ui/ImportActions.h"
#include "core/DocumentExport.h"
#include "imaging/wic_codec.h"
#include "FocusStackFrames.h"
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QMessageBox>
#include <QSettings>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <iostream>
#include <stdexcept>
using namespace compositor;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
QAction* action(MainWindow& window,const char* id){for(auto* candidate:window.findChildren<QAction*>())if(candidate->property("commandId").toString()==id&&candidate->isVisible())return candidate;throw std::runtime_error(std::string("Visible command absent: ")+id);}
void trigger(MainWindow& window,const char* id){auto* item=action(window,id);require(item->isEnabled(),"Command enabled");item->trigger();}
std::filesystem::path native(const QString& path){return std::filesystem::path(path.toStdWString());}
constexpr int W=96,H=64;
Document document(){
    Document result;result.id=newId();result.width=W;result.height=H;
    Layer layer;layer.id=newId();layer.name="Background";layer.transform={0,0,double(W),double(H)};layer.raster=Raster::filled(W,H,{20,40,60,255});
    result.layers.push_back(std::move(layer));return result;
}
struct Fixture {
    QTemporaryDir directory;
    std::unique_ptr<MainWindow> window;
    EditorProject* owner{};
    std::optional<Document> before;
    std::string active;
    QTimer dismiss;
    QStringList errors;
    QString nearPath,midPath,farPath,mismatchPath,corruptPath;
    Fixture(){
        require(directory.isValid(),"Owned fixture directory");
        QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,directory.path());
        auto write=[&](const QString& name,const imaging::RgbaImage& image){const auto path=directory.filePath(name);imaging::WicCodec::encode(native(path),image);return path;};
        nearPath=write("near.png",focus_stack_fixture::frame(W,H,[](int x,int){return x>=W/3;}));
        midPath=write("mid.png",focus_stack_fixture::frame(W,H,[](int x,int){return x<W/3||x>=2*W/3;},2,-1));
        farPath=write("far.png",focus_stack_fixture::frame(W,H,[](int x,int){return x<2*W/3;}));
        mismatchPath=write("mismatch.png",focus_stack_fixture::frame(W+8,H,[](int,int){return false;}));
        corruptPath=directory.filePath("corrupt.png");{QFile file(corruptPath);require(file.open(QIODevice::WriteOnly),"Corrupt fixture");file.write("not an image");}
        window=std::make_unique<MainWindow>(true);owner=&window->addProject(document(),"Focus stack fixture");
        window->resize(1100,800);window->show();QTest::qWait(20);before=owner->document;active=owner->active;
        dismiss.setInterval(5);QObject::connect(&dismiss,&QTimer::timeout,window.get(),[this]{for(auto* box:window->findChildren<QMessageBox*>())if(box->isVisible()){errors.append(box->text());box->close();}});dismiss.start();
    }
    ~Fixture(){dismiss.stop();window.reset();}
    QStringList sources()const{return {nearPath,midPath,farPath};}
    ui::FocusStackJob* job()const{return ui::FocusStackJob::find(window.get());}
    void start(const QStringList& paths,const char* output){
        window->setProperty("testFocusStackPaths",paths);window->setProperty("testFocusStackOutput",QString(output));
        trigger(*window,"file.focus_stack");
    }
    void wait(){QElapsedTimer timer;timer.start();while(job()){if(timer.elapsed()>30000)throw std::runtime_error("Focus stack timed out");QTest::qWait(5);}QTest::qWait(30);}
    void unchanged()const{require(owner->document==before&&owner->active==active&&owner->history.undoCount()==0&&!owner->projectBusy,"Document, history and busy state are unchanged");}
    std::shared_ptr<const Raster> expected()const{return ui::runFocusStack({native(nearPath),native(midPath),native(farPath)},ui::decodeImportImage,{});}
    int tabs()const{auto* widget=window->findChild<QTabWidget*>();require(widget,"Project tabs exist");return widget->count();}
};
bool samePixels(const Raster& a,const Raster& b){return a.width==b.width&&a.height==b.height&&a.rgba()==b.rgba();}
void apply_new_layer_single_undo(){
    Fixture f;f.start(f.sources(),"layer");
    require(f.owner->projectBusy&&f.job(),"Job owns a busy project");require(!action(*f.window,"edit.undo")->isEnabled(),"Undo is disabled while stacking");
    f.wait();
    require(f.errors.isEmpty(),"No error on valid sources");
    require(f.owner->document->layers.size()==2&&f.owner->history.undoCount()==1,"One new layer and one history entry");
    const auto& layer=f.owner->document->layers.back();
    require(layer.name=="Focus Stack"&&f.owner->active==layer.id&&layer.raster&&samePixels(*layer.raster,*f.expected()),"Result layer is active and holds the stacked pixels");
    require(f.owner->document->layers.front()==f.before->layers.front(),"Existing layer is untouched");
    trigger(*f.window,"edit.undo");require(f.owner->document==f.before&&f.owner->active==f.active,"Undo restores the original document");
    trigger(*f.window,"edit.redo");require(f.owner->document->layers.size()==2&&samePixels(*f.owner->document->layers.back().raster,*f.expected()),"Redo restores the result");
}
void new_document_tab(){
    Fixture f;const int tabs=f.tabs();f.start(f.sources(),"document");
    require(!f.owner->projectBusy,"New-document mode leaves the current project editable");
    f.wait();
    require(f.errors.isEmpty(),"No error on valid sources");
    require(f.tabs()==tabs+1,"One new tab");
    auto* widget=f.window->findChild<QTabWidget*>();require(widget->tabText(widget->currentIndex()).contains("Focus Stack"),"New tab is current and titled Focus Stack");
    require(f.owner->document==f.before&&f.owner->history.undoCount()==0,"Source project is untouched");
}
void cancel_keeps_document(){
    {Fixture f;const int tabs=f.tabs();f.start(f.sources(),"cancel");require(!f.job()&&f.tabs()==tabs,"Output cancel starts nothing");f.unchanged();}
    {Fixture f;f.start(f.sources(),"layer");auto* job=f.job();require(job,"Job started");job->cancel();f.wait();f.unchanged();require(f.errors.isEmpty(),"Cancel reports no error");}
    {Fixture f;const int tabs=f.tabs();f.start(f.sources(),"document");f.job()->cancel();f.wait();require(f.tabs()==tabs&&f.errors.isEmpty(),"Cancelled new-document mode adds no tab");f.unchanged();}
}
void invalid_input_unchanged(){
    {Fixture f;const int tabs=f.tabs();f.start({f.nearPath,f.mismatchPath},"layer");f.wait();f.unchanged();require(f.tabs()==tabs,"No tab");
     require(f.errors.size()==1&&f.errors.front().contains("mismatch.png")&&f.errors.front().contains("differs from the first image"),"Size mismatch names the file");}
    {Fixture f;const int tabs=f.tabs();f.start({f.nearPath,f.corruptPath},"document");f.wait();f.unchanged();require(f.tabs()==tabs,"No tab");
     require(f.errors.size()==1&&f.errors.front().startsWith("corrupt.png: "),"Unreadable file is named");}
}
void single_image_rejected(){
    Fixture f;f.start({f.nearPath},"layer");require(!f.job(),"No job for one image");QTest::qWait(30);f.unchanged();
    require(f.errors.size()==1&&f.errors.front()=="Select at least two images to focus stack.","One image is rejected");
}
void stale_result_discarded(){
    Fixture f;f.start(f.sources(),"layer");
    auto changed=*f.owner->document;changed.layers.front().name="Changed";f.owner->document=changed;
    f.wait();
    require(f.owner->document==changed&&f.owner->history.undoCount()==0&&!f.owner->projectBusy,"Stale result is not applied");
    require(f.errors.size()==1&&f.errors.front().contains("document changed"),"Stale result is reported");
}
void export_includes_result(){
    Fixture f;f.start(f.sources(),"layer");f.wait();require(f.owner->history.undoCount()==1,"Result applied");
    const auto out=native(f.directory.filePath("export.png"));
    exportDocumentAtomic(*f.owner->document,out);
    const auto decoded=imaging::WicCodec::decode(out,{});
    const auto expected=f.expected()->rgba();
    require(decoded.image.width==W&&decoded.image.height==H,"Export size");
    for(int y=0;y<H;++y)require(std::equal(expected.begin()+size_t(y)*W*4,expected.begin()+size_t(y+1)*W*4,decoded.image.pixels.begin()+ptrdiff_t(size_t(y)*decoded.image.stride)),"Exported pixels equal the stacked result");
}
}
int main(int argc,char** argv){
    QApplication app(argc,argv);
    try{
        if(argc!=2)throw std::runtime_error("one test name required");
        const std::string name=argv[1];
        if(name=="apply_new_layer_single_undo")apply_new_layer_single_undo();
        else if(name=="new_document_tab")new_document_tab();
        else if(name=="cancel_keeps_document")cancel_keeps_document();
        else if(name=="invalid_input_unchanged")invalid_input_unchanged();
        else if(name=="single_image_rejected")single_image_rejected();
        else if(name=="stale_result_discarded")stale_result_discarded();
        else if(name=="export_includes_result")export_includes_result();
        else throw std::runtime_error("unknown case");
        std::cout<<"PASS "<<name<<'\n';return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
