#include "MainWindow.h"
#include "persistence/ProjectStore.h"
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QTimer>
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QScreen>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QTest>
#include <icm.h>
#include <cmath>

namespace {
QColor monitorExpectedColor(compositor::platform::DisplayProfile& destination,QColor canonical=QColor(35,65,90)){
    // The canonical fixture remains sRGB. Use Windows' CPU color engine as an
    // independent expectation for the displayed opaque pixel, not the D2D path.
    auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    wchar_t system[MAX_PATH]{};require(GetSystemDirectoryW(system,MAX_PATH)!=0,"Cannot locate the sRGB profile");
    auto source=compositor::platform::loadDisplayProfile(std::filesystem::path(system)/L"spool/drivers/color/sRGB Color Space Profile.icm");
    require(source.kind==compositor::platform::DisplayProfileKind::Icc,"System sRGB profile is invalid");
    PROFILE s{PROFILE_MEMBUFFER,source.icc.data(),DWORD(source.icc.size())},d{PROFILE_MEMBUFFER,destination.icc.data(),DWORD(destination.icc.size())};
    struct OpenProfile{HPROFILE value{};~OpenProfile(){if(value)CloseColorProfile(value);}};
    OpenProfile from{OpenColorProfileW(&s,PROFILE_READ,FILE_SHARE_READ,OPEN_EXISTING)},to{OpenColorProfileW(&d,PROFILE_READ,FILE_SHARE_READ,OPEN_EXISTING)};
    require(from.value&&to.value,"WCS cannot open the profile pair");
    HPROFILE profiles[]{from.value,to.value};DWORD intents[]{INTENT_RELATIVE_COLORIMETRIC,INTENT_RELATIVE_COLORIMETRIC};
    struct OpenTransform{HTRANSFORM value{};~OpenTransform(){if(value)DeleteColorTransform(value);}};
    OpenTransform transform{CreateMultiProfileTransform(profiles,2,intents,2,BEST_MODE,INDEX_DONT_CARE)};
    require(transform.value,"WCS cannot create the CPU profile transform");COLOR input{},output{};input.rgb={WORD(canonical.red()*257),WORD(canonical.green()*257),WORD(canonical.blue()*257)};
    require(TranslateColors(transform.value,&input,1,COLOR_RGB,&output,COLOR_RGB),"WCS CPU color translation failed");
    return {int(std::lround(output.rgb.red/257.)),int(std::lround(output.rgb.green/257.)),int(std::lround(output.rgb.blue/257.))};
}
}

