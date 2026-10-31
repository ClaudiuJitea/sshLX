#include "logonrunner.h"

#include <QRegularExpression>

void OutputText::append(const char *data, size_t len)
{
    const QString s = m_decoder.decode(QByteArrayView(data, qsizetype(len)));
    for (QChar c : s) {
        const ushort u = c.unicode();
        switch (m_escape) {
        case 1:
            m_escape = u == '[' ? 2 : u == ']' ? 3 : 0;
            continue;
        case 2:
            if (u >= 0x40 && u <= 0x7e)
                m_escape = 0;
            continue;
        case 3:
            if (u == 0x07)
                m_escape = 0;
            else if (u == 0x1b)
                m_escape = 4;
            continue;
        case 4:
            m_escape = u == '\\' ? 0 : 3;
            continue;
        default:
            break;
        }
        if (u == 0x1b) {
            m_escape = 1;
        } else if (u == '\b') {
            if (!m_text.isEmpty() && !m_text.endsWith('\n'))
                m_text.chop(1);
        } else if (u == '\n' || u == '\t' || u >= 0x20) {
            m_text.append(c);
        }
    }
}

int OutputText::trim(int n)
{
    const int drop = int(m_text.size()) - n;
    if (drop <= 0)
        return 0;
    m_text.remove(0, drop);
    return drop;
}

QString OutputText::lastLine() const
{
    return m_text.mid(m_text.lastIndexOf('\n') + 1);
}

LogonRunner::LogonRunner(QObject *parent) : QObject(parent)
{
    m_check.setSingleShot(true);
    m_check.setInterval(0);
    connect(&m_check, &QTimer::timeout, this, &LogonRunner::check);
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        if (!m_running)
            return;
        const LogonStep &s = m_steps[m_index];
        finish(false, tr("\"%1\" did not appear.").arg(s.wait));
    });
}

QString LogonRunner::patternError(const QString &pattern)
{
    QRegularExpression re(pattern);
    return re.isValid() ? QString() : re.errorString();
}

void LogonRunner::start(const QList<LogonStep> &steps)
{
    stop();
    if (steps.isEmpty())
        return;
    m_steps = steps;
    m_index = 0;
    m_output.clear();
    m_running = true;
    m_waiting = false;
    runStep();
}

void LogonRunner::stop()
{
    m_running = false;
    m_waiting = false;
    m_check.stop();
    m_timeout.stop();
}

void LogonRunner::finish(bool ok, const QString &message)
{
    const bool was = m_running;
    stop();
    if (was)
        emit finished(ok, message);
}

void LogonRunner::feed(const char *data, size_t len)
{
    if (!m_running)
        return;
    m_output.append(data, len);
    m_output.trim(64 * 1024);
    if (m_waiting && !m_check.isActive())
        m_check.start();
}

void LogonRunner::runStep()
{
    if (m_index >= m_steps.size()) {
        finish(true, QString());
        return;
    }
    m_waiting = true;
    emit progress(m_index + 1, int(m_steps.size()), m_steps[m_index].wait);
    m_timeout.start(m_timeoutMs);
    m_check.start();
}

void LogonRunner::check()
{
    if (!m_running || !m_waiting)
        return;
    const LogonStep s = m_steps[m_index];
    int end = 0;
    if (!s.wait.isEmpty()) {
        const QString text = m_output.text();
        if (s.regex) {
            const QRegularExpressionMatch m = QRegularExpression(s.wait).match(text);
            if (!m.hasMatch())
                return;
            end = int(m.capturedEnd());
        } else {
            const int at = int(text.indexOf(s.wait));
            if (at < 0)
                return;
            end = at + int(s.wait.size());
        }
    }
    m_timeout.stop();
    m_waiting = false;
    if (s.password) {
        // Keeps the prompt line until the owner has compared it; stepDone()
        // moves on.
        emit sendPassword(s.credential);
        return;
    }
    // What matched is used up: the next step waits for what follows it.
    m_output.trim(m_output.size() - end);
    emit send(s.send);
    m_index++;
    runStep();
}

void LogonRunner::stepDone()
{
    if (!m_running)
        return;
    m_output.clear();
    m_index++;
    runStep();
}
