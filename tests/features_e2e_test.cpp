// The main window with real sessions: Command Window, button bar, workspaces,
// logon actions, Zmodem through the terminal and Python scripts. Needs a Qt
// platform (offscreen will do). SSHLX_LRZSZ names lrzsz's programs for the
// Zmodem tests.
#include <QApplication>
#include <QFile>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QtTest>

#include <fcntl.h>
#include <pty.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <DockAreaWidget.h>
#include <DockManager.h>

#include "commandwindow.h"
#include "connectionstore.h"
#include "documentarea.h"
#include "keyring.h"
#include "logondialog.h"
#include "macros.h"
#include "mainwindow.h"
#include "qthostloop.h"
#include "scripthost.h"
#include "sessionwidget.h"

static QPushButton *button(QWidget *in, const QString &text)
{
    for (QPushButton *b : in->findChildren<QPushButton *>())
        if (b->isVisible() && b->text().remove('&') == text)
            return b;
    return nullptr;
}

static QString screen(SessionWidget *s)
{
    char *t = sshlx_session_all_text(s->sessionForTests());
    QString r = QString::fromUtf8(t ? t : "");
    sshlx_free(t);
    return r;
}

class FeaturesE2E : public QObject {
    Q_OBJECT
    QTemporaryDir dir;
    QString logPath;
    MainWindow *w = nullptr;

    QStringList received()
    {
        QFile f(logPath);
        if (!f.open(QIODevice::ReadOnly))
            return {};
        return QString::fromUtf8(f.readAll()).split('\n', Qt::SkipEmptyParts);
    }
    void closeAll()
    {
        for (SessionWidget *s : w->sessions())
            w->closeTabNow(s);
        QFile::remove(logPath);
    }
    SessionWidget *device()
    {
        SessionWidget *s = w->openLocalShell();
        [&] { QTRY_VERIFY(screen(s).contains("R1#")); }();
        return s;
    }
    QToolButton *barButton(const QString &text)
    {
        for (QToolButton *b : w->buttonBar()->findChildren<QToolButton *>())
            if (b->text() == text)
                return b;
        return nullptr;
    }
    ConnectionNode *localConnection(const QString &name)
    {
        // A saved session running the local shell, so the tree can open it.
        ConnectionStore *st = w->store();
        sshlx_conf *c = sshlx_conf_new_default();
        sshlx_conf_set_int(c, sshlx_conf_key_protocol(), SessionWidget::protocolId("localshell"));
        sshlx_conf_set_str(c, sshlx_conf_key_find("host"), "localhost");
        char *err = nullptr;
        sshlx_conf_save(c, name.toUtf8().constData(), &err);
        sshlx_free(err);
        sshlx_conf_free(c);
        st->syncSessions();
        return st->tree().findBySession(name);
    }

private slots:
    void initTestCase()
    {
        QVERIFY(dir.isValid());
        logPath = dir.filePath("device.log");
        qputenv("FAKE_IOS_LOG", logPath.toLocal8Bit());
        qputenv("SHELL", SSHLX_FAKE_IOS);
        QCoreApplication::setOrganizationName("sshLX-test");
        QSettings().clear();
        w = new MainWindow;
        w->resize(1100, 700);
        w->show();
    }
    void cleanup() { closeAll(); }

