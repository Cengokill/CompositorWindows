#include "platform/DisplayProfileWatcher.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QTimer>
#include <QWinEventNotifier>
#include <QUuid>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
using namespace compositor::platform;
#define REQUIRE(...) do{if(!(__VA_ARGS__))throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+": " #__VA_ARGS__);}while(false)
namespace {
struct Key {
    HKEY value{};
    ~Key(){if(value)RegCloseKey(value);}
    Key()=default; Key(const Key&)=delete; Key& operator=(const Key&)=delete;
};
struct Fixture {
    static constexpr wchar_t prefix[]=L"Software\\CompositorWindows\\Tests\\";
    std::wstring path=std::wstring(prefix)+QUuid::createUuid().toString(QUuid::WithoutBraces).toStdWString();
    Key root;
    Fixture() {
        DWORD disposition{};
        REQUIRE(path.starts_with(prefix) && path.size()==std::size(prefix)-1+36);
        REQUIRE(RegCreateKeyExW(HKEY_CURRENT_USER,path.c_str(),0,nullptr,REG_OPTION_VOLATILE,
            KEY_ALL_ACCESS|KEY_WOW64_64KEY,nullptr,&root.value,&disposition)==ERROR_SUCCESS);
        REQUIRE(disposition==REG_CREATED_NEW_KEY);
        std::wcout<<L"volatile_fixture="<<path<<L'\n';
    }
    ~Fixture() {
        if(root.value) { RegDeleteTreeW(root.value,nullptr); RegCloseKey(root.value); root.value=nullptr; }
        RegDeleteKeyExW(HKEY_CURRENT_USER,path.c_str(),KEY_WOW64_64KEY,0);
    }
    void create(const wchar_t* child) {
        Key key; DWORD disposition{};
        REQUIRE(RegCreateKeyExW(root.value,child,0,nullptr,REG_OPTION_VOLATILE,KEY_ALL_ACCESS|KEY_WOW64_64KEY,
            nullptr,&key.value,&disposition)==ERROR_SUCCESS);
    }
    void set(const wchar_t* child,DWORD value) {
        Key key;
        REQUIRE(RegOpenKeyExW(root.value,child,0,KEY_SET_VALUE|KEY_WOW64_64KEY,&key.value)==ERROR_SUCCESS);
        REQUIRE(RegSetValueExW(key.value,L"SyntheticProfileGeneration",0,REG_DWORD,
            reinterpret_cast<const BYTE*>(&value),sizeof(value))==ERROR_SUCCESS);
    }
    void erase(const wchar_t* child) {
        REQUIRE(RegDeleteTreeW(root.value,child)==ERROR_SUCCESS);
        const auto error=RegDeleteKeyExW(root.value,child,KEY_WOW64_64KEY,0);
        REQUIRE(error==ERROR_SUCCESS || error==ERROR_FILE_NOT_FOUND);
    }
    DisplayProfileWatchTarget target(const wchar_t* child=L"") const {
        return {DisplayProfileRegistryHive::CurrentUser,path+(child[0]?L"\\"+std::wstring(child):L"")};
    }
};
void pump(int milliseconds) {
    QElapsedTimer timer;timer.start();
    while(timer.elapsed()<milliseconds){QCoreApplication::processEvents();QThread::msleep(1);}
}
void waitFor(const std::function<bool()>& predicate) {
    QElapsedTimer timer;timer.start();
    while(!predicate() && timer.elapsed()<3000){QCoreApplication::processEvents();QThread::msleep(1);}
    REQUIRE(predicate());
}
void rearm() {
    Fixture fixture;fixture.create(L"Nested\\Association");int calls=0;bool rightThread=true;
    DisplayProfileWatcher watcher({fixture.target()},[&]{
        rightThread &= QThread::currentThread()==QCoreApplication::instance()->thread();
        ++calls;
        if(calls==1) fixture.set(L"Nested\\Association",2);
    });
    REQUIRE(watcher.statuses().size()==1 && watcher.statuses()[0].available && !watcher.statuses()[0].ancestor);
    REQUIRE(!watcher.retry());
    fixture.set(L"Nested\\Association",1);waitFor([&]{return calls==2;});
    REQUIRE(rightThread);
    pump(150);REQUIRE(calls==2);
    fixture.set(L"",3);waitFor([&]{return calls==3;});
}
void missingAncestor() {
    Fixture fixture;int calls=0;
    DisplayProfileWatcher watcher({fixture.target(L"Absent\\Display")},[&]{++calls;});
    auto status=watcher.statuses()[0];
    REQUIRE(status.available && status.ancestor && status.watchedSubkey==fixture.path && !status.diagnostic.empty());
    fixture.create(L"Absent\\Display");waitFor([&]{return calls==1;});
    status=watcher.statuses()[0];
    REQUIRE(status.available && !status.ancestor && status.watchedSubkey==fixture.path+L"\\Absent\\Display");
    fixture.set(L"Absent\\Display",1);waitFor([&]{return calls==2;});
}
void deleteRecreate() {
    Fixture fixture;fixture.create(L"Display");int calls=0;
    DisplayProfileWatcher watcher({fixture.target(L"Display")},[&]{++calls;});
    fixture.erase(L"Display");waitFor([&]{return calls==1;});
    REQUIRE(watcher.statuses()[0].available && watcher.statuses()[0].ancestor);
    fixture.create(L"Display");waitFor([&]{return calls==2;});
    REQUIRE(watcher.statuses()[0].available && !watcher.statuses()[0].ancestor);
    fixture.set(L"Display",19);waitFor([&]{return calls==3;});
}
void boundedBurst() {
    Fixture fixture;fixture.create(L"A");fixture.create(L"B");int calls=0;
    DisplayProfileWatcher watcher({fixture.target(L"A"),fixture.target(L"B")},[&]{++calls;});
    for(DWORD value=0;value<100;++value){fixture.set(L"A",value);fixture.set(L"B",value);}
    waitFor([&]{return calls==1;});pump(120);REQUIRE(calls==1);
    QTimer writer;writer.setInterval(1);DWORD value=100;
    QObject::connect(&writer,&QTimer::timeout,[&]{fixture.set(L"A",++value);fixture.set(L"B",value);});
    writer.start();pump(350);writer.stop();pump(150);
    REQUIRE(calls>=2 && calls<=9); // At most 7 interval callbacks plus the trailing delivery and first burst.
    std::cout<<"coalesced_callbacks="<<calls<<" writes="<<2*value<<'\n';
}
void teardown() {
    Fixture fixture;int calls=0;DWORD cold{};REQUIRE(GetProcessHandleCount(GetCurrentProcess(),&cold));
    // Measure the framework event-loop startup separately using only Qt's
    // notifier, before measuring the production watcher's owned resources.
    HANDLE event=CreateEventW(nullptr,TRUE,FALSE,nullptr);REQUIRE(event);
    int controlCalls=0;
    {
        QWinEventNotifier control(event);
        QTimer controlTimer;controlTimer.setSingleShot(true);controlTimer.setTimerType(Qt::PreciseTimer);
        controlTimer.setInterval(DisplayProfileWatcher::coalesceMilliseconds);
        QObject::connect(&controlTimer,&QTimer::timeout,[&]{++controlCalls;});
        QObject::connect(&control,&QWinEventNotifier::activated,[&]{control.setEnabled(false);controlTimer.start();});
        REQUIRE(SetEvent(event));waitFor([&]{return controlCalls==1;});
    }
    REQUIRE(CloseHandle(event));pump(50);
    DWORD baseline{};REQUIRE(GetProcessHandleCount(GetCurrentProcess(),&baseline));
    std::cout<<"plain_qt_control_cold="<<cold<<" after="<<baseline<<'\n';
    for(int index=0;index<64;++index) {
        auto watcher=std::make_unique<DisplayProfileWatcher>(std::vector{fixture.target()},[&]{++calls;});
        fixture.set(L"",DWORD(index));
        QCoreApplication::processEvents(); // Queue the timer, then destroy its owner.
        watcher.reset();
        if(index%16==15){DWORD handles{};REQUIRE(GetProcessHandleCount(GetCurrentProcess(),&handles));std::cout<<"iteration="<<index+1<<" handles="<<handles<<'\n';}
    }
    pump(150);REQUIRE(calls==0);
    DWORD after{};REQUIRE(GetProcessHandleCount(GetCurrentProcess(),&after));
    std::cout<<"handles_before="<<baseline<<" after="<<after<<'\n';
    REQUIRE(after<=baseline); // No extra handles after the independent Qt control.
    std::unique_ptr<DisplayProfileWatcher> self;
    self=std::make_unique<DisplayProfileWatcher>(std::vector{fixture.target()},[&]{++calls;self.reset();});
    fixture.set(L"",99);waitFor([&]{return calls==1;});REQUIRE(!self);
    fixture.set(L"",100);pump(150);REQUIRE(calls==1);
}
void unavailable() {
    int calls=0;
    DisplayProfileWatcher watcher({{static_cast<DisplayProfileRegistryHive>(99),L"Software\\Example"},
        {DisplayProfileRegistryHive::CurrentUser,std::wstring(L"Software\\Bad\0Name",17)}},[&]{++calls;});
    for(const auto& status:watcher.statuses()) REQUIRE(!status.available && status.error==ERROR_INVALID_PARAMETER && !status.diagnostic.empty());
    REQUIRE(!watcher.retry());pump(120);REQUIRE(calls==0);
}
void defaultReadOnly() {
    const auto targets=DisplayProfileWatcher::defaultTargets();
    REQUIRE(targets.size()==2 && targets[0].hive==DisplayProfileRegistryHive::CurrentUser && targets[1].hive==DisplayProfileRegistryHive::LocalMachine);
    REQUIRE(targets[0].subkey==L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\ICM\\ProfileAssociations\\Display\\{4d36e96e-e325-11ce-bfc1-08002be10318}");
    REQUIRE(targets[1].subkey==L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e96e-e325-11ce-bfc1-08002be10318}");
    DisplayProfileWatcher watcher([]{});
    for(const auto& status:watcher.statuses()) {
        // Registration can be unavailable on restricted systems; the error must
        // be explicit. Test fixtures above require actual successful delivery.
        REQUIRE(status.available ? status.error==0 : status.error!=0 && !status.diagnostic.empty());
        std::wcout<<L"production_key_available="<<status.available<<L" ancestor="<<status.ancestor
                  <<L" error="<<status.error<<L" watched="<<status.watchedSubkey<<L'\n';
    }
}
}
int main(int argc,char** argv) {
    QCoreApplication application(argc,argv);
    try {
        REQUIRE(argc==2);
        const std::map<std::string,void(*)()> tests{{"rearm_thread",rearm},{"missing_ancestor",missingAncestor},
            {"delete_recreate",deleteRecreate},{"bounded_burst",boundedBurst},{"teardown",teardown},
            {"unavailable",unavailable},{"default_read_only",defaultReadOnly}};
        REQUIRE(tests.contains(argv[1]));tests.at(argv[1])();std::cout<<"PASS "<<argv[1]<<'\n';return 0;
    } catch(const std::exception& error) {std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
