#pragma once
#include "update/Updater.h"
#include <QString>
class QDialog;
class QSettings;
class QWidget;

namespace compositor::ui {
struct ReleaseNotes {
    QString features, improvements, fixes;
    bool empty() const;
};

// Extract only the named sections under the exact installed-version heading.
ReleaseNotes releaseNotesForVersion(const QString& markdown,const QString& version);
ReleaseNotes installedReleaseNotes();
bool releaseNotesDue(QSettings& settings,const update::Version& installed,const QString& applicationDirectory={});
void acknowledgeReleaseNotes(QSettings& settings,const update::Version& installed,const QString& applicationDirectory={});
QDialog* openWhatsNewDialog(QWidget* parent,const QString& version,const ReleaseNotes& notes);
}