    void commandWindowSendsToAll()
    {
        SessionWidget *a = device(), *b = device();
        CommandWindow *c = w->commandWindow();
        c->setTarget(CommandWindow::All);
        QCOMPARE(c->targets().size(), 2);
        QCOMPARE(c->send("show clock"), 2);
        QTRY_COMPARE(received().count("show clock"), 2);
        Q_UNUSED(a) Q_UNUSED(b)
    }
    void commandWindowEnterKeyAndHistory()
    {
        device();
        CommandWindow *c = w->commandWindow();
        c->setTarget(CommandWindow::Current);
        c->show();
        QTest::keyClicks(c->input(), "show users");
        QTest::keyClick(c->input(), Qt::Key_Return);
        QTRY_VERIFY(received().contains("show users"));
        QVERIFY(c->input()->toPlainText().isEmpty());
        QTest::keyClick(c->input(), Qt::Key_Up);
        QCOMPARE(c->input()->toPlainText(), QString("show users"));
        c->input()->clear();
    }
    void commandWindowChosenTabsOnly()
    {
        SessionWidget *a = device();
        SessionWidget *b = device();
        CommandWindow *c = w->commandWindow();
        c->setTarget(CommandWindow::Chosen);
        c->setChosen({b});
        QCOMPARE(c->send("only b"), 1);
        QTRY_COMPARE(received().count("only b"), 1);
        QVERIFY(!screen(a).contains("only b"));
        QVERIFY(screen(b).contains("only b"));
    }
    void commandWindowSkipsDisconnected()
    {
        SessionWidget *a = device();
        SessionWidget *b = device();
        a->disconnectSession();
        CommandWindow *c = w->commandWindow();
        c->setTarget(CommandWindow::All);
        QCOMPARE(c->send("to the living"), 1);
        QTRY_COMPARE(received().count("to the living"), 1);
        Q_UNUSED(b)
    }
    void commandWindowSeveralLinesPause()
    {
        device();
        device();
        CommandWindow *c = w->commandWindow();
        c->setTarget(CommandWindow::All);
        QCOMPARE(c->send("line one\nline two"), 2);
        QTRY_COMPARE(received().count("line two"), 2);
    }

    void buttonBarRunsAndNests()
    {
        saveMacros({{"Clock", "show clock button", {}, {}},
                    {"Version", "show version button", "Cisco/Show", {}}});
        QTRY_VERIFY(barButton("Clock"));
        QToolButton *folder = barButton("Cisco");
        QVERIFY(folder);
        device();
        barButton("Clock")->click();
        QTRY_VERIFY(received().contains("show clock button"));
        QMenu *m = folder->menu();
        QVERIFY(m);
        QAction *show = m->actions().value(0);
        QCOMPARE(show->text(), QString("Show"));
        QAction *ver = show->menu()->actions().value(0);
        QCOMPARE(ver->text(), QString("Version"));
        ver->trigger();
        QTRY_VERIFY(received().contains("show version button"));
        saveMacros({});
    }
    void macroCanRunOnTargets()
    {
        device();
        device();
        CommandWindow *c = w->commandWindow();
        c->setTarget(CommandWindow::All);
        w->runMacro({"m", "macro to all", {}, {}}, true);
        QTRY_COMPARE(received().count("macro to all"), 2);
    }
    void macroKeepsFolderAndScript()
    {
        saveMacros({{"a", "x", "F/G", {}}, {"b", {}, {}, "/tmp/s.py"}});
        const QList<Macro> m = loadMacros();
        QCOMPARE(m[0].folder, QString("F/G"));
        QCOMPARE(m[1].script, QString("/tmp/s.py"));
        saveMacros({});
    }

    void workspaceSavesAndReplaces()
    {
        device();
        device();
        w->documents()->splitCurrent(Qt::Horizontal);
        QCOMPARE(w->saveWorkspace("Lab / one"), 0);
        QVERIFY(w->workspaces().contains("Lab / one"));
        closeAll();
        QCOMPARE(w->sessions().size(), 0);
        QVERIFY(w->openWorkspace("Lab / one", true));
        QTRY_COMPARE(w->sessions().size(), 2);
        for (SessionWidget *s : w->sessions())
            QTRY_VERIFY(screen(s).contains("R1#"));
        // Still split: the two tabs are in different dock areas.
        QVERIFY(w->documents()->dockFor(w->sessions()[0])->dockAreaWidget()
                != w->documents()->dockFor(w->sessions()[1])->dockAreaWidget());
    }
    void workspaceAddsToOpenTabs()
    {
        device();
        QCOMPARE(w->saveWorkspace("One"), 0);
        QVERIFY(w->openWorkspace("One", false));
        QCOMPARE(w->sessions().size(), 2);
        QVERIFY(w->removeWorkspace("One"));
        QVERIFY(!w->workspaces().contains("One"));
    }
    void workspaceCountsTabsLeftOut()
    {
        sshlx_conf *c = sshlx_conf_new_default();
        sshlx_conf_set_int(c, sshlx_conf_key_protocol(), SessionWidget::protocolId("raw"));
        sshlx_conf_set_str(c, sshlx_conf_key_find("host"), "127.0.0.1");
        w->openSession(c, "adhoc", {}, false);
        sshlx_conf_free(c);
        QCOMPARE(w->saveWorkspace("Adhoc"), 1);
        w->removeWorkspace("Adhoc");
    }

