// ScriptRecorder - turns what is typed in a tab into a Python script for the
// sshlx module: wait for the prompt that was showing, then send the line.
//
// A line typed at a password prompt is never kept. It becomes a call to
// send_password(), which takes the password from the Credentials Manager when
// the script runs. Qt Core only.
#pragma once

#include <QString>
#include <QStringList>

#include "logonrunner.h"

class ScriptRecorder {
public:
    void output(const char *data, size_t len);
    // Text as typed; Enter ends a line, and Backspace edits it.
    void typed(const QString &text);
    void password(const QString &credentialName);   // Send Saved Password
    void note(const QString &text);                 // a comment in the script
    int steps() const { return m_steps; }
    QString script();

    static QString pythonString(const QString &text);

private:
    void commit();
    void flushPartial();
    void waitForPrompt(const QString &prompt);
    void markLineStart();

    OutputText m_output;
    QString m_line, m_prompt;
    bool m_lineStarted = false;
    QStringList m_body;
    int m_steps = 0;
};
