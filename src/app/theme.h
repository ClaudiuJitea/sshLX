// Theme - sshLX's own look: the Fusion style with curated dark and light
// palettes and one application style sheet (spacing, rounded menus and
// inputs, calm selection colours), plus the matching style sheet for the
// docking system. Independent of the desktop's widget theme, so it looks
// the same everywhere; "System" follows the desktop's dark/light setting.
#pragma once

#include <QColor>
#include <QString>

namespace Theme {

enum Mode { System = 0, Dark = 1, Light = 2 };

Mode mode();                 // from settings (ui/theme)
void setMode(Mode m);        // stores it; call apply() afterwards
bool isDark();               // the effective choice
// Apply palette + style sheet to the application.
void apply();
// Style sheet for the ads::CDockManager (replaces ADS's own).
QString dockStyleSheet();

// A few named colours for custom-painted widgets.
QColor accent();
QColor muted();

} // namespace Theme
