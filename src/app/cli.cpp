#include "cli.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>

#include <cstdio>
#include <cstring>
#include <termios.h>
#include <unistd.h>

#include "connectionstore.h"
#include "connecttarget.h"
#include "keyring.h"
#include "qthostloop.h"
#include "sftpclient.h"
#include "sshlx_engine.h"

namespace {

void err(const QString &text)
{
    fprintf(stderr, "sshLX: %s\n", text.toLocal8Bit().constData());
}

// Questions go to the terminal, not stdout, which may be a pipe of data.
QString askTty(const QString &question, bool hidden)
{
    FILE *tty = fopen("/dev/tty", "r+");
    if (!tty)
        return QString();
    fprintf(tty, "%s", question.toLocal8Bit().constData());
    fflush(tty);
    struct termios saved;
    const int fd = fileno(tty);
    const bool have = hidden && tcgetattr(fd, &saved) == 0;
    if (have) {
        struct termios quiet = saved;
        quiet.c_lflag &= ~tcflag_t(ECHO);
        tcsetattr(fd, TCSAFLUSH, &quiet);
    }
    char buf[1024] = "";
    const bool ok = fgets(buf, sizeof buf, tty) != nullptr;
    if (have) {
        tcsetattr(fd, TCSAFLUSH, &saved);
        fprintf(tty, "\n");
    }
    fclose(tty);
    if (!ok)
        return QString();
    QString answer = QString::fromLocal8Bit(buf);
    answer.chop(answer.endsWith('\n') ? 1 : 0);
    const QString result = answer.isNull() ? QStringLiteral("") : answer;
    memset(buf, 0, sizeof buf);
    return result;
}

// One SSH connection, with no terminal to look at.
class CliSession : public QObject {
public:
    std::function<void(const char *, size_t)> onData;
    std::function<void()> onReady;     // the command or subsystem has started
    std::function<void(int exitCode, const QString &error)> onEnd;
    QString account;                   // the keyring's password for this login
    QString passphraseAccount;

    CliSession(const sshlx_conf *conf)
    {
        sshlx_session_callbacks cb{};
        cb.ctx = this;
        cb.output_data = [](void *c, const char *d, size_t n) {
            auto *self = static_cast<CliSession *>(c);
            if (self->onData)
                self->onData(d, n);
        };
        // Before the command starts this is PuTTY's "Using username" and so on.
        cb.stderr_data = [](void *c, const char *d, size_t n) {
            if (static_cast<CliSession *>(c)->m_connected)
                fwrite(d, 1, n, stderr);
        };
        cb.exited = [](void *c, int code, bool) {
            static_cast<CliSession *>(c)->m_exit = code;
        };
        cb.state_changed = [](void *c, sshlx_state st) {
            static_cast<CliSession *>(c)->state(st);
        };
        cb.connection_fatal = [](void *c, const char *m, int) {
            static_cast<CliSession *>(c)->m_error = QString::fromUtf8(m);
        };
        cb.prompt = [](void *c, const sshlx_prompt_request *r) {
            static_cast<CliSession *>(c)->prompt(r);
        };
        m_session = sshlx_session_new(conf, &cb, 80, 24);
        sshlx_session_set_raw(m_session, true);
        sshlx_session_set_password_hook(m_session, true);
    }
    ~CliSession() override { sshlx_session_free(m_session); }

