#include "scripthost.h"

#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>

#include "connectionstore.h"
#include "dialogkit.h"
#include "documentarea.h"
#include "logonrunner.h"
#include "mainwindow.h"
#include "sessionwidget.h"

using namespace DialogKit;

struct ScriptHost::Wait {
    Reply reply;
    QPointer<SessionWidget> session;
    QString pattern;
    QRegularExpression re;
    bool regex = false;
    QTimer *timer = nullptr;
};

struct ScriptHost::Run {
    QString path;
    QProcess *process = nullptr;
    QLocalServer *server = nullptr;
    QLocalSocket *socket = nullptr;
    QTemporaryDir dir;
    QByteArray token;
    bool authed = false;
    QHash<SessionWidget *, std::shared_ptr<OutputText>> output;
    QList<Wait *> waits;
    QByteArray buffer;
    QList<QMetaObject::Connection> tabConnections;
};

ScriptHost::ScriptHost(MainWindow *window) : QObject(window), m_window(window) {}

ScriptHost::~ScriptHost()
{
    stopAll();
}

QString ScriptHost::scriptsFolder()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/scripts";
}

void ScriptHost::log(const QString &text, bool show)
{
    ensureConsole();
    if (show && !m_console->isVisible())
        m_console->show();
    m_output->moveCursor(QTextCursor::End);
    m_output->insertPlainText(text);
    m_output->moveCursor(QTextCursor::End);
}

QString ScriptHost::consoleText() const
{
    return m_output ? m_output->toPlainText() : QString();
}

void ScriptHost::showConsole()
{
    ensureConsole();
    if (!m_console->isVisible())
        m_console->show();
    m_stop->setVisible(!m_runs.isEmpty());
}

void ScriptHost::ensureConsole()
{
    if (!m_console) {
        m_console = new QDialog(m_window);
        m_console->setWindowTitle(tr("Script Console"));
        m_console->resize(680, 420);
        auto *v = new QVBoxLayout(m_console);
        frame(v);
        m_output = new QPlainTextEdit;
        m_output->setReadOnly(true);
        m_output->setMaximumBlockCount(20000);
        m_output->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        v->addWidget(m_output, 1);
        m_stop = secondaryButton(tr("Stop Scripts"));
        auto *clear = secondaryButton(tr("Clear"));
        auto *close = primaryButton(tr("Close"));
        v->addWidget(footer({close}, clear));
        connect(clear, &QPushButton::clicked, m_output, &QPlainTextEdit::clear);
        connect(close, &QPushButton::clicked, m_console, &QDialog::close);
        connect(m_stop, &QPushButton::clicked, this, &ScriptHost::stopAll);
        v->insertWidget(0, m_stop, 0, Qt::AlignRight);
    }
}

