#include "ui/MainWindow.h"
#include "ui/LayerPanel.h"
#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
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
    ComPtr<IUIAutomationElement> named(const QString& name){
        VARIANT value;VariantInit(&value);value.vt=VT_BSTR;value.bstrVal=SysAllocString(reinterpret_cast<const wchar_t*>(name.utf16()));
        ComPtr<IUIAutomationCondition> condition;const auto hr=automation->CreatePropertyCondition(UIA_NamePropertyId,value,&condition);VariantClear(&value);checked(hr,"name condition");
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
Document fixture(){Document d;d.id=newId();d.width=64;d.height=48;Layer group;group.id="group";group.name="Folder";group.group=true;Layer image;image.id="foreground";image.name="Foreground with mask";image.parentId="group";image.transform={8,8,32,24};image.raster=Raster::filled(32,24,{180,90,40,255});image.mask=Mask{std::make_shared<GrayRaster>(GrayRaster{32,24,std::vector<uint8_t>(768,255)})};d.layers={group,image};return d;}
}
int main(int argc,char** argv){
    QApplication app(argc,argv);if(argc!=2){std::cerr<<"Usage: accessibility_visibility_tests OUTPUT.json\n";return 2;}
    MainWindow window(true);auto& project=window.addProject(fixture(),"Visibility accessibility");window.show();window.activateWindow();
    const auto hwnd=reinterpret_cast<HWND>(window.winId());std::atomic<int> exit{2};std::thread worker;
    QTimer::singleShot(300,&window,[&]{worker=std::thread([&]{QJsonArray checks;QJsonObject report{{"schema","VISIBILITY_UIA_V1"},{"transport","Windows UI Automation on an owned HWND from MTA"},{"human_acceptance",false}};bool initialized=false;
        auto record=[&](const char* id){checks.append(QJsonObject{{"id",id},{"passed",true}});std::cout<<"PASS "<<id<<'\n';};
        try{checked(CoInitializeEx(nullptr,COINIT_MULTITHREADED),"MTA");initialized=true;Client client(hwnd);
            auto hide=client.named("Hide Foreground with mask");require(bool(hide),"exact contextual Name");require(bool(client.named("Foreground with mask")),"plain layer Name retained");int role=0;checked(hide->get_CurrentControlType(&role),"visibility role");require(role==UIA_CheckBoxControlTypeId,"visibility CheckBox role");RECT bounds{};checked(hide->get_CurrentBoundingRectangle(&bounds),"visibility rectangle");require(bounds.right>bounds.left&&bounds.bottom>bounds.top,"painted visibility rectangle exposed");auto pattern=togglePattern(hide.Get());ToggleState state{};checked(pattern->get_CurrentToggleState(&state),"initial toggle state");require(state==ToggleState_On,"initial visible state");record("contextual_control");
            Document before;std::string active;std::vector<std::string> selected;bool maskTarget=false;size_t history=0;
            gui(window,[&]{before=*project.document;active=project.active;selected=project.selected;maskTarget=project.maskSelected;history=project.history.undoCount();});
            checked(pattern->Toggle(),"UIA Hide");gui(window,[&]{require(!layer(project,"foreground").visible,"UIA hides canonical layer");require(project.history.undoCount()==history+1&&project.history.undoName()=="Hide Layer","UIA hide commits one transaction");require(project.active==active&&project.selected==selected&&project.maskSelected==maskTarget,"UIA preserves edit target and selection");});require(nameOf(hide.Get())=="Show Foreground with mask","retained UIA element Name updates");checked(pattern->get_CurrentToggleState(&state),"hidden toggle state");require(state==ToggleState_Off,"UIA unchecked state");record("toggle_history_selection");
            gui(window,[&]{command(window,"edit.undo");require(*project.document==before,"Undo restores exact document");});require(bool(client.named("Hide Foreground with mask")),"Undo updates contextual Name");gui(window,[&]{command(window,"edit.redo");require(!layer(project,"foreground").visible,"Redo hides canonical layer");});auto show=client.named("Show Foreground with mask");checked(togglePattern(show.Get())->Toggle(),"UIA Show");gui(window,[&]{require(*project.document==before,"UIA Show restores exact pixels/mask/transform");require(project.history.undoCount()==history+2,"second UIA action adds one history step");});record("undo_redo_show");
            gui(window,[&]{item(window,"group")->setExpanded(false);});require(!client.named("Hide Foreground with mask"),"collapsed child control omitted");require(bool(client.named("Hide Folder")),"group control retained");gui(window,[&]{item(window,"group")->setExpanded(true);});require(bool(client.named("Hide Foreground with mask")),"expanded child control restored");record("collapse_expand");
            gui(window,[&]{item(window,"foreground")->setText(0,QString::fromUtf8("Renamed 実証"));});auto renamed=client.named(QString::fromUtf8("Hide Renamed 実証"));require(bool(renamed),"rename updates exact contextual Name");require(bool(client.named(QString::fromUtf8("Renamed 実証"))),"renamed plain row retained");record("rename");
            Document busyBefore;size_t busyHistory=0;gui(window,[&]{project.importing=true;busyBefore=*project.document;busyHistory=project.history.undoCount();});BOOL enabled=TRUE;checked(renamed->get_CurrentIsEnabled(&enabled),"busy enabled state");require(enabled==FALSE,"busy visibility disabled");const auto disabledResult=togglePattern(renamed.Get())->Toggle();require(SUCCEEDED(disabledResult)||disabledResult==UIA_E_ELEMENTNOTENABLED,"disabled Toggle returns supported no-op/disabled result");gui(window,[&]{require(*project.document==busyBefore&&project.history.undoCount()==busyHistory,"busy UIA action preserves document/history");project.importing=false;});report["disabled_toggle_hresult"]=QString::number(static_cast<unsigned long>(disabledResult),16);record("busy_guard");
            auto stale=togglePattern(renamed.Get());gui(window,[&]{project.document.reset();auto* controller=ui::LayerPanelController::find(tree(window));require(controller!=nullptr,"controller");controller->rebuild();});require(!client.named(QString::fromUtf8("Hide Renamed 実証")),"removed document has no visibility child");const auto staleResult=stale->Toggle();require(staleResult==UIA_E_ELEMENTNOTAVAILABLE,"retained provider reports removed element");report["removed_toggle_hresult"]=QString::number(static_cast<unsigned long>(staleResult),16);record("removed_element_lifetime");
            report["status"]="passed";exit=0;
        }catch(const std::exception& error){report["status"]="failed";report["error"]=error.what();std::cerr<<"FAIL "<<error.what()<<'\n';exit=1;}
        report["checks"]=checks;report["required_groups"]=7;QFile file(QString::fromLocal8Bit(argv[1]));if(!file.open(QIODevice::WriteOnly)||file.write(QJsonDocument(report).toJson())<0)exit=2;
        // QApplication::quit closes top-level windows and prompts to save the
        // deliberate visibility/rename edits. The witness has finished checking
        // those edits; end its event loop directly and destroy its owned window.
        if(initialized)CoUninitialize();QMetaObject::invokeMethod(&window,[&]{app.exit(exit.load());},Qt::QueuedConnection);
    });});app.exec();if(worker.joinable())worker.join();return exit;
}