    bool start()
    {
        return sshlx_session_connect(m_session) == 0;
    }
    void send(const QByteArray &d) { sshlx_session_send_bytes(m_session, d.constData(), size_t(d.size())); }
    size_t backlog() const { return sshlx_session_send_backlog(m_session); }
    void stop() { sshlx_session_disconnect(m_session); }

private:
    void state(sshlx_state st)
    {
        if (st == SSHLX_STATE_CONNECTED) {
            m_connected = true;
            auto *poll = new QTimer(this);
            connect(poll, &QTimer::timeout, this, [this, poll] {
                if (sshlx_session_state(m_session) != SSHLX_STATE_CONNECTED) {
                    poll->deleteLater();
                } else if (sshlx_session_send_ready(m_session)) {
                    poll->deleteLater();
                    if (onReady)
                        onReady();
                }
            });
            poll->start(10);
        } else if (st == SSHLX_STATE_DISCONNECTED && onEnd) {
            auto end = onEnd;
            onEnd = nullptr;
            const QString why = m_error;
            // The exit status can come just after the state.
            QTimer::singleShot(50, this, [this, end, why] {
                // A login that never finished is a failure, however it ended.
                end(!m_connected ? -1 : m_exit >= 0 ? m_exit : sshlx_session_exit_code(m_session), why);
            });
        }
    }
    void prompt(const sshlx_prompt_request *r)
    {
        const sshlx_prompt_id id = r->id;
        if (r->kind == SSHLX_PROMPT_PASSWORD) {
            const bool phrase = r->passphrase;
            const QString text = QString::fromUtf8(r->prompt_text ? r->prompt_text : "Password: ");
            const QString acct = phrase ? passphraseAccount : account;
            auto ask = [this, id, text] {
                const QString pw = askTty(text, true);
                if (pw.isNull()) {
                    err(QStringLiteral("a password is needed, and there is no terminal to ask on"));
                    QCoreApplication::exit(2);
                    return;
                }
                sshlx_session_answer_password(m_session, id, pw.toUtf8().constData());
            };
            if (acct.isEmpty() || !Keyring::available()) {
                QTimer::singleShot(0, this, ask);
                return;
            }
            Keyring::read(acct, [this, id, ask](bool found, const QString &pw) {
                if (found)
                    sshlx_session_answer_password(m_session, id, pw.toUtf8().constData());
                else
                    ask();
            });
            return;
        }
        // Host keys and weak crypto: yes only from a person at a terminal.
        QString question;
        for (size_t i = 0; i < r->n_items; i++)
            question += QString::fromUtf8(r->items[i].text) + '\n';
        QTimer::singleShot(0, this, [this, id, question] {
            const QString a = askTty(question + QStringLiteral("Continue? [y/N] "), false).trimmed();
            const bool yes = a.compare(QStringLiteral("y"), Qt::CaseInsensitive) == 0
                || a.compare(QStringLiteral("yes"), Qt::CaseInsensitive) == 0;
            if (!yes && a.isNull())
                err(QStringLiteral("this needs confirming, and there is no terminal to ask on"));
            sshlx_session_answer_prompt(m_session, id, yes ? SSHLX_ANSWER_ACCEPT : SSHLX_ANSWER_CANCEL);
        });
    }

