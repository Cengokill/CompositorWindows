#pragma once
#include <QAction>
#include <QString>
#include <QTranslator>

class QMenu;
class QWidget;

namespace compositor::ui {
// Simplified Chinese interface text.
//
// The command catalog, the shortcut preferences and the menu ordering all key
// off the English menu and label text, so localization never rewrites those
// keys. Widgets display the translated text while the catalog keeps the string
// it was built with.
QString localizedText(const QString& english);

// Loads the bundled Simplified Chinese catalog. Returns false when the resource
// is missing or cannot be parsed, in which case the interface stays English.
bool installChineseTranslation(QTranslator& translator);

// Adds a menu item whose visible text is translated while its stored data keeps
// the English label that command lookup uses.
QAction* addLocalizedAction(QMenu* menu,const QString& english);
void applyLocalizedText(QAction* action);

// Translates a menu caption in place. The original English title is retained in
// the menu's "localizedMenuTitle" property for later lookups.
void applyLocalizedMenuTitle(QMenu* menu);
void applyLocalizedWindowTitle(QWidget* widget,const QString& english);

// Walks a built window once and translates the visible captions that widgets
// carry directly: label and button text, group boxes, tab titles, dock titles,
// placeholders and tooltips. Widgets remember their English text in the
// "localizedSourceText", "localizedSourceTitle", "localizedSourceTip" and
// "localizedSourcePlaceholder" properties so a later refresh can re-apply it.
void localizeWidgetTree(QWidget* root);

// Translates text a widget is about to display; returns the English text when no
// translation exists so callers can keep updating captions safely.
QString localizedWidgetText(const QString& english);
}
