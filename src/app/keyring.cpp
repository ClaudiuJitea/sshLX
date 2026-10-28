#include "keyring.h"

#include <QSettings>

#ifdef SSHLX_HAVE_KEYCHAIN
#if __has_include(<qt6keychain/keychain.h>)
#include <qt6keychain/keychain.h>
#else
#include <qtkeychain/keychain.h>
#endif
#endif

namespace Keyring {

static const char *kService = "sshLX";

bool available()
{
#ifdef SSHLX_HAVE_KEYCHAIN
    return true;
#else
    return false;
#endif
}

bool enabled()
{
    return available() && QSettings().value("keyring/enabled", false).toBool();
}

void setEnabled(bool on)
{
    QSettings().setValue("keyring/enabled", on && available());
}

QString accountFor(const QString &connectionId)
{
    return QStringLiteral("connection:") + connectionId;
}

void read(const QString &account, std::function<void(bool, const QString &)> done)
{
#ifdef SSHLX_HAVE_KEYCHAIN
    auto *job = new QKeychain::ReadPasswordJob(QLatin1String(kService));
    job->setAutoDelete(true);
    job->setInsecureFallback(false);   // never a plaintext file
    job->setKey(account);
    QObject::connect(job, &QKeychain::Job::finished, [done](QKeychain::Job *j) {
        auto *rj = static_cast<QKeychain::ReadPasswordJob *>(j);
        if (rj->error() == QKeychain::NoError)
            done(true, rj->textData());
        else
            done(false, QString());
    });
    job->start();
#else
    Q_UNUSED(account);
    done(false, QString());
#endif
}

void write(const QString &account, const QString &password,
           std::function<void(bool, const QString &)> done)
{
#ifdef SSHLX_HAVE_KEYCHAIN
    auto *job = new QKeychain::WritePasswordJob(QLatin1String(kService));
    job->setAutoDelete(true);
    job->setInsecureFallback(false);
    job->setKey(account);
    job->setTextData(password);
    QObject::connect(job, &QKeychain::Job::finished, [done](QKeychain::Job *j) {
        if (done)
            done(j->error() == QKeychain::NoError, j->errorString());
    });
    job->start();
#else
    Q_UNUSED(account);
    Q_UNUSED(password);
    if (done)
        done(false, QStringLiteral("keyring support not built"));
#endif
}

void remove(const QString &account, std::function<void(bool)> done)
{
#ifdef SSHLX_HAVE_KEYCHAIN
    auto *job = new QKeychain::DeletePasswordJob(QLatin1String(kService));
    job->setAutoDelete(true);
    job->setInsecureFallback(false);
    job->setKey(account);
    QObject::connect(job, &QKeychain::Job::finished, [done](QKeychain::Job *j) {
        if (done)
            done(j->error() == QKeychain::NoError);
    });
    job->start();
#else
    Q_UNUSED(account);
    if (done)
        done(false);
#endif
}

} // namespace Keyring
