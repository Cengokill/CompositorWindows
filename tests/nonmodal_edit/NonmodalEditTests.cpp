#include "ui/MainWindow.h"
#include "ui/EditPanelSession.h"
#include "ui/LastFilterJob.h"
#include "core/DocumentExport.h"
#include "imaging/wic_codec.h"
#include "persistence/ProjectStore.h"
#include <QMessageBox>
#include "ui/AdjustmentAdvancedControls.h"
#include "effects/Adjustments.h"
#include "filters/PixelFilters.h"
#include "graphics/MaskSampling.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QPushButton>
#include <QToolButton>
#include <QImage>
#include <QPixmap>
#include <QJsonDocument>
#include <QTest>
#include <QTemporaryDir>
#include <QTimer>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <array>
#include <map>

using namespace compositor;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Fn>void wait(Fn predicate,const char* message,int ms=6000){QElapsedTimer elapsed;elapsed.start();while(!predicate()){if(elapsed.elapsed()>ms)throw std::runtime_error(message);QApplication::processEvents(QEventLoop::AllEvents,20);QTest::qWait(1);}}
QAction* action(MainWindow& window,const char* id){for(auto* value:window.findChildren<QAction*>())if(value->property("commandId")==id)return value;throw std::runtime_error(std::string("missing action ")+id);}
void trigger(MainWindow& window,const char* id){auto* value=action(window,id);require(value->isEnabled(),"requested action enabled");value->trigger();}
template<class T>T* control(QObject& owner,const char* name){for(auto* object:owner.findChildren<QObject*>())if(object->objectName()==name)if(auto* result=dynamic_cast<T*>(object))return result;throw std::runtime_error("named panel control");}
QDoubleSpinBox* field(QDialog& panel,const QString& name){for(auto* value:panel.findChildren<QDoubleSpinBox*>())if(value->accessibleName()==name)return value;throw std::runtime_error("named number field");}
QPushButton* apply(QDialog& panel){auto* buttons=panel.findChild<QDialogButtonBox*>();require(buttons,"panel buttons");auto* result=buttons->button(QDialogButtonBox::Apply);require(result,"panel Apply");return result;}
void ready(QDialog& panel){wait([&]{return apply(panel)->isEnabled();},"preview did not become ready");}
QCheckBox* preview(QDialog& panel){for(auto* box:panel.findChildren<QCheckBox*>())if(box->text()=="Preview")return box;throw std::runtime_error("source Preview checkbox is missing");}
Document fixture(Pixel pixel={80,100,120,255}){Document d;d.id="nonmodal-document";d.width=64;d.height=48;Layer l;l.id="pixels";l.name="Pixels";l.transform={16,12,32,24};l.raster=Raster::filled(32,24,pixel);d.layers={l};return d;}
struct Fixture {MainWindow window{true};EditorProject& project;Document before;
    explicit Fixture(Document d=fixture()):project(window.addProject(d)),before(std::move(d)){window.show();QApplication::processEvents();}
    Pixel visible(int x=30,int y=20){auto view=project.canvas->viewportProvider(0,0,64,48,1);require(bool(view.raster),"main canvas raster");return view.raster->pixel(x,y);}
    void unchanged(){require(project.document==before&&project.history.undoCount()==0&&!project.history.modified(),"draft changed canonical document/history");}
    void click(Point point){auto view=project.canvas->viewMapping().toView(point);QTest::mouseClick(project.canvas,Qt::LeftButton,{},QPoint(int(view.x),int(view.y)));QApplication::processEvents();}
};
template<class Fn>void panel(MainWindow& window,const char* command,const QString& title,Fn body){
    QEventLoop loop;QTimer poll;poll.setInterval(10);QElapsedTimer elapsed;elapsed.start();std::exception_ptr failure;bool visited=false;
    QObject::connect(&poll,&QTimer::timeout,&window,[&]{QPointer<QDialog> found;for(auto* candidate:window.findChildren<QDialog*>())if(candidate->isVisible()&&candidate->windowTitle()==title){found=candidate;break;}
        if(!found){if(elapsed.elapsed()<6000)return;poll.stop();failure=std::make_exception_ptr(std::runtime_error("owned editor panel did not open"));if(auto* modal=qobject_cast<QDialog*>(QApplication::activeModalWidget()))modal->reject();loop.quit();return;}
        poll.stop();visited=true;try{body(*found);}catch(...){failure=std::current_exception();}if(found&&found->isVisible())found->reject();loop.quit();});
    poll.start();QTimer::singleShot(0,&window,[&]{try{trigger(window,command);}catch(...){failure=std::current_exception();poll.stop();loop.quit();}});loop.exec();if(failure)std::rethrow_exception(failure);require(visited,"actual editor panel visited");QApplication::processEvents();
}
Pixel exposurePixel(const Document& doc,double amount){auto json=effects::defaultAdjustmentJson("Exposure");auto object=QJsonDocument::fromJson(QByteArray::fromStdString(json)).object();auto settings=object["exposureSettings"].toObject();settings["exposure"]=amount;settings["offset"]=0;settings["gamma"]=1;object["exposureSettings"]=settings;auto output=doc;output.layers[0].raster=effects::applyAdjustment(output.layers[0].raster,QJsonDocument(object).toJson(QJsonDocument::Compact).toStdString());return SoftwareRenderer().render(output,0,0,64,48)->pixel(30,20);}
void adjustment_nonmodal(){Fixture f;panel(f.window,"adjust.exposure","Exposure",[&](QDialog& p){require(!QApplication::activeModalWidget()&&p.windowModality()==Qt::NonModal,"source adjustment panel is nonmodal");require(f.project.canvas->isEnabled(),"main canvas enabled during edit");f.unchanged();});f.unchanged();}
void lens_effects_lifecycle(){
    auto d=fixture();d.id="11111111-1111-4111-8111-111111111111";d.layers[0].id="22222222-2222-4222-8222-222222222222";auto pixels=d.layers[0].raster->rgba();for(int y=8;y<14;++y)for(int x=8;x<14;++x)for(int c=0;c<4;++c)pixels[(y*32+x)*4+c]=255;d.layers[0].raster=Raster::fromRgba(32,24,pixels.data(),128);Fixture f(d);
    panel(f.window,"filter.lens_effects","Lens Effects",[&](QDialog& p){ready(p);control<QComboBox>(p,"lensEffectMode")->setCurrentIndex(3);ready(p);f.unchanged();preview(p)->setChecked(false);preview(p)->setChecked(true);});f.unchanged();require(!f.project.effectPreview,"lens Cancel left overlay");
    panel(f.window,"filter.lens_effects","Lens Effects",[&](QDialog& p){control<QComboBox>(p,"lensEffectMode")->setCurrentIndex(3);ready(p);apply(p)->click();wait([guard=QPointer<QDialog>(&p)]{return !guard||!guard->isVisible();},"lens Apply did not finish");});
    require(f.project.history.undoCount()==1&&f.project.toolState.lastFilterKind==filters::Kind::LensEffects,"lens Apply history and Last Filter");auto applied=f.project.document;trigger(f.window,"edit.undo");f.unchanged();trigger(f.window,"edit.redo");require(f.project.document==applied,"lens Redo");
    QTemporaryDir temp;require(temp.isValid(),"lens temporary directory");auto root=std::filesystem::path(temp.path().toStdWString());ProjectStore store(makeWicProjectCodec());store.save(root/L"lens.comp",*applied,f.project.active);auto reopened=store.load(root/L"lens.comp");require(reopened.document.layers[0].raster->rgba()==applied->layers[0].raster->rgba(),"lens save reopen pixels");
    auto expected=SoftwareRenderer().render(reopened.document,0,0,64,48)->rgba();exportDocumentAtomic(reopened.document,root/L"lens.png");require(imaging::WicCodec::decode(root/L"lens.png").image.pixels==expected,"lens reopened PNG export pixels");

}
void filter_nonmodal(){Fixture f;panel(f.window,"filter.gaussian","Gaussian Blur",[&](QDialog& p){require(!QApplication::activeModalWidget()&&p.windowModality()==Qt::NonModal,"source filter panel is nonmodal");f.unchanged();});f.unchanged();
    auto d=fixture();auto deep=makeDeep(32,24,DeepImage::Format::Unorm16);for(int y=0;y<24;++y)for(int x=0;x<32;++x)deep->setPixel(x,y,{.2f,.3f,.4f,1.f});d.layers[0].deep=deep;d.layers[0].raster=rasterFromDeep(*deep);Fixture wide(d);
    bool answered=false;QTimer confirmation;confirmation.setInterval(5);QObject::connect(&confirmation,&QTimer::timeout,&wide.window,[&]{if(auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())){answered=true;confirmation.stop();box->button(QMessageBox::Ok)->click();}});confirmation.start();
    panel(wide.window,"filter.lens","Lens Correction",[&](QDialog& p){ready(p);wide.unchanged();});confirmation.stop();require(answered,"16-bit filter conversion prompt missing");wide.unchanged();
}
void adjustment_canvas_preview(){Fixture f;const auto expected=exposurePixel(f.before,1);panel(f.window,"adjust.exposure","Exposure",[&](QDialog& p){field(p,"Exposure")->setValue(1);ready(p);f.unchanged();require(f.visible()==expected,"main canvas shows adjustment preview");});f.unchanged();require(f.visible()==Pixel{80,100,120,255},"Cancel restores original canvas");}
void adjustment_preview_toggle(){Fixture f;const auto expected=exposurePixel(f.before,1);panel(f.window,"adjust.exposure","Exposure",[&](QDialog& p){field(p,"Exposure")->setValue(1);ready(p);require(f.visible()==expected,"preview before toggle");preview(p)->setChecked(false);QApplication::processEvents();require(f.visible()==Pixel{80,100,120,255},"Preview off restores original immediately");preview(p)->setChecked(true);ready(p);require(f.visible()==expected,"Preview on restores current adjustment");f.unchanged();});f.unchanged();}
void adjustment_apply_undo(){Fixture f;const auto expected=exposurePixel(f.before,1);panel(f.window,"adjust.exposure","Exposure",[&](QDialog& p){field(p,"Exposure")->setValue(1);ready(p);apply(p)->click();wait([guard=QPointer<QDialog>(&p)]{return !guard||!guard->isVisible();},"Apply did not finish");});require(f.visible()==expected&&f.project.history.undoCount()==1,"Apply commits rendered result once");trigger(f.window,"edit.undo");f.unchanged();}
void adjustment_close_cancel(){Fixture f;panel(f.window,"adjust.exposure","Exposure",[&](QDialog& p){field(p,"Exposure")->setValue(1);ready(p);p.close();});f.unchanged();require(f.visible()==Pixel{80,100,120,255},"window close restores original canvas");}
void adjustment_position(){Fixture f;QPoint position;panel(f.window,"adjust.exposure","Exposure",[&](QDialog& p){p.move(131,147);position=p.pos();});panel(f.window,"adjust.exposure","Exposure",[&](QDialog& p){require(p.pos()==position,"reopened panel retains source position");});f.unchanged();}
void levels_canvas_original_sample(){Fixture f(fixture({80,80,80,255}));panel(f.window,"adjust.levels","Levels",[&](QDialog& p){control<QDoubleSpinBox>(p,"levelsValue3")->setValue(160);ready(p);control<QPushButton>(p,"levelsSampleBlack")->click();f.click({30,20});auto settings=effects_tools::levelsFromAdjustmentJson(control<LevelsAdvancedControls>(p,"levelsAdvancedControls")->adjustmentJson());for(size_t c=1;c<4;++c)require(settings.ranges[c].black==80,"main canvas Levels samples original80 rather than preview");f.unchanged();});f.unchanged();}
void hue_canvas_sample(){Fixture f(fixture({0,255,0,255}));panel(f.window,"adjust.hue_saturation","Hue/Saturation",[&](QDialog& p){control<QComboBox>(p,"hueRange")->setCurrentIndex(int(effects_tools::ColorRange::Reds));auto* controls=control<HueAdvancedControls>(p,"hueAdvancedControls");auto before=effects_tools::hueFromAdjustmentJson(controls->adjustmentJson());control<QPushButton>(p,"hueSampleColor")->click();f.click({30,20});const auto actual=effects_tools::hueFromAdjustmentJson(controls->adjustmentJson());const auto expected=effects_tools::sampleHueRange(before,120,effects_tools::HueSample::Replace);require(actual==expected&&actual!=before,"main canvas Hue samples visible green");f.unchanged();});f.unchanged();}
void hue_canvas_target(){Fixture f(fixture({100,160,100,255}));panel(f.window,"adjust.hue_saturation","Hue/Saturation",[&](QDialog& p){auto* controls=control<HueAdvancedControls>(p,"hueAdvancedControls");auto expected=effects_tools::hueFromAdjustmentJson(controls->adjustmentJson());auto drag=effects_tools::beginHueTargeting(expected,effects_tools::PaletteColor{100/255.,160/255.,100/255.});require(drag.has_value(),"targeting fixture");control<QPushButton>(p,"hueTargetedAdjustment")->click();auto at=f.project.canvas->viewMapping().toView({30,20});QPoint start(int(at.x),int(at.y));QTest::mousePress(f.project.canvas,Qt::LeftButton,{},start);QTest::mouseMove(f.project.canvas,start+QPoint(20,0));QTest::mouseRelease(f.project.canvas,Qt::LeftButton,{},start+QPoint(20,0));QApplication::processEvents();expected=effects_tools::dragHueTargeting(expected,*drag,20,false);require(effects_tools::hueFromAdjustmentJson(controls->adjustmentJson())==expected,"main canvas targeted Hue drag uses view-pixel delta");f.unchanged();});f.unchanged();}
void filter_canvas_preview(){Fixture f;filters::Request job;job.kind=filters::Kind::GaussianBlur;job.source=f.before.layers[0].raster;job.transform=f.before.layers[0].transform;job.settings.radius=3;job.preview=true;auto result=filters::apply(job);auto expected=f.before;expected.layers[0].raster=result.raster;expected.layers[0].transform=result.transform;const auto pixel=SoftwareRenderer().render(expected,0,0,64,48)->pixel(16,12);panel(f.window,"filter.gaussian","Gaussian Blur",[&](QDialog& p){field(p,"Radius")->setValue(3);ready(p);f.unchanged();require(f.visible(16,12)==pixel,"main canvas shows filter preview at layer edge");});f.unchanged();}
void filter_apply_undo(){Fixture f;panel(f.window,"filter.gaussian","Gaussian Blur",[&](QDialog& p){field(p,"Radius")->setValue(3);ready(p);apply(p)->click();wait([guard=QPointer<QDialog>(&p)]{return !guard||!guard->isVisible();},"full filter Apply did not finish");});require(f.project.document!=f.before&&f.project.history.undoCount()==1,"full filter commits one edit");trigger(f.window,"edit.undo");f.unchanged();}
void progressive_canvas_cancel_apply(){
    Fixture f;
    auto drag=[&]{auto a=f.project.canvas->viewMapping().toView({20,16}),b=f.project.canvas->viewMapping().toView({42,30});QPoint start(int(a.x),int(a.y)),end(int(b.x),int(b.y));QTest::mousePress(f.project.canvas,Qt::LeftButton,{},start);QTest::mouseMove(f.project.canvas,end);QTest::mouseRelease(f.project.canvas,Qt::LeftButton,{},end);};
    panel(f.window,"filter.progressive","Progressive Blur",[&](QDialog& p){field(p,"To")->setValue(3);ready(p);auto beforeDrag=f.project.effectPreview;require(bool(beforeDrag),"initial progressive preview");drag();ready(p);require(f.project.effectPreview&&f.project.effectPreview!=beforeDrag&&f.project.effectPreview->layers[0].raster->rgba()!=beforeDrag->layers[0].raster->rgba(),"drag updates progressive preview pixels");f.unchanged();});
    f.unchanged();require(!f.project.effectPreview,"cancel clears progressive preview");
    panel(f.window,"filter.progressive","Progressive Blur",[&](QDialog& p){field(p,"To")->setValue(3);ready(p);drag();ready(p);apply(p)->click();wait([guard=QPointer<QDialog>(&p)]{return !guard||!guard->isVisible();},"progressive apply did not finish");});
    require(f.project.history.undoCount()==1&&f.project.document!=f.before,"progressive apply is one edit");const auto applied=f.project.document;trigger(f.window,"edit.undo");f.unchanged();trigger(f.window,"edit.redo");require(f.project.document==applied,"progressive redo restores output");
}
void rgb_split_small_layer_apply_undo(){
    Fixture f;
    require(f.before.layers[0].transform.width<f.before.width&&f.before.layers[0].transform.height<f.before.height,"fixture layer is smaller than the canvas");
    panel(f.window,"filter.rgb_split","RGB Split",[&](QDialog& p){
        field(p,"Distance")->setValue(8);field(p,"Blur")->setValue(4);
        for(auto* box:p.findChildren<QCheckBox*>())if(box->text()=="Radial")box->setChecked(true);
        ready(p);apply(p)->click();wait([guard=QPointer<QDialog>(&p)]{return !guard||!guard->isVisible();},"RGB Split Apply did not finish");
    });
    require(f.project.document!=f.before&&f.project.history.undoCount()==1,"RGB Split on a smaller layer commits one edit");
    require(f.project.document->layers.size()==1,"RGB Split on a smaller layer keeps a single layer");
    trigger(f.window,"edit.undo");f.unchanged();
}
void rgb_split_canvas_split_layers(){
    auto doc=fixture({64,32,96,128});
    doc.id=newId();doc.layers[0].id=newId();
    auto mask=std::make_shared<GrayRaster>();mask->width=mask->height=1;mask->pixels={100};
    doc.layers[0].mask=Mask{mask};doc.layers[0].opacity=.5;
    Fixture f(std::move(doc));
    f.project.history.begin("Rename",f.project.document,f.project.active);
    f.project.document->layers[0].name="Renamed";
    f.project.history.end(f.project.document,f.project.active);
    panel(f.window,"filter.rgb_split","RGB Split",[&](QDialog& p){
        field(p,"Distance")->setValue(8);ready(p);require(bool(f.project.effectPreview),"RGB Split preview is installed");
        for(auto* box:p.findChildren<QCheckBox*>())if(box->text()=="Radial")box->setChecked(true);
        ready(p);auto before=f.project.effectPreview;f.click({30,20});ready(p);
        require(f.project.effectPreview&&f.project.effectPreview!=before,"radial RGB Split center updates preview");
        trigger(f.window,"edit.undo");f.unchanged();
        f.project.history.byteLimit=0;
        control<QPushButton>(p,"rgbSplitToLayers")->click();
        wait([guard=QPointer<QDialog>(&p)]{return !guard||!guard->isVisible();},"Split to Layers did not close the panel");
    });
    require(f.project.document->layers.size()==5&&f.project.history.undoCount()==1,"Split to Layers adds original-channel layers");
    require(f.project.document->layers[0]==f.before.layers[0],"Split to Layers preserves the current source including its undone name and mask");
    constexpr std::array names{"Red","Green","Blue","Alpha"};
    constexpr std::array<std::uint8_t,4> intensities{128,64,191,128};
    for(std::size_t i=0;i<names.size();++i){
        const auto& layer=f.project.document->layers[i+1];
        const auto value=intensities[i];
        require(layer.name==names[i]&&!layer.visible&&!layer.mask&&layer.opacity==1&&layer.transform==f.before.layers[0].transform,
                "Split channels are hidden, independent layers at the source placement");
        require(layer.raster->pixel(5,5)==Pixel{value,value,value,255},"Split uses original straight channels");
    }
    require(!f.project.effectPreview,"Split clears the preview");
    require(SoftwareRenderer().render(*f.project.document,0,0,64,48)->rgba()==SoftwareRenderer().render(f.before,0,0,64,48)->rgba(),"Split to Layers preserves the rendered canvas");
    const auto applied=f.project.document;
    trigger(f.window,"edit.undo");f.unchanged();
    trigger(f.window,"edit.redo");require(f.project.document==applied,"Split redo restores all four layers");

    QTemporaryDir temp;require(temp.isValid(),"temporary directory for RGB Split round trip");
    const auto root=std::filesystem::path(temp.path().toStdWString());
    ProjectStore store(makeWicProjectCodec());
    const auto package=root/"split.comp";
    store.save(package,*applied,f.project.active);
    const auto reopened=store.load(package);
    require(reopened.document.layers.size()==5,"saved Split reopens with all layers");
    for(std::size_t i=0;i<names.size();++i){
        const auto& layer=reopened.document.layers[i+1];
        const auto value=intensities[i];
        require(layer.name==names[i]&&!layer.visible&&layer.raster->pixel(5,5)==Pixel{value,value,value,255},
                "reopened Split preserves hidden channel pixels");
    }
    exportDocumentAtomic(f.before,root/"before.png");
    exportDocumentAtomic(reopened.document,root/"after.png");
    require(imaging::WicCodec::decode(root/"before.png").image.pixels==imaging::WicCodec::decode(root/"after.png").image.pixels,
            "hidden split layers preserve flattened PNG pixels after reopen");
}
void rgb_split_canvas_cancel_layers(){
    Fixture f;
    panel(f.window,"filter.rgb_split","RGB Split",[&](QDialog& p){
        ready(p);
        control<QPushButton>(p,"rgbSplitToLayers")->click();
        p.close();
        require(!p.isVisible(),"Split can be cancelled while its channels are pending");
    });
    f.unchanged();require(!f.project.effectPreview,"closing during Split clears the preview");

    auto doc=fixture();doc.id=newId();doc.layers[0].id=newId();
    auto deep=makeDeep(32,24,DeepImage::Format::Unorm16);
    for(int x=0;x<2;++x){auto* pixel=deep->at(x,0);pixel[0]=uint16_t(10001+x);pixel[1]=20003;pixel[2]=30007;pixel[3]=65535;}
    doc.layers[0].deep=deep;doc.layers[0].raster=rasterFromDeep(*deep);
    Fixture wide(std::move(doc));
    panel(wide.window,"filter.rgb_split","RGB Split",[&](QDialog& p){
        ready(p);
        auto answer=[&](QMessageBox::StandardButton button){
            bool answered=false;
            QTimer confirmation;
            QObject::connect(&confirmation,&QTimer::timeout,&wide.window,[&]{
                if(auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())){
                    answered=true;confirmation.stop();box->button(button)->click();
                }
            });
            confirmation.start(5);
            control<QPushButton>(p,"rgbSplitToLayers")->click();
            require(answered,"16-bit Split shows a confirmation");
        };
        answer(QMessageBox::Cancel);wide.unchanged();
        require(p.isVisible(),"declining 8-bit channel extraction keeps the filter open");
        answer(QMessageBox::Ok);
        wait([guard=QPointer<QDialog>(&p)]{return !guard||!guard->isVisible();},"16-bit Split did not finish");
    });
    require(wide.project.document->layers.size()==5&&wide.project.history.undoCount()==1&&
            wide.project.document->layers[0].deep==deep&&!wide.project.document->layers[1].deep,
            "16-bit Split keeps source precision and adds 8-bit channel layers");
    QTemporaryDir temp;require(temp.isValid(),"temporary directory for 16-bit Split round trip");
    const auto root=std::filesystem::path(temp.path().toStdWString());
    ProjectStore store(makeWicProjectCodec());
    store.save(root/"split16.comp",*wide.project.document,wide.project.active);
    const auto reopened=store.load(root/"split16.comp");
    require(reopened.document.layers.size()==5&&reopened.document.layers[0].deep&&
            *reopened.document.layers[0].deep==*deep,"reopened Split preserves all 16-bit source samples");
    for(std::size_t i=1;i<5;++i)
        require(!reopened.document.layers[i].visible&&!reopened.document.layers[i].deep,
                "reopened Split channels remain hidden and 8-bit");
    exportDocumentAtomic(wide.before,root/"before16.png");
    exportDocumentAtomic(reopened.document,root/"after16.png");
    const auto beforePng=imaging::WicCodec::decodeProjectDeepPng(root/"before16.png");
    const auto afterPng=imaging::WicCodec::decodeProjectDeepPng(root/"after16.png");
    require(beforePng->samples==afterPng->samples&&beforePng->at(16,12)[0]==10001&&
            beforePng->at(17,12)[0]==10002&&beforePng->at(16,12)[1]==20003&&
            beforePng->at(16,12)[2]==30007&&beforePng->at(16,12)[3]==65535,
            "16-bit PNG export preserves sub-byte samples after Split and reopen");

    Document photo;photo.id=newId();photo.width=photo.height=5000;
    Layer sourceLayer;sourceLayer.id=newId();sourceLayer.name="Source";
    sourceLayer.transform={0,0,5000,5000};sourceLayer.raster=Raster::filled(5000,5000,{128,64,191,255});
    photo.layers.push_back(sourceLayer);
    auto planes=filters::splitChannels(*sourceLayer.raster,{});
    for(std::size_t i=0;i<planes.size();++i){
        Layer channel;channel.id=newId();channel.name="Channel";channel.visible=false;
        channel.transform=sourceLayer.transform;channel.raster=planes[i];photo.layers.push_back(std::move(channel));
    }
    validateDocument(photo);

    auto crowded=fixture();
    auto source=makeDeep(64,48,DeepImage::Format::Unorm16);
    crowded.layers[0].deep=source;crowded.layers[0].raster=rasterFromDeep(*source);
    const auto large=Raster::filled(5000,5000,{}),almost=Raster::filled(5000,4999,{});
    for(int i=0;i<20;++i){Layer layer;layer.id=newId();layer.name="Hidden";layer.visible=false;
        layer.transform={0,0,1,1};layer.raster=i==19?almost:large;crowded.layers.insert(crowded.layers.begin(),std::move(layer));}
    Fixture full(std::move(crowded));
    panel(full.window,"filter.rgb_split","RGB Split",[&](QDialog& p){
        bool confirmed=false,warned=false;
        QTimer prompts;QObject::connect(&prompts,&QTimer::timeout,&full.window,[&]{
            if(auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())){
                if(box->windowTitle()=="Create 8-Bit Channels"){confirmed=true;box->button(QMessageBox::Ok)->click();}
                else if(box->windowTitle()=="Cannot Split to Layers"){
                    warned=box->text().contains("500-megapixel");box->button(QMessageBox::Ok)->click();
                }
            }
        });
        prompts.start(5);control<QPushButton>(p,"rgbSplitToLayers")->click();
        wait([&]{return warned||!p.isVisible();},"Split budget warning did not appear after 16-bit confirmation");
        prompts.stop();
        require(confirmed&&warned&&p.isVisible(),"Split explains when the project pixel limit prevents channel creation");
        full.unchanged();
    });
}