bool ScriptHost::run(const QString &path, const QStringList &args)
{
    if (!QFileInfo(path).isFile()) {
        error(m_window, tr("Run Script"), tr("%1 does not exist.").arg(path));
        return false;
    }
    const QString python = QSettings().value("scripts/python", "python3").toString();
    const QString exe = QStandardPaths::findExecutable(python);
    if (exe.isEmpty()) {
        error(m_window, tr("Run Script"),
              tr("\"%1\" was not found. Install Python 3, or set its path in Options > Terminal.")
                  .arg(python));
        return false;
    }

    auto r = std::make_unique<Run>();
    r->path = path;
    if (!r->dir.isValid())
        return false;
    // The module, for "import sshlx".
    QFile::copy(":/python/sshlx.py", r->dir.filePath("sshlx.py"));
    QFile::setPermissions(r->dir.filePath("sshlx.py"), QFile::ReadOwner | QFile::WriteOwner);

    // The socket is in a directory only this user can enter (QTemporaryDir's
    // default), and a token proves the connection is this script's.
    r->server = new QLocalServer(this);
    r->server->setSocketOptions(QLocalServer::UserAccessOption);
    const QString socketPath = r->dir.filePath("s");
    if (!r->server->listen(socketPath)) {
        error(m_window, tr("Run Script"), r->server->errorString());
        delete r->server;
        return false;
    }
    QByteArray bytes(32, 0);
    QRandomGenerator::system()->generate(reinterpret_cast<quint32 *>(bytes.data()),
                                         reinterpret_cast<quint32 *>(bytes.data()) + 8);
    r->token = bytes.toHex();

    r->process = new QProcess(this);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    // In an AppImage these would point the system Python at bundled files.
    for (const char *v : {"LD_LIBRARY_PATH", "PYTHONHOME", "QT_PLUGIN_PATH"})
        env.remove(v);
    const QString oldPath = env.value("PYTHONPATH");
    env.insert("PYTHONPATH", r->dir.path() + (oldPath.isEmpty() ? QString() : ':' + oldPath));
    env.insert("PYTHONUNBUFFERED", "1");
    env.insert("SSHLX_SCRIPT_SOCKET", socketPath);
    env.insert("SSHLX_SCRIPT_TOKEN", QString::fromLatin1(r->token));
    r->process->setProcessEnvironment(env);
    r->process->setProcessChannelMode(QProcess::MergedChannels);
    r->process->setWorkingDirectory(QFileInfo(path).absolutePath());

    Run *run = r.release();
    m_runs.append(run);
    connect(run->server, &QLocalServer::newConnection, this, [this, run] {
        while (QLocalSocket *c = run->server->nextPendingConnection()) {
            if (run->socket) {
                c->abort();
                c->deleteLater();
                continue;
            }
            run->socket = c;
            connect(c, &QLocalSocket::readyRead, this, [this, run, c] {
                run->buffer += c->readAll();
                int nl;
                while ((nl = int(run->buffer.indexOf('\n'))) >= 0) {
                    const QByteArray line = run->buffer.left(nl);
                    run->buffer.remove(0, nl + 1);
                    onLine(run, c, line);
                    if (!m_runs.contains(run))
                        return;
                }
            });
        }
    });
    connect(run->process, &QProcess::readyRead, this, [this, run] {
        log(QString::fromUtf8(run->process->readAll()));
    });
    connect(run->process, &QProcess::errorOccurred, this, [this, run](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            log(tr("Could not start %1: %2\n").arg(run->path, run->process->errorString()));
            finishRun(run);
        }
    });
    connect(run->process, &QProcess::finished, this, [this, run](int, QProcess::ExitStatus) {
        finishRun(run);
    });
    // The console opens when there is something to read, not for each script.
    log(tr("--- %1\n").arg(QFileInfo(path).fileName()), false);
    run->process->start(exe, QStringList{QStringLiteral("-u"), path} + args);
    m_stop->setVisible(true);
    return true;
}

void ScriptHost::stopAll()
{
    const QList<Run *> runs = m_runs;
    for (Run *r : runs) {
        r->process->disconnect(this);
        r->process->kill();
        r->process->waitForFinished(2000);
        if (r->process->exitStatus() == QProcess::CrashExit && m_output)
            log(tr("--- %1 stopped\n").arg(QFileInfo(r->path).fileName()));
        finishRun(r);
    }
}

void ScriptHost::finishRun(Run *r)
{
    if (!m_runs.removeOne(r))
        return;
    const QByteArray rest = r->process->readAll();
    if (!rest.isEmpty())
        log(QString::fromUtf8(rest));
    const int code = r->process->state() == QProcess::NotRunning ? r->process->exitCode() : -1;
    if (code != 0 || r->process->exitStatus() != QProcess::NormalExit)
        log(tr("--- %1 exited with code %2\n").arg(QFileInfo(r->path).fileName()).arg(code));
    const QString path = r->path;
    // Tabs stop copying their output once no script reads it.
    for (const QMetaObject::Connection &c : std::as_const(r->tabConnections))
        disconnect(c);
    for (Wait *w : std::as_const(r->waits)) {
        delete w->timer;
        delete w;
    }
    if (r->socket)
        r->socket->disconnect(this);
    r->process->disconnect(this);
    r->process->deleteLater();
    r->server->close();
    r->server->deleteLater();
    if (r->socket)
        r->socket->deleteLater();
    delete r;
    if (m_stop)
        m_stop->setVisible(!m_runs.isEmpty());
    emit scriptFinished(path, code);
}

void ScriptHost::onLine(Run *r, QLocalSocket *socket, const QByteArray &line)
{
    const QJsonObject o = QJsonDocument::fromJson(line).object();
    const QJsonValue id = o["id"];
    const QString method = o["method"].toString();
    const QJsonObject params = o["params"].toObject();
    QPointer<QLocalSocket> sock(socket);
    Reply reply = [sock, id](const QJsonValue &result, const QString &err) {
        if (!sock)
            return;
        QJsonObject out{{"id", id}};
        if (err.isEmpty())
            out["result"] = result;
        else
            out["error"] = err;
        sock->write(QJsonDocument(out).toJson(QJsonDocument::Compact) + '\n');
        sock->flush();
    };
    if (!r->authed) {
        const QByteArray token = params["token"].toString().toLatin1();
        if (method == "hello" && token == r->token) {
            r->authed = true;
            reply(true, {});
        } else {
            socket->abort();
        }
        return;
    }
    dispatch(r, method, params, reply);
}

