#include "ProjectChrome.h"
#include "EditorIcons.h"
#include <QAbstractButton>
#include <QAccessibleWidget>
#include <QDockWidget>
#include <QEvent>
#include <QFormLayout>
#include <QMainWindow>
#include <QPainter>
#include <QPointer>
#include <QSettings>
#include <QTabBar>
#include <QTabWidget>
#include <QToolButton>
#include <QStyle>
#include <QTimer>
#include <QTreeWidget>
#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>

namespace compositor::ui {
namespace {
constexpr auto titleProperty = "compositorProjectTitle";
constexpr auto closeProperty = "compositorCanCloseProject";
QAbstractButton* closeButton(QTabWidget* tabs, int index) {
    if (!tabs || index < 0 || index >= tabs->count()) return nullptr;
    for (auto side : {QTabBar::RightSide, QTabBar::LeftSide})
        if (auto* button = qobject_cast<QAbstractButton*>(tabs->tabBar()->tabButton(index, side))) return button;
    return nullptr;
}

class ProjectCloseControl final : public QAccessibleInterface, public QAccessibleActionInterface {
public:
    ProjectCloseControl(QTabWidget* tabs, QWidget* page) : tabs_(tabs), page_(page) {}
    int index() const { return tabs_ && page_ ? tabs_->indexOf(page_) : -1; }
    QAbstractButton* button() const { return closeButton(tabs_, index()); }
    bool isValid() const override { return button() != nullptr; }
    QObject* object() const override { return nullptr; }
    QWindow* window() const override { return nullptr; }
    QAccessibleInterface* parent() const override { return tabs_ ? QAccessible::queryAccessibleInterface(tabs_) : nullptr; }
    QAccessibleInterface* child(int) const override { return nullptr; }
    QAccessibleInterface* childAt(int, int) const override { return nullptr; }
    int childCount() const override { return 0; }
    int indexOfChild(const QAccessibleInterface*) const override { return -1; }
    QString text(QAccessible::Text type) const override {
        return isValid() && (type == QAccessible::Name || type == QAccessible::Description)
            ? QString("Close %1").arg(page_->property(titleProperty).toString()) : QString{};
    }
    void setText(QAccessible::Text, const QString&) override {}
    QRect rect() const override {
        auto* control = button();
        return control ? QRect(control->mapToGlobal(QPoint{}), control->size()) : QRect{};
    }
    QAccessible::Role role() const override { return QAccessible::Button; }
    QAccessible::State state() const override {
        QAccessible::State value;
        auto* control = button();
        value.invalid = !control;
        value.disabled = !control || !control->isEnabled() || !tabs_->property(closeProperty).toBool();
        value.invisible = !control || !control->isVisible();
        value.offscreen = value.invisible || !rect().intersects(QRect(tabs_->tabBar()->mapToGlobal(QPoint{}), tabs_->tabBar()->size()));
        return value;
    }
    void* interface_cast(QAccessible::InterfaceType type) override {
        return type == QAccessible::ActionInterface ? static_cast<QAccessibleActionInterface*>(this) : nullptr;
    }
    QStringList actionNames() const override { return {QAccessibleActionInterface::pressAction()}; }
    QStringList keyBindingsForAction(const QString&) const override { return {}; }
    void doAction(const QString& action) override {
        if (action == QAccessibleActionInterface::pressAction() && !state().disabled)
            if (auto* control = button()) control->click();
    }
    QWidget* page() const { return page_; }
private:
    QPointer<QTabWidget> tabs_;
    QPointer<QWidget> page_;
};

class ProjectContainer final : public QAccessibleWidget {
public:
    explicit ProjectContainer(QTabWidget* tabs) : QAccessibleWidget(tabs), tabs_(tabs) {}
    ~ProjectContainer() override { for (const auto& item : controls_) QAccessible::deleteAccessibleInterface(item.second); }
    int childCount() const override { return QAccessibleWidget::childCount() + (tabs_ ? tabs_->count() : 0); }
    QAccessibleInterface* child(int index) const override {
        const auto base = QAccessibleWidget::childCount();
        if (index < base) return QAccessibleWidget::child(index);
        if (!tabs_ || index < base || index - base >= tabs_->count()) return nullptr;
        auto* page = tabs_->widget(index - base);
        auto found = controls_.find(page);
        if (found == controls_.end()) found = controls_.emplace(page,
            QAccessible::registerAccessibleInterface(new ProjectCloseControl(tabs_, page))).first;
        return QAccessible::accessibleInterface(found->second);
    }
    int indexOfChild(const QAccessibleInterface* item) const override {
        if (const auto* close = dynamic_cast<const ProjectCloseControl*>(item)) {
            const int index = tabs_ ? tabs_->indexOf(close->page()) : -1;
            return index < 0 ? -1 : QAccessibleWidget::childCount() + index;
        }
        return QAccessibleWidget::indexOfChild(item);
    }
    QAccessibleInterface* childAt(int x, int y) const override {
        const auto base = QAccessibleWidget::childCount();
        for (int i = 0; tabs_ && i < tabs_->count(); ++i)
            if (auto* control = child(base + i); control && control->rect().contains(x, y)) return control;
        return QAccessibleWidget::childAt(x, y);
    }
    void changed() {
        for (auto it = controls_.begin(); it != controls_.end();) {
            auto* control = QAccessible::accessibleInterface(it->second);
            if (!control || !control->isValid()) {
                QAccessible::deleteAccessibleInterface(it->second); it = controls_.erase(it);
            } else {
                QAccessibleEvent name(control, QAccessible::NameChanged); QAccessible::updateAccessibility(&name);
                QAccessible::State bits; bits.disabled = true; bits.invisible = true;
                QAccessibleStateChangeEvent state(control, bits); QAccessible::updateAccessibility(&state); ++it;
            }
        }
        QAccessibleEvent structure(widget(), QAccessible::ObjectReorder); QAccessible::updateAccessibility(&structure);
    }
private:
    QPointer<QTabWidget> tabs_;
    mutable std::map<QWidget*, QAccessible::Id> controls_;
};

// QAccessibleTabBar in Qt6.8 obtains its Name from the current tab. A public
// provider keeps the named container and individual page identities distinct.
class ProjectTabControl final : public QAccessibleInterface, public QAccessibleActionInterface {
public:
    ProjectTabControl(QTabWidget* tabs, QWidget* page) : tabs_(tabs), page_(page) {}
    int index() const { return tabs_ && page_ ? tabs_->indexOf(page_) : -1; }
    QWidget* page() const { return page_; }
    bool isValid() const override { return index() >= 0; }
    QObject* object() const override { return nullptr; }
    QWindow* window() const override { return nullptr; }
    QAccessibleInterface* parent() const override { return tabs_ ? QAccessible::queryAccessibleInterface(tabs_->tabBar()) : nullptr; }
    QAccessibleInterface* child(int) const override { return nullptr; }
    QAccessibleInterface* childAt(int, int) const override { return nullptr; }
    int childCount() const override { return 0; }
    int indexOfChild(const QAccessibleInterface*) const override { return -1; }
    QString text(QAccessible::Text type) const override {
        if (!isValid()) return {};
        if (type == QAccessible::Name) return page_->property(titleProperty).toString();
        if (type == QAccessible::Description) return tabs_->tabBar()->tabToolTip(index());
        return {};
    }
    void setText(QAccessible::Text, const QString&) override {}
    QRect rect() const override {
        if (!isValid()) return {};
        auto* bar = tabs_->tabBar(); const auto local = bar->tabRect(index());
        return QRect(bar->mapToGlobal(local.topLeft()), local.size());
    }
    QAccessible::Role role() const override { return QAccessible::PageTab; }
    QAccessible::State state() const override {
        QAccessible::State result;
        result.invalid = !isValid(); if (result.invalid) return result;
        auto* bar = tabs_->tabBar(); const int at = index();
        result.selectable = true; result.selected = at == tabs_->currentIndex(); result.focusable = true;
        result.focused = result.selected && bar->hasFocus();
        result.disabled = !bar->isEnabled() || !bar->isTabEnabled(at) || (!result.selected && !tabs_->property(closeProperty).toBool());
        result.invisible = !bar->isVisible() || !bar->isTabVisible(at);
        result.offscreen = result.invisible || !rect().intersects(QRect(bar->mapToGlobal(QPoint{}), bar->size()));
        return result;
    }
    void* interface_cast(QAccessible::InterfaceType type) override {
        return type == QAccessible::ActionInterface ? static_cast<QAccessibleActionInterface*>(this) : nullptr;
    }
    QStringList actionNames() const override { return {pressAction(), setFocusAction()}; }
    QStringList keyBindingsForAction(const QString&) const override { return {}; }
    void doAction(const QString& action) override {
        if (!isValid() || state().disabled || (action != pressAction() && action != setFocusAction())) return;
        if (action == setFocusAction()) tabs_->tabBar()->setFocus(Qt::OtherFocusReason);
        tabs_->setCurrentIndex(index());
    }
private:
    QPointer<QTabWidget> tabs_;
    QPointer<QWidget> page_;
};
class ProjectTabBar final : public QAccessibleWidget, public QAccessibleSelectionInterface {
public:
    explicit ProjectTabBar(QTabWidget* tabs) : QAccessibleWidget(tabs->tabBar(), QAccessible::PageTabList), tabs_(tabs) {}
    ~ProjectTabBar() override { for (const auto& item : pages_) QAccessible::deleteAccessibleInterface(item.second); }
    QString text(QAccessible::Text type) const override { return type == QAccessible::Name ? QString("Project tabs") : QAccessibleWidget::text(type); }
    QList<QToolButton*> scrollButtons() const { return tabs_ ? tabs_->tabBar()->findChildren<QToolButton*>(QString{}, Qt::FindDirectChildrenOnly) : QList<QToolButton*>{}; }
    int childCount() const override { return tabs_ ? tabs_->count() + int(scrollButtons().size()) : 0; }
    QAccessibleInterface* child(int at) const override {
        if (!tabs_ || at < 0) return nullptr;
        if (at >= tabs_->count()) { const auto buttons = scrollButtons(); at -= tabs_->count(); return at < buttons.size() ? QAccessible::queryAccessibleInterface(buttons[at]) : nullptr; }
        auto* page = tabs_->widget(at); auto found = pages_.find(page);
        if (found == pages_.end()) found = pages_.emplace(page, QAccessible::registerAccessibleInterface(new ProjectTabControl(tabs_, page))).first;
        return QAccessible::accessibleInterface(found->second);
    }
    int indexOfChild(const QAccessibleInterface* item) const override {
        if (!tabs_) return -1;
        if (const auto* page = dynamic_cast<const ProjectTabControl*>(item)) return tabs_->indexOf(page->page());
        const auto buttons = scrollButtons(); for (int i = 0; i < buttons.size(); ++i) if (item && item->object() == buttons[i]) return tabs_->count() + i;
        return -1;
    }
    QAccessibleInterface* childAt(int x, int y) const override {
        for (int i = childCount() - 1; i >= 0; --i) if (auto* item = child(i); item && !item->state().invisible && item->rect().contains(x, y)) return item;
        return nullptr;
    }
    QAccessibleInterface* focusChild() const override { return tabs_ && tabs_->tabBar()->hasFocus() ? child(tabs_->currentIndex()) : nullptr; }
    void* interface_cast(QAccessible::InterfaceType type) override { return type == QAccessible::SelectionInterface ? static_cast<QAccessibleSelectionInterface*>(this) : QAccessibleWidget::interface_cast(type); }
    int selectedItemCount() const override { return tabs_ && tabs_->currentIndex() >= 0 ? 1 : 0; }
    QList<QAccessibleInterface*> selectedItems() const override { return selectedItemCount() ? QList<QAccessibleInterface*>{child(tabs_->currentIndex())} : QList<QAccessibleInterface*>{}; }
    bool select(QAccessibleInterface* item) override {
        const auto at = indexOfChild(item); if (!tabs_ || at < 0 || at >= tabs_->count() || item->state().disabled) return false;
        tabs_->setCurrentIndex(at); return tabs_ && tabs_->currentIndex() == at;
    }
    bool unselect(QAccessibleInterface* item) override { return item && indexOfChild(item) >= 0 && !item->state().selected; }
    bool selectAll() override { return false; }
    bool clear() override { return false; }
    void changed() {
        for (auto it = pages_.begin(); it != pages_.end();) {
            auto* page = QAccessible::accessibleInterface(it->second);
            if (!page || !page->isValid()) { QAccessible::deleteAccessibleInterface(it->second); it = pages_.erase(it); }
            else { QAccessibleEvent name(page, QAccessible::NameChanged); QAccessible::updateAccessibility(&name);
                QAccessible::State bits; bits.selected = true; bits.disabled = true; bits.focused = true;
                QAccessibleStateChangeEvent state(page, bits); QAccessible::updateAccessibility(&state); ++it; }
        }
        QAccessibleEvent structure(widget(), QAccessible::ObjectReorder); QAccessible::updateAccessibility(&structure);
    }
private:
    QPointer<QTabWidget> tabs_;
    mutable std::map<QWidget*, QAccessible::Id> pages_;
};
QAccessibleInterface* factory(const QString&, QObject* object) {
    if (auto* bar = qobject_cast<QTabBar*>(object); bar && bar->property("compositorProjectTabBar").toBool())
        if (auto* tabs = qobject_cast<QTabWidget*>(bar->parentWidget())) return new ProjectTabBar(tabs);
    auto* tabs = qobject_cast<QTabWidget*>(object);
    return tabs && tabs->property("compositorProjectTabs").toBool() ? new ProjectContainer(tabs) : nullptr;
}

class RememberedLayerWidth final : public QObject {
public:
    RememberedLayerWidth(QMainWindow* window, QDockWidget* dock, QTreeWidget* tree)
        : QObject(dock), window_(window), dock_(dock) {
        setObjectName("rememberedLayerPanelWidth");
        // Source LayersPanel.swift:7 permits 202...352. The former tree minimum
        // of 270 made both the source minimum and its 252 default impossible.
        tree->setMinimumWidth(0);
        // LayersPanel.swift39...55 uses three icon-only footer actions. Keep
        // their existing callbacks/names while allowing the source202 minimum.
        for (const auto* name : {"layerGroupButton", "layerAddMaskButton", "layerDeleteButton"})
            if (auto* button = dock->findChild<QAbstractButton*>(name)) {
                const auto label = button->accessibleName(); button->setToolTip(label); button->setText({});
                button->setFixedSize(36, 36); button->setIconSize({20, 20});
                button->setIcon(editorIcon(qstrcmp(name,"layerGroupButton")==0 ? EditorIcon::Group
                    : qstrcmp(name,"layerAddMaskButton")==0 ? EditorIcon::Mask : EditorIcon::Delete));
                button->setStyleSheet("QAbstractButton { background: transparent; border: 1px solid transparent; border-radius: 6px; }"
                    "QAbstractButton:hover { background: rgba(128,128,128,32); }"
                    "QAbstractButton:pressed { background: rgba(128,128,128,55); }"
                    "QAbstractButton:focus { border: 1px solid palette(highlight); }");
            }
        dock->setMinimumWidth(202); dock->setMaximumWidth(352);
        QSettings settings;
        const double stored = settings.value("layersPanelWidth", 252.0).toDouble();
        wanted_ = std::isfinite(stored) ? int(std::lround(std::clamp(stored, 202.0, 352.0))) : 252;
        dock->installEventFilter(this);
        timer_.setSingleShot(true); timer_.setInterval(100);
        QObject::connect(&timer_, &QTimer::timeout, this, [this] { save(); });
        QTimer::singleShot(0, this, [this] { apply(); });
    }
    ~RememberedLayerWidth() override { save(); }
private:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched == dock_) {
            if (event->type() == QEvent::Show && !ready_) QTimer::singleShot(0, this, [this] { apply(); });
            if (event->type() == QEvent::Resize && ready_ && !applying_ && !dock_->isFloating()) {
                remembered_ = std::clamp(dock_->width(), 202, 352); changed_ = true; timer_.start();
            }
            if (event->type() == QEvent::Close && ready_) save();
        }
        return false;
    }
    void apply() {
        if (!window_ || !dock_ || !dock_->isVisible() || ready_) return;
        applying_ = true; window_->resizeDocks({dock_}, {wanted_}, Qt::Horizontal); applying_ = false; ready_ = true;
    }
    void save() {
        // A QObject child can be destroyed after QDockWidget's derived state;
        // persist the last resize value without touching that widget here.
        if (!ready_ || !changed_) return;
        QSettings settings; settings.setValue("layersPanelWidth", remembered_); settings.sync(); changed_ = false;
    }
    QPointer<QMainWindow> window_;
    QPointer<QDockWidget> dock_;
    QTimer timer_;
    int wanted_{252};
    int remembered_{252};
    bool ready_{}, applying_{}, changed_{};
};
}

