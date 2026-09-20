#include "ProjectOpenDialog.h"
#include <QDir>
#include <QFileInfo>
#include <QWidget>
#include <Windows.h>
#include <ShObjIdl.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <stdexcept>

namespace compositor::ui {
namespace {
using Microsoft::WRL::ComPtr;
void checked(HRESULT result,const char* operation){if(FAILED(result))throw std::runtime_error(std::string(operation)+" (HRESULT 0x"+QString::number(quint32(result),16).toStdString()+")");}
QString fileSystemPath(IShellItem* item){PWSTR value{};checked(item->GetDisplayName(SIGDN_FILESYSPATH,&value),"Read selected project directory");const auto result=QString::fromWCharArray(value);CoTaskMemFree(value);return result;}
QStringList selectedPaths(IFileOpenDialog* dialog){ComPtr<IShellItemArray> items;checked(dialog->GetResults(&items),"Read selected project directories");DWORD count{};checked(items->GetCount(&count),"Count selected project directories");QStringList result;for(DWORD index=0;index<count;++index){ComPtr<IShellItem> item;checked(items->GetItemAt(index,&item),"Read selected project item");result.append(fileSystemPath(item.Get()));}return result;}
void explain(IFileDialog* dialog,HWND fallback,const wchar_t* message){ComPtr<IOleWindow> window;HWND owner{};if(SUCCEEDED(dialog->QueryInterface(IID_PPV_ARGS(&window))))window->GetWindow(&owner);MessageBoxW(owner?owner:fallback,message,L"Open Compositor Projects",MB_OK|MB_ICONINFORMATION);}
class PackageEvents final:public IFileDialogEvents {
    std::atomic<ULONG> references_{1};
    HWND owner_{};
public:
    explicit PackageEvents(HWND owner):owner_(owner){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** object) override {if(!object)return E_POINTER;*object=nullptr;if(iid==IID_IUnknown||iid==IID_IFileDialogEvents){*object=static_cast<IFileDialogEvents*>(this);AddRef();return S_OK;}return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef() override{return ++references_;}
    ULONG STDMETHODCALLTYPE Release() override{const auto remaining=--references_;if(!remaining)delete this;return remaining;}
    HRESULT STDMETHODCALLTYPE OnFileOk(IFileDialog* dialog) override {
        try{ComPtr<IFileOpenDialog> open;checked(dialog->QueryInterface(IID_PPV_ARGS(&open)),"Inspect project picker");const auto paths=selectedPaths(open.Get());if(!paths.isEmpty()&&std::all_of(paths.begin(),paths.end(),isProjectPackageDirectory))return S_OK;
            explain(dialog,owner_,L"Select one or more .comp project folders, then press Open. Other folders are available for navigation.");return S_FALSE;
        }catch(const std::exception& error){const auto message=QString::fromUtf8(error.what()).toStdWString();explain(dialog,owner_,message.c_str());return S_FALSE;}
    }
    HRESULT STDMETHODCALLTYPE OnFolderChanging(IFileDialog* dialog,IShellItem* folder) override {
        try{if(canNavigateProjectItem(folder))return S_OK;
            // The Shell can replay a remembered .comp folder while Show is
            // constructing its window. Reject it silently so initialization
            // can reach the explicit safe folder without an invisible modal.
            ComPtr<IOleWindow> window;HWND native{};
            if(FAILED(dialog->QueryInterface(IID_PPV_ARGS(&window)))||FAILED(window->GetWindow(&native))||!IsWindowVisible(native))return E_ACCESSDENIED;
            explain(dialog,owner_,L"A .comp folder is a project package. Select it and press Open; its contents are not available in this picker.");return E_ACCESSDENIED;
        }catch(const std::exception&){return E_ACCESSDENIED;}
    }
    HRESULT STDMETHODCALLTYPE OnFolderChange(IFileDialog*) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnSelectionChange(IFileDialog*) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnShareViolation(IFileDialog*,IShellItem*,FDE_SHAREVIOLATION_RESPONSE* response) override{if(response)*response=FDESVR_DEFAULT;return S_OK;}
    HRESULT STDMETHODCALLTYPE OnTypeChange(IFileDialog*) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnOverwrite(IFileDialog*,IShellItem*,FDE_OVERWRITE_RESPONSE* response) override{if(response)*response=FDEOR_DEFAULT;return S_OK;}
};
}
bool isProjectPackageDirectory(const QString& path){const QFileInfo info(path);return info.isDir()&&info.fileName().endsWith(".comp",Qt::CaseInsensitive);}
bool canNavigateProjectFolder(const QString& path){
    auto insidePackage=[](QString value){value=QDir::cleanPath(QDir::fromNativeSeparators(value));for(const auto& component:value.split('/',Qt::SkipEmptyParts))if(component.endsWith(".comp",Qt::CaseInsensitive))return true;return false;};
    const QFileInfo info(path);if(!info.isDir()||insidePackage(info.absoluteFilePath()))return false;
    // Also reject a filesystem alias leading inside a package. Unresolved paths
    // remain rejected; this policy never replaces ProjectStore reparse checks.
    const auto canonical=info.canonicalFilePath();return !canonical.isEmpty()&&!insidePackage(canonical);
}
bool canNavigateProjectItem(IShellItem* item){
    if(!item)return false;
    SFGAOF attributes{};if(FAILED(item->GetAttributes(SFGAO_FOLDER|SFGAO_FILESYSTEM,&attributes))||!(attributes&SFGAO_FOLDER))return false;
    // This PC and other virtual folder containers are navigation surfaces.
    // FORCEFILESYSTEM and OnFileOk still require actual .comp result paths.
    if(!(attributes&SFGAO_FILESYSTEM))return true;
    try{return canNavigateProjectFolder(fileSystemPath(item));}catch(const std::exception&){return false;}
}
void configureProjectOpenDialog(IFileOpenDialog* dialog){
    FILEOPENDIALOGOPTIONS options{};checked(dialog->GetOptions(&options),"Read project picker options");
    checked(dialog->SetOptions(options|FOS_PICKFOLDERS|FOS_ALLOWMULTISELECT|FOS_FORCEFILESYSTEM),"Configure project picker");
    checked(dialog->SetTitle(L"Open Compositor Projects"),"Set project picker title");checked(dialog->SetOkButtonLabel(L"Open"),"Set project picker action");
    checked(dialog->SetFileNameLabel(L"Project folders"),"Set project picker field label");
}
std::optional<QStringList> chooseProjectDirectories(QWidget* owner){
    const auto initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED|COINIT_DISABLE_OLE1DDE);checked(initialized,"Initialize project picker apartment");
    struct Apartment {~Apartment(){CoUninitialize();}} apartment;
    ComPtr<IFileOpenDialog> dialog;checked(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)),"Create project picker");configureProjectOpenDialog(dialog.Get());
    // Avoid restoring a remembered folder inside a project, which would expose
    // package contents before the first user navigation notification.
    ComPtr<IShellItem> initial;const auto home=QDir::homePath().toStdWString();if(SUCCEEDED(SHCreateItemFromParsingName(home.c_str(),nullptr,IID_PPV_ARGS(&initial))))checked(dialog->SetFolder(initial.Get()),"Set initial project folder");
    // SetFolder can initialize the Shell's remembered folder before applying
    // the requested one. Attach the navigation veto only after that setup;
    // otherwise a previous .comp result raises a dialog before Show has an HWND.
    const auto ownerWindow=owner?reinterpret_cast<HWND>(owner->winId()):nullptr;
    ComPtr<IFileDialogEvents> events;events.Attach(new PackageEvents(ownerWindow));DWORD cookie{};checked(dialog->Advise(events.Get(),&cookie),"Attach project package policy");
    struct Unadvise {IFileOpenDialog* dialog;DWORD cookie;~Unadvise(){dialog->Unadvise(cookie);}} unadvise{dialog.Get(),cookie};
    const auto result=dialog->Show(ownerWindow);
    if(result==HRESULT_FROM_WIN32(ERROR_CANCELLED))return std::nullopt;
    checked(result,"Open project picker");const auto paths=selectedPaths(dialog.Get());
    if(paths.isEmpty()||!std::all_of(paths.begin(),paths.end(),isProjectPackageDirectory))throw std::runtime_error("The project picker returned a path that is not a .comp directory");
    return paths;
}
}
