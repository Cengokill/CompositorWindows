#include "ProjectOpenDialog.h"
#include "VisualStyle.h"
#include "EditorIcons.h"
#include <QDialog>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
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
    installVisualStyle();
    QDialog dialog(owner);dialog.setWindowTitle("Open project");dialog.resize(680,480);
    auto* layout=new QVBoxLayout(&dialog);layout->setContentsMargins(18,16,18,16);layout->setSpacing(12);
    auto* navigation=new QHBoxLayout;
    auto* up=new QPushButton(editorIcon(EditorIcon::ChevronUp),"",&dialog);up->setAccessibleName("Parent folder");up->setToolTip("Parent folder");up->setFixedWidth(34);
    auto* home=new QPushButton("Home",&dialog);
    up->setAutoDefault(false);home->setAutoDefault(false);
    auto* path=new QLineEdit(&dialog);path->setAccessibleName("Folder path");
    navigation->addWidget(up);navigation->addWidget(home);navigation->addWidget(path,1);layout->addLayout(navigation);
    auto* list=new QListWidget(&dialog);list->setObjectName("projectFolders");list->setAccessibleName("Project folders");
    list->setSelectionMode(QAbstractItemView::ExtendedSelection);list->setIconSize({24,24});layout->addWidget(list,1);
    auto* hint=new QLabel("Select a .comp project. Double-click folders to browse.",&dialog);hint->setWordWrap(true);hint->setStyleSheet("color: #989ba3; font-size: 12px;");layout->addWidget(hint);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Open|QDialogButtonBox::Cancel,&dialog);layout->addWidget(buttons);
    auto* open=buttons->button(QDialogButtonBox::Open);open->setEnabled(false);open->setDefault(true);
    buttons->button(QDialogButtonBox::Cancel)->setAutoDefault(false);
    QString folder=QDir::homePath();QStringList result;
    auto selected=[&]{QStringList paths;for(auto* item:list->selectedItems())paths.append(item->data(Qt::UserRole).toString());return paths;};
    std::function<void(const QString&)> navigate=[&](const QString& next){
        if(!canNavigateProjectFolder(next)){hint->setText("Choose a folder outside a .comp project.");return;}
        folder=QDir(next).absolutePath();path->setText(QDir::toNativeSeparators(folder));list->clear();open->setEnabled(false);
        for(const auto& info:QDir(folder).entryInfoList(QDir::Dirs|QDir::NoDotAndDotDot,QDir::Name|QDir::IgnoreCase)){
            const bool project=isProjectPackageDirectory(info.absoluteFilePath());
            auto* item=new QListWidgetItem(editorIcon(project?EditorIcon::Shape:EditorIcon::Folder),info.fileName(),list);
            item->setData(Qt::UserRole,info.absoluteFilePath());item->setSizeHint({0,40});item->setToolTip(info.absoluteFilePath());
        }
        QDir parent(folder);up->setEnabled(parent.cdUp());hint->setText("Select a .comp project. Double-click folders to browse.");
    };
    auto accept=[&]{auto paths=selected();if(!paths.isEmpty()&&std::all_of(paths.begin(),paths.end(),isProjectPackageDirectory)){result=paths;dialog.accept();}};
    QObject::connect(list,&QListWidget::itemSelectionChanged,&dialog,[&]{const auto paths=selected();open->setEnabled(!paths.isEmpty()&&std::all_of(paths.begin(),paths.end(),isProjectPackageDirectory));});
    QObject::connect(list,&QListWidget::itemDoubleClicked,&dialog,[&](QListWidgetItem* item){const auto next=item->data(Qt::UserRole).toString();if(isProjectPackageDirectory(next)){result={next};dialog.accept();}else navigate(next);});
    QObject::connect(path,&QLineEdit::returnPressed,&dialog,[&]{const auto next=QDir::fromNativeSeparators(path->text().trimmed());if(isProjectPackageDirectory(next)){result={next};dialog.accept();}else navigate(next);});
    QObject::connect(up,&QPushButton::clicked,&dialog,[&]{QDir parent(folder);if(parent.cdUp())navigate(parent.absolutePath());});
    QObject::connect(home,&QPushButton::clicked,&dialog,[&]{navigate(QDir::homePath());});
    QObject::connect(buttons,&QDialogButtonBox::accepted,&dialog,accept);
    QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    navigate(folder);if(dialog.exec()!=QDialog::Accepted)return std::nullopt;
    return result;
}
}