    void logonActionsRunAndUsePassword()
    {
        ConnectionNode *n = localConnection("logon-dev");
        QVERIFY(n);
        QList<Credential> creds{{"c1", "Enable", {}, {}}};
        w->store()->tree().setCredentials(creds);
        bool stored = false;
        Keyring::write(Keyring::credentialAccount("c1", false), "s3cret",
                       [&](bool ok, const QString &) { stored = ok; });
        QTRY_VERIFY(stored);
        n->logon = {{"R1#", false, "sudo-test", false, {}},
                    {"Password:", false, {}, true, "c1"},
                    {"R1#", false, "after-password", false, {}}};
        w->openConnection(n);
        QCOMPARE(w->sessions().size(), 1);
        SessionWidget *s = w->sessions().first();
        QTRY_VERIFY_WITH_TIMEOUT(received().contains("after-password"), 8000);
        QStringList r = received();
        QVERIFY(r.contains("sudo-test"));
        QVERIFY(r.contains("secret=s3cret"));
        QVERIFY(!screen(s).contains("s3cret"));     // the device does not echo it
        QTRY_VERIFY(!s->logonRunning());
        n->logon.clear();
        w->store()->tree().setCredentials({});
    }
    void logonPasswordNeedsAPrompt()
    {
        ConnectionNode *n = localConnection("logon-wrong");
        n->logon = {{"R1#", false, {}, true, ""}};
        w->openConnection(n);
        SessionWidget *s = w->sessions().first();
        QTRY_VERIFY(!s->logonRunning());
        QTest::qWait(100);
        for (const QString &l : received())
            QVERIFY(!l.contains("s3cret"));
        n->logon.clear();
    }
    void logonDialogRoundTrip()
    {
        const QList<LogonStep> in{{"a", true, "b", false, {}}, {"c", false, {}, true, "c9"}};
        LogonDialog d(in, {{"c9", "Nine", {}, {}}}, false, w);
        QCOMPARE(d.steps(), in);
    }
    void folderLogonIsInherited()
    {
        ConnectionTree &t = w->store()->tree();
        ConnectionNode *f = t.addFolder(nullptr, "F");
        ConnectionNode *c = t.addConnection(f, "c", "c");
        f->logon = {{"x", false, "y", false, {}}};
        QCOMPARE(t.resolveLogon(c).size(), 1);
        c->logon = {{"p", false, "q", false, {}}, {"r", false, "s", false, {}}};
        QCOMPARE(t.resolveLogon(c).size(), 2);
        // And they survive a save and load.
        const QJsonObject o = t.toJson();
        ConnectionTree copy;
        QVERIFY(copy.fromJson(o));
        QCOMPARE(copy.resolveLogon(copy.findById(c->id)).size(), 2);
        t.remove(f);
    }