void lens_adjustment_lifecycle(){
    Fixture f;const auto source=f.project.document->layers[0].raster;
    panel(f.window,"adjust.new.lens_effects","Lens Effects",[&](QDialog& p){
        ready(p);require(f.project.history.undoCount()==1,"lens layer creation history");
        auto* canvas=f.project.canvas;
        auto* worker=dynamic_cast<ui::ViewportCompositeJob*>(canvas->findChild<QObject*>("viewportCompositeJob"));
        require(worker&&canvas->viewportBounds().valid&&canvas->deferViewportComposite(),"lens canvas uses a background viewport render");
        const auto initial=worker->epoch();
        p.move(p.pos()+QPoint(40,30));
        require(canvas->deferViewportComposite()&&worker->epoch()==initial,"moving the lens window must reuse the viewport");
        control<QComboBox>(p,"lensEffectMode")->setCurrentIndex(3);
        field(p,"Copies")->setValue(2);field(p,"Copies")->setValue(4);
        ready(p);
        require(canvas->deferViewportComposite()&&worker->epoch()>initial,"new lens settings must request a fresh viewport");
        p.reject();
    });
    require(f.project.document->layers.size()==2&&!f.project.effectPreview&&!f.project.canvas->deferViewportComposite&&f.project.history.undoCount()==1,"lens Cancel retains default layer and detaches the viewport worker");
    const auto created=f.project.document;
    panel(f.window,"adjust.edit","Lens Effects",[&](QDialog& p){require(control<QComboBox>(p,"lensEffectMode")->currentIndex()==0,"lens Cancel restored mode");control<QComboBox>(p,"lensEffectMode")->setCurrentIndex(3);field(p,"Copies")->setValue(3);ready(p);require(bool(f.project.effectPreview),"lens preview");apply(p)->click();wait([guard=QPointer<QDialog>(&p)]{return !guard||!guard->isVisible();},"lens Apply");});
    const auto applied=f.project.document;require(f.project.history.undoCount()==2&&f.project.document->layers[0].raster==source&&!f.project.effectPreview,"lens source/history/overlay");
    trigger(f.window,"edit.undo");require(f.project.document==created,"lens undo");trigger(f.window,"edit.redo");require(f.project.document==applied,"lens redo");
    panel(f.window,"adjust.edit","Lens Effects",[&](QDialog& p){require(control<QComboBox>(p,"lensEffectMode")->currentIndex()==3&&field(p,"Copies")->value()==3,"lens controls restored");control<QComboBox>(p,"lensEffectMode")->setCurrentIndex(4);p.reject();});
    require(f.project.document==applied&&!f.project.effectPreview,"lens edit Cancel");
}
void lens_native_canvas(){
    auto document=fixture({16,16,16,255});
    document.width=1366;document.height=2048;
    auto& layer=document.layers.front();
    layer.transform={0,0,1366,2048};
    layer.raster=Raster::filled(1366,2048,{16,16,16,255});
    const std::vector<Pixel> light(96*96,{255,255,255,255});
    layer.raster=layer.raster->replacing(539,976,96,96,light.data(),96);
    Fixture f(std::move(document));
    auto& canvas=*f.project.canvas;
    canvas.zoomAt(1.5,canvas.contentRect().center());
    const auto readBlue=[&]{
        const auto point=canvas.viewMapping().toView({683,1024});
        const auto frame=canvas.captureRendered();
        const double dpr=canvas.devicePixelRatioF();
        return frame.pixelColor(int(point.x*dpr),int(point.y*dpr)).blue();
    };
    const int original=readBlue();
    panel(f.window,"adjust.new.lens_effects","Lens Effects",[&](QDialog& p){
        ready(p);
        wait([&]{return readBlue()>original+4;},"Lens Effects never reached the Windows canvas at 150% zoom");
        field(p,"Strength")->setValue(0);
        ready(p);
        wait([&]{return readBlue()==original;},"zero flare strength did not restore the original canvas pixels");
        field(p,"Strength")->setValue(75);
        ready(p);
        wait([&]{return readBlue()>original+4;},"changing flare strength did not update the Windows canvas");
        apply(p)->click();
        wait([guard=QPointer<QDialog>(&p)]{return !guard||!guard->isVisible();},"native Lens Apply did not close");
    });
    require(readBlue()>original+4,"Apply lost the flare on the Windows canvas");
    trigger(f.window,"edit.undo");
    trigger(f.window,"edit.undo");
    require(readBlue()==original,"Undo did not restore the original Windows canvas");
}
void tonal_lifecycle(){
    Fixture f;const auto source=f.project.document->layers[0].raster;
    panel(f.window,"adjust.new.tonal_contrast","Tonal Contrast",[&](QDialog& p){ready(p);require(f.project.history.undoCount()==1,"creation history");field(p,"Amount")->setValue(81);ready(p);p.reject();});
    require(f.project.document->layers.size()==2&&!f.project.effectPreview&&f.project.history.undoCount()==1,"cancel creation settings");const auto created=f.project.document;
    panel(f.window,"adjust.edit","Tonal Contrast",[&](QDialog& p){require(field(p,"Amount")->value()==50,"cancel restored defaults");field(p,"Amount")->setValue(80);field(p,"Radius")->setValue(5);field(p,"Shadows")->setValue(-50);field(p,"Midtones")->setValue(10);field(p,"Highlights")->setValue(90);ready(p);require(bool(f.project.effectPreview),"preview available");apply(p)->click();wait([guard=QPointer<QDialog>(&p)]{return !guard||!guard->isVisible();},"tonal Apply");});
    const auto applied=f.project.document;require(f.project.history.undoCount()==2&&f.project.document->layers[0].raster==source&&!f.project.effectPreview,"apply history and overlay");trigger(f.window,"edit.undo");require(f.project.document==created,"undo restores settings");trigger(f.window,"edit.redo");require(f.project.document==applied,"redo restores settings");
    panel(f.window,"adjust.edit","Tonal Contrast",[&](QDialog& p){require(field(p,"Shadows")->value()==-50&&field(p,"Radius")->value()==5,"saved controls");field(p,"Amount")->setValue(10);p.reject();});require(f.project.document==applied&&!f.project.effectPreview,"cancel restores document");
}

