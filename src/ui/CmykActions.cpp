#include "MainWindow.h"
#include "StaWorker.h"
#include "imaging/cmyk.h"
#include "imaging/wic_codec.h"
#include <QAction>
#include <QDir>
#include <QFileInfo>
#include <QMenu>
#include <QMessageBox>
#include <QSettings>
#include <QSignalBlocker>
#include <Windows.h>
#include <commdlg.h>

namespace compositor {
namespace {
QString nativeOpenProfilePath(HWND owner){
    static const wchar_t filter[]=L"ICC profiles\0*.icc;*.icm\0All files\0*.*\0";
    std::wstring buffer(32768,L'\0');
    wchar_t system[MAX_PATH]{};
    std::wstring initial;
    if(GetSystemDirectoryW(system,MAX_PATH))initial=std::wstring(system)+L"\\spool\\drivers\\color";
    OPENFILENAMEW dialog{};
    dialog.lStructSize=sizeof(dialog);
    dialog.hwndOwner=owner;
    dialog.lpstrFilter=filter;
    dialog.lpstrFile=buffer.data();
    dialog.nMaxFile=DWORD(buffer.size());
    dialog.lpstrInitialDir=initial.empty()?nullptr:initial.c_str();
    dialog.lpstrTitle=L"Choose CMYK profile";
    dialog.Flags=OFN_EXPLORER|OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_HIDEREADONLY;
    if(!GetOpenFileNameW(&dialog))return {};
    return QDir::cleanPath(QString::fromWCharArray(buffer.c_str()));
}
CmykProfilePtr rememberedProfile(){
    QSettings settings;const auto bytes=settings.value("cmyk/fallbackBytes").toByteArray();
    if(bytes.isEmpty()||bytes.size()>qsizetype(imaging::maxCmykProfileBytes))return {};
    try{return imaging::makeCmykProfile(std::span(reinterpret_cast<const uint8_t*>(bytes.data()),size_t(bytes.size())),settings.value("cmyk/fallbackName").toString().toStdString());}
    catch(const std::exception&){return {};}
}
}
QString MainWindow::chooseNativeCmykProfilePath(){
    if(property("testCmykProfileCancel").toBool())return {};
    const auto injected=property("testCmykProfilePath").toString();
    if(!injected.isEmpty())return injected;
    QString file;
    const auto owner=reinterpret_cast<HWND>(winId());
    try{ui::runOnStaThread([owner,&file]{file=nativeOpenProfilePath(owner);});}
    catch(const std::runtime_error&){return {};}
    return file;
}
bool MainWindow::reviewCmykImports(const QStringList& paths,ui::RawSettings& settings){
    CmykProfilePtr fallback;bool triedRemembered=false;
    for(const auto& path:paths){
        const auto ext=QFileInfo(path).suffix().toLower();const bool psd=ext=="psd"||ext=="psb";
        if(!psd&&ext!="jpg"&&ext!="jpeg"&&ext!="tif"&&ext!="tiff"&&ext!="png")continue;
        const auto file=std::filesystem::path(path.toStdWString());
        for(;;){
            try{auto profile=psd?imaging::photoshopCmykProfile(file,fallback):imaging::WicCodec::sourceCmykProfile(file,fallback);if(profile)settings.cmykProfiles[file]=std::move(profile);break;}
            catch(const imaging::MissingCmykProfile&){
                if(!triedRemembered){triedRemembered=true;fallback=rememberedProfile();if(fallback)continue;}
                QMessageBox message(QMessageBox::Information,"CMYK source profile",{},QMessageBox::Open|QMessageBox::Cancel,this);
                message.setObjectName("missingCmykProfile");
                // Already styled, so the chrome handler keeps Qt's layout; otherwise the message is clipped (KNOWN-ISSUES: Dialog chrome).
                message.setProperty("studioChrome",true);
                message.setTextFormat(Qt::PlainText);
                message.setText(QFileInfo(path).fileName()+" has no embedded profile.");
                message.setInformativeText("Choose the profile used to create this image.");
                if(message.exec()!=QMessageBox::Open)return false;
                try{const auto chosen=chooseNativeCmykProfilePath();if(chosen.isEmpty())return false;fallback=imaging::readCmykProfile(std::filesystem::path(chosen.toStdWString()));}catch(const std::exception& error){QMessageBox::critical(this,"CMYK profile",error.what());return false;}
            }
        }
    }return true;
}
void MainWindow::setupCmykActions(QMenu* view){
    auto* menu=view->addMenu("CMYK");
    auto* proof=action(menu,"Proof Colors",{},[this]{auto* p=current();if(!p||!p->document)return;if(!p->document->cmykProfile){chooseCmykProfile();if(!p->document->cmykProfile){refreshCmykActions();return;}}p->proofColors=!p->proofColors;refreshCmykActions();});
    proof->setObjectName("viewCmykProof");proof->setCheckable(true);
    auto* profile=action(menu,"Load Profile…",{},[this]{chooseCmykProfile();});profile->setObjectName("viewCmykProfile");
    auto* name=menu->addSection("Profile: none");name->setObjectName("viewCmykProfileName");
}
void MainWindow::chooseCmykProfile(){
    auto* p=current();if(!p||!p->document||p->importing||p->projectBusy)return;
    const auto file=chooseNativeCmykProfilePath();if(file.isEmpty())return;
    CmykProfilePtr profile;try{profile=imaging::readCmykProfile(std::filesystem::path(file.toStdWString()));}catch(const std::exception& error){QMessageBox::critical(this,"CMYK profile",error.what());return;}
    if(!profile)return;
    if(!p->document->cmykProfile||p->document->cmykProfile->sha256!=profile->sha256)
        edit("CMYK Profile",[profile](Document& document){document.cmykProfile=profile;},false);
    QSettings settings;settings.setValue("cmyk/fallbackBytes",QByteArray(reinterpret_cast<const char*>(profile->bytes.data()),qsizetype(profile->bytes.size())));settings.setValue("cmyk/fallbackName",QString::fromStdString(profile->name));
    refreshCmykActions();
}
void MainWindow::refreshCmykActions(){
    auto* p=current();const auto profile=p&&p->document?p->document->cmykProfile:nullptr;
    if(auto* action=findChild<QAction*>("viewCmykProof")){QSignalBlocker block(action);action->setChecked(p&&p->proofColors&&bool(profile));}
    if(auto* action=findChild<QAction*>("viewCmykProfileName"))action->setText("Profile: "+(profile?QString::fromStdString(profile->name):QStringLiteral("none")));
    if(p&&p->canvas)p->canvas->setCmykProof(profile,p->proofColors&&bool(profile));
}
}
