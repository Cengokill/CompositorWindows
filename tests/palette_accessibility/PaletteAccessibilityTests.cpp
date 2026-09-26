#define main layer_action_contracts_main
#include "LayerActionAccessibilityBase.cpp"
#undef main
#include "ui/PaletteDialog.h"
#include <QAccessible>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTemporaryDir>
#include <cmath>
#include <limits>
#include <set>

namespace {
using effects_tools::PaletteColor;
QAction* paletteCommand(MainWindow& window,const char* id){for(auto* a:window.findChildren<QAction*>())if(a->property("commandId")==id&&a->isVisible())return a;throw std::runtime_error(std::string("Palette command missing: ")+id);}
void paletteTrigger(MainWindow& window,const char* id){auto* a=paletteCommand(window,id);require(a->isEnabled(),"Palette setup command enabled");a->trigger();QCoreApplication::processEvents();}
PaletteDialog* visiblePalette(MainWindow& window){for(auto* widget:window.findChildren<QWidget*>())if(auto* dialog=dynamic_cast<PaletteDialog*>(widget);dialog&&dialog->isVisible())return dialog;return nullptr;}
PaletteDialog& palette(MainWindow& window){auto* d=visiblePalette(window);require(d,"Owned visible palette");return *d;}
void samplePalette(MainWindow& window,const char* hex){auto color=PaletteColor::fromHex(hex);require(color.has_value(),"Valid frozen fixture color");palette(window).sample(*color);}
void choosePalette(MainWindow& window,bool background,const char* hex){paletteTrigger(window,background?"palette.background":"palette.foreground");samplePalette(window,hex);palette(window).accept();QCoreApplication::processEvents();}
QWidget* colorField(MainWindow& window,bool hue){auto* field=palette(window).findChild<QWidget*>(hue?"paletteHue":"paletteField");require(field&&field->isVisible()&&field->isEnabled(),"Visible enabled custom color field");return field;}
QSpinBox* rgbField(MainWindow& window,int channel){auto* field=palette(window).findChild<QSpinBox*>(QString("paletteRGB%1").arg(channel));require(field,"RGB numeric field");return field;}
void colorEquals(MainWindow& window,const char* hex){const auto value=palette(window).color().hex();std::cout<<"COLOR "<<value<<" expected="<<hex<<'\n';require(value==hex,"Frozen RGB8 palette result");}
void key(QWidget* widget,Qt::Key value,Qt::KeyboardModifiers flags={}){widget->setFocus();QTest::keyClick(widget,value,flags);}
Document paletteDocument(){Document d;d.id=newId();d.width=32;d.height=24;Layer l;l.id="palette-layer";l.name="Palette fixture";l.transform={0,0,32,24};l.raster=Raster::filled(32,24,{31,71,113,255});l.mask=Mask{std::make_shared<GrayRaster>(GrayRaster{1,1,{255}})};d.layers={l};return d;}
void immutablePalette(EditorProject& p,const Document& before){require(p.document==before&&p.history.undoCount()==0&&!p.history.modified(),"Palette interaction leaves document and history unchanged");}

void keyboardCase(const std::string& name,MainWindow& window,EditorProject& project,const Document& before){
    if(name=="hue_arrows"){
        auto* hue=colorField(window,true);key(hue,Qt::Key_Up);colorEquals(window,"00FF04");key(hue,Qt::Key_Down,Qt::ShiftModifier);colorEquals(window,"26FF00");
    }else if(name=="plane_arrows"){
        auto* field=colorField(window,false);key(field,Qt::Key_Left);colorEquals(window,"03FF03");key(field,Qt::Key_Down);colorEquals(window,"03FC03");key(field,Qt::Key_Left,Qt::ShiftModifier);colorEquals(window,"1CFC1C");key(field,Qt::Key_Down,Qt::ShiftModifier);colorEquals(window,"19E319");
    }else if(name=="hsb_clamp"){
        auto* field=colorField(window,false);for(int i=0;i<12;++i){key(field,Qt::Key_Right,Qt::ShiftModifier);key(field,Qt::Key_Up,Qt::ShiftModifier);}colorEquals(window,"00FF00");for(int i=0;i<12;++i)key(field,Qt::Key_Left,Qt::ShiftModifier);colorEquals(window,"FFFFFF");for(int i=0;i<12;++i)key(field,Qt::Key_Down,Qt::ShiftModifier);colorEquals(window,"000000");samplePalette(window,"FF0000");auto* hue=colorField(window,true);key(hue,Qt::Key_Down);colorEquals(window,"FF0000");for(int i=0;i<40;++i)key(hue,Qt::Key_Up,Qt::ShiftModifier);colorEquals(window,"FF0000");key(hue,Qt::Key_Down);colorEquals(window,"FF0004");
    }else if(name=="rgb_shift_steps"){
        samplePalette(window,"646464");auto* red=rgbField(window,0);key(red,Qt::Key_Up);colorEquals(window,"656464");key(red,Qt::Key_Up,Qt::ShiftModifier);colorEquals(window,"6F6464");key(red,Qt::Key_Down,Qt::ShiftModifier);colorEquals(window,"656464");
    }else if(name=="hue_retained_gray_black"){
        samplePalette(window,"808080");auto* hue=colorField(window,true);key(hue,Qt::Key_Up,Qt::ShiftModifier);colorEquals(window,"808080");samplePalette(window,"000000");auto* field=colorField(window,false);for(int i=0;i<10;++i)key(field,Qt::Key_Right,Qt::ShiftModifier);for(int i=0;i<10;++i)key(field,Qt::Key_Up,Qt::ShiftModifier);colorEquals(window,"00FF2A");
    }else if(name=="hex_keyboard_normalization"){
        auto* hex=palette(window).findChild<QLineEdit*>("paletteHex");require(hex,"Hex editor");hex->setFocus();hex->selectAll();QTest::keyClicks(hex,"#3aF");QTest::keyClick(hex,Qt::Key_Tab);colorEquals(window,"33AAFF");require(hex->text()=="33AAFF","Hex focus-out normalizes shorthand");hex->setFocus();hex->selectAll();QTest::keyClicks(hex,"invalid");QTest::keyClick(hex,Qt::Key_Tab);colorEquals(window,"33AAFF");require(hex->text()=="33AAFF","Invalid hex restores working color");
    }else if(name=="tab_traversal"){
        auto* field=colorField(window,false);field->setFocus();std::set<QWidget*> reached;for(int i=0;i<24;++i){auto* focus=QApplication::focusWidget();require(focus,"Palette focus exists");reached.insert(focus);QTest::keyClick(focus,Qt::Key_Tab);}require(reached.contains(field)&&reached.contains(colorField(window,true)),"Both HSB surfaces reachable by Tab");for(int i=0;i<3;++i)require(reached.contains(rgbField(window,i)),"Every RGB field reachable by Tab");require(reached.contains(palette(window).findChild<QLineEdit*>("paletteHex")),"Hex reachable by Tab");auto* buttons=palette(window).findChild<QDialogButtonBox*>();require(buttons&&reached.contains(buttons->button(QDialogButtonBox::Ok))&&reached.contains(buttons->button(QDialogButtonBox::Cancel)),"OK and Cancel reachable by Tab");
    }else if(name=="escape_cancel"){
        samplePalette(window,"A03070");key(colorField(window,true),Qt::Key_Escape);require(!visiblePalette(window),"Escape closes palette");paletteTrigger(window,"palette.foreground");colorEquals(window,"00FF00");
    }else if(name=="ok_commit_palette_only"){
        key(colorField(window,true),Qt::Key_Up);auto* buttons=palette(window).findChild<QDialogButtonBox*>();require(buttons,"Palette buttons");key(buttons->button(QDialogButtonBox::Ok),Qt::Key_Space);require(!visiblePalette(window),"Keyboard OK accepts picker");paletteTrigger(window,"palette.foreground");colorEquals(window,"00FF04");
    }else if(name=="background_independence"){
        palette(window).reject();paletteTrigger(window,"palette.background");colorEquals(window,"204060");samplePalette(window,"6F9F2F");palette(window).accept();paletteTrigger(window,"palette.foreground");colorEquals(window,"00FF00");palette(window).reject();paletteTrigger(window,"palette.background");colorEquals(window,"6F9F2F");
    }else if(name=="mask_target_closes_picker"){
        samplePalette(window,"A03070");paletteTrigger(window,"tool.brush");QComboBox* target=nullptr;for(auto* box:window.findChildren<QComboBox*>())if(box->accessibleName()=="Editing target")target=box;require(target&&target->isEnabled(),"Existing brush target control enabled");target->setCurrentIndex(1);QCoreApplication::processEvents();require(project.maskSelected&&!visiblePalette(window),"Mask target cancels RGB picker");target->setCurrentIndex(0);QCoreApplication::processEvents();paletteTrigger(window,"palette.foreground");colorEquals(window,"00FF00");
    }else if(name=="mouse_routes_unchanged"){
        auto* field=colorField(window,false);QTest::mouseClick(field,Qt::LeftButton,{},QPoint(128,128));colorEquals(window,"408040");auto* hue=colorField(window,true);QTest::mouseClick(hue,Qt::LeftButton,{},QPoint(17,128));colorEquals(window,"408080");
    }else throw std::runtime_error("Unknown palette keyboard case");
    immutablePalette(project,before);
}
ComPtr<IUIAutomationElement> paletteElement(Client& client,const char* name,int expected,QJsonObject& report){auto element=client.named(QString::fromUtf8(name));require(bool(element),"Named palette UIA control");int role=0;checked(element->get_CurrentControlType(&role),"Palette role");report[QString(name)+"_role"]=role;require(role==expected,"Palette control role");return element;}
ComPtr<IUIAutomationRangeValuePattern> range(IUIAutomationElement* element){ComPtr<IUIAutomationRangeValuePattern> out;checked(element->GetCurrentPatternAs(UIA_RangeValuePatternId,IID_PPV_ARGS(&out)),"Palette RangeValue");return out;}
double rangeValue(IUIAutomationRangeValuePattern* value){double out=0;checked(value->get_CurrentValue(&out),"Palette current value");return out;}
void rangeBounds(IUIAutomationRangeValuePattern* value,double lo,double hi){double a=0,b=0;checked(value->get_CurrentMinimum(&a),"Palette minimum");checked(value->get_CurrentMaximum(&b),"Palette maximum");require(a==lo&&b==hi,"Frozen palette value bounds");}
void uiaCase(const std::string& name,MainWindow& window,EditorProject& project,const Document& before,Client& client,QJsonObject& report){
    if(name=="hue_role_value"){
        auto hue=paletteElement(client,"Hue",UIA_SliderControlTypeId,report);auto value=range(hue.Get());rangeBounds(value.Get(),0,360);require(rangeValue(value.Get())==120,"Source green hue120degrees");double step=0;checked(value->get_CurrentSmallChange(&step),"Hue small change");require(step==1,"Hue keyboard step1degree");
    }else if(name=="hue_range_set"){
        auto hue=paletteElement(client,"Hue",UIA_SliderControlTypeId,report);auto value=range(hue.Get());checked(value->SetValue(180),"Set hue through UIA");drain(window);require(rangeValue(value.Get())==180,"Hue range readback");gui(window,[&]{colorEquals(window,"00FFFF");});
    }else if(name=="plane_axes_values"){
        for(const char* namePart:{"Saturation","Brightness"}){auto element=paletteElement(client,namePart,UIA_SliderControlTypeId,report);auto value=range(element.Get());rangeBounds(value.Get(),0,100);require(rangeValue(value.Get())==100,"Source full saturation/brightness percentage");}
    }else if(name=="plane_range_set"){
        auto saturation=range(paletteElement(client,"Saturation",UIA_SliderControlTypeId,report).Get());auto brightness=range(paletteElement(client,"Brightness",UIA_SliderControlTypeId,report).Get());checked(saturation->SetValue(50),"Set saturation through UIA");checked(brightness->SetValue(50),"Set brightness through UIA");drain(window);require(rangeValue(saturation.Get())==50&&rangeValue(brightness.Get())==50,"Plane axes retain precise control values");gui(window,[&]{colorEquals(window,"408040");});
    }else if(name=="disabled_rejects_set"){
        auto hue=paletteElement(client,"Hue",UIA_SliderControlTypeId,report);auto value=range(hue.Get());gui(window,[&]{colorField(window,true)->setEnabled(false);});BOOL enabled=TRUE;checked(hue->get_CurrentIsEnabled(&enabled),"Disabled hue state");require(!enabled,"Disabled hue exposed as disabled");report["write_hresult"]=double(value->SetValue(180));drain(window);require(rangeValue(value.Get())==120,"Disabled UIA setter cannot change hue");gui(window,[&]{colorEquals(window,"00FF00");});
    }else if(name=="nonfinite_rejects_set"){
        auto value=range(paletteElement(client,"Hue",UIA_SliderControlTypeId,report).Get());report["nan_hresult"]=double(value->SetValue(std::numeric_limits<double>::quiet_NaN()));report["infinity_hresult"]=double(value->SetValue(std::numeric_limits<double>::infinity()));drain(window);require(rangeValue(value.Get())==120,"Nonfinite writes preserve hue");gui(window,[&]{colorEquals(window,"00FF00");});
    }else if(name=="removed_lifetime"){
        auto element=paletteElement(client,"Hue",UIA_SliderControlTypeId,report);auto value=range(element.Get());QPointer<PaletteDialog> previous;QAccessible::Id oldId=0;
        gui(window,[&]{oldId=QAccessible::uniqueId(QAccessible::queryAccessibleInterface(colorField(window,true)));previous=&palette(window);palette(window).reject();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);report["destroyed_after_send"]=previous.isNull();});drain(window);
        gui(window,[&]{report["destroyed_after_drain"]=previous.isNull();report["qt_interface_gone"]=QAccessible::accessibleInterface(oldId)==nullptr;require(previous.isNull()&&QAccessible::accessibleInterface(oldId)==nullptr,"Destroyed palette removes QObject and Qt accessibility identity");});
        double current=0;report["removed_hresult"]=double(value->get_CurrentValue(&current));report["removed_current"]=current;
        VARIANT property;VariantInit(&property);const auto propertyStatus=element->GetCurrentPropertyValueEx(UIA_RangeValueValuePropertyId,TRUE,&property);BOOL unsupported=FALSE;
        const auto unsupportedStatus=SUCCEEDED(propertyStatus)?client.automation->CheckNotSupported(property,&unsupported):propertyStatus;
        report["ignore_default_hresult"]=double(propertyStatus);report["ignore_default_variant"]=int(property.vt);report["not_supported"]=bool(unsupported);VariantClear(&property);
        checked(propertyStatus,"Destroyed property lookup without defaults");checked(unsupportedStatus,"Reserved NotSupported comparison");require(unsupported,"Destroyed palette exposes no current range property without OS defaults");
        const auto write=value->SetValue(180);report["removed_write_hresult"]=double(write);require(write==HRESULT(UIA_E_INVALIDOPERATION)||write==HRESULT(UIA_E_ELEMENTNOTAVAILABLE),"Destroyed palette rejects retained setter");
        gui(window,[&]{paletteTrigger(window,"palette.foreground");colorEquals(window,"00FF00");immutablePalette(project,before);});
    }else if(name=="focus_and_keyboard"){
        auto hue=paletteElement(client,"Hue",UIA_SliderControlTypeId,report);checked(hue->SetFocus(),"UIA Hue SetFocus");drain(window);gui(window,[&]{auto* field=colorField(window,true);require(field->hasFocus(),"UIA focuses hue field");QTest::keyClick(field,Qt::Key_Up);colorEquals(window,"00FF04");});
    }else throw std::runtime_error("Unknown palette UIA case");
    gui(window,[&]{immutablePalette(project,before);});
}
}
int main(int argc,char** argv){
    QApplication app(argc,argv);std::cout<<std::unitbuf;if(argc!=3){std::cerr<<"Usage: palette_accessibility_tests CASE REPORT.json\n";return 2;}
    QDir().mkpath(QFileInfo(QString::fromUtf8(__FILE__)).dir().filePath("fixtures"));QTemporaryDir settings(QFileInfo(QString::fromUtf8(__FILE__)).dir().filePath("fixtures/settings-XXXXXX"));require(settings.isValid(),"Owned settings path");settings.setAutoRemove(false);QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    MainWindow window(true);auto& project=window.addProject(paletteDocument(),"Palette keyboard fixture");const auto before=*project.document;window.resize(1200,850);window.show();window.activateWindow();QTest::qWait(20);choosePalette(window,false,"00FF00");choosePalette(window,true,"204060");paletteTrigger(window,"palette.foreground");QTest::qWait(20);palette(window).raise();palette(window).activateWindow();QTest::qWait(30);std::cout<<"PALETTE_ACTIVE "<<(QApplication::activeWindow()==&palette(window))<<" FOCUS "<<(QApplication::focusWidget()?QApplication::focusWidget()->objectName().toStdString():"none")<<'\n';const auto hwnd=reinterpret_cast<HWND>(palette(window).winId());
    const std::set<std::string> native{"hue_role_value","hue_range_set","plane_axes_values","plane_range_set","disabled_rejects_set","nonfinite_rejects_set","removed_lifetime","focus_and_keyboard"};std::atomic<int> result{2};std::thread worker;
    QTimer::singleShot(100,&window,[&]{worker=std::thread([&]{QJsonObject report{{"schema","PALETTE_KEYBOARD_UIA_V1"},{"case",argv[1]},{"human_acceptance",false}};bool initialized=false;try{if(native.contains(argv[1])){checked(CoInitializeEx(nullptr,COINIT_MULTITHREADED),"Palette UIA MTA");initialized=true;Client client(hwnd);uiaCase(argv[1],window,project,before,client,report);}else gui(window,[&]{keyboardCase(argv[1],window,project,before);});report["status"]="passed";result=0;std::cout<<"PASS "<<argv[1]<<'\n';}catch(const std::exception& error){report["status"]="failed";report["error"]=error.what();result=1;std::cerr<<"FAIL "<<argv[1]<<": "<<error.what()<<'\n';}QFile output(QString::fromLocal8Bit(argv[2]));if(!output.open(QIODevice::WriteOnly)||output.write(QJsonDocument(report).toJson())<0)result=2;if(initialized)CoUninitialize();QMetaObject::invokeMethod(&window,[&]{app.exit(result.load());},Qt::QueuedConnection);});});app.exec();if(worker.joinable())worker.join();return result;
}
