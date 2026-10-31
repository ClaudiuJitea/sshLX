// ConfigPush - sends a configuration to a device one line at a time. Each
// line waits for the device's next prompt; an error, a question or silence
// pauses the push until the user says how to go on, and it then carries on
// from where it stopped.
//
// Qt Core only, and knows nothing of sessions: the owner connects send() to
// the session and hands every byte received to feed(). A line must not be
// sent from inside feed(), so everything that sends runs from a timer.
#pragma once

#include <QList>
#include <QObject>
#include <QRegularExpression>
#include <QString>
#include <QStringDecoder>
#include <QStringList>
#include <QTimer>

class ConfigPush : public QObject {
    Q_OBJECT
public:
    struct Options {
        QString promptPattern = defaultPromptPattern();
        QString errorPattern = defaultErrorPattern();
        int timeoutMs = 30000;     // silence after which the user is asked
        int lineDelayMs = 0;       // extra pause between lines
        bool skipBlank = true;
        bool skipComments = true;  // lines starting with '!'
        bool stopOnError = true;   // false: note errors and keep going
    };
    enum class LineState { Pending, Ok, Failed, Skipped };
    enum class Pause { None, DeviceError, Timeout, Question, ByUser, NoPrompt };
    Q_ENUM(Pause)
    struct Line {
        QString text;
        QString original;      // differs from text once amended
        LineState state = LineState::Pending;
        QString error;         // what the device answered, when it failed
    };

    static QString defaultPromptPattern();
    static QString defaultErrorPattern();
    // True when the last line of text ends in a password or passphrase question.
    static bool looksLikePasswordPrompt(const QString &text);
    // A pattern's complaint, or empty if it compiles.
    static QString patternError(const QString &pattern);
    static int settleMs() { return 80; }
    // The part of a prompt that stays when its mode or folder changes:
    // "R1" of R1(config-if)#, "clau@laptop" of (base) clau@laptop:~$.
    static QString promptStem(const QString &prompt);

    explicit ConfigPush(QObject *parent = nullptr);

    void start(const QStringList &lines, const Options &options);
    bool running() const { return m_step != Step::Idle; }
    Pause pauseReason() const { return m_pause; }
    QList<Line> lines() const { return m_lines; }
    int current() const { return m_index; }    // index into lines()
    int total() const { return m_lines.size(); }
    QString report() const;

    void feed(const char *data, size_t len);

    // While paused:
    void retry(const QString &text);   // send the (possibly amended) line again
    void skip();                       // leave this line out, go to the next
    void keepWaiting();                // the device is only slow
    void answer(const QString &text);  // reply to a question the device asked
    void ignoreErrors();               // this error and later ones: just note them
    void requestPause();               // pause after the current line
    void resume();                     // after requestPause()
    void stop();                       // give up; the remaining lines stay unsent
    // Starts again with the lines that failed or were skipped.
    void retryFailed();
    int failedCount() const;
    void connectionLost();

signals:
    void send(const QString &text);      // the owner appends Enter
    void sendKeys(const QString &text);  // as is: a pager's space bar
    void progress(int current, int total);
    void paused(ConfigPush::Pause reason, const QString &deviceOutput);
    // The device got back to its prompt during a pause, as when the answer
    // was typed in the terminal.
    void resumed();
    void finished(bool complete);

private:
    // WaitReady: a line was skipped while it was still running, so the next
    // one must wait for the prompt.
    enum class Step { Idle, Sync, WaitPrompt, WaitReady, TextBlock, Paused, Between };

    void acceptText(const QString &text);
    void onOutput();
    void evaluate();
    void lineDone();
    void advance();
    void sendCurrent();
    void pauseWith(Pause reason, const QString &output);
    void finish(bool complete);
    QString outputSinceSend() const;
    QString tail() const;
    bool sawEcho() const;
    bool looksLikePrompt(const QString &line) const;
    bool isOurPrompt(const QString &line) const;
    bool renamesDevice() const;

    Options m_opts;
    QRegularExpression m_prompt, m_error;
    QList<Line> m_lines;
    int m_index = 0;
    Step m_step = Step::Idle, m_resumeStep = Step::Idle;
    Pause m_pause = Pause::None;
    bool m_pauseWanted = false;
    bool m_quiet = false;       // errors no longer stop the push

    // Screen-free view of what came back since the last send. \r overwrites
    // the line, as a pager wiping its --More-- does.
    QStringDecoder m_decoder{QStringDecoder::Utf8};
    QString m_out;
    int m_lineStart = 0;
    int m_escape = 0;           // 0 none, 1 after ESC, 2 CSI, 3 OSC, 4 OSC after ESC
    bool m_pendingCr = false;

    QChar m_blockEnd;           // delimiter of a banner or macro being sent
    QString m_stem;             // what this device's prompts have in common
    QTimer m_settle, m_timeout, m_quietCheck;
};