void vignette_lifecycle(){
    auto swatch=[](QDialog& panel,const QColor& expected){auto* button=control<QToolButton>(panel,"vignetteColor");auto image=button->grab().toImage();if(button->size()!=QSize(36,22))throw std::runtime_error("Vignette swatch size: "+std::to_string(button->width())+"x"+std::to_string(button->height()));require(button->text().isEmpty()&&image.pixelColor(image.rect().center())==expected,"Vignette displays the selected color as a filled rectangle");};
    Fixture f;const auto source=f.project.document->layers[0].raster;
    panel(f.window,"adjust.new.vignette","Vignette",[&](QDialog& p){ready(p);swatch(p,QColor(Qt::black));require(f.project.history.undoCount()==1,"creation is one undo step");field(p,"Amount")->setValue(81);ready(p);p.reject();});
    require(f.project.document->layers.size()==2&&!f.project.effectPreview&&f.project.history.undoCount()==1,"Cancel retains created layer only");
    const auto created=f.project.document;const auto id=f.project.active;
    panel(f.window,"adjust.edit","Vignette",[&](QDialog& p){require(field(p,"Amount")->value()==35,"Cancel restored defaults");field(p,"Amount")->setValue(81);field(p,"Midpoint")->setValue(18);field(p,"Roundness")->setValue(-42);field(p,"Feather")->setValue(37);field(p,"Highlights")->setValue(67);ready(p);control<QToolButton>(p,"vignetteColor")->click();PaletteDialog* picker=nullptr;for(auto* object:f.window.findChildren<QObject*>())if(auto* candidate=dynamic_cast<PaletteDialog*>(object);candidate&&candidate->isVisible())picker=candidate;require(picker,"Vignette uses shared palette");picker->sample({.2,.6,.8});swatch(p,QColor("#3399cc"));picker->accept();ready(p);require(bool(f.project.effectPreview),"vignette live preview");preview(p)->setChecked(false);ready(p);require(!f.project.effectPreview,"preview off clears overlay");preview(p)->setChecked(true);ready(p);apply(p)->click();wait([guard=QPointer<QDialog>(&p)]{return !guard||!guard->isVisible();},"vignette Apply");});
    const auto applied=f.project.document;require(f.project.history.undoCount()==2&&f.project.active==id&&f.project.document->layers[0].raster==source&&!f.project.effectPreview,"Apply preserves source identity history and clears overlay");
    trigger(f.window,"edit.undo");require(f.project.document==created,"first Undo restores creation settings");trigger(f.window,"edit.undo");require(f.project.document==f.before,"second Undo removes created layer");trigger(f.window,"edit.redo");trigger(f.window,"edit.redo");require(f.project.document==applied,"Redo restores applied settings");
    panel(f.window,"adjust.edit","Vignette",[&](QDialog& p){require(field(p,"Roundness")->value()==-42&&field(p,"Highlights")->value()==67,"controls restore existing values");swatch(p,QColor("#3399cc"));field(p,"Amount")->setValue(99);p.reject();});
    require(f.project.document==applied&&!f.project.effectPreview&&f.project.history.undoCount()==2,"Cancel restores applied document and history");
}

