#include "logrotate.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

namespace LogRotate {

QString expandName(const QString &pattern, const QString &host, int port, const QDateTime &when)
{
    QString out;
    for (int i = 0; i < pattern.size(); i++) {
        if (pattern[i] != '&' || i + 1 == pattern.size()) {
            out += pattern[i];
            continue;
        }
        const QChar c = pattern[++i];
        if (c == 'Y') out += when.toString("yyyy");
        else if (c == 'M') out += when.toString("MM");
        else if (c == 'D') out += when.toString("dd");
        else if (c == 'T') out += when.toString("HHmmss");
        else if (c == 'H') out += host;
        else if (c == 'P') out += QString::number(port);
        else if (c == '&') out += '&';
        else {
            out += '&';
            out += c;
        }
    }
    return out;
}

bool due(const Policy &p, qint64 size, const QDateTime &opened, const QDateTime &now)
{
    if (p.maxBytes > 0 && size >= p.maxBytes)
        return true;
    return p.maxHours > 0 && opened.isValid() && size > 0
        && opened.secsTo(now) >= qint64(p.maxHours) * 3600;
}

QString rotate(const QString &path, const Policy &p, const QDateTime &now)
{
    const QString stamp = now.toString("yyyyMMdd-HHmmss");
    QString to = path + '.' + stamp;
    for (int i = 1; QFileInfo::exists(to); i++)
        to = path + '.' + stamp + '-' + QString::number(i);
    if (!QFile::rename(path, to))
        return {};
    if (p.keep > 0) {
        const QFileInfo fi(path);
        QDir dir = fi.dir();
        // Oldest first by name: the stamp sorts by time.
        static const QRegularExpression stamped("\\.\\d{8}-\\d{6}(-\\d+)?$");
        QStringList old;
        for (const QString &n : dir.entryList(QDir::Files, QDir::Name))
            if (n.startsWith(fi.fileName() + '.') && n.size() > fi.fileName().size()
                && stamped.match(n, fi.fileName().size()).capturedStart() == fi.fileName().size())
                old.append(n);
        for (int i = 0; i < old.size() - p.keep; i++)
            dir.remove(old[i]);
    }
    return to;
}

}
