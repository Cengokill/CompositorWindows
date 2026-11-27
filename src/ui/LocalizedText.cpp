#include "LocalizedText.h"
#include <QAbstractButton>
#include <QComboBox>
#include <QCoreApplication>
#include <QDockWidget>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QStringList>
#include <QTabBar>
#include <QTextEdit>
#include <QWidget>

namespace compositor::ui {
namespace {
// The catalog lives inside the executable so a portable copy stays self-contained.
constexpr auto catalogResource = ":/i18n/compositor_zh_CN.qm";

// A leading mnemonic such as "&File" is stripped before lookup: the Chinese
// catalog is keyed by the plain caption.
QString plain(const QString& text){return QString(text).remove('&');}

// Remembers the shipped English caption the first time a widget is localized and
// reuses it afterwards, so repeated passes stay idempotent.
QString sourceFor(QObject* object,const char* property,const QString& current){
    const auto stored=object->property(property).toString();
    if(!stored.isEmpty())return stored;
    object->setProperty(property,current);
    return current;
}

QString translateAgainst(const QString& english){
    if(english.isEmpty())return english;
    const QString translated=QCoreApplication::translate("Compositor",english.toUtf8().constData());
    return translated.isEmpty()?english:translated;
}
}

QString localizedText(const QString& english){
    return translateAgainst(english);
}

bool installChineseTranslation(QTranslator& translator){
    if(!translator.load(QString::fromLatin1(catalogResource)))return false;
    return QCoreApplication::installTranslator(&translator);
}

QAction* addLocalizedAction(QMenu* menu,const QString& english){
    if(!menu)return nullptr;
    auto* item=menu->addAction(localizedText(plain(english)));
    if(item)item->setData(english);
    return item;
}

void applyLocalizedText(QAction* action){
    if(!action)return;
    const QString english=action->data().toString();
    if(english.isEmpty())return;
    action->setText(localizedText(plain(english)));
}

void applyLocalizedMenuTitle(QMenu* menu){
    if(!menu)return;
    const QString english=menu->title();
    if(english.isEmpty())return;
    const QString original=sourceFor(menu,"localizedMenuTitle",english);
    menu->setTitle(localizedText(plain(original)));
}

void applyLocalizedWindowTitle(QWidget* widget,const QString& english){
    if(!widget)return;
    widget->setWindowTitle(localizedText(english));
}

QString localizedWidgetText(const QString& english){
    return translateAgainst(plain(english));
}

void localizeWidgetTree(QWidget* root){
    if(!root)return;
    QList<QWidget*> widgets{root};
    widgets.append(root->findChildren<QWidget*>());
    for(auto* widget:widgets){
        if(!widget)continue;
        // Docks carry their caption in the title, and custom title bars mirror it.
        if(auto* dock=qobject_cast<QDockWidget*>(widget)){
            const QString english=sourceFor(dock,"localizedSourceTitle",dock->windowTitle());
            dock->setWindowTitle(localizedText(plain(english)));
        }
        if(auto* group=qobject_cast<QGroupBox*>(widget)){
            const QString english=sourceFor(group,"localizedSourceText",group->title());
            group->setTitle(localizedText(plain(english)));
        }
        if(auto* combo=qobject_cast<QComboBox*>(widget)){
            // Item captions live in the model, so translate every entry once and
            // remember the shipped list under one property.
            if(!combo->property("localizedSourceItems").isValid()){
                QStringList english;
                for(int index=0;index<combo->count();++index)english.append(combo->itemText(index));
                combo->setProperty("localizedSourceItems",english);
            }
            const auto english=combo->property("localizedSourceItems").toStringList();
            for(int index=0;index<combo->count()&&index<english.size();++index)
                combo->setItemText(index,localizedText(plain(english.at(index))));
        }
        if(auto* label=qobject_cast<QLabel*>(widget)){
            const QString english=sourceFor(label,"localizedSourceText",label->text());
            label->setText(localizedText(plain(english)));
        }
        if(auto* button=qobject_cast<QAbstractButton*>(widget)){
            const QString english=sourceFor(button,"localizedSourceText",button->text());
            button->setText(localizedText(plain(english)));
        }
        if(auto* edit=qobject_cast<QLineEdit*>(widget)){
            const QString english=sourceFor(edit,"localizedSourcePlaceholder",edit->placeholderText());
            if(!english.isEmpty())edit->setPlaceholderText(localizedText(plain(english)));
        }
        if(auto* text=qobject_cast<QTextEdit*>(widget)){
            const QString english=sourceFor(text,"localizedSourcePlaceholder",text->placeholderText());
            if(!english.isEmpty())text->setPlaceholderText(localizedText(plain(english)));
        }
        if(auto* text=qobject_cast<QPlainTextEdit*>(widget)){
            const QString english=sourceFor(text,"localizedSourcePlaceholder",text->placeholderText());
            if(!english.isEmpty())text->setPlaceholderText(localizedText(plain(english)));
        }
        const QString tip=widget->toolTip();
        if(!tip.isEmpty()&&!widget->property("localizedSourceTip").isValid()){
            const QString english=sourceFor(widget,"localizedSourceTip",tip);
            widget->setToolTip(translateAgainst(plain(english)));
        }
    }
    // Tab captions belong to the tab bar, not to the page widgets.
    for(auto* bar:root->findChildren<QTabBar*>()){
        for(int index=0;index<bar->count();++index){
            const QString english=bar->tabText(index);
            if(english.isEmpty())continue;
            bar->setTabText(index,localizedText(plain(english)));
        }
    }
    for(auto* menu:root->findChildren<QMenu*>())applyLocalizedMenuTitle(menu);
}
}
