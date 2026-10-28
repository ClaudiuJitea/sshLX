#include "keymap.h"

#include <QGuiApplication>
#include <QHash>

namespace KeyMap {

uint32_t modifiers(Qt::KeyboardModifiers m)
{
    uint32_t r = 0;
    if (m & Qt::ShiftModifier) r |= SSHLX_MOD_SHIFT;
    if (m & Qt::ControlModifier) r |= SSHLX_MOD_CTRL;
    if (m & Qt::AltModifier) r |= SSHLX_MOD_ALT;
    if (m & Qt::MetaModifier) r |= SSHLX_MOD_SUPER;
    if (m & Qt::KeypadModifier) r |= SSHLX_MOD_KEYPAD;
    return r;
}

// Fallback mapping from Qt::Key to X11 keysyms, for platforms where Qt
// doesn't give us the keysym (anything other than xcb and Wayland).
static uint32_t keysymFromQt(int key, bool keypad, bool ctrl, const QString &text)
{
    if (keypad) {
        switch (key) {
        case Qt::Key_0: return 0xffb0; case Qt::Key_1: return 0xffb1;
        case Qt::Key_2: return 0xffb2; case Qt::Key_3: return 0xffb3;
        case Qt::Key_4: return 0xffb4; case Qt::Key_5: return 0xffb5;
        case Qt::Key_6: return 0xffb6; case Qt::Key_7: return 0xffb7;
        case Qt::Key_8: return 0xffb8; case Qt::Key_9: return 0xffb9;
        case Qt::Key_Home: return 0xff95; case Qt::Key_Left: return 0xff96;
        case Qt::Key_Up: return 0xff97; case Qt::Key_Right: return 0xff98;
        case Qt::Key_Down: return 0xff99; case Qt::Key_PageUp: return 0xff9a;
        case Qt::Key_PageDown: return 0xff9b; case Qt::Key_End: return 0xff9c;
        case Qt::Key_Clear: return 0xff9d; case Qt::Key_Insert: return 0xff9e;
        case Qt::Key_Delete: return 0xff9f; case Qt::Key_Asterisk: return 0xffaa;
        case Qt::Key_Plus: return 0xffab; case Qt::Key_Minus: return 0xffad;
        case Qt::Key_Period: case Qt::Key_Comma: return 0xffae;
        case Qt::Key_Slash: return 0xffaf; case Qt::Key_Enter: return 0xff8d;
        default: break;
        }
    }
    switch (key) {
    case Qt::Key_Backspace: return 0xff08;
    case Qt::Key_Tab: return 0xff09;
    case Qt::Key_Backtab: return 0xfe20;
    case Qt::Key_Return: return 0xff0d;
    case Qt::Key_Enter: return 0xff8d;
    case Qt::Key_Escape: return 0xff1b;
    case Qt::Key_Insert: return 0xff63;
    case Qt::Key_Delete: return 0xffff;
    case Qt::Key_Pause: return ctrl ? 0xff6b : 0xff13;   /* Ctrl+Pause = Break */
    case Qt::Key_Print: return 0xff61;
    case Qt::Key_Home: return 0xff50;
    case Qt::Key_End: return 0xff57;
    case Qt::Key_Left: return 0xff51;
    case Qt::Key_Up: return 0xff52;
    case Qt::Key_Right: return 0xff53;
    case Qt::Key_Down: return 0xff54;
    case Qt::Key_PageUp: return 0xff55;
    case Qt::Key_PageDown: return 0xff56;
    case Qt::Key_Clear: return 0xff0b;
    case Qt::Key_NumLock: return 0xff7f;
    case Qt::Key_Shift: return 0xffe1;
    case Qt::Key_Control: return 0xffe3;
    case Qt::Key_Alt: return 0xffe9;
    case Qt::Key_AltGr: return 0xfe03;
    case Qt::Key_Meta: return 0xffeb;
    case Qt::Key_Space: return 0x20;
    default: break;
    }
    if (key >= Qt::Key_F1 && key <= Qt::Key_F35)
        return 0xffbe + (key - Qt::Key_F1);
    // Printable keys: keysyms are Latin-1 code points, or 0x01000000|UCS.
    if (!text.isEmpty()) {
        uint u = text.at(0).unicode();
        if (u >= 0x20 && u != 0x7f) {
            if (u < 0x100)
                return u;
            return 0x01000000 | u;
        }
    }
    if (key >= 0x20 && key <= 0xff) {
        // Qt reports letters upper-case; X keysyms follow the shift state.
        return static_cast<uint32_t>(key);
    }
    return 0;
}

bool translate(const QKeyEvent *e, bool release, sshlx_key_event *ev,
               QByteArray *textStorage)
{
    const Qt::KeyboardModifiers m = e->modifiers();
    uint32_t keysym = 0;
    static const bool nativeIsKeysym = [] {
        const QString p = QGuiApplication::platformName();
        return p == "xcb" || p.startsWith("wayland");
    }();
    if (nativeIsKeysym)
        keysym = e->nativeVirtualKey();
    if (!keysym)
        keysym = keysymFromQt(e->key(), m & Qt::KeypadModifier,
                              m & Qt::ControlModifier, e->text());
    // Qt reports Ctrl+letter keysyms as letters already; when falling back
    // to Qt::Key, letters come upper-case: follow Shift like X does.
    if (!nativeIsKeysym && keysym >= 'A' && keysym <= 'Z' &&
        !(m & Qt::ShiftModifier))
        keysym += 'a' - 'A';
    if (!keysym)
        return false;
    *textStorage = e->text().toUtf8();
    ev->keysym = keysym;
    ev->mods = modifiers(m);
    ev->text = textStorage->constData();
    ev->release = release;
    return true;
}

} // namespace KeyMap
