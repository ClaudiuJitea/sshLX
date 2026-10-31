// LogonRunner - runs a connection's logon actions: wait for some text, then
// send a reply or a saved password, as a device's login usually needs after
// the connection itself is up ("enable", then its password).
//
// OutputText is shared with scripting: it turns the terminal byte stream into
// searchable text, without escape sequences.
//
// Qt Core only. Nothing is sent from inside feed().
#pragma once

#include <QList>
#include <QObject>
#include <QStringDecoder>
#include <QTimer>

struct LogonStep {
    QString wait;           // text to wait for; empty: go straight on
    bool regex = false;
    QString send;           // sent followed by Enter, unless password is set
    bool password = false;  // send a saved password instead
    QString credential;     // its credential id; empty: the connection's own
    bool operator==(const LogonStep &o) const
    {
        return wait == o.wait && regex == o.regex && send == o.send
            && password == o.password && credential == o.credential;
    }
};

class OutputText {
public:
    void append(const char *data, size_t len);
    QString text() const { return m_text; }
    int size() const { return int(m_text.size()); }
    void clear() { m_text.clear(); }
    // Keeps the last n characters; returns how many were dropped.
    int trim(int n);
    // The text after the last line break.
    QString lastLine() const;

private:
    QStringDecoder m_decoder{QStringDecoder::Utf8};
    QString m_text;
    int m_escape = 0;      // 0 none, 1 after ESC, 2 CSI, 3 OSC, 4 OSC after ESC
};

class LogonRunner : public QObject {
    Q_OBJECT
public:
    explicit LogonRunner(QObject *parent = nullptr);

    void setTimeoutMs(int ms) { m_timeoutMs = ms; }
    void start(const QList<LogonStep> &steps);
    void stop();
    bool running() const { return m_running; }
    int current() const { return m_index; }
    int total() const { return int(m_steps.size()); }
    void feed(const char *data, size_t len);
    // Text at the end of the output, for checking a password prompt.
    QString lastLine() const { return m_output.lastLine(); }
    // The owner calls this when a password step's secret has gone out.
    void stepDone();
    void abort(const QString &message) { finish(false, message); }

    // A pattern's complaint, or empty if it compiles.
    static QString patternError(const QString &pattern);

signals:
    void send(const QString &text);     // the owner appends Enter
    void sendPassword(const QString &credential);
    void progress(int step, int total, const QString &waitingFor);
    void finished(bool ok, const QString &message);

private:
    void check();
    void runStep();
    void finish(bool ok, const QString &message);

    QList<LogonStep> m_steps;
    int m_index = 0;
    int m_timeoutMs = 30000;
    bool m_running = false, m_waiting = false;
    OutputText m_output;
    QTimer m_check, m_timeout;
};