    sshlx_session *m_session = nullptr;
    QString m_error;
    bool m_connected = false;
    int m_exit = -1;
};

struct Target {
    sshlx_conf *conf = nullptr;
    QString account, passphraseAccount;
};

bool resolve(ConnectionStore &store, const QString &text, Target *t)
{
    for (ConnectionNode *c : store.tree().connections()) {
        if (c->name != text && c->path() + '/' + c->name != text)
            continue;
        t->conf = store.effectiveConf(c);
        if (const Credential *cr = store.tree().resolveCredential(c)) {
            t->account = Keyring::credentialAccount(cr->id, false);
            t->passphraseAccount = Keyring::credentialAccount(cr->id, true);
        } else {
            t->account = Keyring::accountFor(c->id);
        }
        return true;
    }
    ConnectTarget ct;
    if (!ConnectTarget::parse(text, &ct) || ct.protocol != "ssh") {
        err(QStringLiteral("\"%1\" is not a connection or an SSH host").arg(text));
        return false;
    }
    t->conf = sshlx_conf_new_default();
    for (size_t i = 0; i < sshlx_protocol_count(); i++)
        if (!strcmp(sshlx_protocol_get(i)->name, "ssh"))
            sshlx_conf_set_int(t->conf, sshlx_conf_key_protocol(), sshlx_protocol_get(i)->id);
    sshlx_conf_set_str(t->conf, sshlx_conf_key_find("host"), ct.host.toUtf8().constData());
    sshlx_conf_set_int(t->conf, sshlx_conf_key_find("port"), ct.port ? ct.port : 22);
    if (!ct.user.isEmpty())
        sshlx_conf_set_str(t->conf, sshlx_conf_key_find("username"), ct.user.toUtf8().constData());
    return true;
}

bool isSsh(const sshlx_conf *c)
{
    for (size_t i = 0; i < sshlx_protocol_count(); i++)
        if (!strcmp(sshlx_protocol_get(i)->name, "ssh"))
            return sshlx_conf_get_int(c, sshlx_conf_key_protocol()) == sshlx_protocol_get(i)->id;
    return false;
}

int runCommand(const Target &t, const QString &command)
{
    sshlx_conf *c = sshlx_conf_for_transfer(t.conf, command.toUtf8().constData(), false);
    CliSession s(c);
    sshlx_conf_free(c);
    s.account = t.account;
    s.passphraseAccount = t.passphraseAccount;
    s.onData = [](const char *d, size_t n) {
        fwrite(d, 1, n, stdout);
        fflush(stdout);
    };
    int rc = 1;
    s.onEnd = [&rc](int code, const QString &why) {
        if (!why.isEmpty())
            err(why);
        rc = code >= 0 ? code : 1;
        QCoreApplication::exit(rc);
    };
    if (!s.start())
        return 1;
    const int out = QCoreApplication::exec();
    return out ? out : rc;
}

int transfer(const Target &t, bool upload, const QString &local, QString remote)
{
    sshlx_conf *c = sshlx_conf_for_transfer(t.conf, "sftp", true);
    CliSession s(c);
    sshlx_conf_free(c);
    s.account = t.account;
    s.passphraseAccount = t.passphraseAccount;
    SftpClient sftp;
    QObject::connect(&sftp, &SftpClient::send, &s, &CliSession::send);
    s.onData = [&sftp](const char *d, size_t n) {
        QByteArray copy(d, qsizetype(n));
        // Not from inside the session's output callback.
        QTimer::singleShot(0, &sftp, [&sftp, copy] { sftp.feed(copy.constData(), size_t(copy.size())); });
    };
    s.onReady = [&sftp] { sftp.start(); };
    s.onEnd = [&sftp](int, const QString &why) {
        sftp.connectionLost(why.isEmpty() ? QStringLiteral("the connection closed") : why);
        err(why.isEmpty() ? QStringLiteral("the connection closed") : why);
        QCoreApplication::exit(1);
    };
    QObject::connect(&sftp, &SftpClient::protocolError, &s, [](const QString &why) {
        err(why);
        QCoreApplication::exit(1);
    });
    const auto finish = [&s](bool ok, const QString &e) {
        if (!ok)
            err(e);
        s.onEnd = nullptr;
        QCoreApplication::exit(ok ? 0 : 1);
    };
    QObject::connect(&sftp, &SftpClient::ready, &s, [&] {
        if (upload) {
            sftp.upload(local, remote, {}, finish);
            return;
        }
        sftp.stat(remote, [&, finish](bool ok, const QString &e, const SftpClient::Entry &entry) {
            if (!ok) {
                finish(false, e);
                return;
            }
            sftp.download(remote, local, entry.size, {}, finish);
        });
    });
    if (!s.start())
        return 1;
    return QCoreApplication::exec();
}

int usage(FILE *to)
{
    fputs("Usage:\n"
          "  sshLX                            open the window\n"
          "  sshLX HOST | ssh://... | telnet://...  open a tab, in the running window if any\n"
          "  sshLX connect TARGET             the same, for a connection's name\n"
          "  sshLX list                       list saved connections\n"
          "  sshLX run TARGET COMMAND...      run a command, with its exit code\n"
          "  sshLX get TARGET REMOTE [LOCAL]  copy a file from TARGET, by SFTP\n"
          "  sshLX put TARGET LOCAL [REMOTE]  copy a file to TARGET, by SFTP\n"
          "  sshLX keys generate FILE         make a new ed25519 key pair\n"
          "  sshLX keys show FILE             show a key's fingerprint\n"
          "  sshLX keys install TARGET FILE.pub  add a public key to authorized_keys\n"
          "TARGET is a saved connection (Name or Folder/Name) or user@host:port.\n", to);
    return to == stderr ? 2 : 0;
}

int keyTool(const QStringList &args)
{
    const QString tool = QStandardPaths::findExecutable("ssh-keygen");
    if (tool.isEmpty()) {
        err(QStringLiteral("ssh-keygen is not installed"));
        return 1;
    }
    QProcess p;
    p.setProcessChannelMode(QProcess::ForwardedChannels);
    p.setInputChannelMode(QProcess::ForwardedInputChannel);
    p.start(tool, args);
    p.waitForFinished(-1);
    return p.exitCode();
}

} // namespace

