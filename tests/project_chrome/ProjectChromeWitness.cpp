#include "ui/MainWindow.h"
#include <QApplication>
#include <QAbstractButton>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QTest>
#include <QTimer>
#include <QTabBar>
#include <QFileInfo>
#include <QSettings>
#include <Windows.h>
#include <UIAutomation.h>
#include <wrl/client.h>
#include <atomic>
#include <chrono>
#include <iostream>
#include <set>
#include <thread>

using namespace compositor;
using Microsoft::WRL::ComPtr;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void checked(HRESULT result,const char* operation){if(FAILED(result))throw std::runtime_error(std::string(operation)+" HRESULT="+std::to_string(static_cast<unsigned long>(result)));}
QJsonValue property(IUIAutomationElement* element,PROPERTYID id){
    VARIANT value;VariantInit(&value);const auto result=element->GetCurrentPropertyValue(id,&value);
    if(FAILED(result))return QJsonObject{{"hresult",QString::number(static_cast<unsigned long>(result),16)}};
    QJsonValue out;
    switch(value.vt){
    case VT_BSTR:out=QString::fromWCharArray(value.bstrVal);break;
    case VT_BOOL:out=value.boolVal==VARIANT_TRUE;break;
    case VT_I4:out=int(value.lVal);break;
    case VT_R8:out=value.dblVal;break;
    case VT_EMPTY:out=QJsonValue::Null;break;
    default:out=QJsonObject{{"variant_type",int(value.vt)}};break;
    }
    VariantClear(&value);return out;
}
const std::pair<const char*,PROPERTYID> properties[]{
    {"name",UIA_NamePropertyId},{"automation_id",UIA_AutomationIdPropertyId},
    {"control_type",UIA_ControlTypePropertyId},{"localized_role",UIA_LocalizedControlTypePropertyId},
    {"class_name",UIA_ClassNamePropertyId},{"framework",UIA_FrameworkIdPropertyId},
    {"access_key",UIA_AccessKeyPropertyId},{"accelerator",UIA_AcceleratorKeyPropertyId},
    {"focusable",UIA_IsKeyboardFocusablePropertyId},{"focused",UIA_HasKeyboardFocusPropertyId},
    {"enabled",UIA_IsEnabledPropertyId},{"offscreen",UIA_IsOffscreenPropertyId},
    {"help",UIA_HelpTextPropertyId},{"provider",UIA_ProviderDescriptionPropertyId},
    {"invoke",UIA_IsInvokePatternAvailablePropertyId},{"toggle",UIA_IsTogglePatternAvailablePropertyId},
    {"selection",UIA_IsSelectionPatternAvailablePropertyId},{"selection_item",UIA_IsSelectionItemPatternAvailablePropertyId},
    {"value",UIA_IsValuePatternAvailablePropertyId},{"range_value",UIA_IsRangeValuePatternAvailablePropertyId},
    {"expand_collapse",UIA_IsExpandCollapsePatternAvailablePropertyId},{"grid_item",UIA_IsGridItemPatternAvailablePropertyId}
};
struct Client {
    ComPtr<IUIAutomation> automation;
    DWORD process=GetCurrentProcessId();
    Client(){checked(CoCreateInstance(CLSID_CUIAutomation8,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&automation)),"CoCreateInstance");
        ComPtr<IUIAutomation2> timed;checked(automation.As(&timed),"IUIAutomation2");
        checked(timed->put_ConnectionTimeout(1500),"ConnectionTimeout");checked(timed->put_TransactionTimeout(3000),"TransactionTimeout");}
    void own(IUIAutomationElement* element){int id=0;checked(element->get_CurrentProcessId(&id),"ProcessId");require(id==int(process),"UIA element outside owned process");}
    ComPtr<IUIAutomationElement> root(HWND window){DWORD id=0;GetWindowThreadProcessId(window,&id);require(id==process,"HWND outside owned process");
        ComPtr<IUIAutomationElement> result;checked(automation->ElementFromHandle(window,&result),"ElementFromHandle");require(bool(result),"missing owned UIA root");own(result.Get());return result;}
    QJsonObject describe(IUIAutomationElement* element){own(element);QJsonObject out;
        for(const auto& [name,id]:properties)out[name]=property(element,id);
        RECT rect{};const auto hr=element->get_CurrentBoundingRectangle(&rect);if(SUCCEEDED(hr))out["bounds"]=QJsonArray{int(rect.left),int(rect.top),int(rect.right),int(rect.bottom)};
        return out;
    }
    QJsonArray snapshot(IUIAutomationElement* rootElement){
        ComPtr<IUIAutomationTreeWalker> walker;checked(automation->get_ControlViewWalker(&walker),"ControlViewWalker");QJsonArray out;
        std::function<void(IUIAutomationElement*,int,int)> visit=[&](IUIAutomationElement* element,int parent,int depth){
            require(depth<=32&&out.size()<2000,"frozen UIA tree cap reached");auto record=describe(element);const int index=int(out.size());record["index"]=index;record["parent"]=parent;out.append(record);
            ComPtr<IUIAutomationElement> child;checked(walker->GetFirstChildElement(element,&child),"FirstChild");while(child){visit(child.Get(),index,depth+1);ComPtr<IUIAutomationElement> next;checked(walker->GetNextSiblingElement(child.Get(),&next),"NextSibling");child=std::move(next);}
        };visit(rootElement,-1,0);return out;
    }
    ComPtr<IUIAutomationElement> named(IUIAutomationElement* rootElement,const wchar_t* name){VARIANT value;VariantInit(&value);value.vt=VT_BSTR;value.bstrVal=SysAllocString(name);
        ComPtr<IUIAutomationCondition> condition;const auto hr=automation->CreatePropertyCondition(UIA_NamePropertyId,value,&condition);VariantClear(&value);checked(hr,"Name condition");
        ComPtr<IUIAutomationElement> result;checked(rootElement->FindFirst(TreeScope_Subtree,condition.Get(),&result),"Find named");if(result)own(result.Get());return result;
    }
    QJsonObject focused(IUIAutomationElement* rootElement){VARIANT value;VariantInit(&value);value.vt=VT_BOOL;value.boolVal=VARIANT_TRUE;
        ComPtr<IUIAutomationCondition> condition;checked(automation->CreatePropertyCondition(UIA_HasKeyboardFocusPropertyId,value,&condition),"Focus condition");
        ComPtr<IUIAutomationElementArray> result;checked(rootElement->FindAll(TreeScope_Subtree,condition.Get(),&result),"Owned focused");int count=0;checked(result->get_Length(&count),"Focus count");QJsonArray candidates;
        for(int i=0;i<count;++i){ComPtr<IUIAutomationElement> item;checked(result->GetElement(i,&item),"Focus item");candidates.append(describe(item.Get()));}return {{"missing",count==0},{"candidates",candidates}};
    }
};
Document fixtureDocument(){Document d;d.id=newId();d.width=100;d.height=80;Layer l;l.id=newId();l.name="Layer";l.transform={0,0,100,80};l.raster=Raster::filled(100,80,{50,70,90,255});d.layers={l};return d;}
QAction* command(MainWindow& w,const char* id){for(auto* a:w.findChildren<QAction*>())if(a->property("commandId").toString()==id)return a;throw std::runtime_error("missing command");}
template<class F>void gui(MainWindow& w,F fn){std::exception_ptr error;require(QMetaObject::invokeMethod(&w,[&]{try{fn();}catch(...){error=std::current_exception();}},Qt::BlockingQueuedConnection),"GUI dispatch");if(error)std::rethrow_exception(error);}
bool role(const QJsonArray& tree,const QString& name,int type){for(const auto& value:tree){auto r=value.toObject();if(r["name"].toString()==name&&r["control_type"].toInt()==type)return true;}return false;}
void write(const QString& path,const QJsonObject& data){QFile f(path);require(f.open(QIODevice::WriteOnly),"report open");require(f.write(QJsonDocument(data).toJson())>=0,"report write");}
}
int main(int argc,char** argv){QApplication app(argc,argv);if(argc!=2)return 2;app.setQuitOnLastWindowClosed(false);QString output=QString::fromLocal8Bit(argv[1]);QCoreApplication::setOrganizationName("CompositorFixture");QCoreApplication::setApplicationName("ProjectChromeUIA");QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,QFileInfo(output).absolutePath()+"/settings-uia");QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,QFileInfo(output).absolutePath()+"/settings-uia");MainWindow w(true);auto& alpha=w.addProject(fixtureDocument(),"Access Alpha");auto& beta=w.addProject(fixtureDocument(),"Access Beta");auto* tabs=w.findChild<QTabWidget*>();tabs->setCurrentIndex(0);w.setWindowTitle("Compositor project chrome witness");w.show();w.activateWindow();const auto hwnd=reinterpret_cast<HWND>(w.winId());std::thread worker;std::atomic<int> result{2};
QTimer::singleShot(300,&w,[&]{worker=std::thread([&]{QJsonObject report{{"schema_version",1},{"pid",int(GetCurrentProcessId())},{"transport","Windows UIA COM on MTA; owned Qt fixture state"},{"narrator_acceptance","unperformed"}};QJsonArray checks;auto check=[&](const char* id,bool pass,const QString& detail){checks.append(QJsonObject{{"id",id},{"status",pass?"passed":"failed"},{"detail",detail}});};bool initialized=false;
try{checked(CoInitializeEx(nullptr,COINIT_MULTITHREADED),"COM");initialized=true;{Client client;auto root=client.root(hwnd);auto tree=client.snapshot(root.Get());report["initial_tree"]=tree;
check("project_tabs_named",role(tree,"Project tabs",UIA_TabControlTypeId),"ProjectTabs.swift:54 named project tab container");
check("close_alpha_named",role(tree,"Close Access Alpha",UIA_ButtonControlTypeId),"ProjectTabs.swift:116 contextual close button name and role");
check("close_beta_named",role(tree,"Close Access Beta",UIA_ButtonControlTypeId),"Each close action identifies its own project");
gui(w,[&]{beta.defaultTitle="Renamed Beta";tabs->setCurrentIndex(1);tabs->setCurrentIndex(0);});tree=client.snapshot(root.Get());report["renamed_tree"]=tree;
check("close_tracks_title",role(tree,"Close Renamed Beta",UIA_ButtonControlTypeId)&&!role(tree,"Close Access Beta",UIA_ButtonControlTypeId),"Close labels follow a changed session title without stale identity");
gui(w,[&]{command(w,"tool.gradient")->trigger();alpha.canvas->pointerDown({8,30},{});alpha.canvas->pointerMove({80,30},{});alpha.canvas->pointerUp({80,30},{});require(command(w,"gradient.apply")->isEnabled(),"pending gradient fixture");});tree=client.snapshot(root.Get());report["pending_tree"]=tree;
auto closeA=client.named(root.Get(),L"Close Access Alpha");auto closeB=client.named(root.Get(),L"Close Renamed Beta");check("close_disabled_pending",closeA&&closeB&&!property(closeA.Get(),UIA_IsEnabledPropertyId).toBool()&&!property(closeB.Get(),UIA_IsEnabledPropertyId).toBool(),"Source close buttons disabled while workspace cannot switch");
gui(w,[&]{command(w,"gradient.cancel")->trigger();require(!command(w,"gradient.apply")->isEnabled(),"gradient cancellation");});closeA=client.named(root.Get(),L"Close Access Alpha");closeB=client.named(root.Get(),L"Close Renamed Beta");check("close_restored_after_cancel",closeA&&closeB&&property(closeA.Get(),UIA_IsEnabledPropertyId).toBool()&&property(closeB.Get(),UIA_IsEnabledPropertyId).toBool(),"Close buttons reenable after draft cancellation");
const auto original=alpha.document;ComPtr<IUIAutomationInvokePattern> invoke;HRESULT invoked=UIA_E_NOTSUPPORTED;if(closeB&&SUCCEEDED(closeB->GetCurrentPatternAs(UIA_InvokePatternId,IID_PPV_ARGS(&invoke)))&&invoke)invoked=invoke->Invoke();bool closed=false;for(int i=0;i<100&&!closed;++i){gui(w,[&]{closed=tabs->count()==1;});if(!closed)std::this_thread::sleep_for(std::chrono::milliseconds(20));}
check("close_invoke_matching_project",SUCCEEDED(invoked)&&closed,QString("UIA Invoke closes named non-current project; HRESULT0x%1").arg(static_cast<unsigned long>(invoked),0,16));gui(w,[&]{check("close_preserves_other_project",closed&&tabs->currentWidget()==alpha.page&&alpha.document==original&&alpha.history.undoCount()==0,"Closing Beta retains Alpha identity, canonical document and history");});report["final_tree"]=client.snapshot(root.Get());}
bool passed=true;for(const auto& c:checks)passed=passed&&c.toObject()["status"].toString()=="passed";report["status"]=passed?"passed":"failed";result=passed?0:1;
}catch(const std::exception& e){report["status"]="error";report["error"]=e.what();result=2;}if(initialized)CoUninitialize();report["checks"]=checks;try{write(output,report);}catch(...){result=2;}QMetaObject::invokeMethod(&w,[&]{app.exit();},Qt::QueuedConnection);});});app.exec();if(worker.joinable())worker.join();std::cout<<"project chrome UIA exit="<<result.load()<<'\n';return result.load();}