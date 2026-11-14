#include "WhatsNew.h"
#include "MainWindow.h"
#include "ImportActions.h"
#include "WorkspaceDropQueue.h"
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QJsonDocument>
#include <QPointer>
#include <QPushButton>
#include <QResource>
#include <QScreen>
#include <QSettings>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTextOption>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <exception>
#include <optional>
#include <utility>

static void initializeReleaseNotesResource(){Q_INIT_RESOURCE(release_notes);}

namespace compositor::ui {
namespace {
constexpr auto seenVersionKey="updates/whatsNewVersion";
constexpr auto developmentVersionKey="updates/whatsNewDevelopmentVersion";
QString versionKey(const QString& applicationDirectory){
    const auto directory=applicationDirectory.isEmpty()?QCoreApplication::applicationDirPath():applicationDirectory;
    QFile policy(QDir(directory).filePath("release-policy.json"));
    if(policy.open(QIODevice::ReadOnly)&&policy.size()<=4096&&
       QJsonDocument::fromJson(policy.readAll()).object().value("channel").toString()=="stable")return seenVersionKey;
    return developmentVersionKey;
}
bool hasContent(const QString& markdown){
    QTextDocument document;document.setMarkdown(markdown);
    return !document.toPlainText().trimmed().isEmpty();
}
std::optional<update::Version> storedVersion(QSettings& settings,const QString& key){
    try{return update::Version::parse(settings.value(key).toString());}
    catch(const std::exception&){return {};}
}
void storeVersion(QSettings& settings,const update::Version& version,const QString& key){
    settings.setValue(key,version.text());settings.sync();
    if(settings.status()!=QSettings::NoError)qWarning("Could not save the What's New version preference");
}
}

bool ReleaseNotes::empty()const{return !hasContent(features)&&!hasContent(improvements)&&!hasContent(fixes);}

ReleaseNotes releaseNotesForVersion(const QString& markdown,const QString& version){
    ReleaseNotes result;QString* section=nullptr;bool inVersion=false;
    const auto heading=QStringLiteral("# Compositor Windows ")+version;
    for(const auto& line:markdown.split(QLatin1Char('\n'))){
        const auto text=line.trimmed();
        if(text.startsWith(QStringLiteral("# "))){
            if(inVersion)break;
            inVersion=(text==heading);section=nullptr;continue;
        }
        if(!inVersion)continue;
        if(text.startsWith(QStringLiteral("## "))){
            if(text==QStringLiteral("## New features"))section=&result.features;
            else if(text==QStringLiteral("## Improvements"))section=&result.improvements;
            else if(text==QStringLiteral("## Fixes"))section=&result.fixes;
            else section=nullptr;
            continue;
        }
        if(section)*section+=line+QLatin1Char('\n');
    }
    return result;
}
ReleaseNotes installedReleaseNotes(){
    initializeReleaseNotesResource();
    QFile file(":/docs/release-notes.md");
    if(!file.open(QIODevice::ReadOnly))return {};
    return releaseNotesForVersion(QString::fromUtf8(file.readAll()),QCoreApplication::applicationVersion());
}
bool releaseNotesDue(QSettings& settings,const update::Version& installed,const QString& applicationDirectory){
    const auto seen=storedVersion(settings,versionKey(applicationDirectory));
    return !seen||installed>*seen;
}
void acknowledgeReleaseNotes(QSettings& settings,const update::Version& installed,const QString& applicationDirectory){
    const auto key=versionKey(applicationDirectory);
    const auto seen=storedVersion(settings,key);
    if(!seen||installed>*seen)storeVersion(settings,installed,key);
}

QDialog* openWhatsNewDialog(QWidget* parent,const QString& version,const ReleaseNotes& notes){
    auto* dialog=new QDialog(parent);dialog->setObjectName("whatsNewDialog");
    dialog->setWindowTitle("What's New");dialog->setWindowModality(Qt::WindowModal);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowFlag(Qt::WindowContextHelpButtonHint,false);
    dialog->setWindowFlag(Qt::WindowCloseButtonHint,true);
    dialog->setStyleSheet(R"(
        QDialog#whatsNewDialog { background:#222326; border:1px solid #3a3d44; border-radius:12px; }
        QLabel#whatsNewHeading { color:#f2f4f8; font-size:26px; font-weight:600; }
        QLabel#whatsNewVersion { color:#aab0ba; font-size:13px; }
        QTextBrowser#whatsNewContent { background:#1c1d20; border:1px solid #37393f; border-radius:8px; padding:12px; color:#e4e5e7; }
        QPushButton#whatsNewContinue { min-width:110px; padding:8px 18px; }
    )");
    auto* layout=new QVBoxLayout(dialog);layout->setContentsMargins(24,22,24,20);layout->setSpacing(12);
    auto* heading=new QLabel("What's New");heading->setObjectName("whatsNewHeading");
    auto* subtitle=new QLabel(QString("Compositor Windows %1").arg(version));subtitle->setObjectName("whatsNewVersion");
    layout->addWidget(heading);layout->addWidget(subtitle);
    auto* content=new QTextBrowser;content->setObjectName("whatsNewContent");
    content->setAccessibleName("Release notes");content->setOpenExternalLinks(true);
    content->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    content->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    content->setMinimumSize(0,0);
    QString markdown;
    const auto append=[&](const QString& title,const QString& body){if(hasContent(body))markdown+=QStringLiteral("## ")+title+QStringLiteral("\n\n")+body.trimmed()+QStringLiteral("\n\n");};
    append("New features",notes.features);append("Improvements",notes.improvements);append("Fixes",notes.fixes);
    content->setMarkdown(markdown);layout->addWidget(content,1);
    auto* buttons=new QHBoxLayout;buttons->addStretch();
    auto* proceed=new QPushButton("Continue");proceed->setObjectName("whatsNewContinue");proceed->setDefault(true);
    buttons->addWidget(proceed);layout->addLayout(buttons);
    QObject::connect(proceed,&QPushButton::clicked,dialog,&QDialog::accept);

    dialog->ensurePolished();
    const QRect parentBounds=parent?QRect(parent->mapToGlobal(QPoint(0,0)),parent->size()):QRect();
    auto* screen=QGuiApplication::screenAt(parentBounds.center());
    if(!screen)screen=QGuiApplication::primaryScreen();
    const QRect available=screen?screen->availableGeometry():parentBounds;
    QRect bounds=parentBounds.intersected(available);if(bounds.isEmpty())bounds=available;
    const int width=std::min(840,std::max(1,bounds.width()*85/100));
    const int height=std::min(760,std::max(1,bounds.height()*83/100));
    dialog->resize(width,height);
    dialog->move(bounds.center()-QPoint(dialog->width()/2,dialog->height()/2));
    dialog->open();proceed->setFocus(Qt::OtherFocusReason);
    return dialog;
}
}

