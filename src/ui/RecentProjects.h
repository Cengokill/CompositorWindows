#pragma once
#include <QString>
#include <QStringList>
class QSettings;

namespace compositor::ui {
// File > Open Recent. Stored paths are untrusted settings: every read
// validates, deduplicates (Windows paths are case-insensitive), and caps them.
inline constexpr int recentProjectLimit=10;
inline constexpr auto recentProjectsKey="recentProjects";
QStringList recentProjects(QSettings&);
// Moves the project folder to the top of the list.
void recordRecentProject(QSettings&,const QString& path);
// Removes folders on local fixed drives that no longer exist and returns the rest.
// Network, removable and absent-drive entries are kept without touching them.
QStringList pruneRecentProjects(QSettings&);
void clearRecentProjects(QSettings&);
// Menu text: the folder name, with the parent folder added when two names match.
QStringList recentProjectLabels(const QStringList& paths);
}