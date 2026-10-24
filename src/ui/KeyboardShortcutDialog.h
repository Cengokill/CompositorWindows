#pragma once
#include "CommandRegistry.h"
#include <QDialog>
#include <functional>
class QKeySequenceEdit;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;

namespace compositor::ui {
// Edits a draft of the registry's shortcuts. Only Save hands the draft to the
// commit callback; Cancel and a failed commit leave the active shortcuts alone.
class KeyboardShortcutDialog final:public QDialog {
public:
    using Commit=std::function<bool(const ShortcutMap&,QString* error)>;
    KeyboardShortcutDialog(QWidget* parent,const CommandRegistry& registry,Commit commit);
    const ShortcutMap& draft()const{return draft_;}
private:
    ShortcutMap defaults_,draft_;
    Commit commit_;
    QTreeWidget* list_{};
    QLineEdit* search_{};
    QKeySequenceEdit* primary_{};
    QKeySequenceEdit* secondary_{};
    QPushButton* clear_{};
    QPushButton* save_{};
    QLabel* status_{};
    QString selectedId()const;
    void loadSelection();
    void editSelection();
    void refreshRows();
    void validate();
    void save();
};
}