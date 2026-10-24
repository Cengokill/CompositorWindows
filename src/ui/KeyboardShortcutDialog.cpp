#include "KeyboardShortcutDialog.h"
#include <QBrush>
#include <QColor>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <set>

namespace compositor::ui {
namespace {
QString keysText(const QList<QKeySequence>& keys){QStringList text;for(const auto& key:keys)text.push_back(key.toString(QKeySequence::NativeText));return text.join(", ");}
}
KeyboardShortcutDialog::KeyboardShortcutDialog(QWidget* parent,const CommandRegistry& registry,Commit commit)
    :QDialog(parent),defaults_(registry.defaultShortcuts()),draft_(registry.shortcuts()),commit_(std::move(commit)){
    setObjectName("keyboardShortcutDialog");setWindowTitle("Keyboard Shortcuts");resize(640,640);
    auto* layout=new QVBoxLayout(this);
    search_=new QLineEdit;search_->setObjectName("shortcutSearch");search_->setPlaceholderText("Search commands");search_->setClearButtonEnabled(true);layout->addWidget(search_);
    list_=new QTreeWidget;list_->setObjectName("shortcutList");list_->setColumnCount(3);list_->setHeaderLabels({"Command","Menu","Shortcut"});list_->setRootIsDecorated(false);list_->setAccessibleName("Commands and shortcuts");
    list_->header()->setSectionResizeMode(0,QHeaderView::Stretch);
    for(const auto& item:registry.listCommands()){
        if(!draft_.count(item.id))continue;
        auto* row=new QTreeWidgetItem(list_,{item.label,item.menu,{}});row->setData(0,Qt::UserRole,item.id);
    }
    layout->addWidget(list_,1);
    auto* editor=new QHBoxLayout;
    primary_=new QKeySequenceEdit;primary_->setObjectName("shortcutPrimary");primary_->setMaximumSequenceLength(1);primary_->setAccessibleName("Shortcut");
    secondary_=new QKeySequenceEdit;secondary_->setObjectName("shortcutSecondary");secondary_->setMaximumSequenceLength(1);secondary_->setAccessibleName("Alternate shortcut");
    clear_=new QPushButton("Clear");clear_->setObjectName("shortcutClear");
    editor->addWidget(new QLabel("Shortcut"));editor->addWidget(primary_,1);editor->addWidget(new QLabel("Alternate"));editor->addWidget(secondary_,1);editor->addWidget(clear_);
    layout->addLayout(editor);
    status_=new QLabel;status_->setObjectName("shortcutStatus");status_->setWordWrap(true);layout->addWidget(status_);
    auto* buttons=new QHBoxLayout;
    auto* restore=new QPushButton("Restore Defaults");restore->setObjectName("shortcutRestoreDefaults");
    auto* cancel=new QPushButton("Cancel");cancel->setObjectName("shortcutCancel");
    save_=new QPushButton("Save");save_->setObjectName("shortcutSave");save_->setDefault(true);
    buttons->addWidget(restore);buttons->addStretch(1);buttons->addWidget(cancel);buttons->addWidget(save_);
    layout->addLayout(buttons);
    connect(search_,&QLineEdit::textChanged,this,[this]{refreshRows();});
    connect(list_,&QTreeWidget::currentItemChanged,this,[this]{loadSelection();});
    connect(primary_,&QKeySequenceEdit::keySequenceChanged,this,[this]{editSelection();});
    connect(secondary_,&QKeySequenceEdit::keySequenceChanged,this,[this]{editSelection();});
    connect(clear_,&QPushButton::clicked,this,[this]{{QSignalBlocker a(primary_),b(secondary_);primary_->clear();secondary_->clear();}editSelection();});
    connect(restore,&QPushButton::clicked,this,[this]{draft_=defaults_;loadSelection();refreshRows();});
    connect(cancel,&QPushButton::clicked,this,&QDialog::reject);
    connect(save_,&QPushButton::clicked,this,[this]{save();});
    refreshRows();
    if(list_->topLevelItemCount())list_->setCurrentItem(list_->topLevelItem(0));
}
QString KeyboardShortcutDialog::selectedId()const{auto* item=list_->currentItem();return item?item->data(0,Qt::UserRole).toString():QString{};}
void KeyboardShortcutDialog::loadSelection(){
    const auto id=selectedId();const auto keys=id.isEmpty()?QList<QKeySequence>{}:draft_[id];
    QSignalBlocker a(primary_),b(secondary_);
    primary_->setKeySequence(keys.size()>0?keys[0]:QKeySequence{});secondary_->setKeySequence(keys.size()>1?keys[1]:QKeySequence{});
    primary_->setEnabled(!id.isEmpty());secondary_->setEnabled(!id.isEmpty());clear_->setEnabled(!id.isEmpty());
}
void KeyboardShortcutDialog::editSelection(){
    const auto id=selectedId();if(id.isEmpty())return;
    QList<QKeySequence> keys;for(auto* edit:{primary_,secondary_})if(!edit->keySequence().isEmpty())keys.push_back(edit->keySequence());
    draft_[id]=keys;refreshRows();
}
void KeyboardShortcutDialog::refreshRows(){
    const auto needle=search_->text().trimmed();
    for(int i=0;i<list_->topLevelItemCount();++i){
        auto* row=list_->topLevelItem(i);const auto id=row->data(0,Qt::UserRole).toString();
        row->setText(2,keysText(draft_[id]));
        row->setHidden(!needle.isEmpty()&&!row->text(0).contains(needle,Qt::CaseInsensitive)&&!row->text(1).contains(needle,Qt::CaseInsensitive)&&!row->text(2).contains(needle,Qt::CaseInsensitive));
    }
    validate();
}
void KeyboardShortcutDialog::validate(){
    QStringList problems;std::set<QString> flagged;
    auto label=[this](const QString& id){for(int i=0;i<list_->topLevelItemCount();++i)if(list_->topLevelItem(i)->data(0,Qt::UserRole).toString()==id)return list_->topLevelItem(i)->text(0);return id;};
    for(const auto&[id,keys]:draft_)for(const auto& key:keys)if(const auto problem=shortcutProblem(key);!problem.isEmpty()){problems.push_back(label(id)+": "+problem);flagged.insert(id);}
    for(const auto& conflict:findShortcutConflicts(draft_)){
        QStringList names;for(const auto& id:conflict.ids){names.push_back(label(id));flagged.insert(id);}
        problems.push_back(conflict.key.toString(QKeySequence::NativeText)+" is assigned to "+names.join(" and ")+".");
    }
    for(int i=0;i<list_->topLevelItemCount();++i){auto* row=list_->topLevelItem(i);row->setForeground(2,flagged.count(row->data(0,Qt::UserRole).toString())?QBrush(QColor(232,72,72)):list_->palette().text());}
    save_->setEnabled(problems.isEmpty());
    status_->setText(problems.isEmpty()?QString{}:"Save is unavailable until each shortcut is unique and valid. Change or clear these assignments:\n"+problems.join("\n"));
}
void KeyboardShortcutDialog::save(){
    validate();if(!save_->isEnabled())return;
    QString error;
    if(commit_&&commit_(draft_,&error)){accept();return;}
    status_->setText(error.isEmpty()?"The shortcuts could not be saved. Your previous shortcuts are still active.":error);
}
}