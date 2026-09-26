#include "ui/MainWindow.h"
#include "filters/PixelFilters.h"
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QPointer>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <iostream>
#include <stdexcept>
using namespace compositor;
void require(bool ok,const char*message){if(!ok)throw std::runtime_error(message);}
Document fixture(){Document d;d.id="large-filter";d.width=d.height=30000;Layer l;l.id="image";l.transform={14984,14992,32,16};std::vector<Pixel> pixels(512);for(int y=0;y<16;++y)for(int x=0;x<32;++x)pixels[size_t(y)*32+x]={uint8_t(x*7),uint8_t(y*13),90,255};l.raster=Raster::fromRgba(32,16,reinterpret_cast<const uint8_t*>(pixels.data()),128);d.layers.push_back(l);return d;}
void run(const char* command,filters::Kind kind,bool apply){
    MainWindow window(true);auto d=fixture();auto&project=window.addProject(d);project.toolState.filterSettings.pixels.distortion=25;project.toolState.filterSettings.pixels.radius=1;
    filters::Request request;request.kind=kind;request.source=d.layers[0].raster;request.transform=d.layers[0].transform;request.settings=project.toolState.filterSettings.pixels;auto expected=filters::apply(request);
    QAction* action=nullptr;for(auto*item:window.findChildren<QAction*>())if(item->property("commandId").toString()==command)action=item;
    require(action&&action->isEnabled(),"Filter command unavailable");
    QTimer poll;QElapsedTimer elapsed;QEventLoop wait;QPointer<QDialog> owned;bool preview=false,clicked=false,timedOut=false,finished=false;
    poll.setInterval(20);QObject::connect(&poll,&QTimer::timeout,[&]{
        if(elapsed.elapsed()>10000){timedOut=true;if(owned)owned->reject();wait.quit();return;}
        if(!owned){
            for(auto* candidate:window.findChildren<QDialog*>("filterPanel"))if(candidate->isVisible()){owned=candidate;break;}
            if(owned)QObject::connect(owned,&QDialog::finished,&wait,[&](int){finished=true;wait.quit();});
        }
        auto* dialog=owned.data();if(!dialog)return;
        auto* buttons=dialog->findChild<QDialogButtonBox*>();if(!buttons)return;
        if(!clicked&&buttons->button(QDialogButtonBox::Apply)->isEnabled()){
            for(auto* label:dialog->findChildren<QLabel*>())if(label->pixmap().size()==QSize(420,420))preview=true;
            clicked=true;(apply?buttons->button(QDialogButtonBox::Apply):buttons->button(QDialogButtonBox::Cancel))->click();
        }
    });
    elapsed.start();action->trigger();poll.start();if(!finished)wait.exec();poll.stop();
    require(!timedOut&&preview&&clicked,"Actual filter thumbnail never became ready");
    if(!apply){require(project.document==d&&project.history.undoCount()==0,"Cancel changed source or history");return;}
    require(project.document&&project.document->width==30000&&project.document->height==30000,"Filter changed canvas extent");
    const auto& output=project.document->layers[0];
    require(output.transform==expected.transform&&output.raster->width==expected.raster->width&&output.raster->height==expected.raster->height,"Filter output geometry changed");
    require(output.raster->rgba()==expected.raster->rgba(),"Filter output differs from full source-grid processing");
    require(project.history.undoCount()==1,"Filter did not create one history entry");auto undo=project.history.undo();
    require(undo&&undo->document==d,"Filter undo did not restore immutable original");
}
int main(int argc,char**argv){QApplication app(argc,argv);int failed=0;auto test=[&](const char*name,auto fn){if(argc>1&&std::string(argv[1])!=name)return;try{fn();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception&e){++failed;std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n';}};test("filter_lens_30000_apply",[]{run("filter.lens",filters::Kind::LensCorrection,true);});test("filter_gaussian_30000_apply",[]{run("filter.gaussian",filters::Kind::GaussianBlur,true);});test("filter_30000_cancel",[]{run("filter.lens",filters::Kind::LensCorrection,false);});return failed?1:0;}
