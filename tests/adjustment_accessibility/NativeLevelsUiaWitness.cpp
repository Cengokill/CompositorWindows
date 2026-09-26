#include "ui/AdjustmentAdvancedControls.h"
#include "effects/Adjustments.h"
#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <Windows.h>
#include <objbase.h>
#include <UIAutomation.h>
#include <wrl/client.h>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace compositor;
using Microsoft::WRL::ComPtr;
namespace {
void ok(HRESULT result,const char* operation){if(FAILED(result))throw std::runtime_error(std::string(operation)+" HRESULT "+std::to_string(uint32_t(result)));}
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct ComScope {ComScope(){ok(CoInitializeEx(nullptr,COINIT_MULTITHREADED),"CoInitializeEx");}~ComScope(){CoUninitialize();}};
ComPtr<IUIAutomationElement> named(IUIAutomation* automation,IUIAutomationElement* root,const wchar_t* name){
    VARIANT text;VariantInit(&text);text.vt=VT_BSTR;text.bstrVal=SysAllocString(name);require(text.bstrVal,"Allocate owned UIA name");
    ComPtr<IUIAutomationCondition> match;const auto result=automation->CreatePropertyCondition(UIA_NamePropertyId,text,&match);VariantClear(&text);ok(result,"Name condition");
    ComPtr<IUIAutomationElement> element;ok(root->FindFirst(TreeScope_Descendants,match.Get(),&element),"Find owned handle");require(bool(element),"Named Levels handle exists in actual UIA tree");return element;
}
ComPtr<IUIAutomationRangeValuePattern> range(IUIAutomationElement* element){ComPtr<IUIAutomationRangeValuePattern> result;ok(element->GetCurrentPatternAs(UIA_RangeValuePatternId,IID_PPV_ARGS(&result)),"RangeValue pattern");require(bool(result),"Actual UIA RangeValue pattern exists");return result;}
void check(QJsonArray& checks,const char* id,bool condition){checks.append(QJsonObject{{"id",id},{"status",condition?"passed":"failed"}});}
}
int main(int argc,char** argv){
    QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);
    if(argc!=2){std::cerr<<"Provide OUTPUT.json\n";return 2;}
    LevelsAdvancedControls widget;widget.setWindowTitle("Owned Levels UIA witness");widget.setAdjustmentJson(effects::defaultAdjustmentJson("Levels"));widget.resize(520,400);widget.show();widget.activateWindow();
    const HWND ownedWindow=reinterpret_cast<HWND>(widget.winId());
    QJsonArray checks;QString error;
    std::thread worker;
    QTimer::singleShot(150,&app,[&]{worker=std::thread([&]{
        try{
            ComScope apartment;
            ComPtr<IUIAutomation> automation;ok(CoCreateInstance(CLSID_CUIAutomation,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&automation)),"Create UI Automation");
            ComPtr<IUIAutomationElement> root;ok(automation->ElementFromHandle(ownedWindow,&root),"Owned HWND root");require(bool(root),"Owned UIA root exists");int process{};ok(root->get_CurrentProcessId(&process),"Owned root process");require(process==int(GetCurrentProcessId()),"UIA root belongs to witness process");
            const wchar_t* names[]{L"Input black",L"Gamma",L"Input white",L"Output black",L"Output white"};
            const char* ids[]{"input_black","gamma","input_white","output_black","output_white"};
            const double currents[]{0,1,255,0,255},minimums[]{0,.1,1,0,0},maximums[]{254,9.99,255,255,255};
            for(int i=0;i<5;++i){auto element=named(automation.Get(),root.Get(),names[i]);CONTROLTYPEID type{};ok(element->get_CurrentControlType(&type),"Handle type");auto values=range(element.Get());double value{},minimum{},maximum{},step{};BOOL readOnly{};ok(values->get_CurrentValue(&value),"Current value");ok(values->get_CurrentMinimum(&minimum),"Minimum");ok(values->get_CurrentMaximum(&maximum),"Maximum");ok(values->get_CurrentSmallChange(&step),"Small change");ok(values->get_CurrentIsReadOnly(&readOnly),"Read-only state");
                const bool match=type==UIA_SliderControlTypeId&&!readOnly&&std::abs(value-currents[i])<1e-10&&std::abs(minimum-minimums[i])<1e-10&&std::abs(maximum-maximums[i])<1e-10&&std::abs(step-(i==1?.01:1))<1e-10;
                checks.append(QJsonObject{{"id",QString("uia_%1_range").arg(ids[i])},{"status",match?"passed":"failed"},{"control_type",type},{"value",value},{"minimum",minimum},{"maximum",maximum},{"small_change",step},{"read_only",bool(readOnly)}});
            }
            auto gamma=named(automation.Get(),root.Get(),L"Gamma");ok(range(gamma.Get())->SetValue(2.25),"Set Gamma value");double actual{};ok(range(gamma.Get())->get_CurrentValue(&actual),"Read changed Gamma");check(checks,"uia_gamma_set_value",std::abs(actual-2.25)<1e-10);
            ok(gamma->SetFocus(),"Focus Gamma");BOOL focused{};ok(gamma->get_CurrentHasKeyboardFocus(&focused),"Read Gamma focus");check(checks,"uia_gamma_focus",bool(focused));
            ok(range(named(automation.Get(),root.Get(),L"Input black").Get())->SetValue(32),"Set input black");
            ok(range(named(automation.Get(),root.Get(),L"Output black").Get())->SetValue(200),"Set output black");
            ok(range(named(automation.Get(),root.Get(),L"Output white").Get())->SetValue(20),"Set output white");
        }catch(const std::exception& failure){error=QString::fromUtf8(failure.what());}
        QMetaObject::invokeMethod(&app,[&]{app.quit();},Qt::QueuedConnection);
    });});
    app.exec();if(worker.joinable())worker.join();
    const auto settings=effects_tools::levelsFromAdjustmentJson(widget.adjustmentJson());const auto values=settings.ranges[0];
    check(checks,"uia_writes_production_settings",std::abs(values.gamma-2.25)<1e-10&&values.black==32&&values.outputBlack==200&&values.outputWhite==20);
    bool passed=error.isEmpty()&&checks.size()==8;for(const auto& item:checks)passed=passed&&item.toObject()["status"]=="passed";
    const QJsonObject report{{"status",passed?"passed":"failed"},{"error",error},{"checks",checks},{"scope","Task-launched owned HWND; real Windows UI Automation; no Narrator claim"}};
    QFile output(QString::fromLocal8Bit(argv[1]));if(!output.open(QIODevice::WriteOnly)||output.write(QJsonDocument(report).toJson())<0)return 2;
    std::cout<<(passed?"PASS":"FAIL")<<" native_levels_uia "<<checks.size()<<" checks\n";return passed?0:1;
}
