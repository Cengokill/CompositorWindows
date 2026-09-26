// Reuse the unchanged executed fixture; every original eighteen-case assertion
// remains in BrushAppearanceTests.cpp and is compiled in its original target.
#define main originalBrushAppearanceMain
#include "BrushAppearanceTests.cpp"
#undef main
#include <QCheckBox>
#include <QJsonArray>

namespace {
QJsonObject observations;
bool entirelyVisible(QWidget* value){return value->isVisible()&&value->visibleRegion().contains(value->rect());}
void toolbarLayout(const char* commandId,int requestedWidth){
    Fixture f;invoke(f.window,commandId);
    const int minimum=f.window.minimumSizeHint().width();
    f.window.resize(requestedWidth?requestedWidth:minimum,900);events();QTest::qWait(30);events();
    observations["requested_width"]=requestedWidth;observations["minimum_width_hint"]=minimum;observations["actual_width"]=f.window.width();observations["device_pixel_ratio"]=f.window.devicePixelRatioF();
    const bool brush=std::string_view(commandId)=="tool.brush",clone=std::string_view(commandId)=="tool.clone",heal=std::string_view(commandId)=="tool.heal";
    auto* hardness=control<QSlider>(f.window,brush?"brushHardnessSlider":"retouchHardnessSlider");
    auto* opacity=control<QSlider>(f.window,brush?"brushOpacitySlider":"retouchOpacitySlider");
    auto* hardNumber=control<QDoubleSpinBox>(f.window,brush?"Brush hardness":"retouchHardness");
    auto* opacityNumber=control<QDoubleSpinBox>(f.window,brush?"Brush opacity":"retouchOpacity");
    auto* size=control<QDoubleSpinBox>(f.window,brush?"Brush diameter":"retouchDiameter");
    std::vector<QWidget*> required{hardness,opacity,hardNumber,opacityNumber,size};
    if(brush){required.push_back(control<QAbstractButton>(f.window,"brushModePaint"));required.push_back(control<QAbstractButton>(f.window,"brushModeErase"));required.push_back(control<QComboBox>(f.window,"Editing target"));QCheckBox* maskWhite=nullptr;for(auto* box:f.window.findChildren<QCheckBox*>())if(box->text()=="Paint mask white")maskWhite=box;require(maskWhite,"Mask paint option exists");required.push_back(maskWhite);}
    else if(clone){required.push_back(control<QCheckBox>(f.window,"retouchAligned"));required.push_back(control<QCheckBox>(f.window,"retouchAllLayers"));}
    else required.push_back(control<QComboBox>(f.window,heal?"retouchHealingMode":"retouchSmearMode"));
    QJsonArray states;bool visible=true;for(auto* value:required){const bool shown=entirelyVisible(value);visible=visible&&shown;states.append(QJsonObject{{"name",value->objectName().isEmpty()?value->accessibleName():value->objectName()},{"fully_visible",shown},{"width",value->width()},{"height",value->height()}});}observations["controls"]=states;
    require(visible,"Every family option, numeric field and slider is fully visible at the supported window width");
    QTest::mouseClick(hardness,Qt::LeftButton,{},QPoint(hardness->width()/4,hardness->height()/2));events();require(hardNumber->value()>10&&hardNumber->value()<40,"Actual pointer press reaches the visible hardness track");
    QTest::mouseClick(opacityNumber,Qt::LeftButton);QTest::keyClick(opacityNumber,Qt::Key_A,Qt::ControlModifier);QTest::keyClicks(opacityNumber,"43");QTest::keyClick(opacityNumber,Qt::Key_Return);events();equalNumber(opacityNumber->value(),43,"Trailing numeric field accepts pointer and keyboard input");equalNumber(sliderPercent(opacity),43,"Visible numeric input synchronizes opacity slider");
    if(clone){auto* aligned=control<QCheckBox>(f.window,"retouchAligned");const bool before=f.project.cloneAlignment.aligned;QTest::mouseClick(aligned,Qt::LeftButton);events();require(f.project.cloneAlignment.aligned!=before,"Pointer reaches visible Aligned option");auto* all=control<QCheckBox>(f.window,"retouchAllLayers");QTest::mouseClick(all,Qt::LeftButton);events();require(f.project.cloneSampleAllLayers,"Pointer reaches visible Sample All Layers option");}
    unchanged(f);
}
}
int main(int argc,char** argv){QApplication app(argc,argv);QTemporaryDir preferences;require(preferences.isValid(),"Fixture settings directory");QCoreApplication::setOrganizationName("CompositorFixture");QCoreApplication::setApplicationName("BrushToolbarLayout");QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,preferences.path());QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,preferences.path());
    const std::string key=argc>1?argv[1]:"";QJsonObject report{{"case",QString::fromStdString(key)}};int result=1;
    try{require(argc==3,"Provide case and output path");const auto split=key.find('_');require(split!=std::string::npos,"Family and width case");const auto family=key.substr(0,split),width=key.substr(split+1);const std::map<std::string,const char*> tools{{"brush","tool.brush"},{"heal","tool.heal"},{"clone","tool.clone"},{"smear","tool.retouch"}};require(tools.contains(family),"Known family");require(width=="minimum"||width=="1280"||width=="1500","Frozen width");toolbarLayout(tools.at(family),width=="minimum"?0:std::stoi(width));report["status"]="passed";result=0;std::cout<<"PASS "<<key<<'\n';}catch(const std::exception& error){report["status"]="failed";report["error"]=error.what();std::cerr<<"FAIL "<<key<<": "<<error.what()<<'\n';}report["observations"]=observations;if(argc==3){QFile file(QString::fromLocal8Bit(argv[2]));if(!file.open(QIODevice::WriteOnly)||file.write(QJsonDocument(report).toJson())<0)return 2;}return result;
}
