#include "icons.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QPalette>
#include <QStandardPaths>

namespace Icons {

static QHash<QString, QIcon> &cache()
{
    static QHash<QString, QIcon> c;
    return c;
}

void clearCache()
{
    cache().clear();
}

QIcon app()
{
    static QIcon icon = [] {
        QIcon i;
        for (int s : {16, 22, 24, 32, 48, 64, 128, 256, 512})
            i.addFile(QStringLiteral(":/app/sshlx-%1.png").arg(s), QSize(s, s));
        return i;
    }();
    return icon;
}

QString path(const QString &name)
{
    const QString res = QStringLiteral(":/icons/%1.svg").arg(name);
    QFile f(res);
    if (!f.open(QIODevice::ReadOnly))
        return res;
    QByteArray svg = f.readAll();
    if (!svg.contains("currentColor"))
        return res;
    // Style sheets and the SVG icon engine take files: write the copy
    // recoloured for the current palette once.
    const QString colour = QApplication::palette().color(QPalette::WindowText).name();
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
                        "/icons-" + colour.mid(1);
    // The content hash keeps updated icons from being shadowed by an old
    // cached copy.
    const QString out = dir + '/' + name + '-' +
        QString::number(qHash(svg), 16) + ".svg";
    if (!QFile::exists(out)) {
        QDir().mkpath(dir);
        svg.replace("currentColor", colour.toLatin1());
        QFile o(out);
        if (!o.open(QIODevice::WriteOnly))
            return res;
        o.write(svg);
    }
    return out;
}

const QList<NodeIcon> &nodeIcons()
{
    // The first four keys predate the larger set; keep them stable.
    static const QList<NodeIcon> list{
        {"", QApplication::translate("Icons", "(protocol)")},
        {"linux", "Linux"},
        {"windows", "Windows"},
        {"network", QApplication::translate("Icons", "Network device")},
        {"folder", QApplication::translate("Icons", "Folder")},
        {"server", QApplication::translate("Icons", "Server")},
        {"database", QApplication::translate("Icons", "Database")},
        {"storage", QApplication::translate("Icons", "Storage / NAS")},
        {"cloud", QApplication::translate("Icons", "Cloud")},
        {"web", QApplication::translate("Icons", "Web")},
        {"container", QApplication::translate("Icons", "Container")},
        {"cluster", QApplication::translate("Icons", "Cluster")},
        {"router", QApplication::translate("Icons", "Router / Wi-Fi")},
        {"firewall", QApplication::translate("Icons", "Firewall")},
        {"shield", QApplication::translate("Icons", "Security")},
        {"key", QApplication::translate("Icons", "Jump host / bastion")},
        {"terminal", QApplication::translate("Icons", "Terminal")},
        {"desktop", QApplication::translate("Icons", "Workstation")},
        {"laptop", QApplication::translate("Icons", "Laptop")},
        {"embedded", QApplication::translate("Icons", "Embedded / IoT")},
        {"lab", QApplication::translate("Icons", "Lab")},
        {"home", QApplication::translate("Icons", "Home")},
        {"office", QApplication::translate("Icons", "Office / site")},
    };
    return list;
}

QIcon forNode(const QString &key)
{
    for (const NodeIcon &n : nodeIcons())
        if (!key.isEmpty() && n.key == key)
            return get("grp-" + key);
    return {};
}

QIcon get(const QString &name)
{
    auto it = cache().find(name);
    if (it != cache().end())
        return *it;
    QIcon icon(path(name));
    cache().insert(name, icon);
    return icon;
}

} // namespace Icons
