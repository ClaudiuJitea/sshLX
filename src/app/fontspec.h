// Conversion between PuTTY (GTK) font names and QFont.
//
// GTK PuTTY stores fonts as "client:<Pango font description>" (e.g.
// "client:Monospace 12", "client:DejaVu Sans Mono Bold 10") or
// "server:<X11 font>" ("server:fixed"). sshLX renders client fonts with
// Qt; X server fonts are not supported (allowed by the spec) and fall back
// to the system fixed font. Names are written back in the same syntax, so
// saved sessions remain valid for PuTTY.
#pragma once

#include <QFont>
#include <QString>

namespace FontSpec {

struct Parsed {
    QFont font;
    bool serverFont = false;   // was an X11 server font (not renderable)
    bool empty = false;        // name was empty ("use default")
};

Parsed fromPutty(const QString &puttyName);
QString toPutty(const QFont &font);
// Same font with the point size changed by delta (GTK's Ctrl+< / Ctrl+>).
QString resized(const QString &puttyName, int delta);

} // namespace FontSpec
