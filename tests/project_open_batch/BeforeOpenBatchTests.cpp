#include "ui/MainWindow.h"
#include "persistence/ProjectStore.h"
#include <QApplication>
#include <QDir>
#include <QDropEvent>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QMimeData>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <iostream>
#include <map>

using namespace compositor;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
QAction* command(MainWindow& window,const char* id){for(auto* action:window.findChildren<QAction*>())if(action->property("commandId").toString()==id)return action;throw std::runtime_error("missing command");}
Document sample(int width){Document d;d.id=newId();d.width=width;d.height=24;Layer layer;layer.id=newId();layer.name="Saved image";layer.transform={0,0,double(width),24};layer.raster=Raster::filled(width,24,{90,120,150,255});d.layers={layer};return d;}
struct Fixture {
    MainWindow window{true};QTemporaryDir directory{QFileInfo(QString::fromUtf8(__FILE__)).dir().filePath("fixtures/before-XXXXXX")};QString a,b,broken;int errors{};QTimer closeErrors;
    Fixture(){require(directory.isValid(),"unique retained fixture");directory.setAutoRemove(false);a=directory.filePath("A.comp");b=directory.filePath("B.comp");broken=directory.filePath("Broken.comp");ProjectStore store(makeWicProjectCodec());auto first=sample(32),second=sample(48);store.save(std::filesystem::path(a.toStdWString()),first,first.layers[0].id);store.save(std::filesystem::path(b.toStdWString()),second,second.layers[0].id);require(QDir().mkdir(broken),"broken directory");closeErrors.setInterval(10);QObject::connect(&closeErrors,&QTimer::timeout,&window,[this]{for(auto* box:window.findChildren<QMessageBox*>())if(box->isVisible()){++errors;std::cout<<"error="<<box->text().toStdString()<<'\n';box->accept();}});closeErrors.start();std::cout<<"fixture="<<directory.path().toStdString()<<'\n';}
    QTabWidget* tabs(){return window.findChild<QTabWidget*>();}
    void drop(const QStringList& paths){QMimeData mime;QList<QUrl> urls;for(const auto& p:paths)urls.append(QUrl::fromLocalFile(p));mime.setUrls(urls);QDragEnterEvent enter(QPoint(1,1),Qt::CopyAction,&mime,Qt::LeftButton,{});QApplication::sendEvent(&window,&enter);require(enter.isAccepted(),"directory drag accepted");QDropEvent event(QPointF(1,1),Qt::CopyAction,&mime,Qt::LeftButton,{});QApplication::sendEvent(&window,&event);}
};
void drop_order(){Fixture f;f.drop({f.a,f.b});std::cout<<"tabs="<<f.tabs()->count()<<" current="<<f.tabs()->currentIndex()<<'\n';require(f.tabs()->count()==2&&f.tabs()->currentIndex()==1&&f.tabs()->tabText(0).contains("A.comp")&&f.tabs()->tabText(1).contains("B.comp"),"directory order and selected last successful project");}
void drop_mixed_failure(){Fixture f;f.drop({f.a,f.broken,f.b});std::cout<<"tabs="<<f.tabs()->count()<<" errors="<<f.errors<<'\n';require(f.errors==1,"one failed package reported");require(f.tabs()->count()==2&&f.tabs()->currentIndex()==1&&f.tabs()->tabText(1).contains("B.comp"),"failed directory must not prevent later successful directory");}
void drop_existing(){Fixture f;f.window.openPath(f.a);command(f.window,"pixels.invert")->trigger();auto* original=f.tabs()->currentWidget();f.drop({f.b,f.a});require(f.tabs()->count()==2&&f.tabs()->currentWidget()==original&&command(f.window,"edit.undo")->isEnabled(),"batch duplicate reselects unsaved existing tab");}
void drop_pending_guard(){Fixture f;f.window.openPath(f.a);command(f.window,"tool.gradient")->trigger();auto* canvas=f.window.canvas();canvas->pointerDown({4,10},{});canvas->pointerMove({25,10},{});canvas->pointerUp({25,10},{});require(command(f.window,"gradient.apply")->isEnabled(),"gradient fixture");f.drop({f.b});require(f.tabs()->count()==1&&command(f.window,"gradient.apply")->isEnabled(),"pending directory drop may not replace or commit current draft");}
void dialog_cancel(){Fixture f;bool seen=false;QTimer reject;reject.setInterval(10);QObject::connect(&reject,&QTimer::timeout,&f.window,[&]{for(auto* dialog:f.window.findChildren<QFileDialog*>())if(dialog->isVisible()){seen=true;std::cout<<"fileMode="<<int(dialog->fileMode())<<" options="<<int(dialog->options())<<'\n';dialog->reject();}});reject.start();auto* original=f.tabs()->currentWidget();command(f.window,"file.open")->trigger();require(seen&&f.tabs()->count()==1&&f.tabs()->currentWidget()==original,"cancel preserves welcome tab");}
}
int main(int argc,char** argv){QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);QApplication app(argc,argv);std::cout<<std::unitbuf;QDir().mkpath(QFileInfo(QString::fromUtf8(__FILE__)).dir().filePath("fixtures"));const std::map<std::string,void(*)()> cases{{"drop_order",drop_order},{"drop_mixed_failure",drop_mixed_failure},{"drop_existing",drop_existing},{"drop_pending_guard",drop_pending_guard},{"dialog_cancel",dialog_cancel}};try{require(argc==2&&cases.contains(argv[1]),"provide named before case");std::cout<<"START "<<argv[1]<<'\n';cases.at(argv[1])();std::cout<<"PASS "<<argv[1]<<'\n';return 0;}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
