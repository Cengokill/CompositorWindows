#include "MaskTargetRouteBase.cpp"
#include <QToolBar>

namespace {
QComboBox* brushTarget(MainWindow& window){for(auto* combo:window.findChildren<QComboBox*>())if(combo->accessibleName()=="Editing target")return combo;throw std::runtime_error("Brush target combo exists");}
void brushTargetKey(MainWindow& window,bool mask){auto* combo=brushTarget(window);require(combo->isVisible()&&combo->isEnabled(),"Visible enabled brush target");window.raise();window.activateWindow();combo->setFocus();QTest::keyClick(combo,mask?Qt::Key_End:Qt::Key_Home);require(combo->currentIndex()==int(mask),"Brush target choice displayed");}
void brushRoute(const std::string& key,MainWindow& window,EditorProject& project){
    pendingTrigger(window,key=="eraser_roundtrip"?"tool.eraser":"tool.brush");
    require(brushTarget(window)->isVisible(),"Brush toolbar initially visible");
    if(key=="brush_roundtrip"||key=="eraser_roundtrip"){
        const auto before=*project.document;brushTargetKey(window,true);assertTarget(project,"source",true);brushTargetKey(window,false);assertTarget(project,"source",false);require(*project.document==before&&project.history.undoCount()==0,"Target roundtrip adds no document/history mutation");return;
    }
    if(key=="busy_callback"||key=="import_callback"||key=="modal_callback"){
        const auto before=*project.document;QDialog modal(&window);
        if(key=="busy_callback")project.projectBusy=true;
        else if(key=="import_callback")project.importing=true;
        else {modal.setWindowModality(Qt::ApplicationModal);modal.open();QTest::qWait(20);require(QApplication::activeModalWidget()==&modal,"Owned modal guard active");}
        // Explicit stale signal; no claim of clicking a disabled control.
        brushTarget(window)->setCurrentIndex(1);
        require(!project.maskSelected&&*project.document==before&&project.history.undoCount()==0,"Guarded target callback preserves canonical target/history");
        require(brushTarget(window)->currentIndex()==0&&!brushTarget(window)->isEnabled(),"Rejected target signal restores choice and disabled state");return;
    }
    if(key=="running_brush_callback"){
        const auto before=preparePending("brush_guard",window,project);brushTarget(window)->setCurrentIndex(1);
        require(project.brushPreview&&!project.maskSelected&&*project.document==before.before&&project.history.undoCount()==0,"Target callback cannot cancel an active brush");
        require(brushTarget(window)->currentIndex()==0,"Rejected brush target displays original choice");return;
    }
    if(key=="missing_mask"){
        pendingTrigger(window,"mask.delete");require(!layer(project,"source").mask,"Source mask removed for guard fixture");const auto before=*project.document;const auto history=project.history.undoCount();
        require(!brushTarget(window)->isEnabled(),"Missing mask disables target selector");brushTarget(window)->setCurrentIndex(1);
        require(!project.maskSelected&&brushTarget(window)->currentIndex()==0&&*project.document==before&&project.history.undoCount()==history,"Stale target signal cannot select missing mask");return;
    }
    const auto separator=key.find('_');const auto scenario=key.substr(0,separator);
    const char* setup=scenario=="gradient"?"gradient_same_mask":scenario=="crop"?"crop_other_mask":scenario=="polygon"?"polygon_other_image":scenario=="transform"?"transform_same_mask":scenario=="hue"?"hue_other_image":scenario=="filter"?"filter_other_mask":"levels_other_image";
    const auto before=preparePending(setup,window,project);
    if(key.ends_with("_hidden_callback")){
        require(!brushTarget(window)->isVisible(),"Tool change hides brush-only options");
        // Tests stale queued/programmatic delivery through the same callback.
        // This is not represented as a visible keyboard journey.
        brushTarget(window)->setCurrentIndex(1);
    }else brushTargetKey(window,true);
    samePending(scenario,window,project,before);
}
}
int main(int argc,char** argv){
    QApplication app(argc,argv);std::cout<<std::unitbuf;if(argc!=3)return 2;
    QDir().mkpath(QFileInfo(QString::fromUtf8(__FILE__)).dir().filePath("fixtures"));QTemporaryDir settings(QFileInfo(QString::fromUtf8(__FILE__)).dir().filePath("fixtures/settings-XXXXXX"));require(settings.isValid(),"Owned settings fixture");settings.setAutoRemove(false);QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    MainWindow window(true);auto& project=window.addProject(pendingFixture(),"Brush target routes");window.resize(1200,850);window.show();window.activateWindow();QTest::qWait(30);mouse(window,"source",1);project.canvas->setFocus();
    QJsonObject report{{"schema","BRUSH_TARGET_ROUTE_V1"},{"case",argv[1]},{"human_acceptance",false}};int result=0;
    try{brushRoute(argv[1],window,project);report["status"]="passed";std::cout<<"PASS "<<argv[1]<<'\n';}
    catch(const std::exception& error){result=1;report["status"]="failed";report["error"]=error.what();std::cerr<<"FAIL "<<argv[1]<<": "<<error.what()<<'\n';}
    QFile output(QString::fromLocal8Bit(argv[2]));if(!output.open(QIODevice::WriteOnly|QIODevice::NewOnly)||output.write(QJsonDocument(report).toJson())<0)return 2;return result;
}
