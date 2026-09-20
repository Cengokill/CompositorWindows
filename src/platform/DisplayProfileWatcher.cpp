#include "DisplayProfileWatcher.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <QThread>
#include <QTimer>
#include <QWinEventNotifier>
#include <stdexcept>
#include <utility>

namespace compositor::platform {
namespace {
constexpr DWORD notifyFilter=REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_CHANGE_LAST_SET |
    REG_NOTIFY_CHANGE_ATTRIBUTES | REG_NOTIFY_CHANGE_SECURITY | REG_NOTIFY_THREAD_AGNOSTIC;
HKEY hiveHandle(DisplayProfileRegistryHive hive) {
    switch(hive) {
    case DisplayProfileRegistryHive::CurrentUser:return HKEY_CURRENT_USER;
    case DisplayProfileRegistryHive::LocalMachine:return HKEY_LOCAL_MACHINE;
    }
    return nullptr;
}
bool validSubkey(const std::wstring& subkey) {
    if(subkey.empty() || subkey.size()>16383 || subkey.front()==L'\\' || subkey.back()==L'\\' ||
       subkey.find(L'\0')!=std::wstring::npos || subkey.find(L'/')!=std::wstring::npos) return false;
    size_t start=0;
    while(start<subkey.size()) {
        const auto end=subkey.find(L'\\',start);
        const auto part=subkey.substr(start,end==std::wstring::npos?end:end-start);
        if(part.empty() || part==L"." || part==L".." || part.size()>255) return false;
        if(end==std::wstring::npos) break;
        start=end+1;
    }
    return true;
}
struct Registration {
    HKEY key{};
    HANDLE event{};
    std::unique_ptr<QWinEventNotifier> notifier;
    ~Registration() {
        // Closing the key can signal the event: detach Qt before either close.
        notifier.reset();
        if(key) RegCloseKey(key);
        if(event) CloseHandle(event);
    }
};
}

struct DisplayProfileWatcher::Impl {
    struct Entry {
        DisplayProfileWatchStatus status;
        std::unique_ptr<Registration> registration;
        bool pending{};
    };
    DisplayProfileWatcher* owner;
    std::function<void()> onChange;
    QTimer timer;
    std::vector<Entry> entries;

