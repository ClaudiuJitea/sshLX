// ConnectTarget - what the connect bar and ssh:// or telnet:// links name:
// [ssh://|telnet://][user[:ignored]@]host[:port][/...]. Qt Core only.
#pragma once

#include <QString>

struct ConnectTarget {
    QString protocol = QStringLiteral("ssh");    // "ssh" or "telnet"
    QString user, host;
    int port = 0;                                // 0: the protocol's own
    bool isUrl = false;

    // False for anything but a plain host name or address. Such text would
    // otherwise end up as an option or a shell word somewhere along the way.
    static bool parse(const QString &text, ConnectTarget *out);
};