void live_filter_adjustments(){
    Fixture f;
    panel(f.window,"adjust.new.progressive","Progressive Blur",[&](QDialog& p){field(p,"To")->setValue(4);ready(p);require(bool(f.project.effectPreview),"live Progressive Blur preview");apply(p)->click();wait([guard=QPointer<QDialog>(&p)]{return !guard||!guard->isVisible();},"live Progressive Blur Apply");});
    require(f.project.document->layers.size()==2&&f.project.document->layers[1].adjustmentJson.find("Progressive Blur")!=std::string::npos,"Progressive Blur adjustment layer committed");
    panel(f.window,"adjust.new.bloom_glow","Bloom / Glow",[&](QDialog& p){field(p,"Amount")->setValue(55);ready(p);apply(p)->click();wait([guard=QPointer<QDialog>(&p)]{return !guard||!guard->isVisible();},"live Bloom / Glow Apply");});
    panel(f.window,"adjust.new.rgb_split","RGB Split",[&](QDialog& p){field(p,"Distance")->setValue(7);ready(p);apply(p)->click();wait([guard=QPointer<QDialog>(&p)]{return !guard||!guard->isVisible();},"live RGB Split Apply");});
    require(f.project.document->layers.size()==4,"three filter adjustment layers exist");
    const auto withLayers=f.project.document;
    trigger(f.window,"edit.undo");require(f.project.document!=withLayers,"RGB Split adjustment edit is undoable");
}
void filter_preview_checkbox(){Fixture f;panel(f.window,"filter.gaussian","Gaussian Blur",[&](QDialog& p){field(p,"Radius")->setValue(3);ready(p);preview(p)->setChecked(false);QApplication::processEvents();require(f.visible(16,12)==Pixel{80,100,120,255},"filter Preview off restores original canvas");f.unchanged();});f.unchanged();}
void filter_mask_growth(){auto d=fixture();auto mask=std::make_shared<GrayRaster>();mask->width=32;mask->height=24;mask->pixels.assign(32*24,255);for(int y=0;y<24;++y)for(int x=0;x<16;++x)mask->pixels[size_t(y)*32+x]=0;d.layers[0].mask=Mask{mask,false,true,{}};Fixture f(d);panel(f.window,"filter.gaussian","Gaussian Blur",[&](QDialog& p){field(p,"Radius")->setValue(3);ready(p);apply(p)->click();wait([guard=QPointer<QDialog>(&p)]{return !guard||!guard->isVisible();},"masked filter Apply did not finish");});const auto& layer=f.project.document->layers[0];require(layer.raster->width!=32,"blur fixture grows source grid");require(layer.mask&&layer.mask->raster->width==layer.raster->width&&layer.mask->raster->height==layer.raster->height,"grown filter carries attached mask to output grid");require(!layer.mask->enabled&&layer.mask->linked&&!layer.mask->placement,"mask growth preserves enabled/link/placement metadata");trigger(f.window,"edit.undo");f.unchanged();}
void pending_edit_guards(){Fixture f;panel(f.window,"adjust.exposure","Exposure",[&](QDialog& p){require(!QApplication::activeModalWidget(),"pending guard checked for real nonmodal session");require(!action(f.window,"adjust.levels")->isEnabled()&&!action(f.window,"layer.new")->isEnabled(),"pending edit blocks second adjustment and layer mutation");auto old=f.project.canvas->zoom;trigger(f.window,"view.actual");require(f.project.canvas->zoom==1&&old>0,"View Actual Pixels remains available");require(p.isVisible(),"navigation keeps panel open");f.unchanged();});f.unchanged();}
void existing_live_preview_cancel(){auto d=fixture();Layer adjustment;adjustment.id=newId();adjustment.name="Exposure";adjustment.transform={0,0,64,48};adjustment.adjustmentJson=effects::defaultAdjustmentJson("Exposure");d.layers.push_back(adjustment);Fixture f(d);const auto expected=exposurePixel(f.before,1);panel(f.window,"adjust.edit","Exposure",[&](QDialog& p){field(p,"Exposure")->setValue(1);ready(p);require(f.visible()==expected,"existing live adjustment previews in canvas");f.unchanged();});f.unchanged();}
void latest_preview_wins(){Fixture f;const auto expected=exposurePixel(f.before,.5);panel(f.window,"adjust.exposure","Exposure",[&](QDialog& p){auto* exposure=field(p,"Exposure");exposure->setValue(1);exposure->setValue(2);exposure->setValue(.5);ready(p);require(f.visible()==expected,"newest pending settings win canvas preview");f.unchanged();});f.unchanged();}

