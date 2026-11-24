#pragma once
#include <QString>
class QDialog;

namespace compositor::ui {
// Gives a dialog the Liquid Glass surface of What's New: the Windows acrylic backdrop, or an opaque dark surface where
// that backdrop is not available. Call it once the dialog's layout is in place, before it is shown.
// Content opts in through properties: glassHeading, glassSubtitle and glassRule for the title area, glassCard for a
// translucent rounded card, glassSearch for a capsule search field.
void makeGlassDialog(QDialog* dialog);
// The style sheet for the above, part of the application style sheet.
QString glassStyleSheet();
}