    Impl(DisplayProfileWatcher* object,std::vector<DisplayProfileWatchTarget> targets,
         std::function<void()> callback):owner(object),onChange(std::move(callback)) {
        requireThread();
        if(targets.empty() || targets.size()>8) throw std::invalid_argument("Display profile watcher requires 1 to 8 targets");
        timer.setSingleShot(true);
        timer.setTimerType(Qt::PreciseTimer);
        timer.setInterval(DisplayProfileWatcher::coalesceMilliseconds);
        QObject::connect(&timer,&QTimer::timeout,owner,[this]{flush();});
        entries.reserve(targets.size());
        for(auto& target:targets) {
            Entry entry;
            entry.status.target=std::move(target);
            entries.push_back(std::move(entry));
        }
        for(size_t index=0;index<entries.size();++index) registerEntry(index);
    }
    ~Impl() { timer.stop(); entries.clear(); }
    void requireThread() const {
        if(QThread::currentThread()!=owner->thread())
            throw std::logic_error("Display profile watcher must be used on its owning thread");
    }
    void unavailable(Entry& entry,LSTATUS error,const char* operation) {
        entry.registration.reset();
        entry.status.available=false;
        entry.status.error=static_cast<unsigned long>(error);
        entry.status.diagnostic=std::string(operation)+" failed (Win32 "+std::to_string(error)+")";
    }
    void registerEntry(size_t index) {
        auto& entry=entries.at(index);
        auto& status=entry.status;
        const auto root=hiveHandle(status.target.hive);
        if(!root || !validSubkey(status.target.subkey)) {
            unavailable(entry,ERROR_INVALID_PARAMETER,"Display profile registry target validation");
            return;
        }
        // Arm the new handle before releasing the old completed registration.
        // A creation/deletion race retries at most four times; failure is visible.
        for(int attempt=0;attempt<4;++attempt) {
            auto registration=std::make_unique<Registration>();
            std::wstring candidate=status.target.subkey;
            LSTATUS error=ERROR_SUCCESS;
            for(;;) {
                error=RegOpenKeyExW(root,candidate.c_str(),0,KEY_NOTIFY|KEY_WOW64_64KEY,&registration->key);
                if(error==ERROR_SUCCESS) break;
                if(error!=ERROR_FILE_NOT_FOUND && error!=ERROR_PATH_NOT_FOUND && error!=ERROR_KEY_DELETED) break;
                if(candidate.empty()) break;
                const auto split=candidate.find_last_of(L'\\');
                candidate=split==std::wstring::npos?std::wstring{}:candidate.substr(0,split);
            }
            if(error!=ERROR_SUCCESS) {
                unavailable(entry,error,"Open display profile notification key");
                return;
            }
            registration->event=CreateEventW(nullptr,TRUE,FALSE,nullptr);
            if(!registration->event) {
                unavailable(entry,static_cast<LSTATUS>(GetLastError()),"Create display profile notification event");
                return;
            }
            error=RegNotifyChangeKeyValue(registration->key,TRUE,notifyFilter,registration->event,TRUE);
            if(error==ERROR_KEY_DELETED) continue;
            if(error!=ERROR_SUCCESS) {
                unavailable(entry,error,"Register display profile notification");
                return;
            }
            registration->notifier=std::make_unique<QWinEventNotifier>(registration->event,owner);
            QObject::connect(registration->notifier.get(),&QWinEventNotifier::activated,owner,[this,index] {
                auto& changed=entries.at(index);
                // One registration produces one event. Leave it disabled until
                // the coalescing timer replaces it; never duplicate pending waits.
                changed.registration->notifier->setEnabled(false);
                changed.pending=true;
                if(!timer.isActive()) timer.start();
            });
            status.watchedSubkey=std::move(candidate);
            status.available=true;
            status.ancestor=status.watchedSubkey!=status.target.subkey;
            status.error=ERROR_SUCCESS;
            status.diagnostic=status.ancestor?"Watching the nearest existing ancestor until the profile key exists":"";
            entry.registration=std::move(registration);
            return;
        }
        unavailable(entry,ERROR_KEY_DELETED,"Register display profile notification after repeated deletion");
    }
    void flush() {
        bool changed=false;
        for(size_t index=0;index<entries.size();++index) {
            auto& entry=entries[index];
            if(!entry.pending) continue;
            entry.pending=false;
            registerEntry(index);
            changed=true;
        }
        // All active subscriptions are armed before querying current state.
        // Copy the callback because it may synchronously destroy this watcher.
        if(changed) { auto callback=onChange; if(callback) callback(); }
    }
};

std::vector<DisplayProfileWatchTarget> DisplayProfileWatcher::defaultTargets() {
    return {
        {DisplayProfileRegistryHive::CurrentUser,L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\ICM\\ProfileAssociations\\Display\\{4d36e96e-e325-11ce-bfc1-08002be10318}"},
        {DisplayProfileRegistryHive::LocalMachine,L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e96e-e325-11ce-bfc1-08002be10318}"}
    };
}
DisplayProfileWatcher::DisplayProfileWatcher(std::function<void()> onChange,QObject* parent)
    :DisplayProfileWatcher(defaultTargets(),std::move(onChange),parent) {}
DisplayProfileWatcher::DisplayProfileWatcher(std::vector<DisplayProfileWatchTarget> targets,
    std::function<void()> onChange,QObject* parent):QObject(parent),impl_(std::make_unique<Impl>(this,std::move(targets),std::move(onChange))) {}
DisplayProfileWatcher::~DisplayProfileWatcher()=default;
std::vector<DisplayProfileWatchStatus> DisplayProfileWatcher::statuses() const {
    impl_->requireThread();
    std::vector<DisplayProfileWatchStatus> result;
    result.reserve(impl_->entries.size());
    for(const auto& entry:impl_->entries) result.push_back(entry.status);
    return result;
}
bool DisplayProfileWatcher::retry() {
    impl_->requireThread();
    bool recovered=false;
    for(size_t index=0;index<impl_->entries.size();++index) {
        if(impl_->entries[index].status.available) continue;
        impl_->registerEntry(index);
        recovered |= impl_->entries[index].status.available;
    }
    return recovered;
}
}
