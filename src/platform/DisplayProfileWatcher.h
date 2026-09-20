#pragma once
#include <QObject>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace compositor::platform {
enum class DisplayProfileRegistryHive { CurrentUser, LocalMachine };
struct DisplayProfileWatchTarget {
    DisplayProfileRegistryHive hive{DisplayProfileRegistryHive::CurrentUser};
    std::wstring subkey;
};
struct DisplayProfileWatchStatus {
    DisplayProfileWatchTarget target;
    std::wstring watchedSubkey;
    bool available{};
    bool ancestor{};
    unsigned long error{};
    std::string diagnostic;
};
// Construct, use and destroy on the presentation thread with a Qt event loop.
// A notification means "query the current profile again"; it is not proof that
// the active monitor's profile changed. No profile bytes or registry values are
// read here. Registrations use KEY_NOTIFY only and never modify the registry.
class DisplayProfileWatcher final : public QObject {
public:
    explicit DisplayProfileWatcher(std::function<void()> onChange, QObject* parent=nullptr);
    // Alternate read-only locations allow isolated volatile-key verification.
    DisplayProfileWatcher(std::vector<DisplayProfileWatchTarget> targets,
                          std::function<void()> onChange, QObject* parent=nullptr);
    ~DisplayProfileWatcher() override;
    DisplayProfileWatcher(const DisplayProfileWatcher&)=delete;
    DisplayProfileWatcher& operator=(const DisplayProfileWatcher&)=delete;
    std::vector<DisplayProfileWatchStatus> statuses() const;
    // Retry only unavailable registrations; there is no background polling.
    // Returns true if at least one unavailable registration became available.
    bool retry();
    static std::vector<DisplayProfileWatchTarget> defaultTargets();
    static constexpr int coalesceMilliseconds=50;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
