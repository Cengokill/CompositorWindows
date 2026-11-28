#include "ui/MainWindow.h"
#include "ui/SegmentedControl.h"
#include <QApplication>
#include <QFontDatabase>
#include <QSettings>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTemporaryDir>
#include <iostream>
#include <map>
#include <stdexcept>
using namespace compositor;
#define REQUIRE(...) do{if(!(__VA_ARGS__))throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+": " #__VA_ARGS__);}while(false)
Document document(){Document d;d.id=newId();d.width=40;d.height=20;Layer l;l.id=newId();l.transform={0,0,40,20};d.layers={l};return d;}
struct Fixture {
    MainWindow window{true};EditorProject *a,*b;QTabWidget* tabs;
    Fixture(){a=&window.addProject(document(),"A");b=&window.addProject(document(),"B");tabs=window.findChild<QTabWidget*>();REQUIRE(tabs);}
    void select(EditorProject* p){tabs->setCurrentWidget(p->page);}
    template<class T> T* field(const char* key){if(auto* item=window.findChild<T*>(key))return item;for(auto* item:window.findChildren<T*>())if(item->accessibleName()==key)return item;throw std::runtime_error(std::string("Missing control: ")+key);}
    QCheckBox* check(const char* bar,const char* label){auto* scope=window.findChild<QToolBar*>(bar);REQUIRE(scope);for(auto* item:scope->findChildren<QCheckBox*>())if(item->text()==label)return item;throw std::runtime_error(std::string("Missing checkbox: ")+label);}
    void tool(ProjectTool value){for(auto* item:window.findChild<QToolBar*>("tools")->findChildren<QAction*>())if(item->property("editorTool").isValid()&&item->property("editorTool").toInt()==int(value)){item->trigger();return;}throw std::runtime_error("Missing tool");}
};
void source_defaults(){Fixture f;f.select(f.a);REQUIRE(f.a->toolState.tool==ProjectTool::Move&&f.a->toolState.foreground==Qt::black&&f.a->toolState.background==Qt::white);REQUIRE(f.field<QDoubleSpinBox>("Brush diameter")->value()==40);REQUIRE(f.field<QCheckBox>("transformLockRatio")->isChecked()&&f.a->canvas->showPixelGrid);REQUIRE(!f.a->canvas->showRulers&&!f.window.findChild<QAction*>("viewRulers")->isChecked());REQUIRE(f.a->toolState.brushSettings.hardness==1&&f.a->toolState.cloneSettings.hardness==0&&f.a->toolState.blurSettings.hardness==0);REQUIRE(f.a->toolState.blurSettings.mode==retouch::Mode::Liquify&&f.a->toolState.healingMode==retouch::Mode::HealContentAware);REQUIRE(ui::findSegmentedControl(&f.window,"gradientShape")->currentIndex()==0&&f.field<QComboBox>("gradientStyle")->currentIndex()==1);REQUIRE(f.field<QSpinBox>("Wand tolerance")->value()==32&&f.field<QComboBox>("Wand sample size")->currentIndex()==0);}
void tab_isolation(){Fixture f;const auto beforeA=f.a->document,beforeB=f.b->document;f.a->toolState.foreground=QColor(90,80,70);f.a->toolState.background=QColor(20,30,40);f.select(f.a);
    f.field<QDoubleSpinBox>("Brush diameter")->setValue(63);f.tool(ProjectTool::CloneStamp);f.field<QDoubleSpinBox>("retouchDiameter")->setValue(94);f.field<QDoubleSpinBox>("retouchHardness")->setValue(12);f.field<QDoubleSpinBox>("retouchOpacity")->setValue(48);
    ui::findSegmentedControl(&f.window,"retouchHealingMode")->choose(2);ui::findSegmentedControl(&f.window,"retouchSmearMode")->choose(2);
    ui::findSegmentedControl(&f.window,"gradientShape")->choose(1);f.field<QComboBox>("gradientStyle")->setCurrentIndex(0);f.field<QCheckBox>("gradientReverse")->setChecked(true);f.field<QDoubleSpinBox>("Gradient opacity")->setValue(37);
    ui::findSegmentedControl(&f.window,"shapeKind")->choose(1);f.field<QDoubleSpinBox>("Corner radius")->setValue(7);ui::findSegmentedControl(&f.window,"marqueeShape")->choose(1);ui::findSegmentedControl(&f.window,"selectionMode")->segment(1)->click();f.check("selectionOptions","Anti-alias")->setChecked(false);
    f.field<QSpinBox>("Wand tolerance")->setValue(91);f.field<QComboBox>("Wand sample size")->setCurrentIndex(2);f.check("selectionOptions","Contiguous")->setChecked(false);ui::findSegmentedControl(&f.window,"wandScope")->choose(1);f.check("brushOptions","Paint mask white")->setChecked(true);
    f.field<QCheckBox>("transformLockRatio")->setChecked(false);f.field<QCheckBox>("transformAutoSelect")->setChecked(true);f.field<QCheckBox>("transformShowControls")->setChecked(false);f.field<QCheckBox>("transformSnapping")->setChecked(false);f.a->canvas->showPixelGrid=false;f.tool(ProjectTool::Shape);
    f.select(f.b);const auto& a=f.a->toolState;REQUIRE(a.tool==ProjectTool::Shape&&a.brushSettings.radius==31.5&&a.cloneSettings.radius==47&&a.cloneSettings.hardness==.12&&a.cloneSettings.opacity==.48);REQUIRE(a.foreground==QColor(90,80,70)&&a.background==QColor(20,30,40));REQUIRE(a.gradientSettings.opacity==.37&&a.gradientSettings.reversed&&a.shapeStyle.cornerRadius==7&&a.ellipse&&!a.selectionAntialias);REQUIRE(a.wandTolerance==91&&a.wandSampleRadius==2&&!a.wandContiguous&&a.wandAllLayers&&a.maskPaintWhite);REQUIRE(!a.lockRatio&&a.autoSelectLayers&&!a.transformControls&&!a.snapPolicy.enabled&&!a.showPixelGrid);
    REQUIRE(f.field<QDoubleSpinBox>("Brush diameter")->value()==40&&ui::findSegmentedControl(&f.window,"gradientShape")->currentIndex()==0&&f.field<QSpinBox>("Wand tolerance")->value()==32);REQUIRE(f.field<QCheckBox>("transformLockRatio")->isChecked()&&f.b->canvas->showPixelGrid);
    f.tool(ProjectTool::Brush);f.field<QDoubleSpinBox>("Brush diameter")->setValue(99);f.select(f.a);REQUIRE(f.b->toolState.brushSettings.radius==49.5&&f.field<QDoubleSpinBox>("Brush diameter")->value()==63);REQUIRE(ui::findSegmentedControl(&f.window,"shapeKind")->currentIndex()==1&&ui::findSegmentedControl(&f.window,"gradientShape")->currentIndex()==1&&f.field<QDoubleSpinBox>("Gradient opacity")->value()==37);REQUIRE(f.field<QSpinBox>("Wand tolerance")->value()==91&&!f.check("selectionOptions","Contiguous")->isChecked());REQUIRE(f.a->document==beforeA&&f.b->document==beforeB&&f.a->history.undoCount()==0&&f.b->history.undoCount()==0);
}
void control_signals(){Fixture f;f.a->toolState.brushSettings.radius=55;f.a->toolState.gradientSettings.shape=editing::GradientShape::Radial;f.a->toolState.wandTolerance=100;f.a->toolState.selectionMode=editing::SelectionMode::Add;QSignalSpy brush(f.field<QDoubleSpinBox>("Brush diameter"),&QDoubleSpinBox::valueChanged),wand(f.field<QSpinBox>("Wand tolerance"),&QSpinBox::valueChanged);int selectionEdits=0,gradientEdits=0;auto* gradientShape=ui::findSegmentedControl(&f.window,"gradientShape");const auto chooseGradient=gradientShape->changed;gradientShape->changed=[&](int index){++gradientEdits;chooseGradient(index);};auto* selection=ui::findSegmentedControl(&f.window,"selectionMode");const auto choose=selection->changed;selection->changed=[&](int index){++selectionEdits;choose(index);};f.select(f.a);REQUIRE(brush.empty()&&gradientEdits==0&&wand.empty()&&selectionEdits==0&&selection->currentIndex()==1);REQUIRE(f.field<QDoubleSpinBox>("Brush diameter")->value()==110&&f.field<QSpinBox>("Wand tolerance")->value()==100&&f.a->history.undoCount()==0);}
void continuity_local(){Fixture f;f.a->toolState.lastBrushPoint=Point{11,12};f.a->toolState.lastBrushLayer=f.a->active;f.a->toolState.lastBrushMask=true;f.a->toolState.showSampleRing=false;f.a->cloneAlignment.setSource({7,8});f.a->cloneAlignment.beginStroke({17,18});f.a->cloneSampleAllLayers=true;f.a->collapsedGroups.insert("folder-A");f.b->cloneAlignment.setSource({90,91});f.a->canvas->zoom=2;f.a->canvas->pan={3,4};f.b->canvas->zoom=7;f.b->canvas->pan={6,8};f.select(f.a);f.select(f.b);REQUIRE(f.a->toolState.lastBrushPoint==Point{11,12}&&f.a->toolState.lastBrushLayer==f.a->active&&f.a->toolState.lastBrushMask&&!f.a->toolState.showSampleRing);REQUIRE(!f.b->toolState.lastBrushPoint&&f.b->toolState.showSampleRing);REQUIRE(f.a->cloneAlignment.strokeOffset({30,31})==Point{-10,-10}&&f.b->cloneAlignment.source()==Point{90,91});REQUIRE(f.a->cloneSampleAllLayers&&!f.b->cloneSampleAllLayers&&f.a->collapsedGroups.contains("folder-A")&&f.b->collapsedGroups.empty());REQUIRE(f.a->canvas->zoom==2&&f.a->canvas->pan==QPointF(3,4)&&f.b->canvas->zoom==7&&f.b->canvas->pan==QPointF(6,8));}
void brush_family_tips(){Fixture f;f.select(f.a);f.tool(ProjectTool::Brush);f.field<QDoubleSpinBox>("Brush diameter")->setValue(30);f.tool(ProjectTool::SpotHealing);REQUIRE(f.field<QDoubleSpinBox>("retouchHardness")->value()==100);f.tool(ProjectTool::CloneStamp);REQUIRE(f.field<QDoubleSpinBox>("retouchHardness")->value()==0&&f.field<QDoubleSpinBox>("retouchDiameter")->value()==40);f.field<QDoubleSpinBox>("retouchHardness")->setValue(50);f.tool(ProjectTool::SpotHealing);REQUIRE(f.field<QDoubleSpinBox>("retouchHardness")->value()==100&&f.field<QDoubleSpinBox>("retouchDiameter")->value()==30);f.tool(ProjectTool::CloneStamp);REQUIRE(f.field<QDoubleSpinBox>("retouchHardness")->value()==50&&f.field<QDoubleSpinBox>("retouchDiameter")->value()==40);f.select(f.b);f.tool(ProjectTool::CloneStamp);REQUIRE(f.field<QDoubleSpinBox>("retouchHardness")->value()==0);f.select(f.a);REQUIRE(f.field<QDoubleSpinBox>("retouchHardness")->value()==50);}
void snap_grid_sync(){Fixture f;f.select(f.a);const auto before=f.a->document;
    REQUIRE(f.window.findChild<QAction*>("viewSnap")->isChecked()&&!f.window.findChild<QAction*>("viewLayoutGrid")->isChecked()&&!f.a->canvas->layoutGrid().visible);
    // The toolbar checkbox and View > Snap share one policy; each project keeps its own.
    f.field<QCheckBox>("transformSnapping")->setChecked(false);
    REQUIRE(!f.window.findChild<QAction*>("viewSnap")->isChecked());
    f.select(f.b);REQUIRE(f.field<QCheckBox>("transformSnapping")->isChecked()&&f.window.findChild<QAction*>("viewSnap")->isChecked());
    f.select(f.a);REQUIRE(!f.field<QCheckBox>("transformSnapping")->isChecked()&&!f.window.findChild<QAction*>("viewSnap")->isChecked());
    // Snap To targets and the layout grid are per project and never change the document or history.
    f.window.findChild<QAction*>("snapGrid")->trigger();f.window.findChild<QAction*>("viewSnap")->trigger();
    REQUIRE(f.window.findChild<QAction*>("snapGrid")->isChecked()&&f.window.findChild<QAction*>("viewSnap")->isChecked()&&f.field<QCheckBox>("transformSnapping")->isChecked());
    f.window.findChild<QAction*>("viewLayoutGrid")->trigger();
    REQUIRE(f.a->canvas->layoutGrid().visible&&f.window.findChild<QAction*>("viewLayoutGrid")->isChecked());
    f.select(f.b);REQUIRE(!f.b->canvas->layoutGrid().visible&&!f.b->toolState.snapPolicy.grid&&f.b->toolState.snapPolicy.enabled);
    REQUIRE(f.a->document==before&&f.a->history.undoCount()==0&&f.b->history.undoCount()==0);
}
void selection_guide_clears(){Fixture f;f.select(f.a);Guide guide;guide.id="guide-x";guide.position=20;f.a->document->guides.push_back(guide);f.a->canvas->showGuides=true;
    f.tool(ProjectTool::Marquee);
    // The press and the last move both snap to the guide; release must clear the accent line.
    f.a->canvas->pointerDown({19,4},{});f.a->canvas->pointerMove({20,12},{});REQUIRE(f.a->canvas->hasSnapGuides());
    f.a->canvas->pointerUp({20,12},{});REQUIRE(f.a->document->selection);REQUIRE(!f.a->canvas->hasSnapGuides());
}
void remembered_snap_grid(){QSettings settings;settings.setValue("snap/grid",true);settings.setValue("snap/guides",false);settings.setValue("layoutGrid/spacing",250);settings.setValue("layoutGrid/subdivisions",5);settings.setValue("layoutGrid/red",300);settings.setValue("layoutGrid/green",-5);settings.setValue("layoutGrid/blue",90);settings.setValue("layoutGrid/opacity",.5);
    MainWindow window{true};auto& project=window.addProject(document(),"Remembered");
    // Remembered values reach a new project, clamped to 0-255 for color; the grid stays hidden.
    REQUIRE(project.toolState.snapPolicy.grid&&!project.toolState.snapPolicy.guides&&project.toolState.snapPolicy.enabled);
    const auto grid=project.canvas->layoutGrid();
    REQUIRE(grid.spacing==250&&grid.subdivisions==5&&grid.opacity==.5&&!grid.visible);
    REQUIRE(grid.color==std::array<uint8_t,3>{255,0,90});
}

int main(int argc,char**argv){QApplication app(argc,argv);QFontDatabase::addApplicationFont(qEnvironmentVariable("WINDIR")+"/Fonts/segoeui.ttf");QApplication::setFont(QFont("Segoe UI",9));
    // Snap and grid preferences are written to QSettings; keep test runs away from the user's real settings.
    QTemporaryDir preferences;if(!preferences.isValid())return 2;QCoreApplication::setOrganizationName("CompositorFixture");QCoreApplication::setApplicationName("SessionState");QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,preferences.path());QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,preferences.path());
    const std::map<std::string,void(*)()> cases{{"source_defaults",source_defaults},{"tab_isolation",tab_isolation},{"control_signals",control_signals},{"continuity_local",continuity_local},{"brush_family_tips",brush_family_tips},{"snap_grid_sync",snap_grid_sync},{"selection_guide_clears",selection_guide_clears},{"remembered_snap_grid",remembered_snap_grid}};try{REQUIRE(argc==2&&cases.contains(argv[1]));cases.at(argv[1])();std::cout<<"PASS "<<argv[1]<<'\n';return 0;}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
