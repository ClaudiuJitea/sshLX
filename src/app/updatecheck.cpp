#include "updatecheck.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QVersionNumber>

static const char kLatestApi[] = "https://api.github.com/repos/ClaudiuJitea/sshLX/releases/latest";

UpdateCheck::UpdateCheck(QObject *parent)
    : QObject(parent), m_net(new QNetworkAccessManager(this))
{
}

QString UpdateCheck::releasesPage()
{
    return QStringLiteral("https://github.com/ClaudiuJitea/sshLX/releases");
}

static QString stripTag(QString tag)
{
    tag = tag.trimmed();
    if (tag.startsWith('v') || tag.startsWith('V'))
        tag.remove(0, 1);
    return tag;
}

bool UpdateCheck::isNewer(const QString &latest, const QString &current)
{
    const QVersionNumber l = QVersionNumber::fromString(stripTag(latest));
    const QVersionNumber c = QVersionNumber::fromString(stripTag(current));
    if (l.isNull())
        return false;
    return QVersionNumber::compare(l.normalized(), c.normalized()) > 0;
}

bool UpdateCheck::parseRelease(const QByteArray &json, Release *out)
{
    const QJsonObject o = QJsonDocument::fromJson(json).object();
    const QString tag = o.value("tag_name").toString();
    if (tag.isEmpty() || QVersionNumber::fromString(stripTag(tag)).isNull())
        return false;
    out->version = stripTag(tag);
    out->name = o.value("name").toString();
    out->notes = o.value("body").toString();
    out->pageUrl = o.value("html_url").toString();
    if (out->pageUrl.isEmpty())
        out->pageUrl = releasesPage();
    return true;
}

void UpdateCheck::start()
{
    QNetworkRequest req{QUrl(QString::fromLatin1(kLatestApi))};
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("sshLX/%1").arg(QCoreApplication::applicationVersion()));
    req.setTransferTimeout(15000);
    QNetworkReply *reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit failed(reply->errorString());
            return;
        }
        Release r;
        if (!parseRelease(reply->readAll(), &r)) {
            emit failed(tr("GitHub sent a reply sshLX could not read."));
            return;
        }
        emit finished(r, isNewer(r.version, QCoreApplication::applicationVersion()));
    });
}
