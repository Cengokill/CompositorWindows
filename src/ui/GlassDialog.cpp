#include "GlassDialog.h"
#include "LayerPanelTokens.h"
#include <QDialog>
#include <QEvent>
#include <QGuiApplication>
#include <QLinearGradient>
#include <QPainter>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>

namespace compositor::ui {
namespace t = layer_panel;
namespace {
// Applies the backdrop whenever the dialog shows: switching the title bar style changes its window and shows it again.
// Qt paints no background for a translucent window, so without a backdrop the surface is painted here.
class GlassBackdrop final : public QObject {
public:
    explicit GlassBackdrop(QDialog* dialog) : QObject(dialog), dialog_(dialog) { dialog->installEventFilter(this); }
protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched != dialog_) return false;
        if (event->type() == QEvent::Show) apply();
        else if (event->type() == QEvent::Paint && !backdrop_) paint();
        return false;
    }
private:
    void apply() {
        // The native frame stays. The Windows 11 transient (acrylic) backdrop is extended under the client area, which
        // the translucent window lets through. Windows draws no backdrop behind a frameless window.
        backdrop_ = false;
        if (QGuiApplication::platformName() == "windows" && !dialog_->windowFlags().testFlag(Qt::FramelessWindowHint)) {
            if (const auto handle = reinterpret_cast<HWND>(dialog_->effectiveWinId())) {
                constexpr DWORD systemBackdropType = 38;
                constexpr int transientWindow = 3;
                const MARGINS wholeWindow{-1, -1, -1, -1};
                backdrop_ = SUCCEEDED(DwmSetWindowAttribute(handle, systemBackdropType, &transientWindow, sizeof(transientWindow)))
                    && SUCCEEDED(DwmExtendFrameIntoClientArea(handle, &wholeWindow));
            }
        }
        dialog_->update();
    }
    void paint() {
        QPainter painter(dialog_);
        QLinearGradient surface(0, 0, 0, dialog_->height());
        surface.setColorAt(0, QColor(t::control));
        surface.setColorAt(0.22, QColor(t::background));
        surface.setColorAt(1, QColor(t::canvasSurround));
        painter.fillRect(dialog_->rect(), surface);
        painter.setPen(QColor(t::surfaceBorder));
        painter.drawRect(dialog_->rect().adjusted(0, 0, -1, -1));
    }
    QDialog* dialog_;
    bool backdrop_{};
};
}
void makeGlassDialog(QDialog* dialog) {
    dialog->setProperty("glassDialog", true);
    dialog->setAttribute(Qt::WA_TranslucentBackground);
    dialog->ensurePolished();
    new GlassBackdrop(dialog);
}
QString glassStyleSheet() {
    return QString(R"(
        QDialog[glassDialog="true"] QWidget#dialogContent { background: transparent; }
        QLabel[glassHeading="true"] { color: %1; font-size: 28px; font-weight: 600; }
        QLabel[glassSubtitle="true"] { color: %2; font-size: 13px; }
        QFrame[glassRule="true"] { background: %3; border: 0; }
        QFrame[glassCard="true"], QTextBrowser[glassCard="true"], QTreeView[glassCard="true"] { background: rgba(255,255,255,12); border: 1px solid rgba(255,255,255,34); border-radius: 16px; color: %1; }
        QTextBrowser[glassCard="true"] { padding: 14px; }
        QTreeView[glassCard="true"] { padding: 8px; }
        QTreeView[glassCard="true"]::item { padding: 6px 8px; }
        QTreeView[glassCard="true"]::item:hover { background: rgba(255,255,255,18); }
        QTreeView[glassCard="true"]::item:selected { background: %4; color: white; }
        QTreeView[glassCard="true"]::item:first { border-top-left-radius: 12px; border-bottom-left-radius: 12px; }
        QTreeView[glassCard="true"]::item:last { border-top-right-radius: 12px; border-bottom-right-radius: 12px; }
        QTreeView[glassCard="true"] QHeaderView { background: transparent; }
        QTreeView[glassCard="true"] QHeaderView::section { background: transparent; color: %2; border: 0; border-bottom: 1px solid rgba(255,255,255,34); padding: 5px 8px; }
        QLineEdit[glassSearch="true"] { background: rgba(255,255,255,18); color: %1; border: 1px solid rgba(255,255,255,40); border-radius: 16px; padding: 0 12px; min-height: 32px; max-height: 32px; selection-background-color: %4; }
        QLineEdit[glassSearch="true"]:focus { background: rgba(255,255,255,18); border-color: %4; }
        QDialog[glassDialog="true"] QKeySequenceEdit QLineEdit { background: %5; color: %1; border: 1px solid %3; border-radius: %6px; }
        QDialog[glassDialog="true"] QKeySequenceEdit QLineEdit:focus { border-color: %4; }
        )")
        .arg(t::primary, t::secondary, t::separator, t::accent, t::field).arg(t::fieldRadius);
}
}
