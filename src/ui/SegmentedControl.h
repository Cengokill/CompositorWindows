#pragma once
#include <QStringList>
#include <QToolButton>
#include <QWidget>
#include <functional>

class QButtonGroup;

namespace compositor::ui {
// A flat row of mutually exclusive choices. setCurrentIndex only updates what is shown;
// changed runs when the user picks a choice other than the current one.
class SegmentedControl final : public QWidget {
public:
    explicit SegmentedControl(const QStringList& items, QWidget* parent = nullptr);
    int currentIndex() const;
    void setCurrentIndex(int index);
    // Same effect as the user picking the choice, including when the control is disabled or hidden.
    void choose(int index);
    QToolButton* segment(int index) const;
    std::function<void(int)> changed;
private:
    QButtonGroup* group_;
    int shown_{};
};
// The classes here have no Q_OBJECT, so findChild cannot take SegmentedControl directly.
SegmentedControl* findSegmentedControl(const QObject* root, const char* objectName);
}
