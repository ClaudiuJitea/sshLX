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
