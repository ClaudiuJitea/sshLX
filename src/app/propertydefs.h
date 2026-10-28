// Property definitions for the Config panel (property grid).
//
// A curated set of the most used PuTTY options, with labels, editors and
// help text, grouped like mRemoteNG's grid. Every other Conf key is listed
// automatically under "All Settings (advanced)" via the engine's Conf
// reflection, so no option is unreachable from the grid; PuTTY's complete
// dialog is one click away ("Full settings...").
#pragma once

#include <QList>
#include <QPair>
#include <QString>

struct PropertyDef {
    enum Kind { Text, Password, Int, Enum, Bool, Colour, Font, File, KeyedMap };
    QString category;
    QString name;
    // Conf key id (e.g. "host"), or a special id starting with '@':
    // @name @description @icon @panel @favourite @sshtunnel
    QString key;
    Kind kind = Text;
    QList<QPair<int, QString>> enumValues;   // Enum: conf value -> label
    QString help;
    int colourIndex = -1;    // Colour: index into PuTTY's colour list
    int min = 0, max = 0x7fffffff;

    bool isSpecial() const { return key.startsWith('@'); }
    // The Conf key used for inheritance ("colours" for colour entries).
    QString inheritKey() const { return kind == Colour ? QStringLiteral("colours") : key; }
};

namespace PropertyDefs {
// Curated definitions (only those whose Conf key exists in this build).
const QList<PropertyDef> &curated();
// Curated + automatic advanced entries for every other Conf key.
const QList<PropertyDef> &all();
QStringList categoryOrder();
} // namespace PropertyDefs
