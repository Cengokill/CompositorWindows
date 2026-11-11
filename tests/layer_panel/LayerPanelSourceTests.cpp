#include "ui/MainWindow.h"
#include "ui/LayerPanel.h"
#include "ui/NativeCanvas.h"
#include <QApplication>
#include <QCursor>
#include <QFont>
#include <QLineEdit>
#include <QToolButton>
#include <QMouseEvent>
#include <QScrollBar>
#include <QWindow>
#include <algorithm>
#include <QStyleOptionViewItem>
#include <QTest>
#include <iostream>
#include <stdexcept>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
using namespace compositor;
namespace {
void check(bool value,const char*why){if(!value)throw std::runtime_error(why);}
void pixelsAvailable(const QImage& image){check(!image.isNull(),"source pixels #require thumbnail image");check(image.constBits()!=nullptr&&image.sizeInBytes()>=qint64(image.bytesPerLine())*image.height(),"source pixels #require backing bytes");}
QColor pixel(const QImage&image,int x,int y){pixelsAvailable(image);check(x>=0&&y>=0&&x<image.width()&&y<image.height(),"pixel reader index outside image");return QColor::fromRgba(image.pixel(x,y));}
void thumbnail_shapes(){check(ui::canvasThumbnailSize({400,200},36)==QSize(36,18),"400x200 fits36x18");check(ui::canvasThumbnailSize({300,600},36)==QSize(18,36),"300x600 fits18x36");check(ui::canvasThumbnailSize({},30)==QSize(30,30),"zero canvas falls back square");}
void thumbnail_layer_placement(){const auto red=Raster::filled(100,100,{255,0,0,255});check(red&&red->width==100&&red->height==100&&!red->tiles.empty()&&red->tiles.front(),"source image helper #require generated image");auto image=ui::canvasLayerThumbnail(red.get(),{0,0,100,100},{400,200},36);check(image.deviceIndependentSize()==QSizeF(36,18),"source logical thumbnail size");check(image.width()==72,"source fixed2x backing width");check(pixel(image,4,4)==QColor(255,0,0,255),"top-left layer placement");check(pixel(image,12,12).red()==255,"inside source layer");check(pixel(image,60,30).red()<200,"rest canvas checkerboard");check(pixel(image,4,30).red()<200,"bottom-left blank confirms orientation");const auto blank=ui::canvasLayerThumbnail(nullptr,{0,0,400,200},{400,200},36);check(pixel(blank,4,4).alpha()==255,"blank layer opaque checkerboard");}
void thumbnail_mask_edge(){const Transform transform{100,50,100,100};const QSizeF canvas{400,200};const GrayRaster hide{1,1,{0}};check(hide.width==1&&hide.height==1&&hide.pixels.size()==1,"source #require solid gray mask");auto hidden=ui::canvasMaskThumbnail(hide,transform,canvas,30);check(pixel(hidden,1,1).red()<10,"hide-all exterior black");check(pixel(hidden,55,25).red()<10,"hide-all remote exterior black");GrayRaster framed{20,20,std::vector<uint8_t>(400,255)};check(framed.width==20&&framed.height==20&&framed.pixels.size()==400,"source #require gray20x20 context storage");for(int y=5;y<15;++y)for(int x=5;x<15;++x)framed.pixels[size_t(y)*20+x]=0;check(framed.pixels.data()!=nullptr,"source #require framed mask image");check(ui::canvasMaskEdgeTone(framed)==1,"white frame edge tone");auto shown=ui::canvasMaskThumbnail(framed,transform,canvas,30);check(pixel(shown,2,2).red()>245,"white outside layer");check(pixel(shown,22,15).red()<10,"black center in document placement");}
Document example(){Document d;d.id=newId();d.width=400;d.height=300;for(const auto*id:{"a","b","c"}){Layer l;l.id=id;l.name=id;l.transform={0,0,100,100};l.raster=Raster::filled(100,100,{255,0,0,255});d.layers.push_back(l);}return d;}
QTreeWidget*tree(MainWindow&w){auto*t=w.findChild<QTreeWidget*>("layersTree");check(t,"missing tree");return t;}
ui::LayerPanelController*controller(MainWindow&w){auto*c=ui::LayerPanelController::find(tree(w));check(c,"missing controller");return c;}
QTreeWidgetItem*item(MainWindow&w,const char*id){for(QTreeWidgetItemIterator it(tree(w));*it;++it)if((*it)->data(0,Qt::UserRole).toString()==id)return *it;throw std::runtime_error("missing row");}
Layer&layer(EditorProject&p,const char*id){for(auto&l:p.document->layers)if(l.id==id)return l;throw std::runtime_error("missing layer");}
QPoint namePoint(MainWindow&w,const char*id){auto*view=tree(w);auto*row=item(w,id);view->scrollToItem(row);QApplication::processEvents();const auto rect=view->visualItemRect(row);return {view->columnViewportPosition(0)+view->columnWidth(0)/2,rect.top()+4};}
QPoint eyePoint(MainWindow&w,const char*id){auto*view=tree(w);auto*row=item(w,id);return view->visualRect(view->indexFromItem(row,4)).center();}
void mouse(MainWindow&w,QEvent::Type type,QPoint point,Qt::MouseButton button,Qt::MouseButtons buttons,Qt::KeyboardModifiers flags={}){auto*view=tree(w)->viewport();QMouseEvent event(type,point,view->mapToGlobal(point),button,buttons,flags);QApplication::sendEvent(view,&event);}
void thumbnail_cache(){MainWindow w(true);auto&p=w.addProject(example());auto*c=controller(w);const auto count=c->thumbnailRenderCount();check(count>=3,"thumbnail initial renders");c->rebuild();check(c->thumbnailRenderCount()==count,"unchanged rebuild regenerated thumbnails");p.active="a";p.selected={"a"};c->updateSelection();check(c->thumbnailRenderCount()==count,"selection regenerated thumbnail pixels");layer(p,"a").visible=false;c->rebuild();check(c->thumbnailRenderCount()==count,"visibility regenerated thumbnail pixels");layer(p,"b").transform.x+=40;c->rebuild();check(c->thumbnailRenderCount()==count+1,"transform did not invalidate only one thumbnail");check(item(w,"a")->icon(1).actualSize({36,36})==QSize(36,27),"actual panel thumbnail ignores canvas ratio");}
void text_layer_icon(){
    MainWindow w(true);
    auto d=example();
    TextContent text;text.value="Imported text";text.fontFamily="Segoe UI";
    d.layers[0].text=text;
    auto mask=std::make_shared<GrayRaster>();mask->width=mask->height=1;mask->pixels={255};
    d.layers[0].mask=Mask{mask};
    auto&p=w.addProject(d);
    auto*c=controller(w);auto*row=item(w,"a");
    check(row->data(0,Qt::UserRole+2).toString()=="Text","text layer subtitle missing");
    check(row->data(1,Qt::AccessibleTextRole).toString()=="Select image: a","text image accessibility changed");
    check(row->data(3,Qt::AccessibleTextRole).toString()=="Select mask: a","text mask accessibility missing");
    check(!row->icon(1).isNull()&&!row->icon(3).isNull(),"text or mask icon missing");
    const auto textImage=row->icon(1).pixmap({36,36}).toImage();
    bool redDominant=false;
    for(int y=0;y<textImage.height();++y)for(int x=0;x<textImage.width();++x){const auto color=textImage.pixelColor(x,y);redDominant|=color.red()>color.green()+40&&color.red()>color.blue()+40;}
    check(!redDominant,"text layer still uses the raster thumbnail");
    const auto count=c->thumbnailRenderCount();c->rebuild();check(c->thumbnailRenderCount()==count,"unchanged text rebuild regenerated thumbnail");
    layer(p,"a").text.reset();c->rebuild();check(c->thumbnailRenderCount()==count+1,"text cache state did not invalidate thumbnail");
    const auto rasterImage=item(w,"a")->icon(1).pixmap({36,36}).toImage();redDominant=false;
    for(int y=0;y<rasterImage.height();++y)for(int x=0;x<rasterImage.width();++x){const auto color=rasterImage.pixelColor(x,y);redDominant|=color.red()>color.green()+40&&color.red()>color.blue()+40;}
    check(redDominant,"raster thumbnail was not restored after removing text");
}
void search_and_selected_filter(){
    MainWindow w(true);auto d=example();
    d.layers[0].name="Folder 1";d.layers[0].group=true;d.layers[0].raster.reset();
    d.layers[1].name="Blue sky 1";d.layers[1].parentId="a";
    d.layers[2].name="Red sun 2";d.layers[2].parentId="a";
    auto& p=w.addProject(d);p.active="b";p.selected={"b","c"};p.collapsedGroups.insert("a");
    auto* c=controller(w);c->rebuild();w.show();QApplication::processEvents();const auto before=*p.document;const auto undo=p.history.undoCount();
    check(!item(w,"a")->isExpanded()&&p.collapsedGroups.contains("a"),"showing the panel expanded the selected layer's collapsed folder");
    c->updateSelection();QApplication::processEvents();
    check(!item(w,"a")->isExpanded()&&p.collapsedGroups.contains("a"),"selection refresh expanded the selected layer's collapsed folder");
    auto* search=w.findChild<QLineEdit*>("layerSearch");auto* button=w.findChild<QToolButton*>("layerSearchButton");auto* selected=w.findChild<QToolButton*>("layerSelectedFilter");
    check(search&&button&&selected,"filter controls missing");button->click();search->setText("  BLUE  ");
    check(!item(w,"a")->isHidden()&&!item(w,"b")->isHidden()&&item(w,"c")->isHidden(),"search context or case matching failed");
    check(item(w,"a")->isExpanded()&&p.collapsedGroups.contains("a"),"temporary expansion changed saved state");
    check(p.selected.size()==2&&p.active=="b","filter changed selection");
    const auto exposed=c->exposedVisibilityControls();check(std::find(exposed.begin(),exposed.end(),"c")==exposed.end(),"hidden row exposed to accessibility");
    std::unique_ptr<QMimeData> payload(c->dragMime());check(!c->accepts(payload.get(),QPoint(-10,-10),{}),"filtered empty-space drop must be rejected");
    search->setText("1");check(!item(w,"a")->isHidden()&&!item(w,"b")->isHidden()&&item(w,"c")->isHidden(),"matching folder exposed a nonmatching child");
    search->setText("2");check(!item(w,"a")->isHidden()&&item(w,"b")->isHidden()&&!item(w,"c")->isHidden(),"matching child lost its parent context");
    search->setText("folder");check(!item(w,"a")->isHidden()&&item(w,"b")->isHidden()&&item(w,"c")->isHidden(),"folder search exposed nonmatching children");
    search->setText("missing");check(item(w,"a")->isHidden(),"empty results still displayed a row");
    search->clear();p.active="a";p.selected={"a"};selected->click();
    check(!item(w,"a")->isHidden()&&item(w,"b")->isHidden()&&item(w,"c")->isHidden(),"selected folder exposed unselected children");
    p.active="b";p.selected={"b","c"};c->updateSelection();
    check(!item(w,"b")->isHidden()&&!item(w,"c")->isHidden(),"selected filter lost selected rows");
    p.active="c";p.selected={"c"};c->updateSelection();check(item(w,"b")->isHidden()&&!item(w,"c")->isHidden(),"selected filter did not follow selection");
    search->setText("blue");check(item(w,"a")->isHidden(),"search and selected filter must both match");
    button->click();selected->click();check(!item(w,"a")->isHidden()&&!item(w,"b")->isHidden()&&!item(w,"a")->isExpanded(),"clearing filters did not restore tree");
    check(*p.document==before&&p.history.undoCount()==undo&&p.collapsedGroups.contains("a"),"filter modified document or history");
}
void folder_arrow_and_visibility_are_independent(){
    MainWindow w(true);auto d=example();Layer folder;folder.id="folder";folder.name="Folder";folder.group=true;folder.transform={0,0,100,100};d.layers.push_back(folder);d.layers[0].parentId="folder";
    auto& p=w.addProject(d);w.show();QApplication::processEvents();auto* view=tree(w);auto* row=item(w,"folder");
    check(view->treePosition()==0,"tree branches must use layer name column");
    const auto nameRect=view->visualRect(view->indexFromItem(row,0));const auto eyeRect=view->visualRect(view->indexFromItem(row,4));
    const QPoint arrow(nameRect.left()-view->indentation()/2,nameRect.center().y());
    check(!eyeRect.contains(arrow),"folder arrow overlaps visibility column");
    const auto before=*p.document;const auto undo=p.history.undoCount();
    QTest::mouseClick(view->viewport(),Qt::LeftButton,Qt::NoModifier,arrow);QApplication::processEvents();
    check(!item(w,"folder")->isExpanded()&&p.collapsedGroups.contains("folder"),"clicking the folder arrow did not collapse it");
    check(*p.document==before&&p.history.undoCount()==undo,"folder arrow changed layer visibility or history");
    QTest::mouseClick(view->viewport(),Qt::LeftButton,Qt::NoModifier,arrow);QApplication::processEvents();
    check(item(w,"folder")->isExpanded()&&!p.collapsedGroups.contains("folder"),"clicking the folder arrow did not expand it");
    QTest::mouseClick(view->viewport(),Qt::LeftButton,Qt::NoModifier,eyeRect.center());QApplication::processEvents();
    check(!layer(p,"folder").visible&&p.history.undoCount()==undo+1,"clicking the eye did not toggle visibility");
    check(item(w,"folder")->isExpanded(),"clicking the eye collapsed the folder");
    view->setCurrentItem(item(w,"folder"),0);view->setFocus();
    QTest::keyClick(view,Qt::Key_Left);QApplication::processEvents();
    check(!item(w,"folder")->isExpanded()&&p.collapsedGroups.contains("folder"),"keyboard collapse did not update saved state");
    QTest::keyClick(view,Qt::Key_Right);QApplication::processEvents();
    check(item(w,"folder")->isExpanded()&&!p.collapsedGroups.contains("folder"),"keyboard expansion did not update saved state");
    controller(w)->updateSelection();
    check(item(w,"folder")->isExpanded()&&!p.collapsedGroups.contains("folder"),"refresh lost keyboard expansion");
    controller(w)->rebuild();
    check(item(w,"folder")->isExpanded()&&!p.collapsedGroups.contains("folder"),"rebuild lost keyboard expansion");
}
void name_cell_does_not_toggle_visibility(){
    MainWindow w(true);auto d=example();Layer folder;folder.id="folder";folder.name="Folder";folder.group=true;folder.transform={0,0,100,100};d.layers.push_back(folder);d.layers[0].parentId="folder";
    auto& p=w.addProject(d);w.show();QApplication::processEvents();auto* view=tree(w);
    const auto undo=p.history.undoCount();
    for(const char* id:{"b","folder"}){
        auto* row=item(w,id);const auto rect=view->visualRect(view->indexFromItem(row,0));
        QStyleOptionViewItem option;option.initFrom(view);option.rect=rect;option.features|=QStyleOptionViewItem::HasCheckIndicator;option.checkState=Qt::Checked;
        const auto hiddenCheckHit=view->style()->subElementRect(QStyle::SE_ItemViewItemCheckIndicator,&option,view);
        check(rect.intersects(hiddenCheckHit),"hidden name-column check hit area missing");
        for(const QPoint nameOrSubtitle:{hiddenCheckHit.center(),QPoint(rect.left()+rect.width()/2,rect.top()+31)}){
            check(rect.contains(nameOrSubtitle),"name/subtitle point outside layer row");
            QTest::mouseClick(view->viewport(),Qt::LeftButton,Qt::NoModifier,nameOrSubtitle);QApplication::processEvents();
            check(layer(p,id).visible,"name/subtitle click toggled visibility");
            check(p.history.undoCount()==undo,"name/subtitle click added visibility history");
            check(p.active==id&&p.selected==std::vector<std::string>{id},"name/subtitle click did not select layer");
        }
    }
    auto* row=item(w,"b");const auto eye=view->visualRect(view->indexFromItem(row,4)).center();
    QTest::mouseClick(view->viewport(),Qt::LeftButton,Qt::NoModifier,eye);QApplication::processEvents();
    check(!layer(p,"b").visible&&p.history.undoCount()==undo+1,"eye click did not toggle visibility once");
    view->setCurrentItem(row,0);view->setFocus();QTest::keyClick(view,Qt::Key_Space);QApplication::processEvents();QApplication::processEvents();
    check(layer(p,"b").visible&&p.history.undoCount()==undo+2,"keyboard visibility toggle no longer works");
}
void visibility_swipe(){MainWindow w(true);auto&p=w.addProject(example());w.show();QApplication::processEvents();const auto before=p.document;const auto active=p.active;auto start=eyePoint(w,"c"),middle=eyePoint(w,"b"),last=eyePoint(w,"a");mouse(w,QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton);check(!layer(p,"c").visible&&p.history.undoCount()==0,"press must preview first eye in open transaction");mouse(w,QEvent::MouseMove,middle,Qt::NoButton,Qt::LeftButton);mouse(w,QEvent::MouseMove,last,Qt::NoButton,Qt::LeftButton);mouse(w,QEvent::MouseMove,middle,Qt::NoButton,Qt::LeftButton);mouse(w,QEvent::MouseButtonRelease,QPoint(-15,-15),Qt::LeftButton,Qt::NoButton);check(!layer(p,"a").visible&&!layer(p,"b").visible&&!layer(p,"c").visible,"swipe must assign same state without toggling revisited rows");check(p.history.undoCount()==1&&p.history.undoName()=="Hide Layer"&&p.active==active,"eye swipe history or selection changed");const auto undo=p.history.undo();check(undo&&undo->document==before,"eye swipe Undo did not restore exact document");}
Document scrollExample(int count){
    auto d=example();d.layers.clear();
    for(int i=0;i<count;++i){Layer l;l.id="scroll-"+std::to_string(i);l.name=l.id;l.transform={0,0,4,4};l.raster=Raster::filled(4,4,{255,0,0,255});
        auto mask=std::make_shared<GrayRaster>();mask->width=mask->height=1;mask->pixels={255};l.mask=Mask{mask};d.layers.push_back(l);}
    return d;
}
void middle_scroll(){
    for(const int count:{3,40}){
        MainWindow w(true);auto&p=w.addProject(scrollExample(count));w.show();QApplication::processEvents();
        auto*t=tree(w);t->setFixedHeight(240);QApplication::processEvents();auto*bar=t->verticalScrollBar();
        check((bar->maximum()>0)==(count==40),"scroll fixture range");
        const auto before=p.document;const auto selected=p.selected;const auto active=p.active;const auto undo=p.history.undoCount();const auto pan=p.canvas->pan;const auto zoom=p.canvas->zoom;const bool modified=p.history.modified(),redo=p.history.canRedo();
        const QPoint start(t->viewport()->width()/2,100);bar->setValue(0);
        mouse(w,QEvent::MouseButtonPress,start,Qt::MiddleButton,Qt::MiddleButton);
        check(t->viewport()->cursor().shape()==Qt::ClosedHandCursor,"missing middle-scroll cursor");
        mouse(w,QEvent::MouseMove,start-QPoint(0,75),Qt::NoButton,Qt::MiddleButton);
        check(bar->value()==std::min(75,bar->maximum()),"middle drag is not pixel based or has wrong direction");
        mouse(w,QEvent::MouseMove,start-QPoint(0,10000),Qt::NoButton,Qt::MiddleButton);
        check(bar->value()==bar->maximum(),"bottom limit");
        mouse(w,QEvent::MouseMove,start-QPoint(0,9960),Qt::NoButton,Qt::MiddleButton);
        check(bar->value()==std::max(0,bar->maximum()-40),"reverse at bottom must respond immediately");
        mouse(w,QEvent::MouseMove,start+QPoint(0,10000),Qt::NoButton,Qt::MiddleButton);
        check(bar->value()==0,"top limit");
        mouse(w,QEvent::MouseButtonRelease,QPoint(-20,-20),Qt::MiddleButton,Qt::NoButton);
        mouse(w,QEvent::MouseMove,start,Qt::NoButton,Qt::NoButton);
        check(bar->value()==0&&t->viewport()->cursor().shape()==Qt::ArrowCursor,"outside release left gesture active");
        check(p.document==before&&p.selected==selected&&p.active==active&&p.history.undoCount()==undo,"scroll edited project");
        check(p.canvas->pan==pan&&p.canvas->zoom==zoom,"layer scroll moved canvas");
        check(p.history.modified()==modified&&p.history.canRedo()==redo,"scroll changed dirty or redo state");
    }
}
void middle_scroll_rename(){
    MainWindow w(true);auto& p=w.addProject(scrollExample(40));
    auto* t=tree(w);t->setFixedHeight(240);
    w.show();w.activateWindow();
    check(QTest::qWaitForWindowActive(&w,1000),"editor window did not activate");
    auto* bar=t->verticalScrollBar();bar->setValue(0);
    auto* edited=t->topLevelItem(0);
    const auto id=edited->data(0,Qt::UserRole).toString().toStdString();
    const auto before=p.document;
    const auto undo=p.history.undoCount();
    const auto start=t->viewport()->mapTo(&w,t->visualRect(t->indexFromItem(t->topLevelItem(2),0)).center());
    t->editItem(edited,0);
    auto* editor=qobject_cast<QLineEdit*>(QApplication::focusWidget());
    check(editor&&t->viewport()->isAncestorOf(editor),"inline rename editor did not take focus");
    editor->setText("Pending name");

    QTest::mousePress(w.windowHandle(),Qt::MiddleButton,Qt::NoModifier,start);
    QTest::mouseMove(w.windowHandle(),start-QPoint(0,30));
    QTest::mouseRelease(w.windowHandle(),Qt::MiddleButton,Qt::NoModifier,start-QPoint(0,30));
    QApplication::processEvents();
    check(bar->value()==30,"window input did not scroll the Layers panel");
    check(QApplication::focusWidget()==editor,"middle drag moved focus away from the inline editor");
    check(editor->text()=="Pending name"&&p.document==before&&p.history.undoCount()==undo,
          "middle drag committed the pending rename");

    QTest::keyClick(editor,Qt::Key_Return);
    check(QTest::qWaitFor([&]{return layer(p,id.c_str()).name=="Pending name"&&p.history.undoCount()==undo+1;},1000),
          "Return did not commit the preserved rename");
}
void middle_scroll_capture(){
    MainWindow w(true);w.addProject(scrollExample(40));
    auto* t=tree(w);t->setFixedHeight(240);
    w.show();w.activateWindow();
    check(QTest::qWaitForWindowActive(&w,1000),"editor window did not activate");
    auto* viewport=t->viewport();
    auto* receiver=w.findChild<QWidget*>("layerGroupButton");
    check(receiver&&receiver->isVisible(),"missing outside capture receiver");
    const auto outside=receiver->mapTo(&w,receiver->rect().center());
    check(!viewport->rect().contains(viewport->mapFrom(&w,outside)),"capture receiver overlaps viewport");
    const auto start=viewport->mapTo(&w,QPoint(viewport->width()/2,100));
    auto* bar=t->verticalScrollBar();
    bar->setValue(0);

    QTest::mousePress(w.windowHandle(),Qt::MiddleButton,Qt::NoModifier,start);
    QTest::mouseMove(w.windowHandle(),start-QPoint(0,30));
    check(bar->value()==30&&QWidget::mouseGrabber()==viewport,"middle scroll did not capture the mouse");
    receiver->grabMouse();
    check(QWidget::mouseGrabber()==receiver,"mouse grab did not transfer");
    QTest::mouseRelease(w.windowHandle(),Qt::MiddleButton,Qt::NoModifier,outside);
    check(QWidget::mouseGrabber()==receiver,"scroll cleanup released the new grabber");
    check(bar->value()==30&&viewport->cursor().shape()!=Qt::ClosedHandCursor,
          "outside release after capture transfer left scroll active");
    receiver->releaseMouse();

    QTest::mousePress(w.windowHandle(),Qt::MiddleButton,Qt::NoModifier,start);
    QTest::mouseMove(w.windowHandle(),start-QPoint(0,20));
    QTest::mouseRelease(w.windowHandle(),Qt::MiddleButton,Qt::NoModifier,start-QPoint(0,20));
    check(bar->value()==50&&QWidget::mouseGrabber()!=viewport,"fresh scroll failed after capture transfer");

#ifdef Q_OS_WIN
    if(QGuiApplication::platformName()=="windows"){
        QWindow nativeReceiver(w.windowHandle());
        nativeReceiver.setGeometry(QRect(outside,QSize(20,20)));
        nativeReceiver.show();
        const auto hwnd=reinterpret_cast<HWND>(nativeReceiver.winId());
        QTest::mousePress(w.windowHandle(),Qt::MiddleButton,Qt::NoModifier,start);
        check(GetCapture()==reinterpret_cast<HWND>(viewport->effectiveWinId()),"missing native capture");
        SetCapture(hwnd);
        const bool stopped=QTest::qWaitFor([&]{
            return QWidget::mouseGrabber()!=viewport&&viewport->cursor().shape()!=Qt::ClosedHandCursor;
        },1000);
        const bool keptCapture=GetCapture()==hwnd;
        QTest::mouseRelease(&nativeReceiver,Qt::MiddleButton,Qt::NoModifier,QPoint(10,10));
        if(GetCapture()==hwnd)ReleaseCapture();
        check(stopped&&keptCapture,"native capture loss did not stop scroll and preserve the new owner");
    }
#endif
}
void middle_scroll_cancel(){
    MainWindow w(true);w.addProject(scrollExample(40));w.show();QApplication::processEvents();
    auto*t=tree(w);t->setFixedHeight(240);QApplication::processEvents();auto*bar=t->verticalScrollBar();
    const QPoint start(t->viewport()->width()/2,100);
    for(const int reason:{0,1,2,3,4}){
        bar->setValue(100);mouse(w,QEvent::MouseButtonPress,start,Qt::MiddleButton,Qt::MiddleButton);
        mouse(w,QEvent::MouseMove,start-QPoint(0,30),Qt::NoButton,Qt::MiddleButton);
        if(reason==0){QEvent event(QEvent::WindowDeactivate);QApplication::sendEvent(&w,&event);}
        else if(reason==1){QEvent event(QEvent::UngrabMouse);QApplication::sendEvent(t->viewport(),&event);}
        else if(reason==2){t->hide();t->show();QApplication::processEvents();}
        else if(reason==3)controller(w)->rebuild();
        else mouse(w,QEvent::MouseMove,start,Qt::NoButton,Qt::NoButton);
        const int stopped=bar->value();
        mouse(w,QEvent::MouseMove,start-QPoint(0,90),Qt::NoButton,Qt::MiddleButton);
        check(bar->value()==stopped,"cancelled middle gesture resumed");
        check(t->viewport()->cursor().shape()!=Qt::ClosedHandCursor,"cancel left closed hand");
        check(QWidget::mouseGrabber()!=t->viewport(),"cancel retained mouse capture");
        mouse(w,QEvent::MouseButtonRelease,start,Qt::MiddleButton,Qt::NoButton);
        // Each cancellation must permit a fresh gesture.
        bar->setValue(100);mouse(w,QEvent::MouseButtonPress,start,Qt::MiddleButton,Qt::MiddleButton);
        mouse(w,QEvent::MouseMove,start-QPoint(0,20),Qt::NoButton,Qt::MiddleButton);
        check(bar->value()==120,"cannot restart after cancellation");
        mouse(w,QEvent::MouseButtonRelease,start,Qt::MiddleButton,Qt::NoButton);
    }
}
void middle_scroll_controls(){
    MainWindow w(true);auto&p=w.addProject(scrollExample(40));w.show();QApplication::processEvents();
    auto*t=tree(w);t->setFixedHeight(240);QApplication::processEvents();
    p.active="scroll-39";p.selected={"scroll-39","scroll-38"};p.maskSelected=true;controller(w)->updateSelection();
    const auto before=p.document;const auto undo=p.history.undoCount();
    for(const auto flags:{Qt::NoModifier,Qt::AltModifier,Qt::ControlModifier,Qt::ShiftModifier})for(int column=0;column<5;++column){
        controller(w)->updateSelection();t->verticalScrollBar()->setValue(0);
        const auto rows=t->selectedItems();auto*current=t->currentItem();
        const QPoint start(t->columnViewportPosition(column)+t->columnWidth(column)/2,22);
        mouse(w,QEvent::MouseButtonPress,start,Qt::MiddleButton,Qt::MiddleButton,flags);
        mouse(w,QEvent::MouseMove,start-QPoint(0,60),Qt::NoButton,Qt::MiddleButton,flags);
        check(t->verticalScrollBar()->value()==60,"control or edit gate blocked navigation");
        check(t->viewport()->cursor().shape()==Qt::ClosedHandCursor,"modifier replaced scroll cursor");
        mouse(w,QEvent::MouseButtonRelease,start-QPoint(0,60),Qt::MiddleButton,Qt::NoButton,flags);
        check(p.document==before&&p.history.undoCount()==undo,"middle gesture edited visibility, link or document");
        check(p.active=="scroll-39"&&p.selected==std::vector<std::string>{"scroll-39","scroll-38"}&&p.maskSelected,"middle gesture changed selection or mask target");
        check(t->selectedItems()==rows&&t->currentItem()==current,"middle gesture changed tree selection");
    }
}
void source_cursor_conflict(){MainWindow w(true);auto d=example();for(auto&l:d.layers){l.raster.reset();l.transform={0,0,400,300};}w.addProject(d);w.show();QApplication::processEvents();auto*view=tree(w)->viewport();const auto name=namePoint(w,"c");mouse(w,QEvent::MouseMove,name,Qt::NoButton,Qt::NoButton,Qt::AltModifier);check(view->cursor().shape()==Qt::DragCopyCursor,"source Option name offers duplicate");auto*row=item(w,"c");const auto rect=tree(w)->visualItemRect(row);const auto size=row->icon(1).actualSize({36,36});check(size==QSize(36,27),"source thumbnail takes400x300canvas shape");const QPoint picture(tree(w)->columnViewportPosition(1)+tree(w)->columnWidth(1)/2,rect.center().y());mouse(w,QEvent::MouseMove,picture,Qt::NoButton,Qt::NoButton,Qt::AltModifier);const bool thumbnailAssertion=view->cursor().shape()!=Qt::DragCopyCursor;std::cout<<"SOURCE-TEST-CONFLICT CursorTests.optionOverALayerRowOffersDuplicatingExceptOverThumbnails row_top="<<rect.top()<<" row_height="<<rect.height()<<" thumbnail_center_y="<<picture.y()<<" clipping_boundary="<<rect.bottom()+1-rect.height()/4<<" actual_role="<<view->property("layerCursorRole").toString().toStdString()<<" expected=not-duplicate\n";mouse(w,QEvent::MouseMove,name,Qt::NoButton,Qt::NoButton);check(view->cursor().shape()==Qt::ArrowCursor,"source return to plain name restores arrow");check(thumbnailAssertion,"upstream expected Option at image thumbnail center not duplicate; literal pinned source route gives duplicate");}
void panel_cursors(){MainWindow w(true);auto&p=w.addProject(example());(void)p;w.show();QApplication::processEvents();auto*c=controller(w);auto*view=tree(w)->viewport();const auto name=namePoint(w,"c");view->setCursor(Qt::CrossCursor);mouse(w,QEvent::MouseMove,name,Qt::NoButton,Qt::NoButton);check(view->cursor().shape()==Qt::ArrowCursor,"tool cursor persisted in layer name");view->setCursor(Qt::CrossCursor);c->refreshCursor({},name);check(view->cursor().shape()==Qt::ArrowCursor,"cursor refresh failed to restore arrow");view->setCursor(Qt::CrossCursor);mouse(w,QEvent::MouseMove,name,Qt::NoButton,Qt::NoButton,Qt::ControlModifier);check(view->cursor().shape()==Qt::ArrowCursor,"Ctrl away from thumbnail changed cursor");view->setCursor(Qt::CrossCursor);c->refreshCursor({},QPoint(-50,-50));check(view->cursor().shape()==Qt::CrossCursor,"outside modifier refresh touched other view cursor");mouse(w,QEvent::MouseMove,name,Qt::NoButton,Qt::NoButton,Qt::AltModifier);check(view->cursor().shape()==Qt::DragCopyCursor&&view->property("layerCursorRole")=="duplicate","Alt row name did not offer duplicate");auto*row=item(w,"c");const QPoint picture(tree(w)->columnViewportPosition(1)+tree(w)->columnWidth(1)/2,tree(w)->visualItemRect(row).center().y());mouse(w,QEvent::MouseMove,picture,Qt::NoButton,Qt::NoButton,Qt::ControlModifier);check(view->property("layerCursorRole")=="load-selection","Ctrl image did not offer load selection");mouse(w,QEvent::MouseMove,name,Qt::NoButton,Qt::NoButton);check(view->cursor().shape()==Qt::ArrowCursor,"plain row failed to restore arrow");}
}
int main(int argc,char**argv){QApplication app(argc,argv);app.setFont(QFont("Segoe UI",10));try{check(argc==2,"provide one case");const std::string name=argv[1];if(name=="folder_arrow_and_visibility_are_independent")folder_arrow_and_visibility_are_independent();else if(name=="name_cell_does_not_toggle_visibility")name_cell_does_not_toggle_visibility();else if(name=="search_and_selected_filter")search_and_selected_filter();else if(name=="thumbnail_shapes")thumbnail_shapes();else if(name=="thumbnail_layer_placement")thumbnail_layer_placement();else if(name=="thumbnail_mask_edge")thumbnail_mask_edge();else if(name=="thumbnail_cache")thumbnail_cache();else if(name=="text_layer_icon")text_layer_icon();else if(name=="middle_scroll")middle_scroll();else if(name=="middle_scroll_rename")middle_scroll_rename();else if(name=="middle_scroll_capture")middle_scroll_capture();else if(name=="middle_scroll_cancel")middle_scroll_cancel();else if(name=="middle_scroll_controls")middle_scroll_controls();else if(name=="visibility_swipe")visibility_swipe();else if(name=="panel_cursors")panel_cursors();else if(name=="source_cursor_conflict")source_cursor_conflict();else throw std::runtime_error("unknown case");std::cout<<"PASS "<<name<<'\n';return 0;}catch(const std::exception&error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
