#include "asciisender.h"

#include <QFile>

AsciiSender::AsciiSender(QObject *parent) : QObject(parent)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &AsciiSender::step);
}

bool AsciiSender::start(const QString &path, const Options &options, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        *error = f.errorString();
        return false;
    }
    QByteArray all = f.readAll();
    all.replace("\r\n", "\n");
    m_lines = all.split('\n');
    if (!m_lines.isEmpty() && m_lines.last().isEmpty())
        m_lines.removeLast();
    m_opts = options;
    m_line = m_char = 0;
    m_timer.start(0);
    return true;
}

void AsciiSender::stop()
{
    if (!m_timer.isActive())
        return;
    m_timer.stop();
    emit finished(false);
}

void AsciiSender::step()
{
    if (m_line >= m_lines.size()) {
        emit finished(true);
        return;
    }
    const QByteArray &l = m_lines[m_line];
    if (m_opts.charDelayMs > 0 && m_char < l.size()) {
        emit send(l.mid(m_char++, 1));
        m_timer.start(m_opts.charDelayMs);
        return;
    }
    emit send(m_opts.charDelayMs > 0 ? QByteArray("\r") : l + '\r');
    m_line++;
    m_char = 0;
    emit progress(m_line, int(m_lines.size()));
    m_timer.start(m_opts.lineDelayMs);
}
