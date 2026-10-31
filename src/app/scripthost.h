// ScriptHost - runs Python scripts in their own process and answers them
// over a private local socket. Nothing of Python is linked in: scripts use
// the python3 on this machine, with the sshlx module that ships inside the
// program (resources/python/sshlx.py, see docs/SCRIPTING.md).
//
// A script can do what its user can, so running one is always the user's
// choice. It never sees a saved password: send_password goes from the
// keyring to the session, and only at a password prompt.
#pragma once

#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QTemporaryDir>

#include <functional>
#include <memory>

class MainWindow;
class OutputText;
class QDialog;
class QLocalServer;
class QLocalSocket;
class QPlainTextEdit;
class QProcess;
class QPushButton;
class QTimer;
class SessionWidget;

class ScriptHost : public QObject {
    Q_OBJECT
public:
    explicit ScriptHost(MainWindow *window);
    ~ScriptHost() override;

    // tab: what sshlx.current() means to the script, instead of the tab in use.
    bool run(const QString &path, const QStringList &args = {}, SessionWidget *tab = nullptr);
    void stopAll();
    int runningCount() const { return int(m_runs.size()); }
    QString consoleText() const;
    static QString scriptsFolder();     // ~/.config/sshLX/scripts

public slots:
    void showConsole();

signals:
    void scriptFinished(const QString &path, int exitCode);

private:
    struct Run;
    struct Wait;
    using Reply = std::function<void(const QJsonValue &result, const QString &error)>;

    void onConnection();
    void onLine(Run *r, QLocalSocket *socket, const QByteArray &line);
    void dispatch(Run *r, const QString &method, const QJsonObject &p, const Reply &reply);
    int idFor(SessionWidget *s);
    SessionWidget *tab(const QJsonObject &p, QString *error);
    QJsonObject info(SessionWidget *s);
    void capture(Run *r, SessionWidget *s);
    void checkWaits(Run *r, SessionWidget *s);
    void finishRun(Run *r);
    void log(const QString &text, bool show = true);
    void ensureConsole();

    MainWindow *m_window;
    QList<Run *> m_runs;
    QHash<SessionWidget *, int> m_ids;
    int m_nextId = 1;
    QDialog *m_console = nullptr;
    QPlainTextEdit *m_output = nullptr;
    QPushButton *m_stop = nullptr;
};
