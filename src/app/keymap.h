// Translation of Qt key events into the engine's keysym-based key events.
#pragma once

#include <QKeyEvent>
#include <QByteArray>

#include "sshlx_engine.h"

namespace KeyMap {

// Fills ev; textStorage keeps ev->text alive. Returns false for events
// that carry no usable key (e.g. a bare unknown key).
bool translate(const QKeyEvent *e, bool release, sshlx_key_event *ev,
               QByteArray *textStorage);

uint32_t modifiers(Qt::KeyboardModifiers m);

} // namespace KeyMap
