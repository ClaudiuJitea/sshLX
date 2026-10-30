// TerminalLook - colour schemes and the other per-session "look" settings
// (fonts, cursor) that the Appearance menu changes on a live tab.
//
// A scheme is written into PuTTY's 22 colour slots (Window > Colours):
// default fg/bold fg/bg/bold bg, cursor text/colour, then the 8 ANSI
// colours, each normal and bold (bright).
#pragma once

#include <QIcon>
#include <QList>
#include <QRgb>
#include <QString>

#include "sshlx_engine.h"

namespace TerminalLook {

struct Scheme {
    QString name;
    QRgb fg, bg, cursor;
    QRgb ansi[16];   // black, red, green, yellow, blue, magenta, cyan, white; then bright
};

const QList<Scheme> &schemes();
void applyScheme(sshlx_conf *c, const Scheme &s);
// Index in schemes() of the scheme conf's colours match, or -1.
int currentScheme(const sshlx_conf *c);
// A small preview: the background, a line of text and the six colours.
QIcon swatch(const Scheme &s);

// Conf key ids the Appearance menu changes (saved together).
QStringList appearanceKeys();

} // namespace TerminalLook