namespace compositor {
void MainWindow::showWhatsNew(){
    if(auto* dialog=findChild<QDialog*>("whatsNewDialog");dialog&&dialog->isVisible()){
        dialog->raise();dialog->activateWindow();return;
    }
    const auto notes=ui::installedReleaseNotes();if(notes.empty())return;
    std::optional<update::Version> installed;
    try{installed=update::Version::parse(QCoreApplication::applicationVersion());}catch(const std::exception&){}
    if(!installed)return;
    QSettings settings;
    if(ui::releaseNotesDue(settings,*installed)){startWhatsNew();return;}
    ui::openWhatsNewDialog(this,installed->text(),notes);
}
void MainWindow::startWhatsNew(std::function<void()> finished){
    if(findChild<QTimer*>("whatsNewStartup")||findChild<QDialog*>("whatsNewDialog"))return;
    auto* timer=new QTimer(this);timer->setObjectName("whatsNewStartup");timer->setInterval(100);
    connect(timer,&QTimer::timeout,this,[this,timer,finished=std::move(finished)]{
        if(closingWindow_||!isVisible()||QApplication::activeModalWidget()||QApplication::activePopupWidget()||editPanel_||
           (workspaceDrops_&&!workspaceDrops_->idle())||(importQueue_&&!importQueue_->idle()))return;
        timer->stop();timer->deleteLater();
        std::optional<update::Version> installed;
        try{installed=update::Version::parse(QCoreApplication::applicationVersion());}catch(const std::exception&){}
        QSettings settings;
        if(!installed||!ui::releaseNotesDue(settings,*installed)){if(finished)finished();return;}
        const auto notes=ui::installedReleaseNotes();
        if(notes.empty()){if(finished)finished();return;}
        QPointer<QWidget> focus=focusWidget();
        auto* dialog=ui::openWhatsNewDialog(this,installed->text(),notes);
        connect(dialog,&QDialog::finished,this,[this,installed,focus,finished](int){
            QSettings preferences;ui::acknowledgeReleaseNotes(preferences,*installed);
            QTimer::singleShot(0,this,[focus]{if(focus&&focus->isVisible()&&focus->isEnabled())focus->setFocus(Qt::OtherFocusReason);});
            if(finished)finished();
        });
    });
    timer->start();
}
}
