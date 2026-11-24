#pragma once
#include <QKeyEvent>

namespace compositor::ui {
// The digit of a plain press of a top-row or keypad digit key, or -1. The top row is also matched by scan code because
// an AZERTY keyboard types symbols there (& é " ' ( - è _ ç à) unless Shift is held, and the keypad adds a modifier.
inline int plainDigit(const QKeyEvent* event) {
    if ((event->modifiers() & ~Qt::KeypadModifier) != Qt::NoModifier) return -1;
    if (event->key() >= Qt::Key_0 && event->key() <= Qt::Key_9) return event->key() - Qt::Key_0;
    if (event->modifiers() & Qt::KeypadModifier) return -1;
    const auto scan = event->nativeScanCode();
    return scan >= 0x02 && scan <= 0x0B ? int((scan - 1) % 10) : -1;
}
}
