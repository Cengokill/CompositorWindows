#pragma once
#include "PropertyControls.h"
#include <QColor>
#include <QComboBox>
#include <QFont>
#include <QFrame>
#include <QLineEdit>
#include <QPixmap>
#include <QToolButton>
#include <QWidget>
#include <functional>

class QPainter;

namespace compositor::ui {
// Resolve the panel's Segoe UI Variable font and set an exact logical pixel size.
QFont layerPanelFont(int pixels, QFont::Weight weight = QFont::Normal);

// Flat square button; active changes only its ink to the panel accent.
class IconButton : public QToolButton {
public:
    explicit IconButton(const QString& symbol, QWidget* parent = nullptr);
    void setSymbol(const QString& symbol);
    void setActive(bool active);
    bool active() const { return active_; }
protected:
    void paintEvent(QPaintEvent*) override;
    void changeEvent(QEvent*) override;
    void paintSurface(QPainter&) const;
    QColor inkColor() const;
    QString symbol_;
    int iconSize_{};
private:
    bool active_{};
};

// A 30-pixel outlined search box with a plain, single-line Qt text editor.
class SearchField final : public QWidget {
public:
    explicit SearchField(QWidget* parent = nullptr);
    QLineEdit* lineEdit() const { return edit_; }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    QLineEdit* edit_{};
};

// Capsule selector that preserves transient preview and dismissal callbacks.
class CapsuleDropdown final : public QComboBox {
public:
    explicit CapsuleDropdown(QWidget* parent = nullptr);
    std::function<void(int)> preview;
    std::function<void()> dismissed;
    void showPopup() override;
    void hidePopup() override;
    // Shows the list as a rounded Liquid Glass panel: a translucent neutral tint over a light blur of what lies behind it.
    void useGlassPopup();
protected:
    bool eventFilter(QObject*, QEvent*) override;
    void paintEvent(QPaintEvent*) override;
private:
    void dismiss();
    void captureGlassBackdrop();
    void paintGlassPanel();
    bool open_{};
    QPixmap glassBackdrop_;
};

// A button that looks like CapsuleDropdown (left-aligned text, up and down chevrons) but opens its own popup.
class CapsuleButton final : public QToolButton {
public:
    explicit CapsuleButton(QWidget* parent = nullptr);
protected:
    void paintEvent(QPaintEvent*) override;
};

// Thin 3-pixel opacity track with a 10 x 18-pixel upright knob.
class Slider final : public TrackSlider {
public:
    explicit Slider(Qt::Orientation orientation, QWidget* parent = nullptr);
};

// Options-bar slider: a 4-pixel track and a wide, almost round 18 x 15 knob.
class OptionSlider final : public TrackSlider {
public:
    explicit OptionSlider(Qt::Orientation orientation, QWidget* parent = nullptr);
};

// Fixed-width, rounded numeric field. Editing behavior belongs to its owner.
class PercentField : public QLineEdit {
public:
    explicit PercentField(QWidget* parent = nullptr);
};

// Full-width, one-pixel separator in the Layers panel palette.
class Divider final : public QFrame {
public:
    explicit Divider(QWidget* parent = nullptr);
};

// Footer icon button whose padding is part of its clickable area.
class FooterIconButton : public IconButton {
public:
    explicit FooterIconButton(const QString& symbol, QWidget* parent = nullptr);
};

// Footer icon and chevron button; an attached menu opens above the icon.
class FooterMenuButton final : public IconButton {
public:
    explicit FooterMenuButton(const QString& symbol, QWidget* parent = nullptr);
protected:
    void paintEvent(QPaintEvent*) override;
};

// Centered placeholder for an unavailable layer list.
class EmptyState final : public QWidget {
public:
    explicit EmptyState(QWidget* parent = nullptr);
    void setDetail(const QString& detail);
protected:
    void paintEvent(QPaintEvent*) override;
private:
    QString detail_;
};
}
