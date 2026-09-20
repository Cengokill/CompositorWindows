#pragma once

namespace compositor::ui {
// Install before NativeCanvas can be queried by Qt or Windows accessibility.
// Safe to call from each canvas constructor on the GUI thread.
void installCanvasAccessibility();
}
