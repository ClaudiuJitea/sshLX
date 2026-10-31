#include "scriptrecorder.h"

#include "configpush.h"

QString ScriptRecorder::pythonString(const QString &text)
{
    QString out = QStringLiteral("\"");
    for (QChar c : text) {
        const ushort u = c.unicode();
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (u == '\r') {
            out += "\\r";
        } else if (u == '\n') {
            out += "\\n";
        } else if (u == '\t') {
            out += "\\t";
        } else if (u < 0x20 || u == 0x7f) {
            out += QStringLiteral("\\x%1").arg(u, 2, 16, QChar('0'));
        } else {
            out += c;
        }
    }
    return out + '"';
}

void ScriptRecorder::output(const char *data, size_t len)
{
    m_output.append(data, len);
    m_output.trim(16 * 1024);
}

void ScriptRecorder::markLineStart()
{
    if (m_lineStarted)
        return;
    m_lineStarted = true;
    // Taken now: the echo of what is typed lands on the same line.
    m_prompt = m_output.lastLine().trimmed().right(40);
}

void ScriptRecorder::waitForPrompt(const QString &prompt)
{
    if (!prompt.isEmpty())
        m_body << QStringLiteral("tab.wait_for(%1)").arg(pythonString(prompt));
}

void ScriptRecorder::commit()
{
    markLineStart();
    waitForPrompt(m_prompt);
    if (ConfigPush::looksLikePasswordPrompt(m_prompt)) {
        m_body << QStringLiteral("tab.send_password()  # add a credential's name if needed");
    } else {
        m_body << (m_line.isEmpty() ? QStringLiteral("tab.send_line()")
                                    : QStringLiteral("tab.send_line(%1)").arg(pythonString(m_line)));
    }
    m_steps++;
    m_line.clear();
    m_prompt.clear();
    m_lineStarted = false;
    m_output.clear();
}

// Before a control key: what was typed so far goes out without an Enter.
void ScriptRecorder::flushPartial()
{
    if (m_line.isEmpty())
        return;
    if (ConfigPush::looksLikePasswordPrompt(m_prompt)) {
        m_line.clear();     // part of a password: never written down
        return;
    }
    m_body << QStringLiteral("tab.send(%1)").arg(pythonString(m_line));
    m_line.clear();
}

void ScriptRecorder::typed(const QString &text)
{
    for (QChar c : text) {
        const ushort u = c.unicode();
        if (u == '\r' || u == '\n') {
            commit();
        } else if (u == 0x7f || u == '\b') {
            m_line.chop(1);
        } else if (u < 0x20 && u != '\t') {
            markLineStart();
            flushPartial();
            waitForPrompt(m_prompt);
            m_prompt.clear();
            m_body << QStringLiteral("tab.send(%1)").arg(pythonString(QString(c)));
            m_steps++;
            m_lineStarted = false;
            m_output.clear();
        } else {
            markLineStart();
            m_line += c;
        }
    }
}

void ScriptRecorder::password(const QString &credentialName)
{
    m_line.clear();
    m_lineStarted = false;
    markLineStart();
    waitForPrompt(m_prompt);
    m_body << QStringLiteral("tab.send_password(%1)").arg(pythonString(credentialName));
    m_steps++;
    m_prompt.clear();
    m_lineStarted = false;
    m_output.clear();
}

void ScriptRecorder::note(const QString &text)
{
    m_body << "# " + text;
}

QString ScriptRecorder::script()
{
    QStringList s{QStringLiteral("# Recorded by sshLX"), QStringLiteral("import sshlx"), QString(),
                  QStringLiteral("tab = sshlx.current()")};
    s += m_body;
    if (!m_line.isEmpty() && !ConfigPush::looksLikePasswordPrompt(m_prompt))
        s << QStringLiteral("tab.send(%1)").arg(pythonString(m_line));
    return s.join('\n') + '\n';
}