    void zmodemThroughTheTerminal()
    {
        const QString tools = qEnvironmentVariable("SSHLX_LRZSZ");
        if (tools.isEmpty())
            QSKIP("SSHLX_LRZSZ is not set");
        QTemporaryDir work, down;
        QFile f(work.filePath("fw.bin"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        QByteArray data(200000, 0);
        for (int i = 0; i < data.size(); i++)
            data[i] = char((i * 31) ^ (i >> 8));
        f.write(data);
        f.close();
        QSettings().setValue("transfers/downloadDir", down.path());

        const QByteArray oldShell = qgetenv("SHELL");
        qputenv("SHELL", "/bin/sh");
        SessionWidget *s = w->openLocalShell();
        qputenv("SHELL", oldShell);
        QTRY_VERIFY(s->state() == SSHLX_STATE_CONNECTED);
        const QString cd = "cd " + work.path();
        s->sendCommand(cd);

        // sz starts, sshLX offers, the user accepts.
        bool clicked = false;
        auto *poll = new QTimer(this);
        connect(poll, &QTimer::timeout, this, [&] {
            if (QWidget *m = QApplication::activeModalWidget())
                if (QPushButton *b = button(m, "Receive")) {
                    b->click();
                    clicked = true;
                }
        });
        poll->start(20);
        s->sendCommand(tools + "/sz -b fw.bin");
        QTRY_VERIFY_WITH_TIMEOUT(clicked, 8000);
        poll->stop();
        QTRY_VERIFY_WITH_TIMEOUT(!s->transferRunning() && QFile::exists(down.filePath("fw.bin")), 30000);
        QFile g(down.filePath("fw.bin"));
        QVERIFY(g.open(QIODevice::ReadOnly));
        QVERIFY(g.readAll() == data);

        // The terminal is back in order.
        QTRY_VERIFY(s->canSendCommands());
        s->sendCommand("echo back-$((40+2))");
        QTRY_VERIFY_WITH_TIMEOUT(screen(s).contains("back-42"), 8000);
        delete poll;
    }
    void zmodemOfferDeclined()
    {
        const QString tools = qEnvironmentVariable("SSHLX_LRZSZ");
        if (tools.isEmpty())
            QSKIP("SSHLX_LRZSZ is not set");
        QTemporaryDir work;
        QFile f(work.filePath("x.bin"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("data");
        f.close();
        const QByteArray oldShell = qgetenv("SHELL");
        qputenv("SHELL", "/bin/sh");
        SessionWidget *s = w->openLocalShell();
        qputenv("SHELL", oldShell);
        QTRY_VERIFY(s->state() == SSHLX_STATE_CONNECTED);
        s->sendCommand("cd " + work.path());
        bool clicked = false;
        auto *poll = new QTimer(this);
        connect(poll, &QTimer::timeout, this, [&] {
            if (QWidget *m = QApplication::activeModalWidget())
                if (QPushButton *b = button(m, "Cancel")) {
                    b->click();
                    clicked = true;
                }
        });
        poll->start(20);
        s->sendCommand(tools + "/sz -b x.bin");
        QTRY_VERIFY_WITH_TIMEOUT(clicked, 8000);
        delete poll;
        QVERIFY(!s->transferRunning());
        s->sendCommand("echo still-alive-$((40+2))");
        QTRY_VERIFY_WITH_TIMEOUT(screen(s).contains("still-alive-42"), 8000);
    }
    void zmodemSendsThroughTheTerminal()
    {
        const QString tools = qEnvironmentVariable("SSHLX_LRZSZ");
        if (tools.isEmpty())
            QSKIP("SSHLX_LRZSZ is not set");
        QSettings().setValue("transfers/zmodemAuto", false);
        QTemporaryDir work, out;
        QFile f(work.filePath("cfg.bin"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        QByteArray data(50000, 0);
        for (int i = 0; i < data.size(); i++)
            data[i] = char(i * 7);
        f.write(data);
        f.close();
        const QByteArray oldShell = qgetenv("SHELL");
        qputenv("SHELL", "/bin/sh");
        SessionWidget *s = w->openLocalShell();
        qputenv("SHELL", oldShell);
        QTRY_VERIFY(s->state() == SSHLX_STATE_CONNECTED);
        s->sendCommand("cd " + out.path());
        QTest::qWait(100);
        s->sendCommand(tools + "/rz -b");
        QTest::qWait(300);
        QVERIFY(s->sendModem(int(3), {f.fileName()}));
        QTRY_VERIFY_WITH_TIMEOUT(!s->transferRunning(), 30000);
        QFile g(out.filePath("cfg.bin"));
        QTRY_VERIFY(g.exists());
        QVERIFY(g.open(QIODevice::ReadOnly));
        QVERIFY(g.readAll() == data);
        QSettings().setValue("transfers/zmodemAuto", true);
    }

    // The same over real SSH: uppity on stdio, so nothing listens anywhere.
    void zmodemOverRealSsh()
    {
        const QByteArray server = qgetenv("SSHLX_E2E_SFTP_COMMAND");
        const QString tools = qEnvironmentVariable("SSHLX_LRZSZ");
        if (server.isEmpty() || tools.isEmpty())
            QSKIP("no test SSH server");
        QTemporaryDir work, down;
        QFile f(work.filePath("fw.bin"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        QByteArray data(300000, 0);
        for (int i = 0; i < data.size(); i++)
            data[i] = char((i * 13) ^ (i >> 9));
        f.write(data);
        f.close();
        QSettings().setValue("transfers/downloadDir", down.path());
        sshlx_conf *c = sshlx_conf_new_default();
        sshlx_conf_set_int(c, sshlx_conf_key_protocol(), SessionWidget::protocolId("ssh"));
        sshlx_conf_set_str(c, sshlx_conf_key_find("host"), "testserver");
        sshlx_conf_set_int(c, sshlx_conf_key_find("port"), 22);
        const QByteArray user = qgetenv("SSHLX_E2E_SSH_USER");
        const QByteArray key = qgetenv("SSHLX_E2E_SSH_KEY");
        sshlx_conf_set_str(c, sshlx_conf_key_find("username"),
                           user.isEmpty() ? "tester" : user.constData());
        if (!key.isEmpty())
            sshlx_conf_set_str(c, sshlx_conf_key_find("keyfile"), key.constData());
        sshlx_conf_set_int(c, sshlx_conf_key_find("proxy_type"), 5);    // PROXY_CMD
        sshlx_conf_set_str(c, sshlx_conf_key_find("proxy_telnet_command"), server.constData());
        sshlx_conf_set_bool(c, sshlx_conf_key_find("even_proxy_localhost"), true);
        SessionWidget *s = w->openSession(c, "ssh");
        sshlx_conf_free(c);
        bool received = false;
        auto *poll = new QTimer(this);
        connect(poll, &QTimer::timeout, this, [&] {
            for (const char *label : {"Accept", "Connect Anyway", "Yes"})
                if (QPushButton *b = button(s, label))
                    b->click();
            if (QWidget *m = QApplication::activeModalWidget())
                if (QPushButton *b = button(m, "Receive")) {
                    b->click();
                    received = true;
                }
        });
        poll->start(20);
        QTRY_VERIFY_WITH_TIMEOUT(s->state() == SSHLX_STATE_CONNECTED, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(s->sendReady(), 15000);
        QTest::qWait(500);
        s->sendCommand("cd " + work.path());
        s->sendCommand(tools + "/sz -b fw.bin");
        QTRY_VERIFY_WITH_TIMEOUT(received, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!s->transferRunning() && QFile::exists(down.filePath("fw.bin")), 30000);
        delete poll;
        QFile g(down.filePath("fw.bin"));
        QVERIFY(g.open(QIODevice::ReadOnly));
        QVERIFY(g.readAll() == data);
        s->sendCommand("echo back-$((40+2))");
        QTRY_VERIFY_WITH_TIMEOUT(screen(s).contains("back-42"), 8000);
    }

    // rx says it is ready once, then not for many seconds. Starting the send
    // afterwards, as anyone does, must not sit through that.
    void senderCatchesReceiverAlreadyWaiting()
    {
        const QString tools = qEnvironmentVariable("SSHLX_LRZSZ");
        if (tools.isEmpty())
            QSKIP("SSHLX_LRZSZ is not set");
        QTemporaryDir work, out;
        QFile f(work.filePath("a.bin"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(3000, 'x'));
        f.close();
        const QByteArray oldShell = qgetenv("SHELL");
        qputenv("SHELL", "/bin/sh");
        SessionWidget *s = w->openLocalShell();
        qputenv("SHELL", oldShell);
        QTRY_VERIFY(s->state() == SSHLX_STATE_CONNECTED);
        s->sendCommand("cd " + out.path());
        s->sendCommand("stty raw -echo; " + tools + "/rx --xmodem -c got.bin 2>/dev/null");
        QTest::qWait(600);
        QVERIFY(s->sendModem(0, {f.fileName()}));
        // Well inside the 14 s rx would otherwise take to ask again.
        QTRY_VERIFY_WITH_TIMEOUT(!s->transferRunning(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(out.filePath("got.bin")), 3000);
        QVERIFY(QFileInfo(out.filePath("got.bin")).size() >= 3000);
    }

    // What a ROM monitor means: a serial line, with the far end on a pty.
    void modemOverSerialLine_data()
    {
        QTest::addColumn<int>("proto");
        QTest::addColumn<QString>("tool");
        QTest::addColumn<QStringList>("args");
        QTest::addRow("xmodem") << 0 << "rx" << QStringList{"--xmodem", "-c", "got.bin"};
        QTest::addRow("xmodem-1k") << 1 << "rx" << QStringList{"--xmodem", "-c", "got.bin"};
        QTest::addRow("ymodem") << 2 << "rb" << QStringList{"--ymodem"};
        QTest::addRow("zmodem") << 3 << "rz" << QStringList{"-b"};
    }
    void modemOverSerialLine()
    {
        const QString tools = qEnvironmentVariable("SSHLX_LRZSZ");
        if (tools.isEmpty())
            QSKIP("SSHLX_LRZSZ is not set");
        QFETCH(int, proto);
        QFETCH(QString, tool);
        QFETCH(QStringList, args);
        QSettings().setValue("transfers/zmodemAuto", false);
        QTemporaryDir work, out;
        QFile f(work.filePath("image.bin"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        QByteArray data(40000, 0);
        for (int i = 0; i < data.size(); i++)
            data[i] = char((i * 17) ^ (i >> 7));
        f.write(data);
        f.close();

        int master = -1, slave = -1;
        char name[128];
        QCOMPARE(openpty(&master, &slave, name, nullptr, nullptr), 0);
        const pid_t pid = fork();
        if (pid == 0) {
            if (chdir(out.path().toLocal8Bit().constData()) != 0)
                _exit(126);
            dup2(master, 0);
            dup2(master, 1);
            const int devnull = open("/dev/null", O_WRONLY);
            dup2(devnull, 2);
            for (int fd = 3; fd < 256; fd++)
                close(fd);
            QList<QByteArray> store{(tools + '/' + tool).toLocal8Bit()};
            for (const QString &a : args)
                store.append(a.toLocal8Bit());
            std::vector<char *> argv;
            for (QByteArray &a : store)
                argv.push_back(a.data());
            argv.push_back(nullptr);
            execv(argv[0], argv.data());
            _exit(127);
        }
        // Kept open: a real line stays up when the far end's program ends.

        sshlx_conf *c = sshlx_conf_new_default();
        sshlx_conf_set_int(c, sshlx_conf_key_protocol(), SessionWidget::protocolId("serial"));
        sshlx_conf_set_str(c, sshlx_conf_key_find("serline"), name);
        sshlx_conf_set_int(c, sshlx_conf_key_find("serspeed"), 115200);
        sshlx_conf_set_int(c, sshlx_conf_key_find("serflow"), 0);
        SessionWidget *s = w->openSession(c, "console");
        sshlx_conf_free(c);
        QTRY_VERIFY(s->state() == SSHLX_STATE_CONNECTED);
        QVERIFY(s->sendModem(proto, {f.fileName()}));
        QTRY_VERIFY_WITH_TIMEOUT(!s->transferRunning(), 40000);
        QVERIFY(s->canSendCommands());    // the terminal is back
        kill(pid, SIGKILL);
        waitpid(pid, nullptr, 0);
        close(master);
        close(slave);
        QFile g(out.filePath(proto < 2 ? "got.bin" : "image.bin"));
        QVERIFY2(g.open(QIODevice::ReadOnly), "nothing arrived");
        QByteArray got = g.readAll();
        if (proto < 2)
            got.truncate(data.size());
        QVERIFY(got == data);
        QSettings().setValue("transfers/zmodemAuto", true);
    }

    void pythonScriptDrivesTabs()
    {
        SessionWidget *s = device();
        QString out = dir.filePath("script.out");
        const QString path = SSHLX_TEST_SCRIPTS "/drive.py";
        qputenv("SSHLX_TEST_OUT", out.toLocal8Bit());
        QSignalSpy done(w->scripts(), &ScriptHost::scriptFinished);
        QVERIFY(w->scripts()->run(path));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 20000);
        QVERIFY2(done[0][1].toInt() == 0, qPrintable(w->scripts()->consoleText()));
        QFile o(out);
        QVERIFY(o.open(QIODevice::ReadOnly));
        const QJsonObject r = QJsonDocument::fromJson(o.readAll()).object();
        QCOMPARE(r["tabs"].toInt(), 1);
        QCOMPARE(r["state"].toString(), QString("connected"));
        QCOMPARE(r["found"].toString(), QString("R1#"));
        QVERIFY(r["missing"].toBool());
        QCOMPARE(r["regex"].toString(), QString("R1#"));
        QVERIFY(r["refused"].toString().contains("nothing"));
        QVERIFY(r["refused2"].toString().contains("password prompt"));
        QVERIFY(r["screen"].toBool());
        QVERIFY(w->scripts()->consoleText().contains("hello from the script"));
        QVERIFY(received().contains("show ip route"));
        Q_UNUSED(s)
    }
    void scriptNeedsTheToken()
    {
        const QString path = SSHLX_TEST_SCRIPTS "/token.py";
        QSignalSpy done(w->scripts(), &ScriptHost::scriptFinished);
        QVERIFY(w->scripts()->run(path));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 10000);
        QCOMPARE(done[0][1].toInt(), 0);
    }
    void stuckScriptCanBeStopped()
    {
        QString path = dir.filePath("stuck.py");
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("import time\ntime.sleep(100)\n");
        f.close();
        QVERIFY(w->scripts()->run(path));
        QCOMPARE(w->scripts()->runningCount(), 1);
        w->scripts()->stopAll();
        QCOMPARE(w->scripts()->runningCount(), 0);
    }
    void scriptOpensConnectionAndClosesIt()
    {
        localConnection("script-dev");
        const QString path = SSHLX_TEST_SCRIPTS "/open.py";
        QSignalSpy done(w->scripts(), &ScriptHost::scriptFinished);
        QVERIFY(w->scripts()->run(path));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 15000);
        QVERIFY2(done[0][1].toInt() == 0, qPrintable(w->scripts()->consoleText()));
        QCOMPARE(w->sessions().size(), 0);
    }
};

int main(int argc, char **argv)
{
    // Tests must not touch this user's PuTTY sessions, host keys or keyring.
    QTemporaryDir home;
    qputenv("HOME", home.path().toLocal8Bit());
    qputenv("PUTTYDIR", home.path().toLocal8Bit());    // PuTTY does not go by HOME alone
    qputenv("XDG_CONFIG_HOME", home.filePath("config").toLocal8Bit());
    Keyring::useMemoryForTests();
    QApplication app(argc, argv);
    QtHostLoop loop;
    if (!loop.initEngine([](void *, sshlx_global_event, const char *) {}, nullptr))
        return 1;
    FeaturesE2E t;
    int rc = QTest::qExec(&t, argc, argv);
    sshlx_engine_shutdown();
    return rc;
}
#include "features_e2e_test.moc"
