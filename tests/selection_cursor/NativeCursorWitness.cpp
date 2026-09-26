#define main cursor_functional_main
#include "SelectionCursorCoverageTests.cpp"
#undef main
#include <Windows.h>
#include <cstring>

namespace {
QJsonObject nativeDiagnostic;
struct IconInfo {
    ICONINFO value{};
    ~IconInfo(){if(value.hbmMask)DeleteObject(value.hbmMask);if(value.hbmColor)DeleteObject(value.hbmColor);}
};
QImage nativePixels(HWND owner,HBITMAP bitmap){
    BITMAP dimensions{};
    require(GetObjectW(bitmap,sizeof(dimensions),&dimensions)==sizeof(dimensions),"Native cursor bitmap dimensions");
    require(dimensions.bmWidth>0&&dimensions.bmWidth<=2048&&dimensions.bmHeight>0&&dimensions.bmHeight<=2048,"Bounded native cursor pixels");
    QImage result(dimensions.bmWidth,dimensions.bmHeight,QImage::Format_ARGB32);
    BITMAPINFO description{};description.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    description.bmiHeader.biWidth=result.width();description.bmiHeader.biHeight=-result.height();
    description.bmiHeader.biPlanes=1;description.bmiHeader.biBitCount=32;description.bmiHeader.biCompression=BI_RGB;
    HDC dc=GetDC(owner);require(dc!=nullptr,"Owned native DC");
    const int rows=GetDIBits(dc,bitmap,0,UINT(result.height()),result.bits(),&description,DIB_RGB_COLORS);
    ReleaseDC(owner,dc);require(rows==result.height(),"Complete native cursor bitmap read");return result;
}
bool ownedPoint(POINT point){DWORD process{};auto under=WindowFromPoint(point);if(!under)return false;GetWindowThreadProcessId(under,&process);return process==GetCurrentProcessId();}
void observeNative(Fixture& fixture,int kind,int mode,const QString& artifact){
    fixture.icon(kind);fixture.mode(mode);QApplication::processEvents();
    auto* canvas=fixture.project.canvas;auto hwnd=reinterpret_cast<HWND>(canvas->winId());
    require(IsWindowVisible(hwnd)!=FALSE,"Actual canvas HWND visible");
    RECT client{};require(GetClientRect(hwnd,&client)!=FALSE,"Actual canvas client rectangle");
    POINT point{(client.right-client.left)/2,(client.bottom-client.top)/2};
    require(ClientToScreen(hwnd,&point)!=FALSE&&ownedPoint(point),"Pointer destination belongs to this fixture before moving");
    // A one-pixel move wholly inside the owned canvas forces actual WM_SETCURSOR
    // routing even when the previous combination used the same pointer position.
    POINT adjacent{point.x+1,point.y};require(ownedPoint(adjacent),"Adjacent pointer destination is owned");
    require(SetCursorPos(adjacent.x,adjacent.y)!=FALSE,"Move pointer inside owned canvas");QTest::qWait(20);
    require(SetCursorPos(point.x,point.y)!=FALSE,"Place pointer inside owned canvas");QTest::qWait(20);
    CURSORINFO cursor{};cursor.cbSize=sizeof(cursor);require(GetCursorInfo(&cursor)!=FALSE,"Read actual global cursor");
    DWORD pointerPid{};const auto under=WindowFromPoint(cursor.ptScreenPos);if(under)GetWindowThreadProcessId(under,&pointerPid);
    POINT immediate{};const bool gotImmediate=GetCursorPos(&immediate)!=FALSE;
    nativeDiagnostic=QJsonObject{{"requested_x",int(point.x)},{"requested_y",int(point.y)},
        {"cursor_info_x",int(cursor.ptScreenPos.x)},{"cursor_info_y",int(cursor.ptScreenPos.y)},
        {"immediate_available",gotImmediate},{"immediate_x",int(immediate.x)},{"immediate_y",int(immediate.y)},
        {"pointer_owner_pid",qint64(pointerPid)},{"fixture_pid",qint64(GetCurrentProcessId())},
        {"canvas_dpr",canvas->devicePixelRatioF()},{"window_dpi",int(GetDpiForWindow(hwnd))}};
    check("native_cursor_visible",(cursor.flags&CURSOR_SHOWING)!=0);
    check("native_cursor_position_owned",ownedPoint(cursor.ptScreenPos)&&cursor.ptScreenPos.x==point.x&&cursor.ptScreenPos.y==point.y);
    IconInfo info;require(GetIconInfo(cursor.hCursor,&info.value)!=FALSE,"Read actual displayed HCURSOR");
    const auto expected=fixture.cursor().pixmap().toImage().convertToFormat(QImage::Format_ARGB32);
    check("native_color_bitmap",!expected.isNull()&&info.value.hbmColor!=nullptr);
    if(expected.isNull()||!info.value.hbmColor)return;
    const auto actual=nativePixels(hwnd,info.value.hbmColor);
    require(actual.save(artifact+".native.png")&&expected.save(artifact+".assigned.png"),"Retain actual native and assigned cursor pixels");
    const qreal dpr=canvas->devicePixelRatioF();const auto hot=fixture.cursor().hotSpot();
    check("native_physical_size",actual.size()==expected.size());
    check("native_physical_hotspot",info.value.xHotspot==DWORD(qRound(hot.x()*dpr))&&info.value.yHotspot==DWORD(qRound(hot.y()*dpr)));
    bool equal=actual.size()==expected.size();
    if(equal)for(int y=0;y<actual.height();++y)if(std::memcmp(actual.constScanLine(y),expected.constScanLine(y),size_t(actual.width())*4)!=0){equal=false;break;}
    check("native_pixels_equal_assigned",equal);
}
}
int main(int argc,char** argv){
    QApplication app(argc,argv);QString error;QJsonArray results;bool complete=false;
    try{
        require(argc==2,"ABS_NEW_REPORT required");const QString report=QString::fromLocal8Bit(argv[1]);require(!QFile::exists(report),"Fresh native report");
        require(QGuiApplication::platformName()=="windows","Actual Windows platform required");
        Fixture fixture;fixture.window.raise();fixture.window.activateWindow();QTest::qWait(80);
        for(int kind=0;kind<4;++kind)for(int mode=0;mode<3;++mode){
            checks=QJsonArray{};const int previous=failures;
            const QString key=QString("native_%1_%2").arg(kind).arg(mode);
            observeNative(fixture,kind,mode,report+"."+key);
            results.append(QJsonObject{{"case",key},{"passed",previous==failures},{"checks",checks},{"pointer_observation",nativeDiagnostic}});
        }
        complete=true;
    }catch(const std::exception& e){error=QString::fromUtf8(e.what());}
    const bool passed=complete&&failures==0;
    QJsonObject report{{"schema","SELECTION_CURSOR_HCURSOR_V1"},{"status",passed?"passed":complete?"failed":"error"},{"complete",complete},{"rows",results},{"error",error},{"claim","Actual GetCursorInfo HCURSOR on owned visible canvas; no physical scanout or human acceptance"},{"original144_credit",false}};
    if(argc==2){QFile out(QString::fromLocal8Bit(argv[1]));if(!out.open(QIODevice::WriteOnly|QIODevice::NewOnly)||out.write(QJsonDocument(report).toJson())<0)return 2;}
    std::printf("%s native cursor rows=%lld failures=%d\n",passed?"PASS":"FAIL",static_cast<long long>(results.size()),failures);
    if(!error.isEmpty())std::fprintf(stderr,"%s\n",error.toUtf8().constData());return passed?0:complete?1:2;
}
