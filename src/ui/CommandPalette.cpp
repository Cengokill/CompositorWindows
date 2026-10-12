#include "CommandPalette.h"
#include <QApplication>
#include <QColor>
#include <QEvent>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QGuiApplication>
#include <QScreen>
#include <QVBoxLayout>
#include <algorithm>

namespace compositor::ui {
namespace {
constexpr int kIdRole=Qt::UserRole;
constexpr int kEnabledRole=Qt::UserRole+1;
QString rowText(const CommandListItem& item){
    QString text=item.checked?QString::fromUtf8("\xE2\x9C\x93 ")+item.label:item.label;
    if(!item.menu.isEmpty())text+="    "+item.menu;
    if(!item.shortcut.isEmpty())text+="    "+item.shortcut;
    return text;
}
}

CommandPalette::CommandPalette(QWidget* host,CommandRegistry& registry,QWidget* restoreFocus)
    :QWidget(host,Qt::Popup),registry_(registry),restoreFocus_(restoreFocus){
    setObjectName("commandPalette");
    setAttribute(Qt::WA_DeleteOnClose);
    setStyleSheet(R"(
        QWidget#commandPalette { background:#16181c; border:1px solid #3a3d44; border-radius:12px; }
        QLineEdit#commandPaletteSearch {
            background:#1e2126; color:#f2f4f8; border:1px solid #3a3d44; border-radius:8px;
            padding:8px 10px; selection-background-color:#579cf5;
        }
        QListWidget#commandPaletteList { background:transparent; color:#f2f4f8; border:0; outline:0; }
        QListWidget#commandPaletteList::item { padding:8px 10px; border-radius:6px; }
        QListWidget#commandPaletteList::item:selected { background:#579cf5; color:#ffffff; }
    )");
    auto* layout=new QVBoxLayout(this);
    layout->setContentsMargins(12,12,12,12);
    layout->setSpacing(8);
    search_=new QLineEdit(this);
    search_->setObjectName("commandPaletteSearch");
    search_->setPlaceholderText("Search commands and tools");
    search_->setClearButtonEnabled(true);
    results_=new QListWidget(this);
    results_->setObjectName("commandPaletteList");
    results_->setFocusPolicy(Qt::NoFocus);
    results_->setUniformItemSizes(true);
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
        if(!item.enabled)row->setForeground(QColor("#6b6e76"));
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
