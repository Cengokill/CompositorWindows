#include "VisualStyle.h"
#include "EditorIcons.h"
#include <QApplication>
#include <QAbstractButton>
#include <QDialog>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileIconProvider>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProxyStyle>
#include <QPushButton>
#include <QStyleFactory>
#include <QStyleOption>
#include <QToolBar>
#include <QTreeWidget>
#include <QWindow>
#include <QSettings>
#include <QPointer>
#include <QSignalBlocker>
#include <Windows.h>
#include <dwmapi.h>

static void initializeVisualResources() { Q_INIT_RESOURCE(visual_assets); }

namespace compositor::ui {
namespace {
constexpr auto ink = "#e4e5e7";
constexpr auto muted = "#989ba3";
constexpr auto surface = "#222326";
constexpr auto accent = "#579cf5";

class StudioStyle final : public QProxyStyle {
public:
    StudioStyle() : QProxyStyle(QStyleFactory::create("Fusion")) {}
    int styleHint(StyleHint hint,const QStyleOption* option=nullptr,const QWidget* widget=nullptr,QStyleHintReturn* data=nullptr) const override {
        if(hint==SH_UnderlineShortcut)return false;
        return QProxyStyle::styleHint(hint,option,widget,data);
    }
    int pixelMetric(PixelMetric metric, const QStyleOption* option=nullptr, const QWidget* widget=nullptr) const override {
        if(metric==PM_SmallIconSize)return 18;
        if(metric==PM_ScrollBarExtent)return 10;
        if(metric==PM_ToolBarHandleExtent)return 0;
        if(metric==PM_DockWidgetSeparatorExtent)return 1;
        return QProxyStyle::pixelMetric(metric,option,widget);
    }
    QIcon standardIcon(StandardPixmap icon,const QStyleOption* option=nullptr,const QWidget* widget=nullptr) const override {
        switch(icon) {
        case SP_ToolBarHorizontalExtensionButton: return editorIcon(EditorIcon::ChevronRight);
        case SP_ToolBarVerticalExtensionButton: return editorIcon(EditorIcon::ChevronDown);
        case SP_TitleBarCloseButton: case SP_DockWidgetCloseButton: case SP_DialogCloseButton: case SP_TabCloseButton:
            return editorIcon(EditorIcon::Close);
        case SP_TitleBarMinButton: return editorIcon(EditorIcon::Minus);
        case SP_TitleBarMaxButton: case SP_TitleBarNormalButton: return editorIcon(EditorIcon::Maximize);
        case SP_DirIcon: case SP_DirOpenIcon: case SP_DirClosedIcon: case SP_DirHomeIcon: case SP_DriveHDIcon:
            return editorIcon(EditorIcon::Folder);
        case SP_FileIcon: return editorIcon(EditorIcon::File);
        case SP_TrashIcon: return editorIcon(EditorIcon::Delete);
        case SP_ArrowDown: return editorIcon(EditorIcon::ChevronDown);
        case SP_ArrowUp: return editorIcon(EditorIcon::ChevronUp);
        case SP_ArrowLeft: case SP_ArrowBack: return editorIcon(EditorIcon::ChevronLeft);
        case SP_ArrowRight: case SP_ArrowForward: return editorIcon(EditorIcon::ChevronRight);
        default: return QProxyStyle::standardIcon(icon,option,widget);
        }
    }
    void drawPrimitive(PrimitiveElement element,const QStyleOption* option,QPainter* painter,const QWidget* widget=nullptr) const override {
        const bool eye=element==PE_IndicatorItemViewItemCheck&&widget&&
            (widget->objectName()=="layersTree"||(widget->parentWidget()&&widget->parentWidget()->objectName()=="layersTree"));
        if(eye) {
            painter->save(); painter->setRenderHint(QPainter::Antialiasing);
            auto r=QRectF(option->rect);r.adjust(0,2,0,-2);
            painter->setPen(QPen(QColor(option->state&State_On?ink:muted),1.2));painter->setBrush(Qt::NoBrush);
            QPainterPath path;path.moveTo(r.left(),r.center().y());path.quadTo(r.center().x(),r.top()-2,r.right(),r.center().y());path.quadTo(r.center().x(),r.bottom()+2,r.left(),r.center().y());
            painter->drawPath(path);painter->drawEllipse(r.center(),2,2);
            if(!(option->state&State_On))painter->drawLine(r.topLeft(),r.bottomRight());
            painter->restore();return;
        }
        if(element==PE_IndicatorCheckBox||element==PE_IndicatorRadioButton) {
            painter->save();painter->setRenderHint(QPainter::Antialiasing);
            auto r=QRectF(option->rect).adjusted(1.5,1.5,-1.5,-1.5);
            const bool on=option->state&(State_On|State_NoChange);
            painter->setOpacity(option->state&State_Enabled?1:.35);
            painter->setPen(QPen(on?QColor(accent):QColor("#54565c"),1));painter->setBrush(on?QColor(accent):QColor("#303136"));
            if(element==PE_IndicatorRadioButton)painter->drawEllipse(r);else painter->drawRoundedRect(r,4,4);
            if(on){painter->setPen(QPen(Qt::white,1.5,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
                if(element==PE_IndicatorRadioButton){painter->setBrush(Qt::white);painter->drawEllipse(r.center(),2,2);}
                else{QPainterPath check;check.moveTo(r.left()+3,r.center().y());check.lineTo(r.center().x()-1,r.bottom()-3);check.lineTo(r.right()-2.5,r.top()+3);painter->drawPath(check);}}
            painter->restore();return;
        }
        if(element==PE_IndicatorArrowDown||element==PE_IndicatorArrowUp||element==PE_IndicatorArrowLeft||element==PE_IndicatorArrowRight) {
            const auto icon=element==PE_IndicatorArrowDown?EditorIcon::ChevronDown:element==PE_IndicatorArrowUp?EditorIcon::ChevronUp:element==PE_IndicatorArrowLeft?EditorIcon::ChevronLeft:EditorIcon::ChevronRight;
            painter->save();painter->setRenderHint(QPainter::Antialiasing);painter->setPen(QPen(option->palette.color(option->state&State_Enabled?QPalette::Active:QPalette::Disabled,QPalette::ButtonText),1.3,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
            painter->translate(option->rect.center());QPainterPath arrow;
            if(icon==EditorIcon::ChevronDown){arrow.moveTo(-3,-1.5);arrow.lineTo(0,1.5);arrow.lineTo(3,-1.5);}
            else if(icon==EditorIcon::ChevronUp){arrow.moveTo(-3,1.5);arrow.lineTo(0,-1.5);arrow.lineTo(3,1.5);}
            else if(icon==EditorIcon::ChevronLeft){arrow.moveTo(1.5,-3);arrow.lineTo(-1.5,0);arrow.lineTo(1.5,3);}
            else{arrow.moveTo(-1.5,-3);arrow.lineTo(1.5,0);arrow.lineTo(-1.5,3);}
            painter->drawPath(arrow);painter->restore();return;
        }
        if(element==PE_FrameFocusRect)return;
        QProxyStyle::drawPrimitive(element,option,painter,widget);
    }
};

class WindowDot final : public QAbstractButton {
public:
    WindowDot(QWidget* owner,int kind):QAbstractButton(owner),kind_(kind) {
        setFixedSize(22,28);setCursor(Qt::ArrowCursor);
        setAccessibleName(kind==0?"Close window":kind==1?"Minimize window":"Maximize or restore window");setToolTip(accessibleName());
        connect(this,&QAbstractButton::clicked,owner,[owner,kind]{auto* window=owner->window();if(kind==0)window->close();else if(kind==1)window->showMinimized();else if(window->isMaximized())window->showNormal();else window->showMaximized();});
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
        const QColor colors[]{QColor("#ef7770"),QColor("#e2b85a"),QColor("#78bb82")};
        p.setPen(Qt::NoPen);p.setBrush(isEnabled()?colors[kind_]:QColor("#44464b"));p.drawEllipse(QPointF(width()/2.,height()/2.),5.5,5.5);
        if(underMouse()&&isEnabled()){p.setPen(QPen(QColor("#27282c"),1.2,Qt::SolidLine,Qt::RoundCap));const auto c=QPointF(width()/2.,height()/2.);
            if(kind_==0){p.drawLine(c+QPointF(-2,-2),c+QPointF(2,2));p.drawLine(c+QPointF(-2,2),c+QPointF(2,-2));}
            else{p.drawLine(c+QPointF(-2.5,0),c+QPointF(2.5,0));if(kind_==2)p.drawLine(c+QPointF(0,-2.5),c+QPointF(0,2.5));}}
    }
    void enterEvent(QEnterEvent* event) override{update();QAbstractButton::enterEvent(event);}
    void leaveEvent(QEvent* event) override{update();QAbstractButton::leaveEvent(event);}
private:int kind_;
};

class FileIcons final : public QFileIconProvider {
public:
    QIcon icon(const QFileInfo& info) const override{return editorIcon(info.isDir()?EditorIcon::Folder:EditorIcon::File);}
    QIcon icon(IconType type) const override{return editorIcon(type==File?EditorIcon::File:EditorIcon::Folder);}
};

void nativeTitleColor(QWidget* window) {
    if(QGuiApplication::platformName()!="windows"||window->windowFlags().testFlag(Qt::FramelessWindowHint))return;
    const auto hwnd=reinterpret_cast<HWND>(window->effectiveWinId());if(!hwnd)return;
    const BOOL dark=TRUE;DwmSetWindowAttribute(hwnd,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));
    const COLORREF color=RGB(34,35,38);DwmSetWindowAttribute(hwnd,DWMWA_CAPTION_COLOR,&color,sizeof(color));
}

void applyTitleBar(QWidget* window,bool mac) {
    if(!window->property("studioNativeFlags").isValid())window->setProperty("studioNativeFlags",int(window->windowFlags()&~Qt::FramelessWindowHint));
    auto flags=Qt::WindowFlags::fromInt(window->property("studioNativeFlags").toInt());
    if(mac)flags|=Qt::FramelessWindowHint;
    const bool visible=window->isVisible();const auto state=window->windowState();
    const auto bounds=window->isMaximized()||window->isMinimized()?window->normalGeometry():window->geometry();
    QPointer<QWidget> focus=window->focusWidget();
    if(auto* main=qobject_cast<QMainWindow*>(window)){if(auto* identity=main->menuBar()->cornerWidget(Qt::TopLeftCorner))identity->setVisible(mac);}
    else if(auto* title=window->findChild<QWidget*>("dialogTitle",Qt::FindDirectChildrenOnly))title->setVisible(mac);
    if(window->windowFlags()!=flags){
        window->setWindowFlags(flags);
        if(bounds.isValid())window->setGeometry(bounds);
        window->setWindowState(state);
        if(visible)window->show();
        if(focus&&visible)focus->setFocus(Qt::OtherFocusReason);
    }
    nativeTitleColor(window);
}

class ChromeEvents final : public QObject {
public:
    using QObject::QObject;
    bool eventFilter(QObject* object,QEvent* event) override {
        auto* widget=qobject_cast<QWidget*>(object);if(!widget)return false;
        if(event->type()==QEvent::Polish) {
            if(auto* picker=qobject_cast<QFileDialog*>(widget)){static FileIcons icons;picker->setOption(QFileDialog::DontUseNativeDialog);picker->setIconProvider(&icons);}
            if(auto* dialog=qobject_cast<QDialog*>(widget);dialog&&dialog->isWindow()&&!dialog->property("studioChrome").toBool()) {
                dialog->setProperty("studioChrome",true);
                auto* content=new QWidget(dialog);content->setObjectName("dialogContent");if(dialog->layout())content->setLayout(dialog->layout());
                auto* layout=new QVBoxLayout(dialog);layout->setContentsMargins(1,1,1,1);layout->setSpacing(0);
                auto* title=new QWidget(dialog);title->setObjectName("dialogTitle");title->setProperty("studioDrag",true);
                auto* row=new QHBoxLayout(title);row->setContentsMargins(10,5,16,5);row->setSpacing(9);row->addWidget(new WindowDot(title,0));
                auto* text=new QLabel(dialog->windowTitle(),title);text->setAttribute(Qt::WA_TransparentForMouseEvents);row->addWidget(text);row->addStretch();
                QObject::connect(dialog,&QWidget::windowTitleChanged,text,&QLabel::setText);
                layout->addWidget(title);layout->addWidget(content);
                applyTitleBar(dialog,macTitleBarEnabled());
            }
        }
        if((event->type()==QEvent::Show||event->type()==QEvent::WinIdChange)&&widget->isWindow()&&widget->property("studioChrome").toBool())nativeTitleColor(widget);
        if(event->type()==QEvent::MouseButtonPress||event->type()==QEvent::MouseButtonDblClick) {
            auto* mouse=static_cast<QMouseEvent*>(event);if(mouse->button()!=Qt::LeftButton)return false;
            auto* top=widget->window();if(!top->windowFlags().testFlag(Qt::FramelessWindowHint)||!top->windowHandle())return false;
            const auto point=top->mapFromGlobal(mouse->globalPosition().toPoint());
            Qt::Edges edges;if(point.x()<5)edges|=Qt::LeftEdge;if(point.x()>top->width()-5)edges|=Qt::RightEdge;
            if(point.y()<5)edges|=Qt::TopEdge;if(point.y()>top->height()-5)edges|=Qt::BottomEdge;
            if(edges&&!top->isMaximized()&&event->type()==QEvent::MouseButtonPress){top->windowHandle()->startSystemResize(edges);return true;}
            auto* menu=qobject_cast<QMenuBar*>(widget);
            if(widget->property("studioDrag").toBool()||(menu&&!menu->actionAt(mouse->position().toPoint()))) {
                if(event->type()==QEvent::MouseButtonDblClick&&qobject_cast<QMainWindow*>(top)){if(top->isMaximized())top->showNormal();else top->showMaximized();}
                else top->windowHandle()->startSystemMove();return true;
            }
        }
        return false;
    }
};
}

bool macTitleBarEnabled() {return qApp->property("studioMacTitleBar").toBool();}

void setMacTitleBarEnabled(bool enabled) {
    qApp->setProperty("studioMacTitleBar",enabled);
    QSettings settings;settings.setValue("appearance/macTitleBar",enabled);settings.sync();
    QList<QPointer<QWidget>> windows;
    for(auto* window:QApplication::topLevelWidgets())if(window->property("studioChrome").toBool())windows.append(window);
    for(const auto& window:windows)if(window){
        applyTitleBar(window,enabled);
        if(auto* action=window->findChild<QAction*>("macTitleBarAction")){QSignalBlocker block(action);action->setChecked(enabled);}
    }
}

void installVisualStyle() {
    if(qApp->property("studioVisualStyle").toBool())return;
    qApp->setProperty("studioVisualStyle",true);initializeVisualResources();
    qApp->setProperty("studioMacTitleBar",QSettings().value("appearance/macTitleBar",false).toBool());
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    const auto fontId=QFontDatabase::addApplicationFont(":/assets/fonts/InterVariable.ttf");
    const auto family=QFontDatabase::applicationFontFamilies(fontId).value(0,"Inter");
    qApp->setStyle(new StudioStyle);
    QFont font(family);font.setPixelSize(13);qApp->setFont(font);qApp->setProperty("studioFontFamily",family);
    QPalette palette;
    palette.setColor(QPalette::Window,QColor(surface));palette.setColor(QPalette::WindowText,QColor(ink));
    palette.setColor(QPalette::Base,QColor("#1c1d20"));palette.setColor(QPalette::AlternateBase,QColor("#242529"));
    palette.setColor(QPalette::Text,QColor(ink));palette.setColor(QPalette::Button,QColor("#303136"));palette.setColor(QPalette::ButtonText,QColor(ink));
    palette.setColor(QPalette::Highlight,QColor(accent));palette.setColor(QPalette::HighlightedText,Qt::white);
    palette.setColor(QPalette::Mid,QColor("#303237"));palette.setColor(QPalette::Light,QColor("#494c53"));palette.setColor(QPalette::Dark,QColor("#151619"));
    palette.setColor(QPalette::ToolTipBase,QColor("#34363b"));palette.setColor(QPalette::ToolTipText,QColor(ink));
    for(auto role:{QPalette::Text,QPalette::ButtonText,QPalette::WindowText})palette.setColor(QPalette::Disabled,role,QColor("#656870"));
    qApp->setPalette(palette);
    qApp->setStyleSheet(QString("QWidget { font-family: \"%1\"; font-size: 13px; }\n").arg(family)+R"(
        QWidget { color: #e4e5e7; selection-background-color: #376dac; selection-color: white; }
        QMainWindow, QDialog { background: #222326; border: 1px solid #37393f; }
        QMenuBar { background: #222326; padding: 3px 8px; spacing: 3px; border-bottom: 1px solid #303237; }
        QMenuBar::item { padding: 6px 9px; background: transparent; border-radius: 5px; }
        QMenuBar::item:selected { background: #35373c; }
        QMenu { background: #292b30; border: 1px solid #45474e; border-radius: 9px; padding: 6px; }
        QMenu::item { padding: 7px 24px 7px 12px; border-radius: 5px; }
        QMenu::item:selected { background: #376dac; }
        QMenu::item:disabled { color: #686b73; }
        QMenu::separator { height: 1px; background: #3d3f46; margin: 5px 8px; }
        QToolTip { background: #34363b; border: 1px solid #4b4e56; border-radius: 5px; padding: 6px 9px; }
        QToolBar { background: #242529; border: 0; border-bottom: 1px solid #303237; padding: 5px 10px; spacing: 7px; }
        QToolBar::handle { width: 0; height: 0; }
        QToolBar QToolButton { padding: 5px 9px; border: 1px solid transparent; border-radius: 6px; }
        QToolButton#qt_toolbar_ext_button { padding: 2px; min-width: 18px; }
        QToolButton#newProjectDropTarget { padding: 4px; }
        QPushButton, QToolButton { background: #33353b; border: 1px solid #41434b; border-radius: 7px; padding: 5px 12px; min-height: 18px; }
        QPushButton:hover, QToolButton:hover { background: #3d4047; border-color: #565a64; }
        QPushButton:pressed, QToolButton:pressed { background: #27292e; }
        QPushButton:default, QPushButton[primary="true"], QToolButton:checked { background: #428bea; border-color: #589df4; color: white; }
        QPushButton:disabled, QToolButton:disabled { color: #676a73; border-color: #303237; background: #292b30; }
        QPushButton:focus, QToolButton:focus { border-color: #78b0f7; }
        QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox { background: #2b2d32; border: 1px solid #393c43; border-radius: 6px; padding: 4px 7px; min-height: 18px; }
        QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus { border-color: #579cf5; background: #30333a; }
        QLineEdit:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled, QComboBox:disabled { color: #656870; border-color: #303237; background: #26282c; }
        QAbstractSpinBox::up-button, QAbstractSpinBox::down-button { width: 14px; border: 0; background: transparent; }
        QAbstractSpinBox::up-arrow { image: url(:/assets/icons/chevron-up.png); width: 10px; height: 10px; }
        QAbstractSpinBox::down-arrow { image: url(:/assets/icons/chevron-down.png); width: 10px; height: 10px; }
        QComboBox { padding-right: 22px; }
        QComboBox::drop-down { width: 20px; border: 0; }
        QComboBox::down-arrow { image: url(:/assets/icons/chevron-down.png); width: 12px; height: 12px; }
        QComboBox QAbstractItemView { background: #2b2d32; border: 1px solid #454850; padding: 4px; outline: 0; selection-background-color: #376dac; }
        QCheckBox, QRadioButton { spacing: 7px; background: transparent; }
        QCheckBox::indicator, QRadioButton::indicator { width: 16px; height: 16px; }
        QSlider::groove:horizontal { height: 4px; background: #494c53; border-radius: 2px; }
        QSlider::sub-page:horizontal { background: #579cf5; border-radius: 2px; }
        QSlider::handle:horizontal { background: #e3e6eb; border: 1px solid #b9bdc7; width: 13px; height: 13px; margin: -5px 0; border-radius: 7px; }
        QSlider::handle:horizontal:hover { background: white; border-color: white; }
        QSlider::sub-page:horizontal:disabled { background: #555962; }
        QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
        QScrollBar::handle:vertical { background: #50535c; min-height: 30px; border-radius: 3px; }
        QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
        QScrollBar::handle:horizontal { background: #50535c; min-width: 30px; border-radius: 3px; }
        QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
        QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
        QTabWidget::pane { border: 0; }
        QTabBar { background: #202125; }
        QTabBar::tab { background: transparent; color: #969aa4; padding: 8px 13px; margin: 4px 2px; border: 1px solid transparent; border-radius: 7px; }
        QTabBar::tab:selected { background: #303239; color: #eff0f2; border-color: #41444c; }
        QTabBar::tab:hover { color: white; background: #292c31; }
        QTabBar::close-button { subcontrol-position: right; width: 13px; height: 13px; }
        QDockWidget { border: 0; }
        QWidget#sectionTitle { border-bottom: 1px solid #303237; background: #222326; }
        QWidget#sectionTitle QLabel { font-weight: 600; color: #b9bdc5; font-size: 12px; }
        QWidget#dialogTitle { border-bottom: 1px solid #36383e; background: #27292e; }
        QWidget#dialogTitle QLabel { font-weight: 600; }
        QWidget#dialogContent { background: #24262a; }
        QTreeView, QListView { background: #202125; border: 0; outline: 0; show-decoration-selected: 1; }
        QTreeView::item, QListView::item { border: 0; padding: 4px; }
        QTreeView::item:selected, QListView::item:selected { background: #353942; }
        QTreeView::item:hover, QListView::item:hover { background: #2b2e34; }
        QTreeView#layersTree { background: #222326; }
        QTreeView#layersTree::item { padding: 0; }
        QHeaderView::section { background: #27292e; color: #a8acb5; border: 0; border-bottom: 1px solid #383b42; padding: 7px; }
        QGroupBox { border: 1px solid #3b3e45; border-radius: 8px; margin-top: 14px; padding: 12px; }
        QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; color: #b8bdc7; }
        QStatusBar { background: #202125; border-top: 1px solid #303237; color: #959aa4; font-size: 11px; }
        QStatusBar::item { border: 0; }
        QProgressBar { border: 0; background: #33363c; border-radius: 4px; text-align: center; }
        QProgressBar::chunk { background: #579cf5; border-radius: 4px; }
    )");
    qApp->installEventFilter(new ChromeEvents(qApp));
}

void styleWorkspace(QMainWindow* window) {
    window->setProperty("studioChrome",true);
    auto* menu=window->menuBar();
    auto* identity=new QWidget(menu);identity->setProperty("studioDrag",true);
    auto* row=new QHBoxLayout(identity);row->setContentsMargins(5,0,19,0);row->setSpacing(0);
    for(int i=0;i<3;++i)row->addWidget(new WindowDot(identity,i));
    auto* name=new QLabel("Compositor",identity);name->setAttribute(Qt::WA_TransparentForMouseEvents);name->setStyleSheet("font-weight: 600; padding-left: 12px; color: #eff0f2;");row->addWidget(name);
    menu->setCornerWidget(identity,Qt::TopLeftCorner);
    applyTitleBar(window,macTitleBarEnabled());
    for(auto* bar:window->findChildren<QToolBar*>()){bar->setMovable(false);bar->setFloatable(false);}
    for(auto* dock:window->findChildren<QDockWidget*>()) {
        dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
        auto* title=new QWidget(dock);title->setObjectName("sectionTitle");auto* layout=new QHBoxLayout(title);
        layout->setContentsMargins(14,12,14,12);layout->addWidget(new QLabel(dock->windowTitle(),title));layout->addStretch();dock->setTitleBarWidget(title);
        if(dock->widget()&&dock->widget()->layout())dock->widget()->layout()->setContentsMargins(12,10,12,12);
    }
    auto menus=menu->actions();auto findMenu=[&](const QString& label)->QMenu*{for(auto* action:menus)if(action->text().remove('&')==label)return action->menu();return nullptr;};
    if(auto* view=findMenu("View")){
        view->addSeparator();auto* appearance=view->addMenu("Appearance");
        auto* mac=appearance->addAction("Mac-style title bar");mac->setObjectName("macTitleBarAction");mac->setCheckable(true);mac->setChecked(macTitleBarEnabled());
        mac->setToolTip("Use colored window controls on the left. Turn off for the native Windows title bar.");
        QObject::connect(mac,&QAction::toggled,window,[](bool enabled){setMacTitleBarEnabled(enabled);});
    }
    for(const auto& pair:{std::pair{"Clipboard","Edit"},std::pair{"Adjustments","Image"},std::pair{"Canvas","Image"},std::pair{"Transform","Layer"}})
        if(auto* child=findMenu(pair.first))if(auto* parent=findMenu(pair.second)){menu->removeAction(child->menuAction());parent->addSeparator();parent->addMenu(child);}
    for(const auto* title:{"File","Edit","View","Select","Image","Filters","Layer","Help"})if(auto* item=findMenu(title)){menu->removeAction(item->menuAction());menu->addMenu(item);}
}
}
