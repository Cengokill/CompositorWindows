#pragma once
#include <QDialog>
#include <QString>
#include <functional>

class QLabel;
class QPushButton;

namespace compositor::ui {
class ExportProgressStrip;
// The window shown while an export writes files. Opaque, with Liquid Glass tiles: one tile per file when there are
// two or more (up to 84; a longer export shows a bar that fills instead), each filling with accent-blue liquid and a check mark as its file is written; otherwise a bar with a
// sliding capsule, since one file has no honest percentage. Cancel asks for a stop (the cancel handler is called once, the
// button reads "Cancelling…") and the window stays open until the caller accepts it when the work has really ended.
class ExportProgressDialog final : public QDialog {
public:
    // `heading` is the line over the bar when there is a single file; with tiles it reads "Exporting layer n of N".
    ExportProgressDialog(QWidget* parent, const QString& title, const QString& heading, int count);
    // `completed` files are written; `current` names the one being written now.
    void setProgress(int completed, const QString& current);
    // File `index` was handled without writing a PNG (nothing to export, or a failure): its tile does not show a success.
    void markSkipped(int index);
    // The work has ended. `completed` files were handled, or -1 when it was cancelled. With tiles, the last ones are
    // given a moment to finish filling before the window closes; otherwise it closes at once.
    void finish(int completed);
    // Called once, on the interface thread, when Cancel is pressed or the window is closed.
    void setCancelHandler(std::function<void()> handler) { onCancel_ = std::move(handler); }
protected:
    void paintEvent(QPaintEvent*) override;
    void reject() override;
private:
    void requestCancel();
    const int count_;
    QLabel* headingLabel_{};
    QLabel* currentLabel_{};
    QLabel* countLabel_{};
    QPushButton* cancel_{};
    ExportProgressStrip* strip_{};
    std::function<void()> onCancel_;
    bool cancelRequested_{};
};
}