bool isCliCommand(const char *word)
{
    for (const char *c : {"list", "run", "get", "put", "keys", "--help", "-h", "help"})
        if (!strcmp(word, c))
            return true;
    return false;
}

int runCli(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("sshLX");
    QCoreApplication::setApplicationName("sshLX");
    QStringList a = app.arguments();
    a.removeFirst();
    const QString cmd = a.takeFirst();
    if (cmd == "--help" || cmd == "-h" || cmd == "help")
        return usage(stdout);

    if (cmd == "keys" && a.size() == 2 && a[0] == "generate")
        return keyTool({"-t", "ed25519", "-f", a[1]});
    if (cmd == "keys" && a.size() == 2 && a[0] == "show")
        return keyTool({"-l", "-f", a[1]});

    QtHostLoop loop;
    if (!loop.initEngine([](void *, sshlx_global_event, const char *text) {
            if (text)
                err(QString::fromUtf8(text));
        }, nullptr))
        return 1;
    ConnectionStore store(ConnectionTree::defaultPath());
    QString error;
    if (!store.load(&error)) {
        err(error);
        return 1;
    }

    int rc = 2;
    Target t;
    const auto target = [&](const QString &text) {
        if (!resolve(store, text, &t))
            return false;
        if (!isSsh(t.conf)) {
            err(QStringLiteral("\"%1\" is not an SSH connection").arg(text));
            return false;
        }
        return true;
    };
    if (cmd == "list" && a.isEmpty()) {
        for (ConnectionNode *c : store.tree().connections()) {
            const QString folder = c->path();
            printf("%s\t%s\n", (folder.isEmpty() || folder == c->name ? c->name : folder + '/' + c->name)
                                   .toLocal8Bit().constData(),
                   store.hostOf(c).toLocal8Bit().constData());
        }
        rc = 0;
    } else if (cmd == "run" && a.size() >= 2) {
        const QString name = a.takeFirst();
        rc = target(name) ? runCommand(t, a.join(' ')) : 1;
    } else if (cmd == "get" && (a.size() == 2 || a.size() == 3)) {
        const QString local = a.size() == 3 ? a[2] : QFileInfo(a[1]).fileName();
        rc = target(a[0]) ? transfer(t, false, local, a[1]) : 1;
    } else if (cmd == "put" && (a.size() == 2 || a.size() == 3)) {
        const QString remote = a.size() == 3 ? a[2] : QFileInfo(a[1]).fileName();
        rc = target(a[0]) ? transfer(t, true, a[1], remote) : 1;
    } else if (cmd == "keys" && a.size() == 3 && a[0] == "install") {
        QFile f(a[2]);
        if (!f.open(QIODevice::ReadOnly)) {
            err(f.errorString());
            rc = 1;
        } else {
            const QString key = QString::fromUtf8(f.readLine()).trimmed();
            static const QRegularExpression plain("^(ssh-|ecdsa-|sk-)[A-Za-z0-9@.+/=_ -]+$");
            if (!plain.match(key).hasMatch()) {
                // It ends up in a shell command, so only a plain public key will do.
                err(QStringLiteral("%1 is not a public key").arg(a[2]));
                rc = 1;
            } else if (target(a[1])) {
                rc = runCommand(t, QStringLiteral("umask 077; mkdir -p ~/.ssh && printf '%s\\n' '%1' >> ~/.ssh/authorized_keys").arg(key));
            } else {
                rc = 1;
            }
        }
    } else {
        rc = usage(stderr);
    }
    if (t.conf)
        sshlx_conf_free(t.conf);
    sshlx_engine_run_pending();
    sshlx_engine_shutdown();
    return rc;
}
