#include "ui/MainWindow.h"
#include "ui/VisualStyle.h"
#include <QApplication>
#include <QAbstractButton>
#include <QDialog>
#include <QDockWidget>
#include <QTreeWidget>
#include <QStatusBar>
#include <QToolBar>
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
namespace {
// Canvas-only full screen (View > Canvas-Only Full Screen, F11, Escape). Each case runs in its own process.
Document canvasOnlyDocument(){
    Document d;d.id="canvas-only";d.width=100;d.height=80;
    Layer base;base.id="base";base.name="Base";base.transform={0,0,100,80};base.raster=Raster::filled(100,80,{80,120,160,255});
    Layer top;top.id="top";top.name="Top";top.transform={20,16,50,40};top.raster=Raster::filled(50,40,{220,90,40,255});
    d.layers={base,top};return d;
}
QAction* commandAction(MainWindow& window,const QString& id){
    for(auto* action:window.findChildren<QAction*>())if(action->property("commandId").toString()==id)return action;
    throw std::runtime_error("Missing command");
}
void showEditor(MainWindow& window){
    window.resize(1280,820);window.show();window.activateWindow();
    require(QTest::qWaitForWindowActive(&window),"editor window becomes active");QTest::qWait(120);
}
void roundTrip(bool maximized){
    MainWindow window(true);
    auto& project=window.addProject(canvasOnlyDocument());
    showEditor(window);
    if(maximized){window.showMaximized();QTest::qWait(120);require(window.isMaximized(),"maximized before entry");}
    auto* canvas=window.canvas();
    auto* tabs=window.findChild<QTabWidget*>();
    auto* transform=window.findChild<QDockWidget*>("transformDock");
    auto* layers=window.findChild<QDockWidget*>("layersDock");
    auto* layerTree=window.findChild<QTreeWidget*>("layersTree");
    auto* canvasOnly=window.findChild<QAction*>("viewCanvasOnly");
    require(canvas==project.canvas&&tabs&&transform&&layers&&layerTree&&canvasOnly,"editor chrome present");
    transform->hide();layerTree->setFocus();QTest::qWait(60);
    require(QApplication::focusWidget()==layerTree,"focus set before entry");
    const auto tabCount=tabs->count(),tabIndex=tabs->currentIndex();
    const auto document=*project.document;const auto undoCount=project.history.undoCount();
    const auto normal=window.normalGeometry();
    const auto canvasBefore=canvas->size();
    const auto layersWidth=layers->width();
    commandAction(window,"view.actual")->trigger();QTest::qWait(60);
    const auto zoom=canvas->zoom;const auto pan=canvas->pan;
    require(canvas->captureRendered().pixelColor(0,0)!=QColor(0,0,0),"surround is not black outside canvas-only mode");

    QTest::keyClick(QApplication::focusWidget(),Qt::Key_F11);QTest::qWait(150);
    require(window.isFullScreen()&&canvasOnly->isChecked(),"F11 enters canvas-only full screen");
    require(window.menuBar()->isHidden()&&window.statusBar()->isHidden()&&tabs->tabBar()->isHidden(),"menu bar, status bar, and project tabs hidden");
    for(auto* bar:window.findChildren<QToolBar*>(QString(),Qt::FindDirectChildrenOnly))require(bar->isHidden(),"toolbars hidden in canvas-only mode");
    require(layers->isHidden()&&transform->isHidden(),"docks hidden in canvas-only mode");
    require(window.canvas()==canvas&&canvas->size().width()>=canvasBefore.width()&&canvas->size().height()>=canvasBefore.height(),"canvas keeps identity and fills the larger surface");
    require(canvas->captureRendered().pixelColor(0,0)==QColor(0,0,0),"surround painted black in canvas-only mode");
    require(canvas->zoom==zoom,"zoom unchanged in canvas-only mode");

    QTest::keyClick(canvas,Qt::Key_Escape);QTest::qWait(150);
    require(canvas->zoom==zoom&&canvas->pan==pan,"zoom and pan unchanged after exit");
    require(!window.isFullScreen()&&!canvasOnly->isChecked(),"Escape leaves canvas-only mode");
    require(window.normalGeometry()==normal,"previous window placement restored");
    require(window.isMaximized()==maximized,"previous maximized state restored");
    require(!window.menuBar()->isHidden()&&!window.statusBar()->isHidden()&&!tabs->tabBar()->isHidden(),"menu bar, status bar, and project tabs restored");
    require(!layers->isHidden()&&transform->isHidden(),"layers panel restored and closed transform panel stays closed");
    require(layers->width()==layersWidth,"layers panel width restored");
    require(window.canvas()==canvas&&tabs->count()==tabCount&&tabs->currentIndex()==tabIndex,"canvas and active project preserved");
    require(QApplication::focusWidget()==layerTree,"focus restored");
    require(*project.document==document&&project.history.undoCount()==undoCount,"document and history unchanged");
    require(canvas->captureRendered().pixelColor(0,0)!=QColor(0,0,0),"surround restored outside canvas-only mode");
}
void optionBarsStayHidden(){
    MainWindow window(true);
    window.addProject(canvasOnlyDocument());
    showEditor(window);
    auto* canvas=window.canvas();
    commandAction(window,"tool.brush")->trigger();QTest::qWait(60);
    auto* brush=window.findChild<QToolBar*>("brushOptions");
    require(brush&&brush->isVisible(),"brush options visible before entry");
    QTest::keyClick(canvas,Qt::Key_F11);QTest::qWait(150);
    require(window.isFullScreen()&&brush->isHidden(),"brush options hidden in canvas-only mode");
    commandAction(window,"tool.move")->trigger();QTest::qWait(60);
    require(brush->isHidden()&&window.findChild<QToolBar*>("transformOptions")->isHidden(),"tool change keeps option bars hidden in mode");
    QTest::keyClick(canvas,Qt::Key_Escape);QTest::qWait(150);
    require(!window.isFullScreen()&&window.findChild<QToolBar*>("transformOptions")->isVisible()&&brush->isHidden(),"exit shows only the option bar for the current tool");
}
void macTitleBarPreferences(){
    MainWindow window(true);
    window.addProject(canvasOnlyDocument());
    showEditor(window);
    auto* mac=window.findChild<QAction*>("macTitleBarAction");
    require(mac,"Mac title bar preference available");
    for(const bool enabled:{false,true}){
        if(mac->isChecked()!=enabled)mac->trigger();QTest::qWait(150);
        // Switching window flags re-creates the native frame, so the window must regain activation before the shortcut.
        window.activateWindow();require(QTest::qWaitForWindowActive(&window),"window active after title bar switch");
        QTest::keyClick(window.canvas(),Qt::Key_F11);QTest::qWait(150);
        require(window.isFullScreen(),enabled?"F11 enters canvas-only mode under the Mac title bar preference":"F11 enters canvas-only mode under the Windows title bar preference");
        require(window.windowFlags().testFlag(Qt::FramelessWindowHint)==enabled,"canvas-only mode keeps the title bar preference");
        QTest::keyClick(window.canvas(),Qt::Key_Escape);QTest::qWait(150);
        require(!window.isFullScreen(),"Escape exits canvas-only mode under the title bar preference");
        require(window.windowFlags().testFlag(Qt::FramelessWindowHint)==enabled&&!window.menuBar()->isHidden(),"exit keeps the title bar preference and menu bar");
    }
}
void escapeKeepsCropDraft(){
    MainWindow window(true);
    auto& project=window.addProject(canvasOnlyDocument());
    showEditor(window);
    const auto document=*project.document;
    commandAction(window,"tool.crop")->trigger();
    project.canvas->pointerDown({20,20},Qt::ControlModifier);project.canvas->pointerMove({60,50},Qt::ControlModifier);project.canvas->pointerUp({60,50},Qt::ControlModifier);
    auto* apply=commandAction(window,"crop.apply");
    require(apply->isEnabled()&&project.canvas->cropOverlay().has_value(),"pending crop before entry");
    QTest::keyClick(window.canvas(),Qt::Key_F11);QTest::qWait(150);
    require(window.isFullScreen(),"entered canvas-only mode with pending crop");
    QTest::keyClick(window.canvas(),Qt::Key_Escape);QTest::qWait(150);
    require(!window.isFullScreen()&&apply->isEnabled()&&project.canvas->cropOverlay().has_value(),"Escape exits mode without cancelling the crop");
    require(*project.document==document&&project.history.undoCount()==0,"pending crop does not change the document or history");
    QTest::keyClick(window.canvas(),Qt::Key_Escape);QTest::qWait(60);
    require(!apply->isEnabled(),"ordinary Escape still cancels the crop after exit");
}
void gestureBlocksToggle(){
    MainWindow window(true);
    window.addProject(canvasOnlyDocument());
    showEditor(window);
    auto* canvas=window.canvas();
    commandAction(window,"tool.hand")->trigger();
    QTest::keyClick(canvas,Qt::Key_F11);QTest::qWait(150);
    require(window.isFullScreen(),"entered canvas-only mode");
    const QPoint center(canvas->width()/2,canvas->height()/2);
    QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,center);QTest::qWait(30);
    QTest::keyClick(canvas,Qt::Key_F11);QTest::qWait(60);
    require(window.isFullScreen(),"F11 is ignored while a canvas drag is held");
    QTest::keyClick(canvas,Qt::Key_Escape);QTest::qWait(60);
    require(window.isFullScreen(),"Escape during a drag cancels only the drag and keeps the mode");
    QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,center);QTest::qWait(30);
    QTest::keyClick(canvas,Qt::Key_Escape);QTest::qWait(150);
    require(!window.isFullScreen(),"Escape exits after the drag ends");
}
void menuShortcutInMode(){
    MainWindow window(true);
    auto& project=window.addProject(canvasOnlyDocument());
    showEditor(window);
    auto* canvas=window.canvas();
    const auto layerCount=project.document->layers.size();
    commandAction(window,"layer.new")->trigger();QTest::qWait(30);
    require(project.document->layers.size()==layerCount+1,"layer added before entry");
    QTest::keyClick(canvas,Qt::Key_F11);QTest::qWait(150);
    require(window.isFullScreen(),"entered canvas-only mode");
    QTest::keyClick(canvas,Qt::Key_Z,Qt::ControlModifier);QTest::qWait(30);
    require(project.document->layers.size()==layerCount,"Ctrl+Z from the hidden Edit menu still undoes in canvas-only mode");
    QTest::keyClick(canvas,Qt::Key_Escape);QTest::qWait(150);
    require(!window.isFullScreen(),"Escape exits after menu shortcut");
}
void runCanvasOnly(const std::string& name){
    if(name=="canvas_only.round_trip_normal")roundTrip(false);
    else if(name=="canvas_only.round_trip_maximized")roundTrip(true);
    else if(name=="canvas_only.option_bars_stay_hidden")optionBarsStayHidden();
    else if(name=="canvas_only.mac_title_bar_preferences")macTitleBarPreferences();
    else if(name=="canvas_only.escape_keeps_crop_draft")escapeKeepsCropDraft();
    else if(name=="canvas_only.gesture_blocks_toggle")gestureBlocksToggle();
    else if(name=="canvas_only.menu_shortcut_in_mode")menuShortcutInMode();
    else throw std::runtime_error("unknown canvas-only case");
}
}
int main(int argc,char** argv){
    QApplication app(argc,argv);app.setOrganizationName("CompositorChromeFixture");app.setApplicationName("WindowChrome");
    QTemporaryDir directory;QSettings::setDefaultFormat(QSettings::IniFormat);
    const auto root=argc==3?QString::fromLocal8Bit(argv[2]):directory.path();QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root);QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,root);
    try{
        if(argc==2&&QString::fromLocal8Bit(argv[1]).startsWith("canvas_only.")){runCanvasOnly(argv[1]);std::cout<<"PASS "<<argv[1]<<'\n';return 0;}
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
