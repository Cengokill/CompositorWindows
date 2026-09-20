#pragma once
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <filesystem>
#include <compare>
#include <functional>
#include <memory>
#include <vector>

namespace compositor::update {
struct Version {unsigned major{},minor{},patch{};static Version parse(const QString&);QString text()const;auto operator<=>(const Version&)const=default;};
struct Payload {QString path;std::uint64_t size{};QByteArray sha256;};
struct Manifest {Version version;QString channel;std::vector<Payload> files;QByteArray signedBytes,signature;};
struct ActiveState {Version current;QString channel{"test"};QString previous;bool pending{};};
struct Options {
    bool allowTestKey{};
    std::uint64_t maxPayloadBytes{4ull*1024*1024*1024};
    std::function<bool()> cancelled;
    std::function<void(std::uint64_t,std::uint64_t)> progress;
    // Test-only fault injection. Ignored unless test trust was explicitly enabled.
    enum class Fault {None,AfterVersionRename,AfterPendingActivation,CrashAfterPendingActivation,BeforeFinalize};
    Fault fault{Fault::None};
    unsigned healthTimeoutMs{120000};
};
struct CheckResult {Manifest manifest;bool available{};};
struct InstallResult {QString version;bool installed{},rolledBack{};};
struct UninstallResult {std::uint64_t removedFiles{};QStringList retainedPaths;};
class Updater {
public:
    // Root must already contain install.json, versions and state from packaging.
    explicit Updater(std::filesystem::path root);
    const std::filesystem::path& root()const{return root_;}
    ActiveState state()const;
    CheckResult check(const QString& testFeed,const Options&)const;
    InstallResult install(const QString& testFeed,const Options&);
    // Launcher recovery: an interrupted activation rolls back conservatively.
    ActiveState recover();
    // Remove only signature-listed files that still match their packaged hashes.
    // User changes, .comp packages, unknown files and untrusted versions remain.
    UninstallResult uninstallPayloads(const Options&);
private:std::filesystem::path root_;
};
// Trust verification is independent from installation and never executes payloads.
Manifest verifyManifest(const QByteArray& envelope,const Options&);
QByteArray sha256(const QByteArray&);
QString quoteWindowsArgument(const QString&);
int launchCurrent(const std::filesystem::path& root,const QStringList& arguments,bool waitForExit=false);
}