void adjustment_reduced_preview_full_apply(){auto d=fixture();d.width=4096;d.height=4;d.layers[0].transform={0,0,4096,4};d.layers[0].raster=Raster::filled(4096,4,{40,60,80,255});Fixture f(d);panel(f.window,"adjust.exposure","Exposure",[&](QDialog& p){field(p,"Exposure")->setValue(1);ready(p);require(f.project.effectPreview&&f.project.effectPreview->layers[0].raster->width==2048,"source FilterEdit preview limit2048");f.unchanged();apply(p)->click();wait([guard=QPointer<QDialog>(&p)]{return !guard||!guard->isVisible();},"source-size Apply completed");});require(f.project.document->layers[0].raster->width==4096&&f.project.history.undoCount()==1,"full Apply preserves original grid");trigger(f.window,"edit.undo");f.unchanged();}
void levels_reduced_preview_full_apply(){auto d=fixture();d.width=9001;d.height=3;d.layers[0].transform={0,0,9001,3};d.layers[0].raster=Raster::filled(9001,3,{80,80,80,255});Fixture f(d);panel(f.window,"adjust.levels","Levels",[&](QDialog& p){control<QDoubleSpinBox>(p,"levelsValue0")->setValue(40);ready(p);require(f.project.effectPreview&&f.project.effectPreview->layers[0].raster->width==8000,"source Levels preview limit8000");f.unchanged();apply(p)->click();wait([guard=QPointer<QDialog>(&p)]{return !guard||!guard->isVisible();},"source-size Levels Apply completed");});require(f.project.document->layers[0].raster->width==9001&&f.project.history.undoCount()==1,"Levels Apply preserves original grid");trigger(f.window,"edit.undo");f.unchanged();}
void worker_cancel_no_late_result(){auto d=fixture();d.width=2000;d.height=2000;d.layers[0].transform={0,0,2000,2000};d.layers[0].raster=Raster::filled(2000,2000,{40,60,80,255});Fixture f(d);panel(f.window,"adjust.exposure","Exposure",[&](QDialog& p){field(p,"Exposure")->setValue(2);p.reject();f.unchanged();require(!f.project.effectPreview,"Cancel clears canvas draft immediately");});wait([&]{for(auto* object:f.window.findChildren<QObject*>())if(dynamic_cast<ui::EditPanelSession*>(object))return false;return true;},"cancelled worker retired without publishing");f.unchanged();require(!f.project.effectPreview,"finished cancelled worker cannot resurrect draft");}
void palette_visible_preview(){Fixture f;const auto expected=exposurePixel(f.before,1);panel(f.window,"adjust.exposure","Exposure",[&](QDialog& p){field(p,"Exposure")->setValue(1);ready(p);trigger(f.window,"palette.foreground");PaletteDialog* picker=nullptr;for(auto* object:f.window.findChildren<QObject*>())if(auto* candidate=dynamic_cast<PaletteDialog*>(object);candidate&&candidate->isVisible())picker=candidate;require(picker,"source palette opens over active adjustment");f.click({30,20});auto actual=picker->color().quantized();require(actual.red==expected.r/255.&&actual.green==expected.g/255.&&actual.blue==expected.b/255.,"palette samples displayed adjustment rather than canonical pixels");picker->reject();f.unchanged();});f.unchanged();}
void filter_mask_placed_unchanged(){auto d=fixture();auto mask=std::make_shared<GrayRaster>();mask->width=2;mask->height=2;mask->pixels={0,64,128,255};d.layers[0].mask=Mask{mask,false,false,Transform{20,15,9,7}};const auto original=*d.layers[0].mask;Fixture f(d);panel(f.window,"filter.gaussian","Gaussian Blur",[&](QDialog& p){field(p,"Radius")->setValue(3);ready(p);apply(p)->click();wait([guard=QPointer<QDialog>(&p)]{return !guard||!guard->isVisible();},"placed-mask Apply completed");});require(f.project.document->layers[0].mask==original,"explicit mask placement and pixels preserved across filter growth");trigger(f.window,"edit.undo");f.unchanged();}
void filter_solid_mask_unchanged(){auto d=fixture();auto mask=std::make_shared<GrayRaster>();mask->width=mask->height=1;mask->pixels={255};d.layers[0].mask=Mask{mask,true,false,{}};const auto original=*d.layers[0].mask;Fixture f(d);panel(f.window,"filter.gaussian","Gaussian Blur",[&](QDialog& p){field(p,"Radius")->setValue(3);ready(p);apply(p)->click();wait([guard=QPointer<QDialog>(&p)]{return !guard||!guard->isVisible();},"solid-mask Apply completed");});require(f.project.document->layers[0].mask==original,"source uniform1x1 mask stays constant without growth allocation");trigger(f.window,"edit.undo");f.unchanged();}
void filter_mask_pretrim_order(){auto d=fixture();auto mask=std::make_shared<GrayRaster>();mask->width=32;mask->height=24;mask->pixels.assign(32*24,255);for(int y=0;y<24;++y)for(int x=0;x<16;++x)mask->pixels[size_t(y)*32+x]=0;d.layers[0].mask=Mask{mask,false,false,{}};Fixture f(d);panel(f.window,"filter.gaussian","Gaussian Blur",[&](QDialog& p){field(p,"Radius")->setValue(3);ready(p);require(f.project.effectPreview&&f.project.effectPreview->layers[0].mask->placement==d.layers[0].transform,"preview pins original attached mask placement");apply(p)->click();wait([guard=QPointer<QDialog>(&p)]{return !guard||!guard->isVisible();},"mask-order Apply completed");});const auto& out=f.project.document->layers[0];require(out.mask&&out.mask->raster&&out.raster->width<54&&out.raster->height<46,"blur was padded11 then trimmed");const auto& actual=*out.mask->raster;for(int y=0;y<actual.height;++y)for(int x=0;x<actual.width;++x){const double sx=-11+(x+.5)*54/actual.width,sy=-11+(y+.5)*46/actual.height;double expected=255;if(sx>=0&&sy>=0&&sx<32&&sy<24){const double fx=sx-.5;const int ix=int(std::floor(fx));const auto pixel=[](int xx){return std::clamp(xx,0,31)<16?0.:255.;};expected=pixel(ix)*(1-(fx-ix))+pixel(ix+1)*(fx-ix);}require(actual.pixel(x,y)==uint8_t(std::lround(expected)),"mask samples pretrim padded placement using final grid size");}require(!out.mask->enabled&&!out.mask->linked&&!out.mask->placement,"mask metadata survives exact carry");trigger(f.window,"edit.undo");f.unchanged();}
void filter_mask_canvas_trim(){
    Document d;d.id="canvas-mask";d.width=64;d.height=48;
    Layer layer;layer.id="pixels";layer.name="Pixels";layer.transform={0,0,64,48};
    layer.raster=Raster::filled(64,48,{});
    {auto pixels=layer.raster->rgba();for(int y=8;y<40;++y)for(int x=16;x<48;++x){auto i=size_t(y)*64+x;pixels[i*4]=80;pixels[i*4+1]=100;pixels[i*4+2]=120;pixels[i*4+3]=255;}layer.raster=Raster::fromRgba(64,48,pixels.data(),size_t(64)*4);}
    auto mask=std::make_shared<GrayRaster>();mask->width=64;mask->height=48;mask->pixels.resize(64*48);
    for(int y=0;y<48;++y)for(int x=0;x<64;++x)mask->pixels[size_t(y)*64+x]=uint8_t(x<32?0:255);
    layer.mask=Mask{mask,true,true,{}};d.layers={layer};
    const auto originalMask=*mask;const auto originalTransform=layer.transform;
    Fixture f(d);
    panel(f.window,"filter.gaussian","Gaussian Blur",[&](QDialog& p){field(p,"Radius")->setValue(3);ready(p);apply(p)->click();wait([guard=QPointer<QDialog>(&p)]{return !guard||!guard->isVisible();},"canvas-sized masked filter Apply did not finish");});
    const auto& out=f.project.document->layers[0];
    require(out.raster&&(out.raster->width!=64||out.raster->height!=48||out.transform!=originalTransform),"canvas-sized blur trimmed transparent margins");
    require(out.mask&&out.mask->raster&&out.mask->raster->width==out.raster->width&&out.mask->raster->height==out.raster->height,"trimmed canvas layer carries attached mask to output grid");
    for(int y=0;y<out.mask->raster->height;++y)for(int x=0;x<out.mask->raster->width;++x){
        const auto point=out.transform.fromUnit({(x+.5)/out.raster->width,(y+.5)/out.raster->height});
        const auto expected=graphics::sampleMask(originalMask,originalTransform.toUnit(point),Transform::Sampling::High,0);
        require(out.mask->raster->pixel(x,y)==uint8_t(std::clamp(std::lround(expected*255),0L,255L)),"trimmed mask stays aligned in document space");
    }
    require(out.mask->enabled&&out.mask->linked&&!out.mask->placement,"canvas trim preserves mask metadata");
    const auto applied=f.project.document;trigger(f.window,"edit.undo");f.unchanged();trigger(f.window,"edit.redo");require(f.project.document==applied,"canvas trim mask redo restores output");
}
void property_step_and_slider(){Fixture f;panel(f.window,"adjust.exposure","Exposure",[&](QDialog& p){auto* exposure=field(p,"Exposure");exposure->setFocus();QTest::keyClick(exposure,Qt::Key_Up,Qt::ShiftModifier);require(exposure->value()==.1,"source Exposure precision2 uses Shift ten0.01 steps");bool found=false;for(auto* slider:p.findChildren<QSlider*>())if(slider->accessibleName()=="Exposure slider"){found=true;slider->setValue(5250);}require(found&&exposure->value()==1,"Exposure slider and exact field share state");f.unchanged();});f.unchanged();}
void hdr_conversion_cancel_undo(){
    auto d=fixture();auto deep=makeDeep(32,24);for(int y=0;y<24;++y)for(int x=0;x<32;++x)deep->setPixel(x,y,{2,1,.5f,1});d.layers[0].deep=deep;d.layers[0].raster=rasterFromDeep(*deep);Fixture f(d);
    auto confirm=[&](QMessageBox::StandardButton choice){bool answered=false;QTimer::singleShot(0,&f.window,[&]{if(auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())){answered=true;box->button(choice)->click();}});trigger(f.window,"layer.convert_hdr");require(answered,"HDR conversion confirmation missing");};
    confirm(QMessageBox::Cancel);f.unchanged();require(!ui::LastFilterJob::find(&f.window),"Declined conversion started worker");
    confirm(QMessageBox::Ok);auto* job=ui::LastFilterJob::find(&f.window);require(job,"HDR conversion worker missing");job->cancel();wait([&]{return !ui::LastFilterJob::find(&f.window);},"Cancelled conversion did not retire");f.unchanged();require(!f.project.projectBusy,"Cancelled conversion left project busy");
    confirm(QMessageBox::Ok);wait([&]{return !ui::LastFilterJob::find(&f.window);},"HDR conversion did not finish");require(f.project.history.undoCount()==1&&!f.project.document->layers[0].deep->isHdr()&&f.project.document->layers[0].deep->pixel(0,0)[0]==1,"Conversion did not clip in one undo step");trigger(f.window,"edit.undo");f.unchanged();trigger(f.window,"edit.redo");require(!f.project.document->layers[0].deep->isHdr(),"Conversion redo lost 16-bit result");
}
void hdr_adjustment_apply_cancel_undo(){
    auto d=fixture();auto deep=makeDeep(32,24);for(int y=0;y<24;++y)for(int x=0;x<32;++x)deep->setPixel(x,y,{float(linearToSrgb(4)),1,.5f,1});d.layers[0].deep=deep;d.layers[0].raster=rasterFromDeep(*deep);Fixture f(d);
    panel(f.window,"adjust.exposure","Exposure",[&](QDialog& p){field(p,"Exposure")->setValue(-2);ready(p);f.unchanged();require(f.project.effectPreview&&f.project.effectPreview->layers[0].deep&&f.project.effectPreview->layers[0].deep->isHdr(),"HDR exposure preview lost retained pixels");require(std::abs(srgbToLinear(f.project.effectPreview->layers[0].deep->pixel(0,0)[0])-1)<.006,"HDR exposure preview used clipped pixels");require(f.visible().r>=254,"HDR exposure preview failed");});f.unchanged();require(!f.project.effectPreview,"HDR Cancel left a preview installed");
    panel(f.window,"adjust.exposure","Exposure",[&](QDialog& p){field(p,"Exposure")->setValue(-1);ready(p);apply(p)->click();wait([guard=QPointer<QDialog>(&p)]{return !guard||!guard->isVisible();},"HDR Apply did not finish");});
    require(f.project.history.undoCount()==1,"HDR Apply did not create exactly one history entry");require(f.project.document->layers[0].deep&&f.project.document->layers[0].deep->isHdr(),"HDR Apply lost retained pixels");require(!f.project.effectPreview,"HDR Apply left a preview installed");require(std::abs(srgbToLinear(f.project.document->layers[0].deep->pixel(0,0)[0])-2)<.006,"HDR Apply used clipped source");const auto applied=f.project.document;trigger(f.window,"edit.undo");f.unchanged();trigger(f.window,"edit.redo");require(f.project.document==applied&&f.project.history.undoCount()==1,"HDR redo did not restore the exact applied result");
}

