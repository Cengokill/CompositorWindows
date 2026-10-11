#include "AppLogging.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QTextStream>
#include <QThread>
#include <QtGlobal>
#include <windows.h>

namespace compositor::logging {
namespace {
QMutex mutex;
QString path;
bool installed{};

QString typeName(QtMsgType type) {
    switch(type) {
    case QtDebugMsg: return "debug";
    case QtInfoMsg: return "info";
    case QtWarningMsg: return "warning";
    case QtCriticalMsg: return "critical";
    case QtFatalMsg: return "fatal";
    }
    return "unknown";
}

QString frameDescription(void* address) {
    MEMORY_BASIC_INFORMATION memory{};
    if(!VirtualQuery(address,&memory,sizeof(memory)))return QString("0x%1").arg(quintptr(address),0,16);
    wchar_t module[MAX_PATH]{};
    const auto length=GetModuleFileNameW(static_cast<HMODULE>(memory.AllocationBase),module,MAX_PATH);
    const auto moduleName=length?QString::fromWCharArray(module,length):QStringLiteral("<unknown>");
    const auto offset=quintptr(address)-quintptr(memory.AllocationBase);
    return QString("%1+0x%2").arg(moduleName).arg(offset,0,16);
}

QString stackTrace() {
    void* frames[32]{};
    const auto count=CaptureStackBackTrace(2,32,frames,nullptr);
    QString result{"stack:"};
    for(USHORT index=0;index<count;++index)
        result+=QString("\n  #%1 %2").arg(index).arg(frameDescription(frames[index]));
    return result;
}

void append(const QString& line) {
    QMutexLocker lock(&mutex);
    if(path.isEmpty())return;
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly|QIODevice::Append|QIODevice::Text))return;
    QTextStream stream(&file);
    stream<<line<<Qt::endl;
    stream.flush();
}

void messageHandler(QtMsgType type,const QMessageLogContext& context,const QString& message) {
    const auto timestamp=QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
    const auto threadId=QString::number(GetCurrentThreadId());
    auto line=QString("[%1][thread=%2][%3]").arg(timestamp,threadId,typeName(type));
    if(context.category&&!QString::fromUtf8(context.category).isEmpty())
        line+=QString("[%1]").arg(QString::fromUtf8(context.category));
    line+=" "+message;
    if(context.file)line+=QString(" (%1:%2)").arg(QString::fromUtf8(context.file)).arg(context.line);
    append(line);
    if(type==QtFatalMsg)append(stackTrace());
}
}

QString logPath() {
    QMutexLocker lock(&mutex);
    return path;
}

void install() {
    QMutexLocker lock(&mutex);
    if(installed)return;
    auto directory=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if(directory.isEmpty())directory=QDir::homePath()+"/AppData/Local/CompositorWindows";
    directory+="/logs";
    QDir().mkpath(directory);
    path=directory+"/compositor.log";
    installed=true;
    const auto started=QString("[%1][thread=%2][startup] diagnostic logging started")
        .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs))
        .arg(GetCurrentThreadId());
    QFile file(path);
    if(file.open(QIODevice::WriteOnly|QIODevice::Append|QIODevice::Text)){
        QTextStream stream(&file);
        stream<<started<<Qt::endl;
    }
    qInstallMessageHandler(messageHandler);
}

void trace(const QString& message) {
    const auto timestamp=QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
    append(QString("[%1][thread=%2][trace] %3").arg(timestamp).arg(GetCurrentThreadId()).arg(message));
}

}
