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

// The built-in schemes, then the user's.
const QList<Scheme> &schemes();
QList<Scheme> userSchemes();
bool isUserScheme(const QString &name);
// Adds or replaces by name. False if the name is a built-in's.
bool saveUserScheme(const Scheme &s);
void removeUserScheme(const QString &name);
// conf's colours as a scheme.
Scheme schemeFromConf(const sshlx_conf *c, const QString &name);
// A scheme in a file of its own, to share.
bool exportScheme(const Scheme &s, const QString &path, QString *error);
bool importScheme(const QString &path, Scheme *out, QString *error);
void applyScheme(sshlx_conf *c, const Scheme &s);
// Index in schemes() of the scheme conf's colours match, or -1.
int currentScheme(const sshlx_conf *c);
// A small preview: the background, a line of text and the six colours.
QIcon swatch(const Scheme &s);

// Conf key ids the Appearance menu changes (saved together).
QStringList appearanceKeys();

} // namespace TerminalLook
