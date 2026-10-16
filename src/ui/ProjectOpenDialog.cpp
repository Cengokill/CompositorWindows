#include "ProjectOpenDialog.h"
#include "StaWorker.h"
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QThread>
#include <QWidget>
#include <Windows.h>
#include <ShObjIdl.h>
#include <OleIdl.h>
#include <wrl/client.h>
#include <atomic>
#include <exception>
#include <memory>
#include <stdexcept>

namespace compositor::ui {
namespace {
using Microsoft::WRL::ComPtr;
void checked(HRESULT result,const char* operation){if(FAILED(result))throw std::runtime_error(std::string(operation)+" (HRESULT 0x"+QString::number(quint32(result),16).toStdString()+")");}
QString fileSystemPath(IShellItem* item){PWSTR value{};checked(item->GetDisplayName(SIGDN_FILESYSPATH,&value),"Read selected project directory");const auto result=QString::fromWCharArray(value);CoTaskMemFree(value);return result;}

class ProjectOpenDialogEvents final : public IFileDialogEvents {
public:
    explicit ProjectOpenDialogEvents(HWND owner) : owner_(owner) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** object) override {
        if(!object)return E_POINTER;
        if(id==IID_IUnknown||id==IID_IFileDialogEvents){*object=static_cast<IFileDialogEvents*>(this);AddRef();return S_OK;}
        *object=nullptr;return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override{return ++references_;}
    ULONG STDMETHODCALLTYPE Release() override {const auto remaining=--references_;if(!remaining)delete this;return remaining;}
    HRESULT STDMETHODCALLTYPE OnFileOk(IFileDialog* dialog) override {
        ComPtr<IFileOpenDialog> open;if(FAILED(dialog->QueryInterface(IID_PPV_ARGS(&open))))return S_FALSE;
        ComPtr<IShellItemArray> items;if(FAILED(open->GetSelectedItems(&items)))return S_FALSE;
        DWORD count{};if(FAILED(items->GetCount(&count))||count==0)return S_FALSE;
        for(DWORD index=0;index<count;++index){
            ComPtr<IShellItem> item;if(FAILED(items->GetItemAt(index,&item)))return S_FALSE;
            try{if(!isProjectPackageDirectory(fileSystemPath(item.Get())))return reject(dialog);}catch(const std::exception&){return reject(dialog);}
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnFolderChanging(IFileDialog*,IShellItem* folder) override{return canNavigateProjectItem(folder)?S_OK:S_FALSE;}
    HRESULT STDMETHODCALLTYPE OnFolderChange(IFileDialog*) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnSelectionChange(IFileDialog*) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnShareViolation(IFileDialog*,IShellItem*,FDE_SHAREVIOLATION_RESPONSE* response) override {if(response)*response=FDESVR_DEFAULT;return S_OK;}
    HRESULT STDMETHODCALLTYPE OnTypeChange(IFileDialog*) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnOverwrite(IFileDialog*,IShellItem*,FDE_OVERWRITE_RESPONSE* response) override {if(response)*response=FDEOR_DEFAULT;return S_OK;}
private:
    HRESULT reject(IFileDialog* dialog) const {
        HWND dialogWindow=owner_;
        ComPtr<IOleWindow> oleWindow;
        if(SUCCEEDED(dialog->QueryInterface(IID_PPV_ARGS(&oleWindow))))oleWindow->GetWindow(&dialogWindow);
        MessageBoxW(dialogWindow,L"Select one or more Compositor project folders ending in .comp.",L"Open Compositor Projects",MB_OK|MB_ICONINFORMATION);
        return S_FALSE;
    }
    HWND owner_{};
    std::atomic_ulong references_{1};
};

QStringList selectedProjectDirectories(IFileOpenDialog* dialog){
    ComPtr<IShellItemArray> items;checked(dialog->GetResults(&items),"Read selected project folders");
    DWORD count{};checked(items->GetCount(&count),"Count selected project folders");
    if(count==0)throw std::runtime_error("No project folders were selected");
    QStringList paths;paths.reserve(qsizetype(count));
    for(DWORD index=0;index<count;++index){
        ComPtr<IShellItem> item;checked(items->GetItemAt(index,&item),"Read selected project folder");
        const auto path=fileSystemPath(item.Get());
        if(!isProjectPackageDirectory(path))throw std::runtime_error("Selected folder is not a Compositor project");
        paths.append(path);
    }
    return paths;
}

std::optional<QStringList> showProjectOpenDialog(HWND owner,const QString& initialDirectory){
    ComPtr<IFileOpenDialog> dialog;checked(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)),"Create project picker");
    configureProjectOpenDialog(dialog.Get());
    if(!initialDirectory.isEmpty()){
        const auto path=initialDirectory.toStdWString();ComPtr<IShellItem> folder;
        if(SUCCEEDED(SHCreateItemFromParsingName(path.c_str(),nullptr,IID_PPV_ARGS(&folder))))dialog->SetFolder(folder.Get());
    }
    ComPtr<ProjectOpenDialogEvents> events;events.Attach(new ProjectOpenDialogEvents(owner));
    DWORD cookie{};checked(dialog->Advise(events.Get(),&cookie),"Attach project picker validation");
    const auto shown=dialog->Show(owner);dialog->Unadvise(cookie);
    if(shown==HRESULT_FROM_WIN32(ERROR_CANCELLED))return std::nullopt;
    checked(shown,"Show project picker");
    return selectedProjectDirectories(dialog.Get());
}
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
std::optional<QStringList> chooseProjectDirectories(QWidget* owner,const QString& initialDirectory){
    if(!owner)throw std::invalid_argument("A project picker owner is required");
    const auto nativeOwner=reinterpret_cast<HWND>(owner->winId());
    struct Result {std::optional<QStringList> paths;std::exception_ptr error;};
    auto result=std::make_shared<Result>();
    runOnStaThread([result,nativeOwner,initialDirectory]{
        try{
            result->paths=showProjectOpenDialog(nativeOwner,initialDirectory);
        }catch(...){result->error=std::current_exception();}
    });
    if(result->error)std::rethrow_exception(result->error);
    return result->paths;
}
}
