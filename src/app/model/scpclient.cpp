#include "scpclient.h"

#include <QFile>
#include <QRegularExpression>
#include <QSaveFile>

constexpr int kChunk = 32768;
constexpr int kMaxLine = 4096;

ScpClient::ScpClient(QObject *parent) : QObject(parent) {}
ScpClient::~ScpClient() = default;

QString ScpClient::commandFor(bool upload, const QString &remotePath)
{
    static const QRegularExpression plain(QStringLiteral(R"(^[\w./:~+@%=-]+$)"));
    QString path = remotePath;
    if (!plain.match(path).hasMatch())
        path = '\'' + path.replace('\'', QStringLiteral("'\\''")) + '\'';
    // Only when it must, as devices may not know --: a path starting with -
    // cannot be read as an option.
    const QString flag = upload ? QStringLiteral("scp -t ") : QStringLiteral("scp -f ");
    return flag + (remotePath.startsWith('-') ? QStringLiteral("-- ") : QString()) + path;
}

void ScpClient::startUpload(const QString &local, const QString &remoteName)
{
    m_file = std::make_unique<QFile>(local);
    if (!m_file->open(QIODevice::ReadOnly)) {
        finish(false, m_file->errorString());
        return;
    }
    m_name = remoteName;
    m_total = m_file->size();
    m_done = 0;
    m_in.clear();
    m_step = Step::WaitReady;
}

void ScpClient::startDownload(const QString &local)
{
    m_out = std::make_unique<QSaveFile>(local);
    if (!m_out->open(QIODevice::WriteOnly)) {
        finish(false, m_out->errorString());
        return;
    }
    m_total = m_done = 0;
    m_in.clear();
    m_step = Step::WaitHeader;
    emit send(QByteArray(1, '\0'));
}

void ScpClient::feed(const char *data, size_t len)
{
    if (m_step == Step::Idle)
        return;
    m_in.append(data, qsizetype(len));
    step();
}

// True once a whole acknowledgement has arrived and been removed. A refusal
// leaves its text in m_ackError.
bool ScpClient::takeAck()
{
    if (m_in.isEmpty())
        return false;
    m_ackError.clear();
    if (m_in[0] != 0) {
        const int nl = int(m_in.indexOf('\n'));
        if (nl < 0 && m_in.size() < kMaxLine)
            return false;
        m_ackError = QString::fromUtf8(m_in.mid(1, nl < 0 ? -1 : nl - 1)).trimmed();
        if (m_ackError.isEmpty())
            m_ackError = tr("The device refused the transfer.");
        m_in.clear();
        return true;
    }
    m_in.remove(0, 1);
    return true;
}

bool ScpClient::takeLine(QByteArray *line)
{
    const int nl = int(m_in.indexOf('\n'));
    if (nl < 0)
        return false;
    *line = m_in.left(nl);
    m_in.remove(0, nl + 1);
    return true;
}

void ScpClient::step()
{
    for (;;) {
        switch (m_step) {
        case Step::WaitReady: {
            if (!takeAck())
                return;
            if (!m_ackError.isEmpty())
                return finish(false, m_ackError);
            QByteArray header = "C0644 " + QByteArray::number(m_total) + ' ' + m_name.toUtf8() + '\n';
            m_step = Step::WaitHeaderAck;
            emit send(header);
            break;
        }
        case Step::WaitHeaderAck:
            if (!takeAck())
                return;
            if (!m_ackError.isEmpty())
                return finish(false, m_ackError);
            m_step = Step::Sending;
            pump();
            return;
        case Step::WaitDataAck:
            if (!takeAck())
                return;
            return finish(m_ackError.isEmpty(), m_ackError);
        case Step::WaitHeader: {
            QByteArray line;
            if (m_in.size() > kMaxLine && m_in.indexOf('\n') < 0)
                return finish(false, tr("The device's reply makes no sense."));
            if (!m_in.isEmpty() && (m_in[0] == 1 || m_in[0] == 2)) {
                takeAck();
                if (m_ackError.isEmpty())
                    return;
                return finish(false, m_ackError);
            }
            if (!takeLine(&line))
                return;
            if (line.startsWith('T')) {        // times come first with -p
                emit send(QByteArray(1, '\0'));
                break;
            }
            const QList<QByteArray> f = line.split(' ');
            bool ok = false;
            m_total = f.size() >= 3 && line.startsWith('C') ? f[1].toLongLong(&ok) : -1;
            if (line.startsWith('D'))
                return finish(false, tr("That is a folder, and SCP mode transfers single files."));
            if (!ok || m_total < 0)
                return finish(false, tr("The device's reply makes no sense."));
            m_step = Step::Receiving;
            emit send(QByteArray(1, '\0'));
            break;
        }
        case Step::Receiving: {
            const qint64 n = qMin(m_total - m_done, qint64(m_in.size()));
            if (n > 0) {
                if (m_out->write(m_in.constData(), n) != n)
                    return finish(false, m_out->errorString());
                m_in.remove(0, n);
                m_done += n;
                emit progress(m_done, m_total);
            }
            if (m_done < m_total)
                return;
            m_step = Step::WaitTrailer;
            break;
        }
        case Step::WaitTrailer:
            if (!takeAck())
                return;
            if (m_ackError.isEmpty())
                emit send(QByteArray(1, '\0'));
            return finish(m_ackError.isEmpty(), m_ackError);
        default:
            return;
        }
    }
}

void ScpClient::pump()
{
    if (m_step != Step::Sending)
        return;
    const QByteArray data = m_file->read(kChunk);
    if (!data.isEmpty()) {
        m_done += data.size();
        emit send(data);
        emit progress(m_done, m_total);
    }
    if (m_done >= m_total) {
        m_step = Step::WaitDataAck;
        emit send(QByteArray(1, '\0'));
    } else if (data.isEmpty()) {
        finish(false, m_file->errorString());
    }
}

void ScpClient::cancel() { finish(false, QString()); }

void ScpClient::connectionLost(const QString &why)
{
    if (m_step != Step::Idle)
        finish(false, why);
}

void ScpClient::finish(bool ok, const QString &error)
{
    m_step = Step::Idle;
    QString err = error;
    if (m_out) {
        if (ok && !m_out->commit()) {
            ok = false;
            err = m_out->errorString();
        } else if (!ok) {
            m_out->cancelWriting();
        }
        m_out.reset();
    }
    m_file.reset();
    emit finished(ok, err);
}