int ScriptHost::idFor(SessionWidget *s)
{
    auto it = m_ids.find(s);
    if (it != m_ids.end())
        return *it;
    const int id = m_nextId++;
    m_ids.insert(s, id);
    connect(s, &QObject::destroyed, this, [this, s] { m_ids.remove(s); });
    return id;
}

SessionWidget *ScriptHost::tab(const QJsonObject &p, QString *err)
{
    const int id = p["tab"].toInt();
    for (auto it = m_ids.begin(); it != m_ids.end(); ++it)
        if (it.value() == id)
            return it.key();
    *err = tr("That tab has been closed.");
    return nullptr;
}

QJsonObject ScriptHost::info(SessionWidget *s)
{
    static const char *const states[] = {"idle", "connecting", "connected", "disconnected"};
    QString proto;
    for (size_t i = 0; i < sshlx_protocol_count(); i++)
        if (sshlx_protocol_get(i)->id == s->protocol())
            proto = QString::fromUtf8(sshlx_protocol_get(i)->name);
    return {{"id", idFor(s)}, {"name", s->displayName()}, {"title", s->title()},
            {"state", states[s->state()]}, {"protocol", proto}};
}

// From the first touch on, so that nothing a tab answers is missed.
void ScriptHost::capture(Run *r, SessionWidget *s)
{
    if (r->output.contains(s))
        return;
    r->output.insert(s, std::make_shared<OutputText>());
    r->tabConnections << connect(s, &SessionWidget::outputReceived, this, [this, r, s](const QByteArray &data) {
        if (!m_runs.contains(r))
            return;
        OutputText &t = *r->output[s];
        t.append(data.constData(), size_t(data.size()));
        t.trim(1024 * 1024);
        // Not from here: this runs inside the session's output callback.
        QTimer::singleShot(0, this, [this, r, s] {
            if (m_runs.contains(r))
                checkWaits(r, s);
        });
    });
    r->tabConnections << connect(s, &QObject::destroyed, this, [this, r, s] {
        if (!m_runs.contains(r))
            return;
        r->output.remove(s);
        for (Wait *w : std::as_const(r->waits))
            if (w->session.isNull() || w->session == s)
                w->timer->start(0);
    });
}

void ScriptHost::checkWaits(Run *r, SessionWidget *s)
{
    for (Wait *w : QList<Wait *>(r->waits)) {
        if (w->session != s)
            continue;
        OutputText &t = *r->output[s];
        const QString text = t.text();
        int start = -1, end = -1;
        if (w->regex) {
            const QRegularExpressionMatch m = w->re.match(text);
            if (m.hasMatch()) {
                start = int(m.capturedStart());
                end = int(m.capturedEnd());
            }
        } else {
            start = int(text.indexOf(w->pattern));
            end = start + int(w->pattern.size());
        }
        if (start < 0)
            continue;
        const QJsonObject found{{"text", text.mid(start, end - start)}, {"before", text.left(start)}};
        t.trim(int(text.size()) - end);
        r->waits.removeOne(w);
        const Reply reply = w->reply;
        delete w->timer;
        delete w;
        reply(found, {});
    }
}

