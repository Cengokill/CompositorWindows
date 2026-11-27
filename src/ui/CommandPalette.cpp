#include "CommandPalette.h"
#include "LayerPanelTokens.h"
#include "LayerPanelWidgets.h"
#include "LocalizedText.h"
#include <QApplication>
#include <QColor>
#include <QEvent>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QGuiApplication>
#include <QPainter>
#include <QScreen>
#include <QStyledItemDelegate>
#include <QVBoxLayout>
#include <algorithm>

namespace compositor::ui {
namespace {
constexpr int kIdRole=Qt::UserRole;
constexpr int kEnabledRole=Qt::UserRole+1;
constexpr int kLabelRole=Qt::UserRole+2;
constexpr int kMenuRole=Qt::UserRole+3;
constexpr int kShortcutRole=Qt::UserRole+4;
constexpr int kRowHeight=34;
constexpr int kRowInset=14;
constexpr int kRowGap=12;
QString labelText(const CommandListItem& item){const auto label=localizedText(item.label);return item.checked?QString::fromUtf8("\xE2\x9C\x93 ")+label:label;}
QString rowText(const CommandListItem& item){
    QString text=labelText(item);
    if(!item.menu.isEmpty())text+="    "+localizedText(item.menu);
    if(!item.shortcut.isEmpty())text+="    "+item.shortcut;
    return text;
}
// The label, its menu in dimmer ink and the shortcut on the right. The current row is a full capsule, in the application
// blue when the command can run.
class RowDelegate final:public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem& option,const QModelIndex& index)const override{
        return {QStyledItemDelegate::sizeHint(option,index).width(),kRowHeight};
    }
    void paint(QPainter* painter,const QStyleOptionViewItem& option,const QModelIndex& index)const override{
        const bool enabled=index.data(kEnabledRole).toBool(),current=option.state&QStyle::State_Selected,hovered=option.state&QStyle::State_MouseOver;
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        if(current||hovered){
            const QRectF pill=QRectF(option.rect).adjusted(0,1,0,-1);
            painter->setPen(Qt::NoPen);
            painter->setBrush(current&&enabled?QColor(layer_panel::accent):QColor(255,255,255,20));
            painter->drawRoundedRect(pill,pill.height()/2,pill.height()/2);
        }
        const QColor ink=!enabled?QColor(layer_panel::glassDisabled):current?QColor(Qt::white):QColor(layer_panel::primary);
        const QColor detail=!enabled?ink:current?QColor(255,255,255,200):QColor(layer_panel::glassDetail);
        const auto metrics=option.fontMetrics;
        QRect area=option.rect.adjusted(kRowInset,0,-kRowInset,0);
        painter->setFont(option.font);
        if(const auto shortcut=index.data(kShortcutRole).toString();!shortcut.isEmpty()){
            painter->setPen(detail);
            painter->drawText(area,Qt::AlignRight|Qt::AlignVCenter,shortcut);
            area.setRight(area.right()-metrics.horizontalAdvance(shortcut)-kRowGap);
        }
        const auto label=metrics.elidedText(index.data(kLabelRole).toString(),Qt::ElideRight,area.width());
        painter->setPen(ink);
        painter->drawText(area,Qt::AlignLeft|Qt::AlignVCenter,label);
        area.setLeft(area.left()+metrics.horizontalAdvance(label)+kRowGap);
        if(area.width()>0){
            painter->setPen(detail);
            painter->drawText(area,Qt::AlignLeft|Qt::AlignVCenter,metrics.elidedText(index.data(kMenuRole).toString(),Qt::ElideRight,area.width()));
        }
        painter->restore();
    }
};
}

