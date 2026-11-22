#pragma once

class QWidget;

namespace compositor::ui {
// Puts the dark Windows 11 transient (acrylic) backdrop under the whole client area of a top-level widget that has
// Qt::WA_TranslucentBackground, so its translucent surface shows a blurred view of what lies behind it. A window mask
// also clips the backdrop. Does nothing outside the native Windows platform.
void applyGlassBackdrop(QWidget* window);
}