namespace compositor {
void MainWindow::exerciseNativeUi(const QString&dir){
    QDir().mkpath(dir);if(!canvas())addFeasibilityDocument();QTest::qWait(150);
    auto require=[](bool ok,const char*error){if(!ok)throw std::runtime_error(error);};
    QJsonArray checks;auto passed=[&](const char*name){checks.append(name);QFile progress(dir+"/native-ui-progress.json");require(progress.open(QIODevice::WriteOnly),"UI progress evidence output failed");progress.write(QJsonDocument(QJsonObject{{"status","running"},{"completed_checks",checks}}).toJson());};
    canvas()->repaint();QTest::qWait(30);
    if(!canvas()->deviceReady())throw std::runtime_error("Native graphics device unavailable: "+canvas()->deviceError().toStdString());
    auto original=active()->transform;auto middle=canvas()->rect().center();
    QTest::mousePress(canvas(),Qt::LeftButton,Qt::NoModifier,middle);QTest::mouseMove(canvas(),middle+QPoint(23,11));QTest::mouseRelease(canvas(),Qt::LeftButton,Qt::NoModifier,middle+QPoint(23,11));
    require(active()->transform!=original&&current()->history.canUndo(),"Native pointer transaction failed");undo_->trigger();require(active()->transform==original,"Native undo failed");redo_->trigger();passed("mouse move / undo / redo");
    auto viewPoint=[&](Point p){auto at=canvas()->viewMapping().toView(p);return QPointF(at.x,at.y).toPoint();};
    selectTool(Tool::Marquee);auto start=viewPoint({20,20}),end=viewPoint({120,100});
    QTest::mousePress(canvas(),Qt::LeftButton,Qt::NoModifier,start);QTest::mouseMove(canvas(),end);QTest::mouseRelease(canvas(),Qt::LeftButton,Qt::NoModifier,end);
    require(current()->document->selection&&current()->document->selection->coverage->pixel(60,50)==255&&current()->document->selection->coverage->pixel(200,150)==0,"Native marquee coverage failed");
    undo_->trigger();require(!current()->document->selection,"Selection undo failed");redo_->trigger();require(current()->document->selection.has_value(),"Selection redo failed");passed("native marquee and selection history");
    edit("Deselect",[](Document&d){d.selection.reset();});
    selectTool(Tool::Brush);brushSettings_.radius=16;brushSettings_.hardness=.2;brushSettings_.opacity=.5;
    auto source=active()->raster;auto historyCount=current()->history.undoCount();Raster::resetMaterializationCount();
    for(int stroke=0;stroke<2;++stroke){auto a=viewPoint({260.+stroke*30,180}),b=viewPoint({290.+stroke*30,205});QTest::mousePress(canvas(),Qt::LeftButton,Qt::NoModifier,a);for(int i=1;i<=8;++i)QTest::mouseMove(canvas(),a+(b-a)*i/8);QTest::mouseRelease(canvas(),Qt::LeftButton,Qt::NoModifier,b);QTest::qWait(20);}
    require(active()->raster!=source&&current()->history.undoCount()==historyCount+2,"Native brush commits failed");
    const auto flattenCount=Raster::materializationCount();require(flattenCount==0,"Brush/UI forced full raster materialization");passed("two native brush strokes, one history step each, zero rgba materializations");
    // A tab switch cancels the original tab's live edit before its project changes.
    selectTool(Tool::Move);auto*owner=current();auto beforeMove=*owner->document;pointerBegin(QPointF(200,160),Qt::NoModifier);pointerUpdate(QPointF(240,180),Qt::NoModifier);Document blank;blank.id=newId();blank.width=32;blank.height=32;addProject(blank);
    require(*owner->document==beforeMove,"Tab switch did not cancel original transaction");closeProject(tabs_->currentIndex());passed("tab switch cancels captured transaction");
    const auto beforeAdjustment=*current()->document;
    bool applied=false,timedOut=false;QElapsedTimer elapsed;elapsed.start();QTimer drive;drive.setInterval(40);
    connect(&drive,&QTimer::timeout,this,[&]{auto*d=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!d)return;if(elapsed.elapsed()>30000){timedOut=true;d->reject();return;}for(auto*spin:d->findChildren<QDoubleSpinBox*>())if(spin->accessibleName()=="Exposure"&&!applied){spin->setValue(1);applied=true;}for(auto*box:d->findChildren<QDialogButtonBox*>())if(auto*button=box->button(QDialogButtonBox::Apply);button&&button->isEnabled()&&applied)button->click();});
    drive.start();adjust("Exposure",false);drive.stop();require(!timedOut&&applied&&*current()->document!=beforeAdjustment,"Native adjustment Apply failed");undo_->trigger();require(*current()->document==beforeAdjustment,"Adjustment undo failed");passed("Exposure dialog Apply / undo");
    for(const QString kind:{"Hue/Saturation","Levels","Curves","Exposure","Gradient Map","Grain"}){auto before=*current()->document;QTimer::singleShot(180,this,[]{if(auto*d=qobject_cast<QDialog*>(QApplication::activeModalWidget()))d->reject();});adjust(kind,false);require(*current()->document==before,"Adjustment Cancel changed document");}passed("six adjustment dialogs Cancel preserve document");
    auto gesture=[&](Tool tool,Point a,Point b){selectTool(tool);QTest::qWait(30);auto start=viewPoint(a),end=viewPoint(b);QTest::mousePress(canvas(),Qt::LeftButton,Qt::NoModifier,start);QTest::mouseMove(canvas(),end);QTest::mouseRelease(canvas(),Qt::LeftButton,Qt::NoModifier,end);};
    {auto before=*current()->document;auto count=current()->history.undoCount();gesture(Tool::Shape,{40,40},{130,110});require(current()->document->layers.size()==before.layers.size()+1&&current()->history.undoCount()==count+1&&!active()->shapeJson.empty(),"Native shape transaction failed");undo_->trigger();require(*current()->document==before,"Shape undo failed");passed("shape mouse gesture / live metadata / one undo");}
    {auto before=*current()->document;const auto history=current()->history.undoCount();gesture(Tool::Gradient,{220,160},{330,240});require(*current()->document==before&&current()->gradientPreview&&current()->history.undoCount()==history,"Native pending gradient failed");findChild<QAction*>("applyGradient")->trigger();require(current()->history.undoCount()==history+1,"Native gradient Apply failed");undo_->trigger();require(*current()->document==before,"Gradient undo failed");passed("gradient mouse gesture / exact undo");}
    {auto before=*current()->document;current()->maskSelected=true;maskPaintWhite_=false;gesture(Tool::Brush,{280,190},{310,210});require(active()->mask!=before.layers.back().mask&&active()->raster==before.layers.back().raster,"Native mask brush failed");undo_->trigger();require(*current()->document==before,"Mask brush undo failed");current()->maskSelected=false;passed("mask brush preserves image / exact undo");}
    {auto before=*current()->document;gesture(Tool::Crop,{30,25},{200,180});require(cropDraft_.has_value()&&*current()->document==before,"Crop preview mutated document");applyCrop();require(current()->document->width<before.width&&current()->document->height<before.height,"Native crop Apply failed");undo_->trigger();require(*current()->document==before,"Crop undo failed");canvas()->fit();passed("crop preview / Apply / exact undo");}
    {auto before=*current()->document;selectTool(Tool::Wand);wandAllLayers_=true;wandTolerance_=0;runWand({20,20},Qt::NoModifier);require(current()->document->selection&&current()->document->selection->coverage->pixel(20,20)>0,"Native wand result failed");undo_->trigger();require(*current()->document==before,"Wand undo failed");passed("asynchronous wand / exact undo");}
    {auto paletteBefore=foreground_;openPalette();require(colorPicker_,"Floating picker failed to open");pointerBegin({100,350},Qt::NoModifier);pointerEnd({100,350},Qt::NoModifier);auto sample=colorPicker_->color();require(sample==effects_tools::PaletteColor{35/255.,65/255.,90/255.}&&foreground_==paletteBefore,"Picker sampling changed palette before Apply");colorPicker_->reject();require(foreground_==paletteBefore,"Palette Cancel changed foreground");openPalette();pointerBegin({100,350},Qt::NoModifier);pointerEnd({100,350},Qt::NoModifier);colorPicker_->accept();require(foreground_.red()==35&&foreground_.green()==65&&foreground_.blue()==90,"Palette Apply failed");foreground_=paletteBefore;passed("floating palette original-composite sampling / Cancel / Apply");}
    {auto zoom=canvas()->zoom;selectTool(Tool::Zoom);auto point=canvas()->rect().center();auto documentBefore=canvas()->documentPoint(point);QTest::mouseClick(canvas(),Qt::LeftButton,{},point);require(std::abs(canvas()->zoom-zoom*2)<1e-9&&QLineF(canvas()->documentPoint(point),documentBefore).length()<1e-6,"Anchored zoom click failed");QTest::mouseClick(canvas(),Qt::LeftButton,Qt::AltModifier,point);require(std::abs(canvas()->zoom-zoom)<1e-9,"Zoom out click failed");passed("anchored zoom tool / Alt zoom out");}
    {Document large;large.id=newId();large.width=large.height=30000;Layer emptyLayer;emptyLayer.id=newId();emptyLayer.name="Layer 1";emptyLayer.transform={0,0,30000,30000};large.layers.push_back(emptyLayer);QElapsedTimer clock;clock.start();Raster::resetMaterializationCount();addProject(large);canvas()->repaint();QTest::qWait(30);require(canvas()->deviceReady()&&canvas()->deviceError().isEmpty(),"Large blank canvas presentation failed");auto frame=canvas()->captureRendered();require(!frame.isNull(),"Large blank canvas readback failed");QFile timing(dir+"/large-canvas.json");if(timing.open(QIODevice::WriteOnly))timing.write(QJsonDocument(QJsonObject{{"width",30000},{"height",30000},{"create_and_present_ms",double(clock.elapsed())},{"display_tile_budget",64},{"rgba_materializations",double(Raster::materializationCount())}}).toJson());require(Raster::materializationCount()==0,"Blank canvas presentation flattened pixels");closeProject(tabs_->currentIndex());passed("30000-square blank canvas native presentation with bounded viewport tiles");}
    {
        // A real mouse stroke publishes a renderer-only source before commit.
        // Capture the D3D back buffer while the canonical Layer is still intact.
        Document document;document.id=newId();document.width=64;document.height=32;
        Layer layer;layer.id=newId();layer.name="Native retouch extent witness";layer.transform={8,8,16,16};layer.raster=Raster::filled(16,16,{70,180,120,255});document.layers={layer};
        auto& project=addProject(document);selectTool(Tool::CloneStamp);cloneSettings_.radius=4;cloneSettings_.hardness=1;cloneSettings_.opacity=1;project.cloneSampleAllLayers=false;refreshRetouchControls();canvas()->fit();QTest::qWait(60);
        const auto sourcePoint=viewPoint({16,16}),destination=viewPoint({40,16}),endpoint=viewPoint({44,16});
        QTest::mouseClick(canvas(),Qt::LeftButton,Qt::AltModifier,sourcePoint);
        require(project.cloneAlignment.source().has_value()&&project.history.undoCount()==0,"Native Clone source sampling changed history");
        const auto canonical=project.document;const auto originalRaster=active()->raster;
        auto probe=[&](const QImage& image){auto point=viewPoint({40.5,16.5})*canvas()->devicePixelRatioF();require(image.rect().contains(point),"Native retouch sample outside viewport");return image.pixelColor(point);};
        auto beforeFrame=canvas()->captureRendered();require(!beforeFrame.isNull()&&beforeFrame.save(dir+"/retouch-before.png"),"Native retouch baseline capture failed");const auto beforeColor=probe(beforeFrame);
        QTest::mousePress(canvas(),Qt::LeftButton,Qt::NoModifier,destination);QTest::qWait(40);
        require(retouch_&&project.retouchPreview&&project.document==canonical&&active()->raster==originalRaster&&project.history.undoCount()==0,"Native retouch live preview mutated canonical state");
        auto liveFrame=canvas()->captureRendered();require(!liveFrame.isNull()&&liveFrame.save(dir+"/retouch-live.png"),"Native retouch live D3D capture failed");
        require(retouch_&&project.retouchPreview&&project.document==canonical,"Native presentation committed the retouch stroke");
        auto profile=platform::discoverDisplayProfile(reinterpret_cast<void*>(canvas()->winId()));
        require(canvas()->presentationProfileHash()==QString::fromStdString(profile.sha256),"Monitor profile changed during retouch readback");
        const auto expected=canvas()->presentationConvertsColor()?monitorExpectedColor(profile,QColor(70,180,120)):QColor(70,180,120);
        require(canvas()->presentationConvertsColor()||!canvas()->presentationDiagnostic().isEmpty(),"Retouch sRGB fallback lacks diagnostic");
        const auto liveColor=probe(liveFrame);auto matches=[&](QColor actual,QColor wanted){return std::abs(actual.red()-wanted.red())<=1&&std::abs(actual.green()-wanted.green())<=1&&std::abs(actual.blue()-wanted.blue())<=1;};
        require(matches(liveColor,expected),"Presented live Clone pixels differ from profile-qualified source color");
        require(!matches(beforeColor,expected),"Native retouch baseline lacks a distinct exterior precondition");
        QTest::qWait(40);auto liveWindow=screen()->grabWindow(0,mapToGlobal(QPoint(0,0)).x(),mapToGlobal(QPoint(0,0)).y(),width(),height());require(!liveWindow.isNull()&&liveWindow.save(dir+"/retouch-window-live.png"),"Live retouch window screenshot failed");
        QTest::mouseMove(canvas(),endpoint);require(project.document==canonical&&project.retouchPreview,"Native Clone motion changed canonical layer");
        QTest::mouseRelease(canvas(),Qt::LeftButton,Qt::NoModifier,endpoint);QTest::mouseMove(canvas(),sourcePoint);QTest::qWait(30);
        require(!retouch_&&!project.retouchPreview&&project.history.undoCount()==1&&project.history.undoName()=="Clone Stamp","Native Clone did not commit one history transaction");
        require(active()->transform.x==8&&active()->transform.y==8&&active()->transform.width>16&&active()->raster!=originalRaster,"Native Clone commit lost grown placement");
        require(SoftwareRenderer().render(*project.document,0,0,64,32)->pixel(40,16)==Pixel{70,180,120,255}&&originalRaster->pixel(8,8)==Pixel{70,180,120,255},"Native Clone committed pixels or immutable input differ");
        const auto committed=project.document;auto commitFrame=canvas()->captureRendered();require(!commitFrame.isNull()&&commitFrame.save(dir+"/retouch-committed.png"),"Native Clone commit capture failed");const auto committedColor=probe(commitFrame);require(matches(committedColor,expected),"Native Clone commit presentation lost live pixels");
        undo_->trigger();require(project.document==canonical&&!project.retouchPreview,"Native growing Clone undo lost original layer metadata");auto undoFrame=canvas()->captureRendered();require(!undoFrame.isNull()&&undoFrame.save(dir+"/retouch-undone.png"),"Native Clone undo capture failed");const auto undoneColor=probe(undoFrame);require(matches(undoneColor,beforeColor),"Native Clone undo retained exterior preview pixels");
        redo_->trigger();require(project.document==committed&&!project.retouchPreview,"Native growing Clone redo differs from committed layer");undo_->trigger();require(project.document==canonical,"Native Clone cleanup undo differs");
        auto rgb=[](QColor c){return QJsonArray{c.red(),c.green(),c.blue()};};QFile evidence(dir+"/retouch-pixel-oracle.json");require(evidence.open(QIODevice::WriteOnly),"Retouch pixel evidence output failed");
        evidence.write(QJsonDocument(QJsonObject{{"canonical_rgb",QJsonArray{70,180,120}},{"expected_display_rgb",rgb(expected)},{"before_rgb",rgb(beforeColor)},{"live_rgb",rgb(liveColor)},{"committed_rgb",rgb(committedColor)},{"undone_rgb",rgb(undoneColor)},{"tolerance",1},{"canvas",QJsonArray{64,32}},{"original_layer_bounds",QJsonArray{8,8,16,16}},{"sample_source",QJsonArray{16,16}},{"stroke_begin",QJsonArray{40,16}},{"stroke_end",QJsonArray{44,16}},{"live_canonical_unchanged",true},{"history_transactions",1},{"undo_redo_exact",true},{"profile_sha256",canvas()->presentationProfileHash()},{"converted",canvas()->presentationConvertsColor()},{"oracle",canvas()->presentationConvertsColor()?"Windows WCS CPU profile transform":"explicit sRGB fallback"}}).toJson());
        project.history.markSaved();closeProject(tabs_->currentIndex());passed("native Clone extent growth / immutable live D3D presentation / commit / undo / redo");
    }
    selectTool(Tool::Move);
    auto rendered=SoftwareRenderer().render(*current()->document,0,0,current()->document->width,current()->document->height)->rgba();
    current()->path=dir+"/Project 実証 test.comp";require(saveProject(),"Native save failed");auto path=current()->path;
    ProjectStore store(makeWicProjectCodec());const auto persisted=store.load(std::filesystem::path(path.toStdWString()));
    auto reopened=SoftwareRenderer().render(persisted.document,0,0,persisted.document.width,persisted.document.height)->rgba();
    require(rendered==reopened&&persisted.activeLayer==current()->active&&!current()->history.modified(),"Persisted project changed rendered pixels, active layer or dirty state");
    auto* savedProject=current();const auto tabCount=tabs_->count();const auto savedDocument=current()->document;openPath(path);
    require(current()==savedProject&&tabs_->count()==tabCount&&current()->document==savedDocument&&!current()->history.modified(),"Opening an existing project did not preserve its tab and state");
    passed("native save/load Unicode directory package, exact composite round trip and existing-tab reuse");
    canvas()->recreateDevice();resize(1100,740);QTest::qWait(100);require(canvas()->deviceReady(),"Device recreation failed");canvas()->repaint();QTest::qWait(120);
    auto gpu=canvas()->captureRendered();require(!gpu.isNull()&&gpu.save(dir+"/canvas-readback.png"),"GPU capture failed");
    auto sample=viewPoint({100,350})*devicePixelRatioF();require(gpu.rect().contains(sample),"GPU sample is outside the resized viewport");auto color=gpu.pixelColor(sample);
    auto profile=platform::discoverDisplayProfile(reinterpret_cast<void*>(canvas()->winId()));
    require(canvas()->presentationProfileHash()==QString::fromStdString(profile.sha256),"Monitor profile changed during the native pixel check");
    const auto expected=canvas()->presentationConvertsColor()?monitorExpectedColor(profile):QColor(35,65,90);
    require(canvas()->presentationConvertsColor()||!canvas()->presentationDiagnostic().isEmpty(),"Native sRGB fallback lacks a diagnostic");
    QFile pixelEvidence(dir+"/pixel-oracle.json");require(pixelEvidence.open(QIODevice::WriteOnly),"Pixel oracle evidence output failed");
    pixelEvidence.write(QJsonDocument(QJsonObject{{"canonical_rgb",QJsonArray{35,65,90}},{"expected_display_rgb",QJsonArray{expected.red(),expected.green(),expected.blue()}},{"actual_display_rgb",QJsonArray{color.red(),color.green(),color.blue()}},{"tolerance",1},{"profile_sha256",canvas()->presentationProfileHash()},{"converted",canvas()->presentationConvertsColor()},{"oracle",canvas()->presentationConvertsColor()?"Windows WCS CPU profile transform":"explicit sRGB fallback"}}).toJson());
    require(std::abs(color.red()-expected.red())<=1&&std::abs(color.green()-expected.green())<=1&&std::abs(color.blue()-expected.blue())<=1,"GPU background pixels differ from the profile-qualified canonical composite");passed("WARP readback pixel invariant, resize and device recreation");
    auto screenshot=screen()->grabWindow(0,mapToGlobal(QPoint(0,0)).x(),mapToGlobal(QPoint(0,0)).y(),width(),height());require(!screenshot.isNull()&&screenshot.save(dir+"/native-window.png"),"Native screenshot failed");
    QFile report(dir+"/native-ui.json");require(report.open(QIODevice::WriteOnly),"UI evidence output failed");report.write(QJsonDocument(QJsonObject{{"status","passed"},{"checks",checks},{"devicePixelRatio",devicePixelRatioF()},{"brush_full_raster_materializations",double(flattenCount)},{"human_acceptance",false},{"timestamp",QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}}).toJson());for(auto&p:projects_)p->history.markSaved();
}
}
