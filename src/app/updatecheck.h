// UpdateCheck - asks GitHub for sshLX's latest release and compares it with
// the running version. One request per check; nothing runs in the
// background and nothing is downloaded.
#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;

class UpdateCheck : public QObject {
    Q_OBJECT
public:
    struct Release {
        QString version;   // "0.5.0" (tag without the leading "v")
        QString name;      // release title
        QString notes;     // release body (Markdown)
        QString pageUrl;   // the release's web page
    };

    explicit UpdateCheck(QObject *parent = nullptr);

    // Starts a check; emits exactly one of finished() or failed().
    void start();

    // True if `latest` (a tag such as "v0.5" or "0.5.0") is newer than
    // `current`. Missing components count as 0, so "v0.4" == "0.4.0".
    static bool isNewer(const QString &latest, const QString &current);
    // Parses a GitHub "latest release" JSON reply; false if it is not one.
    static bool parseRelease(const QByteArray &json, Release *out);

    static QString releasesPage();

signals:
    void finished(const UpdateCheck::Release &latest, bool newer);
    void failed(const QString &error);

private:
    QNetworkAccessManager *m_net;
};
