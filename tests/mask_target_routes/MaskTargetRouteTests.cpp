#include "PendingLayerSelectionBase.cpp"
#include <QComboBox>

namespace {
QComboBox* targetCombo(MainWindow& window){
    auto* combo=window.findChild<QComboBox*>("layerEditTarget");
    require(combo&&combo->isVisible(),"Visible layer target control");return combo;
}
void targetKey(MainWindow& window,bool mask){
    auto* combo=targetCombo(window);require(combo->isEnabled(),"Layer target route enabled");
    window.raise();window.activateWindow();combo->setFocus();
    QTest::keyClick(combo,mask?Qt::Key_End:Qt::Key_Home);
    require(combo->currentIndex()==int(mask),"Target control displays requested choice");
}
void samePending(const std::string& scenario,MainWindow& window,EditorProject& project,const PendingObservation& before){
    assertTarget(project,"source",true);
    require(layer(project,"target")==before.target&&layer(project,"live")==before.live,"Other layer values unchanged");
    if(scenario=="gradient"){
        require(!project.gradientPreview&&project.history.undoCount()==1&&project.history.undoName()=="Gradient","Target resolves gradient in exactly one transaction");
        require(layer(project,"source").raster!=before.source.raster&&layer(project,"source").mask==before.source.mask,"Gradient commits only original image target");
    }else if(scenario=="transform"){
        require(pendingCommand(window,"transform.apply")->isEnabled()&&project.history.undoCount()==0&&layer(project,"source").transform.x==17,"Same-layer mask target keeps uncommitted transform");
    }else{
        require(*project.document==before.before&&project.history.undoCount()==0,"Target choice preserves canonical document and history");
        if(scenario=="crop")require(project.canvas->cropOverlay()==before.crop&&pendingCommand(window,"crop.apply")->isEnabled(),"Target keeps exact crop draft");
        else if(scenario=="polygon")require(project.canvas->selectionDraft()&&project.canvas->selectionDraft()->points==before.polygon,"Target keeps exact polygon draft");
        else if(scenario=="hue"||scenario=="filter"||scenario=="levels"){
            require(before.panel&&before.panel->panel()->isVisible()&&pendingPanel(window)==before.panel,"Target retains captured edit panel");
            pendingApply(before.panel->panel())->click();QElapsedTimer timer;timer.start();
            while(before.panel&&before.panel->panel()&&before.panel->panel()->isVisible()){require(timer.elapsed()<8000,"Captured panel Apply completes");QTest::qWait(5);}
            require(project.history.undoCount()==1&&layer(project,"source").raster!=before.source.raster,"Captured editor applies to original image in one transaction");
            const auto& applied=layer(project,"source");
            if(scenario=="filter"){
                // Filters.swift414-424 carries an unplaced raster mask to the
                // final image grid. This fixture's white coverage stays255.
                require(applied.mask&&before.source.mask&&applied.mask->raster&&applied.raster,"Gaussian keeps attached mask backing");
                require(applied.mask->enabled==before.source.mask->enabled&&applied.mask->linked==before.source.mask->linked&&applied.mask->placement==before.source.mask->placement,"Gaussian mask carry preserves flags and placement");
                require(applied.mask->raster->width==applied.raster->width&&applied.mask->raster->height==applied.raster->height,"Gaussian mask carries to final image dimensions");
                for(int y=0;y<applied.mask->raster->height;++y)for(int x=0;x<applied.mask->raster->width;++x)require(applied.mask->raster->pixel(x,y)==255,"Carried white mask preserves exact coverage");
            }else require(applied.mask==before.source.mask,"Color adjustment preserves original mask");
            require(layer(project,"target")==before.target,"Captured Apply preserves the other layer");
            assertTarget(project,"source",true);
        }
    }
}
void runRoute(const std::string& key,MainWindow& window,EditorProject& project){
    if(key=="layer_busy_guard"||key=="layer_import_guard"){
        const auto before=*project.document;
        if(key=="layer_busy_guard")project.projectBusy=true;else project.importing=true;
        auto* controller=ui::LayerPanelController::find(tree(window));require(controller,"Layer controller");controller->updateSelection();
        // Disabled state is an observable UI contract; a forced signal below
        // separately exercises the callback guard and is not a physical click.
        const bool disabled=!targetCombo(window)->isEnabled();
        targetCombo(window)->setCurrentIndex(1);
        require(!project.maskSelected&&*project.document==before&&project.history.undoCount()==0,"Busy callback cannot alter target/document/history");
        require(targetCombo(window)->currentIndex()==0,"Rejected callback restores displayed target");
        require(disabled,"Busy target selector is disabled");return;
    }
    if(key=="layer_brush_guard_synchronous"){
        const auto before=preparePending("brush_guard",window,project);
        targetCombo(window)->setCurrentIndex(1);
        require(project.brushPreview&&!project.maskSelected&&*project.document==before.before&&project.history.undoCount()==0,"Synchronous target callback cannot cancel an active brush or alter target");
        require(targetCombo(window)->currentIndex()==0,"Rejected brush callback restores displayed target");return;
    }
    if(key=="layer_selected_sync"){
        const auto before=*project.document;targetKey(window,true);assertTarget(project,"source",true);
        targetKey(window,false);assertTarget(project,"source",false);
        require(*project.document==before&&project.history.undoCount()==0,"Idle target roundtrip creates no history");return;
    }
    const bool menu=key.starts_with("menu_");
    const bool disabled=key.ends_with("_disabled");
    const bool image=key=="menu_polygon_image";
    if(image)targetKey(window,true);
    const auto scenario=key.substr(menu?5:6,key.find('_',menu?5:6)-(menu?5:6));
    const char* setup=scenario=="gradient"?"gradient_same_mask":scenario=="crop"?"crop_other_mask":scenario=="polygon"?"polygon_other_image":scenario=="transform"?"transform_same_mask":scenario=="hue"?"hue_other_image":scenario=="filter"?"filter_other_mask":"levels_other_image";
    const auto before=preparePending(setup,window,project);
    if(menu){
        auto* action=pendingCommand(window,image?"mask.edit_image":"mask.edit_mask");
        if(disabled){
            require(!action->isEnabled(),"Existing Windows menu guard keeps this edit disabled");action->trigger();
            require(!project.maskSelected&&project.history.undoCount()==0,"Disabled menu creates no target/history mutation");
            if(scenario=="crop")require(project.canvas->cropOverlay()==before.crop,"Disabled menu retains crop");
            else require(pendingCommand(window,"transform.apply")->isEnabled()&&layer(project,"source").transform.x==17,"Disabled menu retains transform");
            return;
        }
        require(action->isEnabled(),"Polygon target menu is actually enabled");action->trigger();
    }else targetKey(window,true);
    if(image){
        assertTarget(project,"source",false);
        require(project.canvas->selectionDraft()&&project.canvas->selectionDraft()->points==before.polygon,"Image menu preserves polygon draft");
        require(*project.document==before.before&&project.history.undoCount()==0,"Image target preserves canonical/history");
    }else samePending(scenario,window,project,before);
}
}
int main(int argc,char** argv){
    QApplication app(argc,argv);std::cout<<std::unitbuf;if(argc!=3){std::cerr<<"Usage: mask_target_route_tests CASE NEW_REPORT.json\n";return 2;}
    QDir().mkpath(QFileInfo(QString::fromUtf8(__FILE__)).dir().filePath("fixtures"));QTemporaryDir settings(QFileInfo(QString::fromUtf8(__FILE__)).dir().filePath("fixtures/settings-XXXXXX"));require(settings.isValid(),"Owned settings");settings.setAutoRemove(false);QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    MainWindow window(true);auto& project=window.addProject(pendingFixture(),"Mask target routes");window.resize(1200,850);window.show();window.activateWindow();QTest::qWait(30);mouse(window,"source",1);project.canvas->setFocus();
    QJsonObject report{{"schema","MASK_TARGET_ROUTE_V1"},{"case",argv[1]},{"human_acceptance",false}};int result=0;
    try{runRoute(argv[1],window,project);report["status"]="passed";std::cout<<"PASS "<<argv[1]<<'\n';}
    catch(const std::exception& error){result=1;report["status"]="failed";report["error"]=error.what();std::cerr<<"FAIL "<<argv[1]<<": "<<error.what()<<'\n';}
    QFile output(QString::fromLocal8Bit(argv[2]));if(!output.open(QIODevice::WriteOnly|QIODevice::NewOnly)||output.write(QJsonDocument(report).toJson())<0)return 2;
    return result;
}
