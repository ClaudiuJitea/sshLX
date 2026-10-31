#include "connecttarget.h"

#include <QRegularExpression>
#include <QUrl>

bool ConnectTarget::parse(const QString &text, ConnectTarget *out)
{
    ConnectTarget t;
    QString s = text.trimmed();
    static const QRegularExpression scheme("^([a-zA-Z][a-zA-Z0-9+.-]*)://");
    const QRegularExpressionMatch m = scheme.match(s);
    if (m.hasMatch()) {
        t.protocol = m.captured(1).toLower();
        if (t.protocol != "ssh" && t.protocol != "telnet")
            return false;
        t.isUrl = true;
        s = QUrl::fromPercentEncoding(s.mid(m.capturedLength()).toUtf8());
        s = s.section('/', 0, 0);
    }
    const int at = int(s.lastIndexOf('@'));
    if (at >= 0) {
        // A password in a link is dropped: it would be in shell history and
        // browser logs, and PuTTY asks for it anyway.
        t.user = s.left(at).section(':', 0, 0);
        s = s.mid(at + 1);
    }
    if (s.startsWith('[')) {
        const int end = int(s.indexOf(']'));
        if (end < 0)
            return false;
        const QString rest = s.mid(end + 1);
        t.host = s.mid(1, end - 1);
        if (rest.startsWith(':'))
            t.port = rest.mid(1).toInt();
        else if (!rest.isEmpty())
            return false;
    } else if (s.count(':') == 1) {
        bool ok = false;
        t.port = s.section(':', 1).toInt(&ok);
        if (!ok)
            return false;
        t.host = s.section(':', 0, 0);
    } else {
        t.host = s;
    }
    static const QRegularExpression hostOk("^[A-Za-z0-9_][A-Za-z0-9._:-]*$");
    static const QRegularExpression userOk("^[A-Za-z0-9_][A-Za-z0-9._-]*$");
    if (!hostOk.match(t.host).hasMatch() || t.port < 0 || t.port > 65535
        || (!t.user.isEmpty() && !userOk.match(t.user).hasMatch()))
        return false;
    if (out)
        *out = t;
    return true;
}
