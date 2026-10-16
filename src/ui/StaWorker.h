#pragma once
#include <QEventLoop>
#include <QObject>
#include <QThread>
#include <Windows.h>
#include <exception>
#include <functional>
#include <stdexcept>

namespace compositor::ui {
template<typename Callable>
void runOnStaThread(Callable&& work){
    std::exception_ptr error;
    auto* thread=QThread::create([&work,&error]{
        const HRESULT apartment=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
        struct Release{HRESULT value;~Release(){if(SUCCEEDED(value))CoUninitialize();}} release{apartment};
        try{work();}catch(...){error=std::current_exception();}
    });
    if(!thread)throw std::runtime_error("Cannot create STA worker thread");
    QEventLoop loop;
    QObject::connect(thread,&QThread::finished,thread,&QObject::deleteLater);
    QObject::connect(thread,&QThread::finished,&loop,&QEventLoop::quit,Qt::QueuedConnection);
    thread->start();
    if(!thread->isFinished())loop.exec(QEventLoop::ExcludeUserInputEvents);
    if(error)std::rethrow_exception(error);
}
}
