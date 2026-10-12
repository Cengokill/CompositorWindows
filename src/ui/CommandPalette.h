#pragma once
#include "CommandRegistry.h"
#include <QPointer>
#include <QWidget>
class QHideEvent;
class QLineEdit;
class QListWidget;
class QListWidgetItem;

namespace compositor::ui {
class CommandPalette final:public QWidget {
public:
    CommandPalette(QWidget* host,CommandRegistry& registry,QWidget* restoreFocus);
    void popup();
protected:
    bool eventFilter(QObject*,QEvent*)override;
    void hideEvent(QHideEvent*)override;
private:
    CommandRegistry& registry_;
    QPointer<QWidget> restoreFocus_;
    QLineEdit* search_{};
    QListWidget* results_{};
    bool restoring_{};
    void refill();
    void restoreFocus();
    void runCurrent();
    void moveSelection(int delta);
};
}
