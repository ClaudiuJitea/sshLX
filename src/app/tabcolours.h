// TabColours - the colour menu shared by tabs, tab groups and connections,
// and the colours given to tab groups, which outlive any one tab.
#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

#include <functional>

class QMenu;
class QWidget;

namespace TabColours {

// Swatches, Custom and None. picked gets "#rrggbb", or "" for None.
void fillMenu(QMenu *menu, const QString &current, QWidget *dialogParent,
              const std::function<void(const QString &)> &picked);
QIcon swatch(const QString &colour);
// A small style sheet for a pill or chip in that colour.
QString pillStyle(const QString &colour);

QString groupColour(const QString &group);
void setGroupColour(const QString &group, const QString &colour);

}
