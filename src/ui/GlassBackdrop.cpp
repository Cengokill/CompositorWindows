#include "GlassBackdrop.h"
#include <QGuiApplication>
#include <QWidget>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>

namespace compositor::ui {
void applyGlassBackdrop(QWidget* window) {
    if (!window || QGuiApplication::platformName() != QLatin1String("windows")) return;
    const auto handle = reinterpret_cast<HWND>(window->winId());
    if (!handle) return;
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(handle, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    constexpr DWORD systemBackdropType = 38;
    constexpr int transientWindow = 3;
    DwmSetWindowAttribute(handle, systemBackdropType, &transientWindow, sizeof(transientWindow));
    const MARGINS wholeWindow{-1, -1, -1, -1};
    DwmExtendFrameIntoClientArea(handle, &wholeWindow);
}
}
