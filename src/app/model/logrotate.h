// Session log rotation: the sums and file handling. PuTTY keeps the log open;
// SessionWidget closes it around a rotation.
#pragma once

#include <QDateTime>
#include <QString>

namespace LogRotate {

struct Policy {
    qint64 maxBytes = 0;     // 0: no limit
    int maxHours = 0;        // 0: no limit
    int keep = 5;            // rotated files kept; 0: all
    bool active() const { return maxBytes > 0 || maxHours > 0; }
};

// A log file name with PuTTY's &Y &M &D &T &H &P and && filled in.
QString expandName(const QString &pattern, const QString &host, int port, const QDateTime &when);
bool due(const Policy &p, qint64 size, const QDateTime &opened, const QDateTime &now);
// Renames path to path.yyyyMMdd-HHmmss, which never replaces a file, and
// removes the oldest rotated files beyond p.keep. Returns the new name.
QString rotate(const QString &path, const Policy &p, const QDateTime &now);

}