void ScriptHost::dispatch(Run *r, const QString &method, const QJsonObject &p, const Reply &reply)
{
    QString err;
    if (method == "tabs") {
        QJsonArray list;
        for (SessionWidget *s : m_window->sessions()) {
            if (s->isFileTransfer())
                continue;
            capture(r, s);
            list.append(info(s));
        }
        reply(list, {});
    } else if (method == "current") {
        SessionWidget *s = m_window->documents()->currentSession();
        if (s)
            capture(r, s);
        reply(s ? QJsonValue(info(s)) : QJsonValue(), {});
    } else if (method == "open" || method == "local_shell") {
        SessionWidget *s = method == "open" ? m_window->openByName(p["name"].toString(), &err)
                                            : m_window->openLocalShell();
        if (!s) {
            reply({}, err);
            return;
        }
        capture(r, s);
        reply(info(s), {});
    } else if (method == "connections") {
        QJsonArray list;
        for (ConnectionNode *c : m_window->store()->tree().connections())
            list.append(QJsonObject{{"name", c->name}, {"path", c->path()}, {"id", c->id}});
        reply(list, {});
    } else if (method == "status") {
        m_window->statusBar()->showMessage(p["text"].toString(), 8000);
        reply(true, {});
    } else if (method == "message") {
        DialogKit::info(m_window, p["title"].toString(), p["text"].toString());
        reply(true, {});
    } else if (method == "confirm") {
        reply(confirm(m_window, p["title"].toString(), p["text"].toString(), tr("Yes"), tr("No")), {});
    } else if (method == "prompt") {
        bool ok = false;
        const QString text = QInputDialog::getText(
            m_window, p["title"].toString(), p["text"].toString(),
            p["password"].toBool() ? QLineEdit::Password : QLineEdit::Normal,
            p["default"].toString(), &ok);
        reply(ok ? QJsonValue(text) : QJsonValue(), {});
    } else {
        SessionWidget *s = tab(p, &err);
        if (!s) {
            reply({}, err);
            return;
        }
        capture(r, s);
        if (method == "tab_info") {
            reply(info(s), {});
        } else if (method == "send") {
            if (!s->canSendCommands()) {
                reply({}, tr("This tab can't take text now: it is not connected, or is busy."));
                return;
            }
            s->typeText(p["text"].toString());
            reply(true, {});
        } else if (method == "send_password") {
            const QString wanted = p["credential"].toString();
            QString id;
            if (!wanted.isEmpty()) {
                for (const Credential &c : m_window->store()->tree().credentials())
                    if (c.name == wanted)
                        id = c.id;
                if (id.isEmpty()) {
                    reply({}, tr("No credential is called \"%1\".").arg(wanted));
                    return;
                }
            }
            if (!s->atPasswordPrompt()) {
                reply({}, tr("The last line is not a password prompt, so nothing was sent."));
                return;
            }
            s->sendPasswordQuietly(id, [reply](bool ok, const QString &e) {
                reply(ok, ok ? QString() : e);
            });
        } else if (method == "wait_for") {
            auto *w = new Wait;
            w->reply = reply;
            w->session = s;
            w->pattern = p["pattern"].toString();
            w->regex = p["regex"].toBool();
            if (w->regex) {
                w->re = QRegularExpression(w->pattern);
                if (!w->re.isValid()) {
                    const QString bad = w->re.errorString();
                    delete w;
                    reply({}, tr("Bad pattern: %1").arg(bad));
                    return;
                }
            }
            w->timer = new QTimer;
            w->timer->setSingleShot(true);
            connect(w->timer, &QTimer::timeout, this, [this, r, w] {
                if (!m_runs.contains(r) || !r->waits.removeOne(w))
                    return;
                const Reply answer = w->reply;
                w->timer->deleteLater();
                delete w;
                answer(QJsonValue(), {});
            });
            w->timer->start(int(p["timeout"].toDouble(30) * 1000));
            r->waits.append(w);
            checkWaits(r, s);
        } else if (method == "wait_connected") {
            QPointer<SessionWidget> ps(s);
            auto *timer = new QTimer(this);
            auto limit = std::make_shared<QElapsedTimer>();
            limit->start();
            const qint64 ms = qint64(p["timeout"].toDouble(30) * 1000);
            connect(timer, &QTimer::timeout, this, [this, r, ps, timer, limit, ms, reply] {
                const bool done = !m_runs.contains(r) || !ps || ps->state() == SSHLX_STATE_CONNECTED
                    || ps->state() == SSHLX_STATE_DISCONNECTED || limit->elapsed() > ms;
                if (!done)
                    return;
                timer->deleteLater();
                reply(ps && ps->state() == SSHLX_STATE_CONNECTED, {});
            });
            timer->start(50);
        } else if (method == "screen") {
            reply(s->allTextForScripts(), {});
        } else if (method == "activate") {
            m_window->documents()->activate(s);
            reply(true, {});
        } else if (method == "disconnect") {
            s->disconnectSession();
            reply(true, {});
        } else if (method == "reconnect") {
            s->reconnect();
            reply(true, {});
        } else if (method == "close") {
            reply(true, {});
            m_window->closeTabNow(s);
        } else {
            reply({}, tr("Unknown request: %1").arg(method));
        }
    }
}
