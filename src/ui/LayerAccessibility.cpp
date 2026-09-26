#include "LayerAccessibility.h"
#include "LayerPanel.h"
#include <QAccessibleWidget>
#include <QPointer>
#include <algorithm>
#include <map>
#include <mutex>

namespace compositor::ui {
namespace {
// The item-view provider combines its checkbox and row label. Expose the
// existing painted checkbox independently while retaining Qt's tree provider.
class VisibilityControl final : public QAccessibleInterface, public QAccessibleActionInterface {
public:
    VisibilityControl(LayerPanelController* controller, QWidget* parent, std::string id)
        : controller_(controller), parent_(parent), id_(std::move(id)) {}
    std::optional<LayerVisibilityControl> control() const {
        return controller_ ? controller_->visibilityControl(id_) : std::nullopt;
    }
    bool isValid() const override { return parent_ && control().has_value(); }
    QObject* object() const override { return nullptr; }
    // A virtual child has no native HWND. Qt walks parent() for its fragment
    // root; returning the top-level window here creates a second HWND host.
    QWindow* window() const override { return nullptr; }
    QAccessibleInterface* parent() const override { return parent_ ? QAccessible::queryAccessibleInterface(parent_) : nullptr; }
    QAccessibleInterface* child(int) const override { return nullptr; }
    QAccessibleInterface* childAt(int, int) const override { return nullptr; }
    int childCount() const override { return 0; }
    int indexOfChild(const QAccessibleInterface*) const override { return -1; }
    QString text(QAccessible::Text type) const override {
        const auto value = control();
        return value && type == QAccessible::Name ? value->name : QString{};
    }
    void setText(QAccessible::Text, const QString&) override {}
    QRect rect() const override { const auto value = control(); return value ? value->globalRect : QRect{}; }
    QAccessible::Role role() const override { return QAccessible::CheckBox; }
    QAccessible::State state() const override {
        QAccessible::State result;
        const auto value = control();
        result.invalid = !value;
        result.checkable = true;
        result.checked = value && value->visible;
        result.disabled = !value || !value->enabled;
        result.invisible = !value || !value->exposed;
        result.offscreen = !value || value->globalRect.isEmpty();
        return result;
    }
    void* interface_cast(QAccessible::InterfaceType type) override {
        return type == QAccessible::ActionInterface ? static_cast<QAccessibleActionInterface*>(this) : nullptr;
    }
    QStringList actionNames() const override { return {QAccessibleActionInterface::toggleAction()}; }
    QStringList keyBindingsForAction(const QString&) const override { return {}; }
    void doAction(const QString& action) override {
        if (action == QAccessibleActionInterface::toggleAction() && controller_)
            controller_->toggleVisibilityControl(id_);
    }
    const std::string& layerId() const { return id_; }
private:
    QPointer<LayerPanelController> controller_;
    QPointer<QWidget> parent_;
    std::string id_;
};

class ActionControl final : public QAccessibleInterface, public QAccessibleActionInterface {
public:
    using Key=std::pair<std::string,LayerControlKind>;
    ActionControl(LayerPanelController* controller,QWidget* parent,Key key)
        :controller_(controller),parent_(parent),key_(std::move(key)){}
    std::optional<LayerActionControl> control()const{return controller_?controller_->actionControl(key_.first,key_.second):std::nullopt;}
    bool isValid()const override{return parent_&&control().has_value();}
    QObject* object()const override{return nullptr;}
    QWindow* window()const override{return nullptr;}
    QAccessibleInterface* parent()const override{return parent_?QAccessible::queryAccessibleInterface(parent_):nullptr;}
    QAccessibleInterface* child(int)const override{return nullptr;}
    QAccessibleInterface* childAt(int,int)const override{return nullptr;}
    int childCount()const override{return 0;}
    int indexOfChild(const QAccessibleInterface*)const override{return -1;}
    QString text(QAccessible::Text type)const override{const auto value=control();if(!value)return {};return type==QAccessible::Name?value->name:type==QAccessible::Description?value->description:QString{};}
    void setText(QAccessible::Text,const QString&)override{}
    QRect rect()const override{const auto value=control();return value?value->globalRect:QRect{};}
    QAccessible::Role role()const override{return QAccessible::Button;}
    QAccessible::State state()const override{QAccessible::State result;const auto value=control();result.invalid=!value;result.disabled=!value||!value->enabled;result.invisible=!value||!value->exposed;result.offscreen=!value||value->globalRect.isEmpty();return result;}
    void* interface_cast(QAccessible::InterfaceType type)override{return type==QAccessible::ActionInterface?static_cast<QAccessibleActionInterface*>(this):nullptr;}
    QStringList actionNames()const override{return {QAccessibleActionInterface::pressAction()};}
    QStringList keyBindingsForAction(const QString&)const override{return {};}
    void doAction(const QString& action)override{if(action==QAccessibleActionInterface::pressAction()&&controller_)controller_->invokeActionControl(key_.first,key_.second);}
    const Key& key()const{return key_;}
private:
    QPointer<LayerPanelController> controller_;
    QPointer<QWidget> parent_;
    Key key_;
};

class LayerContainer final : public QAccessibleWidget {
public:
    LayerContainer(QWidget* parent, LayerPanelController* controller)
        : QAccessibleWidget(parent), controller_(controller) {}
    ~LayerContainer() override {
        for (const auto& [id, token] : controls_) { (void)id; QAccessible::deleteAccessibleInterface(token); }
        for (const auto& [key, token] : actionControls_) { (void)key; QAccessible::deleteAccessibleInterface(token); }
    }
    std::vector<std::string> ids() const { return controller_ ? controller_->exposedVisibilityControls() : std::vector<std::string>{}; }
    std::vector<ActionControl::Key> actionIds()const{return controller_?controller_->exposedActionControls():std::vector<ActionControl::Key>{};}
    int childCount() const override { return QAccessibleWidget::childCount() + int(ids().size()) + int(actionIds().size()); }
    QAccessibleInterface* child(int index) const override {
        const int base = QAccessibleWidget::childCount();
        if (index < base) return QAccessibleWidget::child(index);
        const auto visible = ids();
        if (index < base) return nullptr;
        if(size_t(index-base)>=visible.size()){
            const auto actions=actionIds();const size_t position=size_t(index-base)-visible.size();
            if(position>=actions.size())return nullptr;
            const auto& key=actions[position];auto found=actionControls_.find(key);
            if(found==actionControls_.end())found=actionControls_.emplace(key,QAccessible::registerAccessibleInterface(new ActionControl(controller_,widget(),key))).first;
            return QAccessible::accessibleInterface(found->second);
        }
        const auto& id = visible[size_t(index - base)];
        auto found = controls_.find(id);
        if (found == controls_.end()) {
            const auto token = QAccessible::registerAccessibleInterface(new VisibilityControl(controller_, widget(), id));
            found = controls_.emplace(id, token).first;
        }
        return QAccessible::accessibleInterface(found->second);
    }
    int indexOfChild(const QAccessibleInterface* child) const override {
        if (const auto* visibility = dynamic_cast<const VisibilityControl*>(child)) {
            const auto visible = ids();
            const auto found = std::find(visible.begin(), visible.end(), visibility->layerId());
            return found == visible.end() ? -1 : QAccessibleWidget::childCount() + int(found - visible.begin());
        }
        if(const auto* action=dynamic_cast<const ActionControl*>(child)){
            const auto actions=actionIds();const auto found=std::find(actions.begin(),actions.end(),action->key());
            return found==actions.end()?-1:QAccessibleWidget::childCount()+int(ids().size())+int(found-actions.begin());
        }
        return QAccessibleWidget::indexOfChild(child);
    }
    QAccessibleInterface* childAt(int x, int y) const override {
        const int base = QAccessibleWidget::childCount();
        const auto count=ids().size()+actionIds().size();
        for (size_t i = 0; i < count; ++i) {
            auto* candidate = child(base + int(i));
            if (candidate && candidate->rect().contains(x, y)) return candidate;
        }
        return QAccessibleWidget::childAt(x, y);
    }
    void changed() {
        for (auto it = controls_.begin(); it != controls_.end();) {
            auto* control = QAccessible::accessibleInterface(it->second);
            if (!control || !control->isValid()) {
                QAccessible::deleteAccessibleInterface(it->second);
                it = controls_.erase(it);
            } else {
                QAccessibleEvent name(control, QAccessible::NameChanged);
                QAccessible::updateAccessibility(&name);
                QAccessible::State changes; changes.checked = true; changes.disabled = true; changes.invisible = true;
                QAccessibleStateChangeEvent state(control, changes);
                QAccessible::updateAccessibility(&state);
                ++it;
            }
        }
        for(auto it=actionControls_.begin();it!=actionControls_.end();){
            auto* control=QAccessible::accessibleInterface(it->second);
            if(!control||!control->isValid()){QAccessible::deleteAccessibleInterface(it->second);it=actionControls_.erase(it);}
            else{
                QAccessibleEvent name(control,QAccessible::NameChanged);QAccessible::updateAccessibility(&name);
                QAccessible::State changes;changes.disabled=true;changes.invisible=true;changes.offscreen=true;
                QAccessibleStateChangeEvent state(control,changes);QAccessible::updateAccessibility(&state);++it;
            }
        }
        QAccessibleEvent structure(widget(), QAccessible::ObjectReorder);
        QAccessible::updateAccessibility(&structure);
    }
private:
    QPointer<LayerPanelController> controller_;
    mutable std::map<std::string, QAccessible::Id> controls_;
    mutable std::map<ActionControl::Key,QAccessible::Id> actionControls_;
};

QAccessibleInterface* factory(const QString&, QObject* object) {
    auto* parent = qobject_cast<QWidget*>(object);
    if (!parent || !parent->property("compositorLayerAccessibility").toBool()) return nullptr;
    auto* tree = parent->findChild<QTreeWidget*>("layersTree", Qt::FindDirectChildrenOnly);
    auto* controller = tree ? LayerPanelController::find(tree) : nullptr;
    return controller ? new LayerContainer(parent, controller) : nullptr;
}
}
void installLayerAccessibility(QTreeWidget* tree) {
    static std::once_flag installed;
    std::call_once(installed, [] { QAccessible::installFactory(factory); });
    tree->parentWidget()->setProperty("compositorLayerAccessibility", true);
}
void updateLayerAccessibility(QTreeWidget* tree) {
    if (!QAccessible::isActive()) return;
    if (auto* container = dynamic_cast<LayerContainer*>(QAccessible::queryAccessibleInterface(tree->parentWidget())))
        container->changed();
}
}
