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
Document fixtureDocument(){Document d;d.id=newId();d.width=64;d.height=48;
    Layer backdrop;backdrop.id=newId();backdrop.name="Backdrop";backdrop.transform={0,0,64,48};backdrop.raster=Raster::filled(64,48,{50,70,90,255});
    Layer folder;folder.id=newId();folder.name="Folder";folder.group=true;
    Layer foreground;foreground.id=newId();foreground.name="Foreground with mask";foreground.parentId=folder.id;foreground.transform={8,8,32,24};foreground.raster=Raster::filled(32,24,{180,90,40,255});
    foreground.mask=Mask{std::make_shared<GrayRaster>(GrayRaster{32,24,std::vector<uint8_t>(768,255)})};d.layers={backdrop,folder,foreground};return d;
}
QAction* command(MainWindow& window,const char* id){for(auto* action:window.findChildren<QAction*>())if(action->property("commandId").toString()==id)return action;throw std::runtime_error("missing command");}
template<class Function>void gui(MainWindow& window,Function fn){std::exception_ptr error;
    require(QMetaObject::invokeMethod(&window,[&]{try{fn();}catch(...){error=std::current_exception();}},Qt::BlockingQueuedConnection),"GUI dispatch failed");if(error)std::rethrow_exception(error);
}
bool ownedBy(QObject* object,QObject* owner){for(auto* current=object;current;current=current->parent())if(current==owner)return true;return false;}
bool focusMatches(const QJsonObject& qt,const QJsonObject& actual){
    const auto name=qt["accessible_name"].toString();auto text=qt["button_text"].toString();text.remove('&');
    for(const auto& value:actual["candidates"].toArray()){const auto item=value.toObject();const auto actualName=item["name"].toString();
        if(!name.isEmpty()&&actualName==name)return true;
        if(!text.isEmpty()&&actualName==text)return true;
        if(qt["class"].toString()=="QTabBar"&&item["control_type"].toInt()==UIA_TabItemControlTypeId)return true;
    }return false;
}
QJsonObject qtFocus(QWidget* scope){auto* widget=QApplication::focusWidget();if(!widget)return {{"missing",true}};
    QJsonObject out{{"class",widget->metaObject()->className()},{"object_name",widget->objectName()},{"accessible_name",widget->accessibleName()},
        {"in_scope",widget==scope||scope->isAncestorOf(widget)},{"focus_policy",int(widget->focusPolicy())},
        {"identity",QString::number(reinterpret_cast<quintptr>(widget),16)}};
    if(auto* button=qobject_cast<QAbstractButton*>(widget))out["button_text"]=button->text();return out;
}
QJsonObject traversal(Client& client,MainWindow& window,IUIAutomationElement* rootElement,QWidget* scope,bool backward){
    QJsonArray steps;std::set<QString> visited;bool cycle=false;bool contained=true;int missingFocus=0;
    for(int i=0;i<100;++i){QJsonObject qt;gui(window,[&]{qt=qtFocus(scope);});const auto identity=qt["identity"].toString();
        if(!identity.isEmpty()&&!visited.insert(identity).second){cycle=true;break;}
        const auto actual=client.focused(rootElement);const bool matches=focusMatches(qt,actual);if(!matches)++missingFocus;contained=contained&&qt["in_scope"].toBool();
        steps.append(QJsonObject{{"index",i},{"qt",qt},{"uia",actual},{"uia_matches_qt_control",matches}});
        gui(window,[&]{auto* focus=QApplication::focusWidget();require(focus&&(focus==scope||scope->isAncestorOf(focus)),"keyboard target outside owned scope");QTest::keyClick(focus,Qt::Key_Tab,backward?Qt::ShiftModifier:Qt::NoModifier);});
    }
    return {{"direction",backward?"shift_tab":"tab"},{"steps",steps},{"cycle_detected",cycle},{"within_scope",contained},{"uia_focus_missing_count",missingFocus}};
}
bool has(const QJsonArray& tree,const QString& name){for(const auto& value:tree)if(value.toObject()["name"].toString()==name)return true;return false;}
bool role(const QJsonArray& tree,const QString& name,std::initializer_list<int> types){for(const auto& value:tree){const auto record=value.toObject();if(record["name"].toString()==name&&std::find(types.begin(),types.end(),record["control_type"].toInt())!=types.end())return true;}return false;}
void write(const QString& path,const QJsonObject& object){QFile file(path);require(file.open(QIODevice::WriteOnly),"cannot write evidence");require(file.write(QJsonDocument(object).toJson())>=0,"evidence write failed");}
}
int main(int argc,char** argv){
    QApplication app(argc,argv);if(argc!=2){std::cerr<<"Usage: accessibility_witness OUTPUT.json\n";return 2;}
    app.setQuitOnLastWindowClosed(false);const QString output=QString::fromLocal8Bit(argv[1]);MainWindow window(true);
    auto& alpha=window.addProject(fixtureDocument(),"Access Alpha");window.addProject(fixtureDocument(),"Access Beta");
    window.findChild<QTabWidget*>()->setCurrentWidget(alpha.page);window.setWindowTitle("Compositor accessibility witness");window.show();window.activateWindow();
    const auto hwnd=reinterpret_cast<HWND>(window.winId());std::thread worker;std::atomic<int> result{2};
    QTimer::singleShot(300,&window,[&]{worker=std::thread([&]{QJsonObject report{{"schema_version",1},{"pid",int(GetCurrentProcessId())},{"qt_version",qVersion()},
        {"transport","Windows UIA COM on MTA; Qt-targeted keyboard events"},{"narrator_acceptance","unperformed"},{"mac_reference","blocked_reference"}};
        QJsonArray checks;auto check=[&](const char* id,bool pass,const QString& detail){checks.append(QJsonObject{{"id",id},{"status",pass?"passed":"failed"},{"detail",detail}});};
        bool initialized=false;
        try{checked(CoInitializeEx(nullptr,COINIT_MULTITHREADED),"CoInitializeEx");initialized=true;
            {Client client;auto mainRoot=client.root(hwnd);gui(window,[&]{window.activateWindow();alpha.canvas->setFocus(Qt::OtherFocusReason);});
            const auto mainTree=client.snapshot(mainRoot.Get());report["main_tree"]=mainTree;
            for(const auto* name:{"Move (V)","Hand (H)","Marquee (M)","Lasso (L)","Polygon","Wand (W)","Brush (B)","Eraser (E)","Clone (S)","Heal (J)","Retouch (R)","Gradient (G)","Shape (U)","Crop (C)","Eyedropper (I)","Zoom (Z)"})check(name,role(mainTree,name,{UIA_ButtonControlTypeId,UIA_RadioButtonControlTypeId,UIA_CheckBoxControlTypeId}),"tool name and actionable control role");
            check("canvas_name",has(mainTree,"Image canvas"),"canvas discoverable by accessible name");
            check("canvas_image_role",role(mainTree,"Image canvas",{UIA_ImageControlTypeId}),"pinned EditorCanvas.swift:509 exposes image role");
            check("project_tabs",role(mainTree,"Access Alpha",{UIA_TabItemControlTypeId})&&role(mainTree,"Access Beta",{UIA_TabItemControlTypeId}),"both project labels exposed as tab items");
            check("layer_list",role(mainTree,"Layer list",{UIA_TreeControlTypeId,UIA_TableControlTypeId,UIA_DataGridControlTypeId,UIA_ListControlTypeId}),"named hierarchical/data container");
            check("layer_row_names",has(mainTree,"Backdrop")&&has(mainTree,"Folder")&&has(mainTree,"Foreground with mask"),"all three fixture layers represented");
            check("layer_image_name",has(mainTree,"Select image: Foreground with mask"),"NativeLayerList.swift:572 exact contextual source label");
            check("layer_mask_name",has(mainTree,"Select mask: Foreground with mask"),"NativeLayerList.swift:573 exact contextual source label");
            check("layer_link_name",has(mainTree,"Unlink mask: Foreground with mask"),"NativeLayerList.swift:569 exact contextual source label");
            check("layer_visibility_name",has(mainTree,"Hide Foreground with mask"),"NativeLayerList.swift:586 exact contextual source label");
            auto forward=traversal(client,window,mainRoot.Get(),&window,false);auto backward=traversal(client,window,mainRoot.Get(),&window,true);
            report["main_focus"]=QJsonArray{forward,backward};check("main_focus_cycle",forward["cycle_detected"].toBool()&&backward["cycle_detected"].toBool(),"100-event frozen cap in each direction");
            check("main_uia_focus",forward["uia_focus_missing_count"].toInt()==0&&backward["uia_focus_missing_count"].toInt()==0,"every Qt focus step is exposed as focused inside owned UIA root");
            std::optional<Document> before;size_t undo=0;gui(window,[&]{before=alpha.document;undo=alpha.history.undoCount();auto* action=command(window,"adjust.exposure");require(action->isEnabled(),"Exposure action enabled");
                QTimer::singleShot(0,&window,[action]{action->trigger();});});
            QDialog* dialog=nullptr;HWND dialogHwnd=nullptr;
            for(int i=0;i<100&&!dialog;++i){gui(window,[&]{auto* candidate=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(candidate&&ownedBy(candidate,&window)){dialog=candidate;dialogHwnd=reinterpret_cast<HWND>(candidate->winId());}});if(!dialog)std::this_thread::sleep_for(std::chrono::milliseconds(20));}
            require(dialog&&dialogHwnd,"Exposure dialog opened within 2 seconds");auto dialogRoot=client.root(dialogHwnd);const auto dialogTree=client.snapshot(dialogRoot.Get());report["dialog_tree"]=dialogTree;
            for(const auto* name:{"Exposure","Offset","Gamma"})check(name,role(dialogTree,name,{UIA_SpinnerControlTypeId,UIA_EditControlTypeId}),"named numeric adjustment field");
            check("dialog_preview",role(dialogTree,"Preview",{UIA_CheckBoxControlTypeId}),"named Preview checkbox");
            check("dialog_buttons",role(dialogTree,"Apply",{UIA_ButtonControlTypeId})&&role(dialogTree,"Cancel",{UIA_ButtonControlTypeId}),"named Apply and Cancel");
            auto dialogForward=traversal(client,window,dialogRoot.Get(),dialog,false);auto dialogBackward=traversal(client,window,dialogRoot.Get(),dialog,true);report["dialog_focus"]=QJsonArray{dialogForward,dialogBackward};
            check("dialog_focus_cycle",dialogForward["cycle_detected"].toBool()&&dialogBackward["cycle_detected"].toBool()&&dialogForward["within_scope"].toBool()&&dialogBackward["within_scope"].toBool(),"Tab and Shift+Tab remain inside Exposure and cycle");
            check("dialog_uia_focus",dialogForward["uia_focus_missing_count"].toInt()==0&&dialogBackward["uia_focus_missing_count"].toInt()==0,"Windows UIA observes every dialog focus step");
            auto cancel=client.named(dialogRoot.Get(),L"Cancel");ComPtr<IUIAutomationInvokePattern> invoke;HRESULT invocation=UIA_E_NOTSUPPORTED;
            if(cancel&&SUCCEEDED(cancel->GetCurrentPatternAs(UIA_InvokePatternId,IID_PPV_ARGS(&invoke)))&&invoke)invocation=invoke->Invoke();
            check("uia_cancel_invoke",SUCCEEDED(invocation),QString("HRESULT 0x%1").arg(static_cast<unsigned long>(invocation),0,16));
            bool closed=false;for(int i=0;i<100&&!closed;++i){gui(window,[&]{closed=!QApplication::activeModalWidget();});if(!closed)std::this_thread::sleep_for(std::chrono::milliseconds(20));}
            check("cancel_closed",closed,"UIA Invoke closes actual owned Exposure dialog within 2 seconds");
            gui(window,[&]{check("cancel_immutable",alpha.document==before&&alpha.history.undoCount()==undo,"Cancel preserves canonical document and undo count");});
            }
            bool success=true;for(const auto& entry:checks)success=success&&entry.toObject()["status"].toString()=="passed";report["status"]=success?"passed":"failed";result=success?0:1;
        }catch(const std::exception& e){report["status"]="error";report["error"]=e.what();result=2;}
        if(initialized)CoUninitialize();report["checks"]=checks;
        try{write(output,report);}catch(const std::exception& e){std::cerr<<e.what()<<'\n';result=2;}
        QMetaObject::invokeMethod(&window,[&]{if(auto* modal=qobject_cast<QDialog*>(QApplication::activeModalWidget()))if(ownedBy(modal,&window))modal->reject();app.quit();},Qt::QueuedConnection);
    });});
    app.exec();if(worker.joinable())worker.join();std::cout<<"accessibility witness exit="<<result.load()<<'\n';return result.load();
}
