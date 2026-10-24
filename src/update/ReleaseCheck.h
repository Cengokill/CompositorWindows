#pragma once
#include "update/Updater.h"
#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <functional>
#include <optional>
class QSettings;

namespace compositor::update {
// Stable release notification. Installation stays manual: this only reads the
// latest GitHub release and never downloads or runs a payload.
inline constexpr auto stableReleaseUrl="https://api.github.com/repos/Cengokill/CompositorWindows/releases/latest";
inline constexpr auto releasePagePrefix="/Cengokill/CompositorWindows/releases/";
inline constexpr qsizetype releaseResponseLimit=256*1024;
inline constexpr qint64 releaseCheckIntervalSeconds=24*60*60;
struct ReleaseInfo {Version version;QString tag;QString pageUrl;};
// Strict reader for GitHub's latest-release JSON. Throws on malformed or
// oversized input; returns nothing for drafts and prereleases.
std::optional<ReleaseInfo> parseLatestRelease(const QByteArray& json);
// Stable URL. Debug builds alone honor COMPOSITOR_RELEASE_CHECK_URL for manual
// tests; Release builds ignore it and never read the development update feed.
QString releaseCheckUrl();
// Blocking HTTPS GET with the system proxy, bounded time and size. Worker threads only.
// cancelled is also polled from a helper thread; once it returns true the request
// handle is closed, which aborts the pending WinHTTP call.
QByteArray fetchReleaseMetadata(const QString& url,const std::function<bool()>& cancelled);
// Settings are untrusted: unknown values fall back to the defaults.
bool automaticChecksEnabled(QSettings&);
void setAutomaticChecksEnabled(QSettings&,bool);
bool releaseCheckDue(QSettings&,const QDateTime& nowUtc);
void recordReleaseCheck(QSettings&,const QDateTime& nowUtc);
std::optional<Version> skippedRelease(QSettings&);
void skipRelease(QSettings&,const Version&);
}