CommandPalette::CommandPalette(QWidget* host,CommandRegistry& registry,QWidget* restoreFocus)
    :QWidget(host,Qt::Popup),registry_(registry),restoreFocus_(restoreFocus){
    setObjectName("commandPalette");
    setAttribute(Qt::WA_DeleteOnClose);
    makeGlassPopup(this);
    auto* layout=new QVBoxLayout(this);
    layout->setContentsMargins(12,12,12,12);
    layout->setSpacing(8);
    search_=new QLineEdit(this);
    search_->setObjectName("commandPaletteSearch");
    search_->setProperty("glassSearch",true);
    search_->setPlaceholderText("Search commands and tools");
    search_->setClearButtonEnabled(true);
    results_=new QListWidget(this);
    results_->setObjectName("commandPaletteList");
    results_->setFocusPolicy(Qt::NoFocus);
    results_->setUniformItemSizes(true);
    results_->setMouseTracking(true);
    results_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    results_->setItemDelegate(new RowDelegate(results_));
    results_->setStyleSheet("QListWidget { background:transparent; border:0; }");
    layout->addWidget(search_);
    layout->addWidget(results_,1);
    setFixedSize(560,420);
    search_->installEventFilter(this);
    connect(search_,&QLineEdit::textChanged,this,[this]{refill();});
    connect(results_,&QListWidget::itemClicked,this,[this](QListWidgetItem*){runCurrent();});
    refill();
}

void CommandPalette::popup(){
    auto* host=parentWidget();
    QPoint origin=host?host->mapToGlobal(QPoint((host->width()-width())/2,host->height()/6)):QPoint();
    if(auto* screen=QGuiApplication::screenAt(origin)){
        const auto area=screen->availableGeometry();
        origin.setX(std::clamp(origin.x(),area.left(),std::max(area.left(),area.right()+1-width())));
        origin.setY(std::clamp(origin.y(),area.top(),std::max(area.top(),area.bottom()+1-height())));
    }
    move(origin);
    show();
    raise();
    search_->setFocus();
    search_->selectAll();
}

void CommandPalette::refill(){
    const auto items=filterCommands(registry_.listCommands(),search_->text());
    results_->clear();
    for(const auto& item:items){
        auto* row=new QListWidgetItem(rowText(item),results_);
        row->setData(kIdRole,item.id);
        row->setData(kEnabledRole,item.enabled);
        row->setData(kLabelRole,labelText(item));
        row->setData(kMenuRole,item.menu);
        row->setData(kShortcutRole,item.shortcut);
    }
    if(results_->count()>0)results_->setCurrentRow(0);
}

void CommandPalette::restoreFocus(){
    if(restoring_)return;
    restoring_=true;
    if(restoreFocus_)restoreFocus_->setFocus();
}

void CommandPalette::runCurrent(){
    auto* row=results_->currentItem();
    if(!row||!row->data(kEnabledRole).toBool())return;
    const auto id=row->data(kIdRole).toString();
    auto* restore=restoreFocus_.data();
    auto* registry=&registry_;
    restoring_=true;
    hide();
    close();
    if(restore)restore->setFocus();
    registry->invoke(id);
}

void CommandPalette::moveSelection(int delta){
    if(results_->count()<=0)return;
    const int next=std::clamp(results_->currentRow()+delta,0,results_->count()-1);
    results_->setCurrentRow(next);
}

bool CommandPalette::eventFilter(QObject* object,QEvent* event){
    if(object==search_&&(event->type()==QEvent::KeyPress||event->type()==QEvent::ShortcutOverride)){
        auto* key=static_cast<QKeyEvent*>(event);
        if(key->key()==Qt::Key_Up||key->key()==Qt::Key_Down){
            if(event->type()==QEvent::KeyPress)moveSelection(key->key()==Qt::Key_Up?-1:1);
            event->accept();
            return true;
        }
        if(key->key()==Qt::Key_Return||key->key()==Qt::Key_Enter){
            if(event->type()==QEvent::KeyPress)runCurrent();
            event->accept();
            return true;
        }
        if(key->key()==Qt::Key_Escape){
            if(event->type()==QEvent::KeyPress)close();
            event->accept();
            return true;
        }
    }
    return QWidget::eventFilter(object,event);
}

void CommandPalette::hideEvent(QHideEvent* event){
    if(!restoring_)restoreFocus();
    QWidget::hideEvent(event);
}
}
