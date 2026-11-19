#pragma once
#include <QRectF>
#include <QStringList>
#include <QToolButton>
#include <QWidget>
#include <functional>

class QButtonGroup;
class QVariantAnimation;

namespace compositor::ui {
// A capsule of mutually exclusive choices whose accent pill slides to the chosen one.
// setCurrentIndex only updates what is shown; changed runs when the user picks a choice other than the current one.
class SegmentedControl final : public QWidget {
public:
    explicit SegmentedControl(const QStringList& items, QWidget* parent = nullptr);
    int currentIndex() const;
    // Moves the pill at once, without animation or the changed callback.
    void setCurrentIndex(int index);
    // Same effect as the user picking the choice, including when the control is disabled or hidden.
    void choose(int index);
    QToolButton* segment(int index) const;
    // Where the accent pill is drawn now, which differs from the chosen segment while it slides.
    QRectF pillRect() const;
    std::function<void(int)> changed;
protected:
    void paintEvent(QPaintEvent*) override;
private:
    QButtonGroup* group_;
    QVariantAnimation* slide_;
    QRectF from_, to_;
    int shown_{};
};
// The classes here have no Q_OBJECT, so findChild cannot take SegmentedControl directly.
SegmentedControl* findSegmentedControl(const QObject* root, const char* objectName);
}