void hdr_filter_apply_cancel_undo(){
    auto d=fixture();auto deep=makeDeep(32,24);for(int y=0;y<24;++y)for(int x=0;x<32;++x)deep->setPixel(x,y,{2,1,.5f,1});d.layers[0].deep=deep;d.layers[0].raster=rasterFromDeep(*deep);Fixture f(d);
    panel(f.window,"filter.gaussian","Gaussian Blur",[&](QDialog& p){field(p,"Radius")->setValue(3);ready(p);f.unchanged();require(f.project.effectPreview&&f.project.effectPreview->layers[0].deep&&f.project.effectPreview->layers[0].deep->isHdr(),"HDR filter preview lost retained pixels");});f.unchanged();require(!f.project.effectPreview,"HDR filter Cancel left a preview installed");
    panel(f.window,"filter.gaussian","Gaussian Blur",[&](QDialog& p){field(p,"Radius")->setValue(3);ready(p);apply(p)->click();wait([guard=QPointer<QDialog>(&p)]{return !guard||!guard->isVisible();},"HDR filter Apply did not finish");});require(f.project.history.undoCount()==1&&f.project.document->layers[0].deep&&f.project.document->layers[0].deep->isHdr(),"HDR filter Apply lost pixels/history");require(!f.project.effectPreview,"HDR filter Apply left a preview installed");const auto applied=f.project.document;trigger(f.window,"edit.undo");f.unchanged();trigger(f.window,"edit.redo");require(f.project.document==applied,"HDR filter redo did not restore the exact result");
}
void deep_adjustment_apply_cancel_undo(){
    auto d=fixture();auto deep=makeDeep(32,24,DeepImage::Format::Unorm16);
    for(int y=0;y<24;++y)for(int x=0;x<32;++x){auto p=deep->at(x,y);p[0]=uint16_t(10001+x);p[1]=20003;p[2]=30007;p[3]=65535;}
    d.layers[0].deep=deep;d.layers[0].raster=rasterFromDeep(*deep);Fixture f(d);
    panel(f.window,"adjust.exposure","Exposure",[&](QDialog& p){field(p,"Exposure")->setValue(1);ready(p);f.unchanged();require(f.project.effectPreview&&f.project.effectPreview->layers[0].deep&&!f.project.effectPreview->layers[0].deep->isHdr(),"16-bit preview lost precision");});
    f.unchanged();require(!f.project.effectPreview,"Cancel left a preview installed");
    panel(f.window,"adjust.exposure","Exposure",[&](QDialog& p){field(p,"Exposure")->setValue(1);ready(p);apply(p)->click();wait([guard=QPointer<QDialog>(&p)]{return !guard||!guard->isVisible();},"Apply did not finish");});
    const auto applied=f.project.document;require(applied->layers[0].deep&&applied->layers[0].deep->at(0,0)[0]!=applied->layers[0].deep->at(1,0)[0],"16-bit adjustment lost sub-byte detail");
    require(f.project.history.undoCount()==1,"Apply did not create one history entry");trigger(f.window,"edit.undo");f.unchanged();trigger(f.window,"edit.redo");require(f.project.document==applied,"Redo did not restore adjusted pixels");
}
void hdr_stale_pixels_discard_apply(){
    auto d=fixture();auto deep=makeDeep(32,24);for(int y=0;y<24;++y)for(int x=0;x<32;++x)deep->setPixel(x,y,{2,1,.5f,1});d.layers[0].deep=deep;d.layers[0].raster=rasterFromDeep(*deep);Fixture f(d);std::optional<Document> changed;
    panel(f.window,"adjust.exposure","Exposure",[&](QDialog& p){field(p,"Exposure")->setValue(-1);ready(p);auto replacement=std::make_shared<DeepImage>(*deep);replacement->setPixel(0,0,{3,1,.5f,1});f.project.document->layers[0].deep=replacement;changed=f.project.document;require(f.project.document->layers[0].raster==f.before.layers[0].raster,"Stale HDR fixture changed the SDR raster");apply(p)->click();wait([guard=QPointer<QDialog>(&p)]{return !guard||!guard->isVisible();},"Stale HDR Apply did not finish");});require(f.project.document==changed&&f.project.history.undoCount()==0,"Stale HDR result replaced newer retained pixels");require(!f.project.effectPreview,"Discarded HDR result left a preview installed");
}

}
int main(int argc,char** argv){QApplication app(argc,argv);const std::map<std::string,void(*)()> cases{{"lens_native_canvas",lens_native_canvas},{"lens_adjustment_lifecycle",lens_adjustment_lifecycle},{"tonal_lifecycle",tonal_lifecycle},{"vignette_lifecycle",vignette_lifecycle},{"lens_effects_lifecycle",lens_effects_lifecycle},{"deep_adjustment_apply_cancel_undo",deep_adjustment_apply_cancel_undo},{"rgb_split_canvas_split_layers",rgb_split_canvas_split_layers},{"rgb_split_canvas_cancel_layers",rgb_split_canvas_cancel_layers},{"rgb_split_small_layer_apply_undo",rgb_split_small_layer_apply_undo},{"live_filter_adjustments",live_filter_adjustments},{"hdr_filter_apply_cancel_undo",hdr_filter_apply_cancel_undo},{"hdr_stale_pixels_discard_apply",hdr_stale_pixels_discard_apply},{"hdr_conversion_cancel_undo",hdr_conversion_cancel_undo},{"hdr_adjustment_apply_cancel_undo",hdr_adjustment_apply_cancel_undo},{"adjustment_reduced_preview_full_apply",adjustment_reduced_preview_full_apply},{"levels_reduced_preview_full_apply",levels_reduced_preview_full_apply},{"worker_cancel_no_late_result",worker_cancel_no_late_result},{"palette_visible_preview",palette_visible_preview},{"filter_mask_placed_unchanged",filter_mask_placed_unchanged},{"filter_solid_mask_unchanged",filter_solid_mask_unchanged},{"filter_mask_pretrim_order",filter_mask_pretrim_order},{"filter_mask_canvas_trim",filter_mask_canvas_trim},{"property_step_and_slider",property_step_and_slider},{"adjustment_nonmodal",adjustment_nonmodal},{"filter_nonmodal",filter_nonmodal},{"adjustment_canvas_preview",adjustment_canvas_preview},{"adjustment_preview_toggle",adjustment_preview_toggle},{"adjustment_apply_undo",adjustment_apply_undo},{"adjustment_close_cancel",adjustment_close_cancel},{"adjustment_position",adjustment_position},{"levels_canvas_original_sample",levels_canvas_original_sample},{"hue_canvas_sample",hue_canvas_sample},{"hue_canvas_target",hue_canvas_target},{"progressive_canvas_cancel_apply",progressive_canvas_cancel_apply},{"filter_canvas_preview",filter_canvas_preview},{"filter_apply_undo",filter_apply_undo},{"filter_preview_checkbox",filter_preview_checkbox},{"filter_mask_growth",filter_mask_growth},{"pending_edit_guards",pending_edit_guards},{"existing_live_preview_cancel",existing_live_preview_cancel},{"latest_preview_wins",latest_preview_wins}};try{require(argc==2&&cases.contains(argv[1]),"provide named nonmodal case");cases.at(argv[1])();std::printf("PASS %s\n",argv[1]);return 0;}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s: %s\n",argc>1?argv[1]:"argument",e.what());return 1;}}