void installProjectChrome(QMainWindow* window, QTabWidget* tabs, QDockWidget* dock, QTreeWidget* tree) {
    static std::once_flag installed;
    std::call_once(installed, [] { QAccessible::installFactory(factory); });
    tabs->setProperty("compositorProjectTabs", true);
    tabs->tabBar()->setProperty("compositorProjectTabBar", true);
    // Construction can have caused Qt to cache its generic interface before
    // this widget was tagged. Installation occurs before the window is shown.
    if (auto* existing = QAccessible::queryAccessibleInterface(tabs);
        existing && !dynamic_cast<ProjectContainer*>(existing))
        QAccessible::deleteAccessibleInterface(QAccessible::uniqueId(existing));
    if (auto* existing = QAccessible::queryAccessibleInterface(tabs->tabBar()); existing && !dynamic_cast<ProjectTabBar*>(existing))
        QAccessible::deleteAccessibleInterface(QAccessible::uniqueId(existing));
    tabs->setAccessibleName("Project workspace"); tabs->tabBar()->setAccessibleName("Project tabs");
    // Both docks share a QMainWindow column. Keep all numeric contents visible
    // by wrapping Transform labels when the source-sized Layers panel narrows.
    if (auto* transform = window->findChild<QDockWidget*>("transformDock"))
        if (auto* form = qobject_cast<QFormLayout*>(transform->widget()->layout())) {
            form->setRowWrapPolicy(QFormLayout::WrapLongRows);
            const auto own = form->contentsMargins(), aligned = dock->widget()->layout()->contentsMargins();
            form->setContentsMargins(aligned.left(), own.top(), aligned.right(), own.bottom());
            // Qt includes this gap in its wrapped minimum: retain space while
            // fitting the full numeric field at the source's202-pixel minimum.
            form->setHorizontalSpacing(4);
        }
    new RememberedLayerWidth(window, dock, tree);
}
void refreshProjectTabs(QTabWidget* tabs, bool canClose, const QStringList& titles) {
    if (!tabs) return;
    tabs->setProperty(closeProperty, canClose);
    for (int i = 0; i < tabs->count(); ++i) {
        const auto title = i < titles.size() ? titles[i] : tabs->tabText(i);
        tabs->widget(i)->setProperty(titleProperty, title);
        if (auto* button = closeButton(tabs, i)) {
            button->setAccessibleName(QString("Close %1").arg(title));
            button->setToolTip(QString("Close %1").arg(title)); button->setEnabled(canClose);
        }
    }
    if (QAccessible::isActive()) {
        if (auto* container = dynamic_cast<ProjectContainer*>(QAccessible::queryAccessibleInterface(tabs))) container->changed();
        if (auto* bar = dynamic_cast<ProjectTabBar*>(QAccessible::queryAccessibleInterface(tabs->tabBar()))) bar->changed();
    }
}
}
