#include "MainWindow.h"
#include "imaging/cmyk.h"
#include "imaging/wic_codec.h"
#include <QFileDialog>
#include <QAction>
#include <QFileInfo>
#include <QMenu>
#include <QMessageBox>
#include <QSettings>
#include <QSignalBlocker>

namespace compositor {
namespace {
CmykProfilePtr pickProfile(QWidget* parent){
    const auto file=QFileDialog::getOpenFileName(parent,"Choose CMYK profile",{},"ICC profiles (*.icc *.icm)");
    if(file.isEmpty())return {};
    return imaging::readCmykProfile(std::filesystem::path(file.toStdWString()));
}
CmykProfilePtr rememberedProfile(){
    QSettings settings;const auto bytes=settings.value("cmyk/fallbackBytes").toByteArray();
    if(bytes.isEmpty()||bytes.size()>qsizetype(imaging::maxCmykProfileBytes))return {};
    try{return imaging::makeCmykProfile(std::span(reinterpret_cast<const uint8_t*>(bytes.data()),size_t(bytes.size())),settings.value("cmyk/fallbackName").toString().toStdString());}
    catch(const std::exception&){return {};}
}
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
                QMessageBox message(QMessageBox::Information,"CMYK source profile",QFileInfo(path).fileName()+" has no embedded profile. Choose the profile used to create this image.",QMessageBox::Open|QMessageBox::Cancel,this);message.setObjectName("missingCmykProfile");
                if(message.exec()!=QMessageBox::Open)return false;
                try{fallback=pickProfile(this);if(!fallback)return false;}catch(const std::exception& error){QMessageBox::critical(this,"CMYK profile",error.what());return false;}
            }
        }
    }return true;
}
void MainWindow::setupCmykActions(QMenu* view){
    auto* menu=view->addMenu("CMYK");
    auto* proof=action(menu,"Proof Colors",{},[this]{auto* p=current();if(!p||!p->document)return;if(!p->document->cmykProfile){chooseCmykProfile();if(!p->document->cmykProfile){refreshCmykActions();return;}}p->proofColors=!p->proofColors;refreshCmykActions();});
    proof->setObjectName("viewCmykProof");proof->setCheckable(true);
    auto* profile=action(menu,"Load Profile…",{},[this]{chooseCmykProfile();});profile->setObjectName("viewCmykProfile");
    auto* name=menu->addAction("Profile: none");name->setObjectName("viewCmykProfileName");name->setEnabled(false);
}
void MainWindow::chooseCmykProfile(){
    auto* p=current();if(!p||!p->document||p->importing||p->projectBusy)return;
    const auto profile=pickProfile(this);if(!profile)return;
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
