// Icons - sshLX's bundled icon set. Line icons use "currentColor", which is
// rendered in the current palette's text colour so they read well on dark
// and light themes alike.
#pragma once

#include <QIcon>
#include <QString>

namespace Icons {
QIcon get(const QString &name);
// File path of the (recoloured) SVG, for style sheets' url(...).
QString path(const QString &name);
void clearCache();   // after a palette change
// The application icon, with every bundled size.
QIcon app();

// Icons a connection or folder can show in the tree (property "Icon").
// key is what connections.json stores; "" means the protocol's icon.
struct NodeIcon {
    QString key;
    QString label;
};
const QList<NodeIcon> &nodeIcons();
QIcon forNode(const QString &key);   // null for "" or an unknown key
} // namespace Icons
