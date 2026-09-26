#include "ui/MainWindow.h"
#include "ui/VisualStyle.h"
#include <QApplication>
#include <QAbstractButton>
#include <QDialog>
#include <QLabel>
#include <QMenuBar>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QVBoxLayout>
#include <QPainter>
#include <QProcess>
#include <Windows.h>
#include <dwmapi.h>
#include <iostream>

using namespace compositor;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void capture(MainWindow& window,const QString& path){
    const auto hwnd=reinterpret_cast<HWND>(window.winId());RECT bounds{};GetWindowRect(hwnd,&bounds);
    const int width=bounds.right-bounds.left,height=bounds.bottom-bounds.top;
    auto dc=GetDC(hwnd);auto memory=CreateCompatibleDC(dc);auto bitmap=CreateCompatibleBitmap(dc,width,height);auto old=SelectObject(memory,bitmap);
    const auto captured=PrintWindow(hwnd,memory,2);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    QImage frame(width,height,QImage::Format_RGB32);SelectObject(memory,old);const auto copied=GetDIBits(memory,bitmap,0,UINT(height),frame.bits(),&info,DIB_RGB_COLORS);DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(hwnd,dc);
    require(captured&&copied==height,"window frame capture");
    POINT origin{};ClientToScreen(hwnd,&origin);const auto ratio=window.devicePixelRatioF();const auto offset=window.canvas()->mapTo(&window,QPoint{});
    auto canvas=window.canvas()->captureRendered();require(!canvas.isNull(),"canvas remains renderable after title bar switch");
    QPainter painter(&frame);painter.drawImage(QRectF(origin.x-bounds.left+offset.x()*ratio,origin.y-bounds.top+offset.y()*ratio,window.canvas()->width()*ratio,window.canvas()->height()*ratio),canvas);painter.end();require(frame.save(path),"save title bar capture");
}
}
int main(int argc,char** argv){
    QApplication app(argc,argv);app.setOrganizationName("CompositorChromeFixture");app.setApplicationName("WindowChrome");
    QTemporaryDir directory;QSettings::setDefaultFormat(QSettings::IniFormat);
    const auto root=argc==3?QString::fromLocal8Bit(argv[2]):directory.path();QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root);QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,root);
    try{
        if(argc==3){ui::installVisualStyle();require(ui::macTitleBarEnabled(),"Mac preference survives process restart");return 0;}
        require(argc==2&&directory.isValid(),"provide capture directory");QDir().mkpath(QString::fromLocal8Bit(argv[1]));
        MainWindow window(true);window.addFeasibilityDocument();window.resize(1280,820);window.show();QTest::qWait(180);
        auto* action=window.findChild<QAction*>("macTitleBarAction");require(action&&action->isEnabled()&&!action->isChecked()&&!ui::macTitleBarEnabled(),"Windows is the default");
        auto* canvas=window.canvas();auto* tabs=window.findChild<QTabWidget*>();const int tabCount=tabs->count();
        QDialog dialog(&window);dialog.setWindowFlags(Qt::Tool|Qt::WindowTitleHint|Qt::WindowCloseButtonHint);dialog.setWindowTitle("Title bar preview");auto* layout=new QVBoxLayout(&dialog);layout->addWidget(new QLabel("The editor design stays the same."));dialog.show();QTest::qWait(30);
        const auto theme=qApp->styleSheet();const auto font=qApp->font();
        for(bool mac:{false,true,false}){
            if(action->isChecked()!=mac)action->trigger();QTest::qWait(120);
            require(ui::macTitleBarEnabled()==mac&&QSettings().value("appearance/macTitleBar",false).toBool()==mac,"toggle updates and persists preference");
            require(window.windowFlags().testFlag(Qt::FramelessWindowHint)==mac&&dialog.windowFlags().testFlag(Qt::FramelessWindowHint)==mac,"main window and existing dialog update together");
            require(window.isVisible()&&dialog.isVisible()&&window.canvas()==canvas&&tabs->count()==tabCount,"switch preserves windows and open canvas");
            require(qApp->styleSheet()==theme&&qApp->font()==font,"interior visual design unchanged");
            if(!mac){const auto hwnd=reinterpret_cast<HWND>(window.winId());const auto style=GetWindowLongPtrW(hwnd,GWL_STYLE);require((style&(WS_CAPTION|WS_SYSMENU|WS_THICKFRAME|WS_MAXIMIZEBOX|WS_MINIMIZEBOX))==(WS_CAPTION|WS_SYSMENU|WS_THICKFRAME|WS_MAXIMIZEBOX|WS_MINIMIZEBOX),"native caption and system controls");BOOL dark=FALSE;require(SUCCEEDED(DwmGetWindowAttribute(hwnd,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark)))&&dark,"native dark title bar");
                SendMessageW(hwnd,WM_SYSCOMMAND,SC_MAXIMIZE,0);QTest::qWait(30);require(window.isMaximized(),"native maximize");SendMessageW(hwnd,WM_SYSCOMMAND,SC_MINIMIZE,0);QTest::qWait(30);require(window.isMinimized(),"native minimize");SendMessageW(hwnd,WM_SYSCOMMAND,SC_RESTORE,0);QTest::qWait(30);if(window.isMaximized()){SendMessageW(hwnd,WM_SYSCOMMAND,SC_RESTORE,0);QTest::qWait(30);}require(!window.isMinimized()&&!window.isMaximized(),"native restore");
            }else{
                QAbstractButton* maximize=nullptr;for(auto* button:window.findChildren<QAbstractButton*>())if(button->accessibleName()=="Maximize or restore window")maximize=button;
                require(maximize&&maximize->isVisible(),"Mac controls visible");maximize->click();QTest::qWait(30);require(window.isMaximized(),"Mac maximize");maximize->click();QTest::qWait(30);require(!window.isMaximized(),"Mac restore");
            }
            capture(window,QString::fromLocal8Bit(argv[1])+(mac?"/mac.png":"/windows.png"));
        }
        window.showMaximized();QTest::qWait(30);action->trigger();QTest::qWait(30);require(window.isMaximized(),"maximized state survives switch to Mac");action->trigger();QTest::qWait(30);require(window.isMaximized(),"maximized state survives switch to Windows");window.showNormal();QTest::qWait(30);
        SendMessageW(reinterpret_cast<HWND>(dialog.winId()),WM_CLOSE,0,0);QTest::qWait(30);require(!dialog.isVisible(),"native dialog close");
        action->trigger();QProcess child;child.start(QCoreApplication::applicationFilePath(),{"--persisted",root});require(child.waitForFinished(10000)&&child.exitCode()==0,"persisted mode in separate process");
        QDialog next(&window);next.show();QTest::qWait(30);require(next.windowFlags().testFlag(Qt::FramelessWindowHint),"new dialogs inherit Mac preference");
        bool closed=false;for(auto* button:next.findChildren<QAbstractButton*>())if(button->accessibleName()=="Close window"){button->click();closed=true;break;}require(closed&&!next.isVisible(),"Mac dialog close");
        action->trigger();std::cout<<"PASS native Windows default; live toggle; system controls; dialog inheritance; canvas survival; persistent preference; unchanged interior theme\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
