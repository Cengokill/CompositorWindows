#include "ui/MainWindow.h"
#include "ui/LayerPanel.h"
#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QTest>
#include <QSignalBlocker>
#include <Windows.h>
#include <UIAutomation.h>
#include <wrl/client.h>
#include <atomic>
#include <iostream>
#include <thread>

using namespace compositor;
using Microsoft::WRL::ComPtr;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void checked(HRESULT result,const char* message){if(FAILED(result))throw std::runtime_error(std::string(message)+" HRESULT="+std::to_string(static_cast<unsigned long>(result)));}
template<class Fn>void gui(MainWindow& window,Fn fn){std::exception_ptr error;require(QMetaObject::invokeMethod(&window,[&]{try{fn();}catch(...){error=std::current_exception();}},Qt::BlockingQueuedConnection),"GUI dispatch");if(error)std::rethrow_exception(error);}
struct Client {
    ComPtr<IUIAutomation> automation;
    ComPtr<IUIAutomationElement> root;
    explicit Client(HWND hwnd){
        DWORD pid=0;GetWindowThreadProcessId(hwnd,&pid);require(pid==GetCurrentProcessId(),"HWND belongs to fixture");
        checked(CoCreateInstance(CLSID_CUIAutomation8,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&automation)),"UIA create");
        ComPtr<IUIAutomation2> timed;checked(automation.As(&timed),"UIA2");checked(timed->put_ConnectionTimeout(1500),"connection timeout");checked(timed->put_TransactionTimeout(3000),"transaction timeout");
        checked(automation->ElementFromHandle(hwnd,&root),"owned root");
    }
    ComPtr<IUIAutomationElement> named(const QString& name, bool buttonOnly=false){
        VARIANT value;VariantInit(&value);value.vt=VT_BSTR;value.bstrVal=SysAllocString(reinterpret_cast<const wchar_t*>(name.utf16()));
        ComPtr<IUIAutomationCondition> condition;const auto hr=automation->CreatePropertyCondition(UIA_NamePropertyId,value,&condition);VariantClear(&value);checked(hr,"name condition");
        if(buttonOnly){VARIANT role;VariantInit(&role);role.vt=VT_I4;role.lVal=UIA_ButtonControlTypeId;ComPtr<IUIAutomationCondition> roleCondition, both;checked(automation->CreatePropertyCondition(UIA_ControlTypePropertyId,role,&roleCondition),"role condition");checked(automation->CreateAndCondition(condition.Get(),roleCondition.Get(),&both),"name and Button condition");condition=both;}
        ComPtr<IUIAutomationElement> element;checked(root->FindFirst(TreeScope_Subtree,condition.Get(),&element),"find owned name");
        if(element){int pid=0;checked(element->get_CurrentProcessId(&pid),"element PID");require(pid==int(GetCurrentProcessId()),"element belongs to fixture");}return element;
    }
};
ComPtr<IUIAutomationTogglePattern> togglePattern(IUIAutomationElement* element){require(element!=nullptr,"named visibility control");ComPtr<IUIAutomationTogglePattern> pattern;checked(element->GetCurrentPatternAs(UIA_TogglePatternId,IID_PPV_ARGS(&pattern)),"Toggle pattern");return pattern;}
QString nameOf(IUIAutomationElement* element){BSTR value=nullptr;checked(element->get_CurrentName(&value),"current Name");const auto result=QString::fromWCharArray(value);SysFreeString(value);return result;}
QTreeWidget* tree(MainWindow& window){auto* result=window.findChild<QTreeWidget*>("layersTree");require(result!=nullptr,"layer tree");return result;}
QTreeWidgetItem* item(MainWindow& window,const char* id){for(QTreeWidgetItemIterator it(tree(window));*it;++it)if((*it)->data(0,Qt::UserRole)==id)return *it;throw std::runtime_error("fixture layer row");}
const Layer& layer(const EditorProject& project,const char* id){require(project.document.has_value(),"fixture document");for(const auto& l:project.document->layers)if(l.id==id)return l;throw std::runtime_error("fixture layer");}
void command(MainWindow& window,const char* id){for(auto* action:window.findChildren<QAction*>())if(action->property("commandId")==id){require(action->isEnabled(),"history command enabled");action->trigger();return;}throw std::runtime_error("history command");}
Document fixture(){Document d;d.id=newId();d.width=64;d.height=48;Layer group;group.id="group";group.name="Folder";group.group=true;Layer image;image.id="foreground";image.name="Foreground with mask";image.parentId="group";image.transform={8,8,32,24};image.raster=Raster::filled(32,24,{180,90,40,255});image.mask=Mask{std::make_shared<GrayRaster>(GrayRaster{32,24,std::vector<uint8_t>(768,255)})};group.mask=image.mask;Layer plain=image;plain.id="plain";plain.name="Plain image";plain.parentId.clear();plain.mask.reset();Layer adjustment;adjustment.id="adjustment";adjustment.name="Exposure layer";adjustment.adjustmentJson=R"({"kind":"Exposure"})";adjustment.mask=image.mask;d.layers={group,image,plain,adjustment};return d;}
}
namespace {
const QString imageName="Select image: Foreground with mask";
const QString maskName="Select mask: Foreground with mask";
const QString unlinkName="Unlink mask: Foreground with mask";
const QString linkName="Link mask: Foreground with mask";
ComPtr<IUIAutomationElement> button(Client& client,const QString& name,QJsonObject& report){
    auto result=client.named(name,true);
    if(!result){auto existing=client.named(name);if(existing){int type=0;checked(existing->get_CurrentControlType(&type),"observed role");report["observed_control_type"]=type;report["observed_name"]=nameOf(existing.Get());}throw std::runtime_error("Missing named UIA Button: "+name.toStdString());}
    return result;
}
ComPtr<IUIAutomationInvokePattern> invokePattern(IUIAutomationElement* element){ComPtr<IUIAutomationInvokePattern> pattern;checked(element->GetCurrentPatternAs(UIA_InvokePatternId,IID_PPV_ARGS(&pattern)),"Invoke pattern");return pattern;}
void drain(MainWindow& window){gui(window,[]{QCoreApplication::processEvents();});}
void selectFixture(MainWindow& window,EditorProject& project,const char* id,bool mask){project.active=id;project.selected={id};project.maskSelected=mask;ui::LayerPanelController::find(tree(window))->updateSelection();}
void assertTarget(const EditorProject& project,const char* id,bool mask){require(project.active==id&&project.selected==std::vector<std::string>{id}&&project.maskSelected==mask,"single layer edit target");}
void mouse(MainWindow& window,const char* id,int column){auto* list=tree(window);const auto rect=list->visualItemRect(item(window,id));const QPoint point(list->columnViewportPosition(column)+list->columnWidth(column)/2,rect.center().y());require(list->viewport()->rect().contains(point),"mouse target visible");QTest::mouseClick(list->viewport(),Qt::LeftButton,Qt::NoModifier,point);}
void perform(const std::string& which,MainWindow& window,EditorProject& project,Client& client,QJsonObject& report){
    if(which=="contextual_names"){
        for(const auto& name:{imageName,maskName,unlinkName})require(bool(client.named(name)),"exact source contextual label");require(bool(client.named("Foreground with mask")),"plain row Name retained");return;
    }
    if(which=="image_button"||which=="mask_button"||which=="link_button"){
        const auto name=which=="image_button"?imageName:which=="mask_button"?maskName:unlinkName;auto element=button(client,name,report);int role=0;checked(element->get_CurrentControlType(&role),"Button role");require(role==UIA_ButtonControlTypeId,"source NSButton maps to UIA Button");RECT rect{};checked(element->get_CurrentBoundingRectangle(&rect),"button rectangle");require(rect.right>rect.left&&rect.bottom>rect.top,"painted button bounds");BOOL enabled=FALSE,offscreen=TRUE;checked(element->get_CurrentIsEnabled(&enabled),"button enabled");checked(element->get_CurrentIsOffscreen(&offscreen),"button offscreen");require(enabled&&!offscreen,"button exposed and enabled");require(bool(invokePattern(element.Get())),"Button Invoke available");return;
    }
    if(which=="image_invoke"||which=="mask_invoke"){
        const bool mask=which=="mask_invoke";Document before;size_t count=0;gui(window,[&]{selectFixture(window,project,mask?"group":"foreground",!mask);before=*project.document;count=project.history.undoCount();});auto control=button(client,mask?maskName:imageName,report);checked(invokePattern(control.Get())->Invoke(),"select target through UIA");drain(window);gui(window,[&]{assertTarget(project,"foreground",mask);require(*project.document==before&&project.history.undoCount()==count,"target selection preserves canonical document and history");});return;
    }
    if(which=="link_history"||which=="link_undo_redo"){
        Document before;std::string active;std::vector<std::string> selected;bool mask=false;size_t count=0;gui(window,[&]{before=*project.document;active=project.active;selected=project.selected;mask=project.maskSelected;count=project.history.undoCount();});auto control=button(client,unlinkName,report);auto invoke=invokePattern(control.Get());checked(invoke->Invoke(),"UIA Unlink");drain(window);Document unlinked;gui(window,[&]{require(!layer(project,"foreground").mask->linked,"UIA unlinks canonical mask");auto expected=before;for(auto& l:expected.layers)if(l.id=="foreground")l.mask->linked=false;require(*project.document==expected,"only linked flag changed");require(project.history.undoCount()==count+1&&project.history.undoName()=="Unlink Layer Mask","one source-named history transaction");require(project.active==active&&project.selected==selected&&project.maskSelected==mask,"link action preserves target");unlinked=*project.document;});require(nameOf(control.Get())==linkName,"retained link Name updates");
        if(which=="link_history"){checked(invoke->Invoke(),"retained UIA Link");drain(window);gui(window,[&]{require(*project.document==before,"Link preserves exact raster/mask/placement");require(project.history.undoCount()==count+2&&project.history.undoName()=="Link Layer Mask","Link creates one further source transaction");});}
        else{gui(window,[&]{command(window,"edit.undo");require(*project.document==before,"Undo restores exact linked document");});require(bool(client.named(unlinkName,true)),"Undo updates named link button");gui(window,[&]{command(window,"edit.redo");require(*project.document==unlinked,"Redo restores exact unlinked document");});require(bool(client.named(linkName,true)),"Redo updates named link button");}return;
    }
    if(which=="busy_import"||which=="busy_project"){
        std::vector<ComPtr<IUIAutomationElement>> elements;for(const auto& name:{imageName,maskName,unlinkName})elements.push_back(button(client,name,report));Document before;size_t count=0;std::string active;std::vector<std::string> selected;bool mask=false;gui(window,[&]{project.importing=which=="busy_import";project.projectBusy=which=="busy_project";before=*project.document;count=project.history.undoCount();active=project.active;selected=project.selected;mask=project.maskSelected;});QJsonArray results;
        for(const auto& element:elements){BOOL enabled=TRUE;checked(element->get_CurrentIsEnabled(&enabled),"busy state");require(!enabled,"busy thumbnail/link is disabled");const auto hr=invokePattern(element.Get())->Invoke();require(SUCCEEDED(hr)||hr==UIA_E_ELEMENTNOTENABLED,"disabled Invoke is rejected or no-op");results.append(QString::number(static_cast<unsigned long>(hr),16));}
        drain(window);gui(window,[&]{require(*project.document==before&&project.history.undoCount()==count,"busy Invoke preserves canonical state/history");require(project.active==active&&project.selected==selected&&project.maskSelected==mask,"busy Invoke preserves target");project.importing=false;project.projectBusy=false;});report["disabled_hresult"]=results;return;
    }
    if(which=="collapse_expand"||which=="stale_collapsed"){
        auto control=button(client,maskName,report);auto invoke=invokePattern(control.Get());gui(window,[&]{item(window,"group")->setExpanded(false);});require(!client.named(imageName,true)&&!client.named(maskName,true)&&!client.named(unlinkName,true),"collapsed descendant buttons omitted");require(bool(client.named("Select image: Folder",true)),"folder image button retained");
        if(which=="stale_collapsed"){Document before;size_t count=0;gui(window,[&]{before=*project.document;count=project.history.undoCount();});const auto hr=invoke->Invoke();require(SUCCEEDED(hr)||hr==UIA_E_ELEMENTNOTAVAILABLE||hr==UIA_E_ELEMENTNOTENABLED,"collapsed stale Invoke outcome");drain(window);gui(window,[&]{assertTarget(project,"group",false);require(*project.document==before&&project.history.undoCount()==count,"collapsed stale Invoke cannot select or edit child");});report["collapsed_hresult"]=QString::number(static_cast<unsigned long>(hr),16);}
        gui(window,[&]{item(window,"group")->setExpanded(true);});require(bool(client.named(imageName,true))&&bool(client.named(maskName,true))&&bool(client.named(unlinkName,true)),"expanded buttons restored");return;
    }
    if(which=="rename"){
        auto image=button(client,imageName,report);auto mask=button(client,maskName,report);auto link=button(client,unlinkName,report);gui(window,[&]{item(window,"foreground")->setText(0,QString::fromUtf8("Renamed 実証"));});drain(window);require(nameOf(image.Get())==QString::fromUtf8("Select image: Renamed 実証"),"retained image Name tracks Unicode rename");require(nameOf(mask.Get())==QString::fromUtf8("Select mask: Renamed 実証"),"retained mask Name tracks Unicode rename");require(nameOf(link.Get())==QString::fromUtf8("Unlink mask: Renamed 実証"),"retained link Name tracks Unicode rename");require(bool(client.named(QString::fromUtf8("Renamed 実証"))),"renamed row label retained");return;
    }
    if(which=="removed_lifetime"||which=="mask_removed_lifetime"){
        std::vector<ComPtr<IUIAutomationInvokePattern>> retained;for(const auto& name:which=="removed_lifetime"?QStringList{imageName,maskName,unlinkName}:QStringList{maskName,unlinkName})retained.push_back(invokePattern(button(client,name,report).Get()));
        gui(window,[&]{if(which=="removed_lifetime")project.document.reset();else for(auto& l:project.document->layers)if(l.id=="foreground")l.mask.reset();ui::LayerPanelController::find(tree(window))->rebuild();});require(!client.named(maskName,true)&&!client.named(unlinkName,true),"removed mask buttons omitted");if(which=="removed_lifetime")require(!client.named(imageName,true),"removed document image button omitted");else require(bool(client.named(imageName,true)),"image button survives mask deletion");
        for(const auto& stale:retained)require(stale->Invoke()==UIA_E_ELEMENTNOTAVAILABLE,"retained removed fragment rejects Invoke");return;
    }
    if(which=="kind_visibility"){
        for(const auto& name:{QString("Select image: Folder"),QString("Select mask: Folder"),QString("Select image: Exposure layer"),QString("Select mask: Exposure layer"),QString("Select image: Plain image")})require(bool(button(client,name,report)),"source thumbnail button exposed");
        for(const auto& name:{QString("Unlink mask: Folder"),QString("Unlink mask: Exposure layer"),QString("Select mask: Plain image"),QString("Unlink mask: Plain image")})require(!client.named(name,true),"source hidden button absent");return;
    }
    if(which=="visibility_row_regression"){
        require(bool(client.named("Foreground with mask")),"plain row retained");auto eye=client.named("Hide Foreground with mask");auto toggle=togglePattern(eye.Get());checked(toggle->Toggle(),"existing visibility Toggle");drain(window);gui(window,[&]{require(!layer(project,"foreground").visible&&project.history.undoCount()==1,"visibility transaction retained");assertTarget(project,"group",false);});return;
    }
    if(which=="mouse_routes_regression"){
        gui(window,[&]{const auto before=*project.document;mouse(window,"foreground",3);assertTarget(project,"foreground",true);mouse(window,"foreground",1);assertTarget(project,"foreground",false);require(*project.document==before&&project.history.undoCount()==0,"ordinary thumbnail mouse selection unchanged");mouse(window,"foreground",2);require(!layer(project,"foreground").mask->linked&&project.history.undoCount()==1,"ordinary link mouse transaction unchanged");assertTarget(project,"foreground",false);});return;
    }
    throw std::runtime_error("Unknown case");
}
}
int main(int argc,char** argv){
    QApplication app(argc,argv);if(argc!=3){std::cerr<<"Usage: layer_action_accessibility_tests CASE OUTPUT.json\n";return 2;}app.setApplicationName("Layer action UIA witness");
    MainWindow window(true);auto& project=window.addProject(fixture(),"Layer action accessibility");selectFixture(window,project,"group",false);window.show();window.activateWindow();
    const auto hwnd=reinterpret_cast<HWND>(window.winId());std::atomic<int> result{2};std::thread worker;
    QTimer::singleShot(300,&window,[&]{worker=std::thread([&]{QJsonObject report{{"schema","LAYER_ACTION_UIA_V1"},{"case",argv[1]},{"transport","Windows UI Automation on owned HWND from MTA"},{"human_acceptance",false}};bool initialized=false;
        try{checked(CoInitializeEx(nullptr,COINIT_MULTITHREADED),"MTA");initialized=true;Client client(hwnd);perform(argv[1],window,project,client,report);report["status"]="passed";result=0;std::cout<<"PASS "<<argv[1]<<'\n';}
        catch(const std::exception& error){report["status"]="failed";report["error"]=error.what();result=1;std::cerr<<"FAIL "<<argv[1]<<": "<<error.what()<<'\n';}
        QFile file(QString::fromLocal8Bit(argv[2]));if(!file.open(QIODevice::WriteOnly)||file.write(QJsonDocument(report).toJson())<0)result=2;
        if(initialized)CoUninitialize();QMetaObject::invokeMethod(&window,[&]{app.exit(result.load());},Qt::QueuedConnection);
    });});app.exec();if(worker.joinable())worker.join();return result;
}
