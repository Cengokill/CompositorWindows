#include "AboutDialog.h"
#include "GlassDialog.h"
#include "LayerPanelTokens.h"
#include "ToolbarControls.h"
#include <QCoreApplication>
#include <QDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStringList>
#include <QVBoxLayout>

namespace compositor::ui {
namespace {
constexpr int aboutWidth = 520;
QString link(const QString& address, const QString& text) {
    return QString("<a style=\"color: %1; text-decoration: none;\" href=\"%2\">%3</a>").arg(layer_panel::accent, address, text);
}
// Paragraph spacing sits between paragraphs only, so nothing follows the last sentence.
QString aboutText() {
    const QStringList paragraphs{
        "An independent Windows port of Compositor by Robbie Tilton.",
        "Compose images with layers, masks, selections, painting, adjustments, filters, and background removal. Import and develop RAW images, combine photos with focus stacking, and save editable .comp projects.",
        "Maintained by " + link("https://x.com/Cengokill", "@Cengokill") + ", Compositor Windows receives regular updates that follow Compositor's update history, with approximately one release per week. Full Mac equivalence and project exchange are unverified.",
        link("https://github.com/robbietilton/Compositor", "Original Compositor project") + "<br>Copyright Wonder Assembly LLC. MIT source.",
        "This independent port is not endorsed by the upstream author.",
        "Qt and other dependency notices accompany this package."};
    QString text;
    for (const auto& paragraph : paragraphs)
        text += QString("<p style=\"margin: %1px 0 0 0;\">%2</p>").arg(text.isEmpty() ? 0 : 10).arg(paragraph);
    return text;
}
}
QDialog* openAboutDialog(QWidget* parent) {
    auto* dialog = new QDialog(parent);
    dialog->setObjectName("aboutDialog");
    dialog->setWindowTitle("About Compositor");
    dialog->setWindowModality(Qt::WindowModal);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    auto* layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(24, 22, 24, 20);
    layout->setSpacing(12);
    auto* heading = new QLabel("Compositor");
    heading->setProperty("glassHeading", true);
    auto* version = new QLabel("Compositor Windows " + QCoreApplication::applicationVersion());
    version->setObjectName("aboutVersion");
    version->setTextFormat(Qt::PlainText);
    version->setProperty("glassSubtitle", true);
    auto* rule = new QFrame;
    rule->setProperty("glassRule", true);
    rule->setFixedHeight(1);
    layout->addWidget(heading);
    layout->addWidget(version);
    layout->addWidget(rule);
    auto* card = new QFrame;
    card->setObjectName("aboutCard");
    card->setProperty("glassCard", true);
    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(16, 14, 16, 14);
    auto* text = new QLabel(aboutText());
    text->setObjectName("aboutText");
    text->setWordWrap(true);
    text->setTextFormat(Qt::RichText);
    text->setTextInteractionFlags(Qt::TextBrowserInteraction);
    text->setOpenExternalLinks(true);
    cardLayout->addWidget(text);
    layout->addWidget(card);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    auto* close = new QPushButton("OK");
    close->setObjectName("aboutClose");
    close->setDefault(true);
    close->setProperty("primary", true);
    markPill(close);
    buttons->addWidget(close);
    layout->addLayout(buttons);
    QObject::connect(close, &QPushButton::clicked, dialog, &QDialog::accept);
    makeGlassDialog(dialog);
    // As tall as the wrapped text needs at this width: no spare room under the last sentence.
    dialog->setFixedSize(aboutWidth, dialog->layout()->totalHeightForWidth(aboutWidth));
    dialog->open();
    close->setFocus(Qt::OtherFocusReason);
    return dialog;
}
}
