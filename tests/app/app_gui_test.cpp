// GUI-level tests for the Qt application, run on the offscreen platform.
#include <QAbstractButton>
#include <QApplication>
#include <QClipboard>
#include <QLabel>
#include <QLineEdit>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QSet>
#include <QListWidget>
#include <QTimer>
#include <QMenuBar>
#include <QSettings>
#include <QMimeData>
#include <QTreeView>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QElapsedTimer>
#include <QToolBar>
#include <DockAreaWidget.h>
#include <DockManager.h>
#include <DockWidget.h>
#include <QTreeWidget>
#include <QComboBox>
#include <QGridLayout>
#include <QtTest>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pty.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include "connectionspanel.h"
#include "connectionstore.h"
#include "connectiontreemodel.h"
#include "documentarea.h"
#include "propertydefs.h"
#include "propertygrid.h"
#include "theme.h"
#include "dialogkit.h"
#include "optionsdialog.h"
#include "icons.h"
#include "serialmanager.h"
#include "fontspec.h"
#include "mainwindow.h"
#include "promptbar.h"
#include "qthostloop.h"
#include "sessionwidget.h"
#include "settingsdialog.h"
#include "terminalview.h"

static int protoId(const char *name)
{
    for (size_t i = 0; i < sshlx_protocol_count(); i++)
        if (!strcmp(sshlx_protocol_get(i)->name, name))
            return sshlx_protocol_get(i)->id;
    return -1;
}

struct Pty {
    int master = -1;
    QString name;
    bool open()
    {
        int slave;
        char buf[256];
        if (openpty(&master, &slave, buf, nullptr, nullptr) != 0)
            return false;
        termios t;
        tcgetattr(master, &t);
        cfmakeraw(&t);
        tcsetattr(master, TCSANOW, &t);
        fcntl(master, F_SETFL, O_NONBLOCK);
        fcntl(master, F_SETFD, FD_CLOEXEC);
        ::close(slave);   // PuTTY opens the slave by name
        name = QString::fromUtf8(buf);
        return true;
    }
    void write(const QByteArray &b) { QVERIFY(::write(master, b.constData(), b.size()) == b.size()); }
    QByteArray readAll()
    {
        QByteArray out;
        char buf[4096];
        ssize_t n;
        while ((n = ::read(master, buf, sizeof(buf))) > 0)
            out.append(buf, int(n));
        return out;
    }
    ~Pty() { if (master >= 0) ::close(master); }
};

static sshlx_conf *serialConf(const QString &line)
{
    sshlx_conf *c = sshlx_conf_new_default();
    sshlx_conf_set_int(c, sshlx_conf_key_protocol(), protoId("serial"));
    sshlx_conf_set_str(c, sshlx_conf_key_find("SerialLine"), line.toUtf8().constData());
    return c;
}

class AppGuiTest : public QObject {
    Q_OBJECT
    QtHostLoop *m_loop = nullptr;
    MainWindow *m_main = nullptr;
    QTemporaryDir m_tmp;
    pid_t m_uppity = -1;

private slots:
    void initTestCase()
    {
        QVERIFY(m_tmp.isValid());
        // Never touch the user's real configuration (QSettings, QStandardPaths
        // and the connection tree all follow XDG_CONFIG_HOME).
        qputenv("PUTTYDIR", m_tmp.path().toUtf8());
        qputenv("XDG_CONFIG_HOME", m_tmp.filePath("config").toUtf8());
        // Serial detection runs against a fake sysfs + /dev tree.
        QDir().mkpath(m_tmp.filePath("sys/class/tty"));
        QDir().mkpath(m_tmp.filePath("dev/serial/by-id"));
        qputenv("SSHLX_SERIAL_SYSFS_ROOT", m_tmp.filePath("sys").toUtf8());
        qputenv("SSHLX_SERIAL_DEV_ROOT", m_tmp.filePath("dev").toUtf8());
        if (qEnvironmentVariableIsSet("SSHLX_SCREENSHOT_DIR")) {
            // sshLX's own dark theme, as on a dark desktop.
            QSettings().setValue("ui/theme", qEnvironmentVariable("SSHLX_SCREENSHOT_THEME", "1").toInt());
            QApplication::setFont(QFont("Ubuntu Sans", 11));
            Theme::apply();
            QApplication::setWindowIcon(Icons::app());
        }
        m_loop = new QtHostLoop;
        QVERIFY(m_loop->initEngine(nullptr, nullptr));
        m_main = new MainWindow;
        m_main->show();
    }

    void cleanupTestCase()
    {
        delete m_main;
        sshlx_engine_run_pending();
        sshlx_engine_shutdown();
        if (m_uppity > 0) {
            kill(m_uppity, SIGTERM);
            waitpid(m_uppity, nullptr, 0);
        }
    }

    // Visual check only (SSHLX_SCREENSHOT_DIR): the window as a user sees it
    // at first start, with a dark palette like a dark GNOME desktop.
    void screenshotStartWindow()
    {
        if (!qEnvironmentVariableIsSet("SSHLX_SCREENSHOT_DIR"))
            QSKIP("screenshots not requested");

        auto *w = new MainWindow;
        ConnectionNode *lin = w->store()->newFolder(nullptr, "Linux");
        lin->icon = "linux";
        w->store()->newConnection(lin, "web-01");
        w->store()->newConnection(lin, "db-01");
        ConnectionNode *win = w->store()->newFolder(nullptr, "Windows");
        win->icon = "windows";
        w->store()->newConnection(win, "jump-host");
        QList<ConnectionNode *> extra;
        for (auto [name, icon, child, childIcon] :
             {std::tuple{"Datacenter", "server", "db-cluster", "database"},
              std::tuple{"Network", "router", "core-switch", "network"},
              std::tuple{"Cloud", "cloud", "k8s-prod", "cluster"},
              std::tuple{"Lab", "lab", "pi-sensor", "embedded"}}) {
            ConnectionNode *f = w->store()->newFolder(nullptr, name);
            f->icon = icon;
            w->store()->newConnection(f, child)->icon = childIcon;
            extra << f;
        }
        w->resize(1280, 760);
        w->show();
        w->restoreStartupState();
        QTest::qWait(400);
        const QString dir = qEnvironmentVariable("SSHLX_SCREENSHOT_DIR");
        // A selected connection: the icon must stay visible on the selection.
        ConnectionNode *sel = w->store()->tree().findBySession("db-01");
        w->connectionsPanel()->select(sel);
        QTest::qWait(200);
        w->grab().save(dir + "/sshlx-start.png");
        if (QToolBar *tb = w->connectionsPanel()->findChild<QToolBar *>())
            tb->grab().save(dir + "/sshlx-toolbar.png");
        // The File menu, as it pops up.
        QMenu *file = w->menuBar()->actions().first()->menu();
        file->popup(w->mapToGlobal(QPoint(10, 30)));
        QTest::qWait(300);
        file->grab().save(dir + "/sshlx-menu.png");
        file->close();

        // Every tree icon, as a gallery and as the property grid's drop-down.
        {
            QWidget g;
            g.setAutoFillBackground(true);
            auto *grid = new QGridLayout(&g);
            grid->setContentsMargins(16, 16, 16, 16);
            grid->setHorizontalSpacing(18);
            const auto &icons = Icons::nodeIcons();
            for (int i = 1; i < icons.size(); i++) {
                auto *big = new QLabel;
                big->setPixmap(Icons::forNode(icons[i].key).pixmap(32, 32));
                auto *small = new QLabel;
                small->setPixmap(Icons::forNode(icons[i].key).pixmap(16, 16));
                int r = (i - 1) % 8, c = (i - 1) / 8 * 3;
                grid->addWidget(big, r, c);
                grid->addWidget(small, r, c + 1);
                grid->addWidget(new QLabel(icons[i].label), r, c + 2);
            }
            g.show();
            QTest::qWait(200);
            g.grab().save(dir + "/icons-gallery.png");
            QComboBox cb;
            QPixmap blank(16, 16);
            blank.fill(Qt::transparent);
            for (const auto &n : icons)
                cb.addItem(n.key.isEmpty() ? QIcon(blank) : Icons::forNode(n.key), n.label);
            cb.setMaxVisibleItems(16);
            cb.show();
            cb.showPopup();
            QTest::qWait(300);
            if (QWidget *popup = cb.view()->window())
                popup->grab().save(dir + "/icons-dropdown.png");
            cb.hidePopup();
        }

        auto grabModal = [dir](const QString &name) {
            QTimer::singleShot(400, [dir, name] {
                if (QWidget *m = QApplication::activeModalWidget()) {
                    m->grab().save(dir + "/" + name);
                    m->close();
                }
            });
        };
        {
            OptionsDialog o(w);
            o.show();
            QTest::qWait(300);
            o.grab().save(dir + "/dlg-options-general.png");
            o.findChild<QListWidget *>("Nav")->setCurrentRow(1);
            QTest::qWait(200);
            o.grab().save(dir + "/dlg-options-serial.png");
        }
        {
            sshlx_conf *c = sshlx_conf_new_default();
            SettingsDialog sd(c, false, 0, "sshLX Configuration");
            sd.show();
            QTest::qWait(300);
            sd.grab().save(dir + "/dlg-settings.png");
            sshlx_conf_free(c);
        }
        // Session tabs: connected, disconnected, and the dock tabs beside them.
        {
            Pty pty;
            QVERIFY(pty.open());
            sshlx_conf *c = serialConf(pty.name);
            SessionWidget *live = w->openSession(c, "web-01");
            sshlx_conf_free(c);
            c = serialConf(m_tmp.filePath("no-such-tty"));
            SessionWidget *dead = w->openSession(c, "/dev/ttyS4");
            sshlx_conf_free(c);
            QTRY_COMPARE(live->state(), SSHLX_STATE_CONNECTED);
            QTRY_COMPARE(dead->state(), SSHLX_STATE_DISCONNECTED);
            w->documents()->activate(dead);
            QTest::qWait(300);
            w->grab().save(dir + "/sshlx-tabs.png");
            w->documents()->activate(live);
            QTest::qWait(200);
            w->grab().save(dir + "/sshlx-tabs-live.png");
            for (SessionWidget *x : w->sessions())
                w->documents()->removeSession(x);
        }
        grabModal("dlg-confirm.png");
        DialogKit::confirm(w, "Close this session?",
                           "Are you sure you want to close this session?", "Close",
                           "Cancel", false, true);
        grabModal("dlg-about.png");
        w->showAbout();
        w->store()->remove(lin);
        w->store()->remove(win);
        for (ConnectionNode *f : extra)
            w->store()->remove(f);
        delete w;
    }

    void fontSpecRoundTrip()
    {
        auto p = FontSpec::fromPutty("client:DejaVu Sans Mono Bold 11");
        QCOMPARE(p.font.family(), QString("DejaVu Sans Mono"));
        QCOMPARE(p.font.weight(), QFont::Bold);
        QCOMPARE(p.font.pointSizeF(), 11.0);
        QCOMPARE(FontSpec::toPutty(p.font), QString("client:DejaVu Sans Mono Bold 11"));
        QVERIFY(FontSpec::fromPutty("server:fixed").serverFont);
        QCOMPARE(FontSpec::resized("client:Monospace 12", +1), QString("client:Monospace 13"));
    }

    void serialTyping()
    {
        Pty pty;
        QVERIFY(pty.open());
        sshlx_conf *c = serialConf(pty.name);
        SessionWidget *s = m_main->openSession(c, "serial-test");
        sshlx_conf_free(c);
        QTRY_COMPARE(s->state(), SSHLX_STATE_CONNECTED);
        pty.write("hello-from-device\r\n");
        QTRY_VERIFY(s->view()->screenText().contains("hello-from-device"));

        s->view()->setFocus();
        QTest::keyClicks(s->view(), "abc");
        QTest::keyClick(s->view(), Qt::Key_Return);
        QByteArray got;
        QTRY_VERIFY((got += pty.readAll()).contains("abc\r"));

        // Cursor keys: PuTTY sends ESC [ A in normal cursor-key mode.
        got.clear();
        QTest::keyClick(s->view(), Qt::Key_Up);
        QTRY_VERIFY((got += pty.readAll()).contains("\x1b[A"));
        // Backspace: PuTTY default is DEL (0x7f).
        got.clear();
        QTest::keyClick(s->view(), Qt::Key_Backspace);
        QTRY_VERIFY((got += pty.readAll()).contains('\x7f'));
        // Ctrl+C
        got.clear();
        QTest::keyClick(s->view(), Qt::Key_C, Qt::ControlModifier);
        QTRY_VERIFY((got += pty.readAll()).contains('\x03'));
        // F1 in PuTTY's default (ESC[n~) mode
        got.clear();
        QTest::keyClick(s->view(), Qt::Key_F1);
        QTRY_VERIFY((got += pty.readAll()).contains("\x1b[11~"));

        // Optional visual check: render attributes, colours, line drawing,
        // wide and combining characters, then save a screenshot.
        if (qEnvironmentVariableIsSet("SSHLX_SCREENSHOT_DIR")) {
            pty.write("\x1b[2J\x1b[H"
                      "\x1b[1mbold\x1b[0m \x1b[4munderline\x1b[0m \x1b[7mreverse\x1b[0m "
                      "\x1b[2mdim\x1b[0m \x1b[9mstrike\x1b[0m\r\n"
                      "\x1b[31mred \x1b[32mgreen \x1b[34mblue \x1b[38;2;255;128;0mtruecolour"
                      "\x1b[0m \x1b[48;5;27m 256-bg \x1b[0m\r\n"
                      "\x1b(0lqqqwqqqk\r\nx   x   x\r\nmqqqvqqqj\x1b(B\r\n"
                      "wide: \xe4\xb8\xad\xe6\x96\x87 combining: e\xcc\x81 \xc3\xa9\r\n"
                      "\x1b#6double width line\r\n"
                      "\x1b#3double height\r\n\x1b#4double height\r\n");
            QTest::qWait(400);
            QString dir = qEnvironmentVariable("SSHLX_SCREENSHOT_DIR");
            m_main->grab().save(dir + "/sshlx-terminal.png");
        }

        // Window title escape sequence follows to the tab.
        pty.write("\x1b]0;My Title\x07");
        QTRY_COMPARE(s->title(), QString("My Title"));
        m_main->findChild<DocumentArea *>()->removeSession(s);
    }

    void clipboardCopyPaste()
    {
        Pty pty;
        QVERIFY(pty.open());
        sshlx_conf *c = serialConf(pty.name);
        SessionWidget *s = m_main->openSession(c, "clip-test");
        sshlx_conf_free(c);
        QTRY_COMPARE(s->state(), SSHLX_STATE_CONNECTED);
        pty.write("copy-me-please\r\n");
        QTRY_VERIFY(s->view()->rowText(0).startsWith("copy-me-please"));
        QSize cell = s->view()->cellSize();
        int b = 1;
        QPoint from(b + 1, b + cell.height() / 2);
        QPoint to(b + cell.width() * 14 - 2, b + cell.height() / 2);
        QTest::mousePress(s->view(), Qt::LeftButton, {}, from);
        QMouseEvent move(QEvent::MouseMove, to, s->view()->mapToGlobal(to),
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(s->view(), &move);
        QTest::mouseRelease(s->view(), Qt::LeftButton, {}, to);
        QClipboard::Mode mode = QGuiApplication::clipboard()->supportsSelection()
            ? QClipboard::Selection : QClipboard::Clipboard;
        QTRY_COMPARE(QGuiApplication::clipboard()->text(mode), QString("copy-me-please"));

        // Paste with Shift+Insert (PuTTY default: paste the implicit clipboard).
        pty.readAll();
        QTest::keyClick(s->view(), Qt::Key_Insert, Qt::ShiftModifier);
        QByteArray got;
        QTRY_VERIFY((got += pty.readAll()).contains("copy-me-please"));
        m_main->findChild<DocumentArea *>()->removeSession(s);
    }

    void manyTabsIsolation()
    {
        const int N = 12;
        QList<Pty *> ptys;
        QList<SessionWidget *> tabs;
        for (int i = 0; i < N; i++) {
            auto *p = new Pty;
            QVERIFY(p->open());
            ptys << p;
            sshlx_conf *c = serialConf(p->name);
            tabs << m_main->openSession(c, QString("tab-%1").arg(i));
            sshlx_conf_free(c);
        }
        for (int i = 0; i < N; i++) {
            QTRY_COMPARE(tabs[i]->state(), SSHLX_STATE_CONNECTED);
            ptys[i]->write(QString("tab %1 says hi\r\n").arg(i).toUtf8());
        }
        // Background tabs keep their terminal state; it is drawn when shown.
        for (int i = 0; i < N; i++) {
            m_main->findChild<DocumentArea *>()->activate(tabs[i]);
            QTRY_VERIFY2(tabs[i]->view()->screenText().contains(QString("tab %1 says hi").arg(i)),
                         qPrintable(QString("tab %1 (%2x%3 visible=%4): [%5]").arg(i)
                                    .arg(tabs[i]->view()->width()).arg(tabs[i]->view()->height())
                                    .arg(tabs[i]->view()->isVisible())
                                    .arg(tabs[i]->view()->screenText().simplified().left(200))));
        }

        // Device 3 vanishes (hang-up), tab 5 is closed by the user.
        ::close(ptys[3]->master);
        ptys[3]->master = -1;
        QTRY_COMPARE(tabs[3]->state(), SSHLX_STATE_DISCONNECTED);
        m_main->findChild<DocumentArea *>()->removeSession(tabs[5]);
        QTest::qWait(200);
        for (int i = 0; i < N; i++) {
            if (i == 3 || i == 5)
                continue;
            ptys[i]->write(QString("still alive %1\r\n").arg(i).toUtf8());
        }
        for (int i = 0; i < N; i++) {
            if (i == 3 || i == 5)
                continue;
            m_main->findChild<DocumentArea *>()->activate(tabs[i]);
            QTRY_VERIFY2(tabs[i]->view()->screenText().contains(QString("still alive %1").arg(i)),
                         qPrintable(QString("tab %1 state=%2: [%3]").arg(i).arg(int(tabs[i]->state()))
                                    .arg(tabs[i]->view()->screenText().simplified().left(300))));
        }
        for (int i = 0; i < N; i++)
            if (i != 5)
                m_main->findChild<DocumentArea *>()->removeSession(tabs[i]);
        qDeleteAll(ptys);
    }

    void serialPermissionExplained()
    {
        if (geteuid() == 0)
            QSKIP("running as root");
        QString path = m_tmp.filePath("ttyDENIED");
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.close();
        ::chmod(path.toUtf8().constData(), 0);
        sshlx_conf *c = serialConf(path);
        SessionWidget *s = m_main->openSession(c, "denied");
        sshlx_conf_free(c);
        QTRY_COMPARE(s->state(), SSHLX_STATE_DISCONNECTED);
        bool found = false;
        for (QLabel *l : s->findChildren<QLabel *>())
            if (l->isVisibleTo(s) && l->text().contains("dialout"))
                found = true;
        QVERIFY(found);
        m_main->findChild<DocumentArea *>()->removeSession(s);
    }

    void settingsDialogDrivesConf()
    {
        sshlx_conf *c = sshlx_conf_new_default();
        SettingsDialog dlg(c, false, 0, "test");
        auto *tree = dlg.findChild<QTreeWidget *>("Nav");
        QVERIFY(tree);
        int panels = tree->findItems("*", Qt::MatchWildcard | Qt::MatchRecursive).size();
        QVERIFY2(panels >= 25, qPrintable(QString::number(panels)));
        // Modal buttons are custom-styled: no stock icons (no red/green glyphs).
        for (QPushButton *b : dlg.findChildren<QPushButton *>())
            QVERIFY2(b->icon().isNull(), qPrintable(b->text()));
        {
            QDialogButtonBox box(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
            for (QAbstractButton *b : box.buttons())
                QVERIFY2(b->icon().isNull(), qPrintable(b->text()));
        }
        QLineEdit *host = nullptr;
        for (QLabel *l : dlg.findChildren<QLabel *>())
            if (l->text().remove('&').startsWith("Host Name"))
                host = qobject_cast<QLineEdit *>(l->buddy());
        QVERIFY(host);
        host->setFocus();
        QTest::keyClicks(host, "gui.example");
        QCOMPARE(QString::fromUtf8(sshlx_conf_get_str(c, sshlx_conf_key_find("HostName"))),
                 QString("gui.example"));
        sshlx_conf_free(c);
    }

    void connectionTree()
    {
        ConnectionStore *store = m_main->store();
        // A PuTTY saved session created outside sshLX (e.g. by PuTTY).
        Pty pty;
        QVERIFY(pty.open());
        sshlx_conf *c = serialConf(pty.name);
        QVERIFY(sshlx_conf_save(c, "Lab Console", nullptr) == 0);
        sshlx_conf_free(c);
        store->syncSessions();   // what startup does: zero-step import
        ConnectionNode *n = store->tree().findBySession("Lab Console");
        QVERIFY(n);

        // Folder inheritance: the folder sets a username, the connection
        // inherits it; the launched Conf must carry it.
        ConnectionNode *folder = store->newFolder(nullptr, "Lab");
        sshlx_conf *fv = sshlx_conf_new_default();
        int userKey = sshlx_conf_key_find("username");
        sshlx_conf_set_str(fv, userKey, "labuser");
        char *exported = sshlx_conf_key_export(fv, userKey);
        store->setFolderProperty(folder, "username", QString::fromUtf8(exported));
        sshlx_free(exported);
        sshlx_conf_free(fv);

        // Move the connection into the folder via the model's drop path.
        ConnectionTreeModel *model = m_main->connectionsPanel()->model();
        QMimeData *md = model->mimeData({model->indexOf(n)});
        model->dropMimeData(md, Qt::MoveAction, -1, 0, model->indexOf(folder));
        delete md;
        QCOMPARE(n->parent, folder);
        store->setInherited(n, "username", true);
        sshlx_conf *eff = store->effectiveConf(n);
        QCOMPARE(QString::fromUtf8(sshlx_conf_get_str(eff, userKey)), QString("labuser"));
        sshlx_conf_free(eff);

        // Persisted to connections.json (in the test's config dir).
        ConnectionTree reread;
        QVERIFY(reread.load(store->path()));
        ConnectionNode *r = reread.findById(n->id);
        QVERIFY(r);
        QCOMPARE(r->parent->name, QString("Lab"));
        QCOMPARE(r->inherit, QStringList{"username"});

        // Double-click in the tree opens a tab for it.
        QTreeView *view = m_main->connectionsPanel()->view();
        m_main->connectionsPanel()->select(n);
        int before = m_main->sessions().size();
        QPoint at = view->visualRect(view->currentIndex()).center();
        QTest::mouseClick(view->viewport(), Qt::LeftButton, {}, at);
        QTest::mouseDClick(view->viewport(), Qt::LeftButton, {}, at);
        QTRY_COMPARE(m_main->sessions().size(), before + 1);
        SessionWidget *s = m_main->sessions().last();
        QCOMPARE(s->connectionId(), n->id);
        QTRY_COMPARE(s->state(), SSHLX_STATE_CONNECTED);
        pty.write("tree-opened\r\n");
        QTRY_VERIFY(s->view()->screenText().contains("tree-opened"));
        m_main->findChild<DocumentArea *>()->removeSession(s);

        // Rename renames the PuTTY session; delete removes it.
        QString err;
        QVERIFY2(store->rename(n, "Lab Console 2", &err), qPrintable(err));
        QVERIFY(store->savedSessions().contains("Lab Console 2"));
        QVERIFY(!store->savedSessions().contains("Lab Console"));
        store->remove(folder);
        QVERIFY(!store->savedSessions().contains("Lab Console 2"));
        QVERIFY(!store->tree().findById(n->id));
    }

    static const PropertyDef *findDef(const QString &key)
    {
        for (const PropertyDef &d : PropertyDefs::all())
            if (d.key == key && d.kind != PropertyDef::Colour)
                return &d;
        return nullptr;
    }

    static QModelIndex findRow(QAbstractItemModel *m, const QString &name,
                               const QModelIndex &parent = {})
    {
        for (int r = 0; r < m->rowCount(parent); r++) {
            QModelIndex i = m->index(r, 0, parent);
            if (i.data().toString() == name)
                return i;
            QModelIndex c = findRow(m, name, i);
            if (c.isValid())
                return c;
        }
        return {};
    }

    void propertyGrid()
    {
        ConnectionStore *store = m_main->store();
        PropertyGrid *grid = m_main->propertyGrid();
        // Every Conf key is reachable from the grid (curated or advanced),
        // except ordered preference lists, which use Full settings.
        QVERIFY2(PropertyDefs::all().size() >= 190,
                 qPrintable(QString::number(PropertyDefs::all().size())));
        QVERIFY(PropertyDefs::curated().size() >= 80);

        ConnectionNode *folder = store->newFolder(nullptr, "Grid Folder");
        ConnectionNode *conn = store->newConnection(folder, "Grid Conn");
        const PropertyDef *user = findDef("username");
        const PropertyDef *port = findDef("port");
        const PropertyDef *proto = findDef("protocol");
        const PropertyDef *wrap = findDef("wrap_mode");
        QVERIFY(user && port && proto && wrap);

        // Folder value set through the grid.
        grid->setNode(folder);
        grid->applyValue(*user, QString("folderuser"));
        QVERIFY(grid->folderDefines(*user));
        QVERIFY(folder->properties.contains("username"));

        // Connection inherits it.
        grid->setNode(conn);
        QVERIFY(!grid->isInherited(*user));
        grid->setInherited(*user, true);
        QVERIFY(grid->isInherited(*user));
        QCOMPARE(grid->editValue(*user).toString(), QString("folderuser"));
        sshlx_conf *eff = store->effectiveConf(conn);
        QCOMPARE(QString::fromUtf8(sshlx_conf_get_str(eff, sshlx_conf_key_find("username"))),
                 QString("folderuser"));
        sshlx_conf_free(eff);

        // Changing the folder value flows to the connection.
        grid->setNode(folder);
        grid->applyValue(*user, QString("changed"));
        grid->setNode(conn);
        QCOMPARE(grid->editValue(*user).toString(), QString("changed"));

        // Editing an inherited value makes it the connection's own.
        grid->applyValue(*user, QString("ownuser"));
        QVERIFY(!grid->isInherited(*user));
        bool exists = false;
        sshlx_conf *saved = sshlx_conf_load(conn->session.toUtf8().constData(), &exists);
        QVERIFY(exists);
        QCOMPARE(QString::fromUtf8(sshlx_conf_get_str(saved, sshlx_conf_key_find("username"))),
                 QString("ownuser"));
        sshlx_conf_free(saved);

        // Protocol switch moves the default port, as in PuTTY's dialog.
        int telnet = protoId("telnet");
        QCOMPARE(grid->editValue(*port).toInt(), 22);
        grid->applyValue(*proto, telnet);
        QCOMPARE(grid->editValue(*port).toInt(), 23);
        QCOMPARE(grid->displayValue(*proto), QString("Telnet"));

        // A real delegate edit on the Port cell, saved immediately.
        grid->setAlphabetical(false);
        QTreeView *view = grid->view();
        QModelIndex nameIdx = findRow(view->model(), "Port");
        QVERIFY(nameIdx.isValid());
        QModelIndex valIdx = nameIdx.sibling(nameIdx.row(), 1);
        view->setCurrentIndex(valIdx);
        view->edit(valIdx);
        QSpinBox *sb = view->findChild<QSpinBox *>();
        QVERIFY(sb);
        sb->setValue(2323);
        QTest::keyClick(sb, Qt::Key_Return);
        QTRY_COMPARE(grid->editValue(*port).toInt(), 2323);
        saved = sshlx_conf_load(conn->session.toUtf8().constData(), &exists);
        QCOMPARE(sshlx_conf_get_int(saved, sshlx_conf_key_find("port")), 2323);
        sshlx_conf_free(saved);

        // Bool via the checkable cell.
        bool w = grid->editValue(*wrap).toBool();
        QModelIndex wIdx = findRow(view->model(), wrap->name);
        auto *sm = qobject_cast<QStandardItemModel *>(view->model());
        sm->itemFromIndex(wIdx.sibling(wIdx.row(), 1))->setCheckState(w ? Qt::Unchecked : Qt::Checked);
        QTRY_COMPARE(grid->editValue(*wrap).toBool(), !w);

        // Alphabetical view lists the same properties without categories.
        grid->setAlphabetical(true);
        QVERIFY(findRow(view->model(), "Port").isValid());
        QVERIFY(!findRow(view->model(), "Connection").isValid());
        grid->setAlphabetical(false);

        store->remove(folder);
        grid->setNode(nullptr);
    }

    // Fake a USB serial adapter whose device node is a pty.
    void plugFake(const QString &tty, const QString &ptyPath, const QString &serial)
    {
        QString sys = m_tmp.filePath("sys"), dev = m_tmp.filePath("dev");
        QString usb = sys + "/devices/usb1/1-" + tty, intf = usb + "/1-" + tty + ":1.0";
        QDir().mkpath(intf + "/" + tty);
        auto w = [](const QString &p, const QByteArray &c) {
            QFile f(p);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(c);
        };
        w(usb + "/idVendor", "0403\n");
        w(usb + "/idProduct", "6001\n");
        w(usb + "/manufacturer", "FTDI\n");
        w(usb + "/product", "FT232R USB UART\n");
        w(usb + "/serial", serial.toUtf8() + "\n");
        QDir().mkpath(sys + "/class/tty/" + tty);
        QFile::remove(sys + "/class/tty/" + tty + "/device");
        QVERIFY(QFile::link(intf, sys + "/class/tty/" + tty + "/device"));
        QFile::remove(dev + "/" + tty);
        QVERIFY(QFile::link(ptyPath, dev + "/" + tty));
        QString byId = dev + "/serial/by-id/usb-FTDI_FT232R_USB_UART_" + serial + "-if00-port0";
        QFile::remove(byId);
        QVERIFY(QFile::link("../../" + tty, byId));
    }

    void unplugFake(const QString &tty)
    {
        QString sys = m_tmp.filePath("sys"), dev = m_tmp.filePath("dev");
        QDir(sys + "/class/tty/" + tty).removeRecursively();
        QFile::remove(dev + "/" + tty);
        for (const QFileInfo &fi : QDir(dev + "/serial/by-id").entryInfoList(QDir::System))
            if (!fi.exists())
                QFile::remove(fi.filePath());
    }

    void serialHotplug()
    {
        SerialManager *sm = m_main->serialManager();
        QVERIFY(sm);
        QCOMPARE(sm->ports()->backend(), QString("poll"));
        QCOMPARE(sm->ports()->count(), 0);
        auto *banner = m_main->findChild<QToolBar *>("DeviceBannerBar");
        QVERIFY(banner && !banner->isVisible());

        Pty pty;
        QVERIFY(pty.open());
        QElapsedTimer t;
        t.start();
        plugFake("ttyUSB7", pty.name, "A10KZ8QX");
        // Shown in the tree within about a second.
        QTRY_COMPARE_WITH_TIMEOUT(sm->ports()->count(), 1, 1500);
        qInfo("device detected after %lld ms", t.elapsed());
        QVERIFY(t.elapsed() <= 1200);
        const SerialPortList::Device d = sm->ports()->at(0);
        QCOMPARE(d.vidPid(), QString("0403:6001"));
        QCOMPARE(d.product, QString("FT232R USB UART"));
        QVERIFY(d.byId.endsWith("usb-FTDI_FT232R_USB_UART_A10KZ8QX-if00-port0"));
        // The tree's "Serial Ports" section lists it.
        auto *model = m_main->connectionsPanel()->model();
        QModelIndex section = model->index(model->rowCount() - 1, 0);
        QVERIFY(model->isSerialSection(section));
        QCOMPARE(model->rowCount(section), 1);
        QVERIFY(model->index(0, 0, section).data().toString().contains("/ttyUSB7"));

        // Non-modal banner; "Always connect" opens a tab and saves a rule.
        QTRY_VERIFY(banner->isVisible());
        QVERIFY(!QApplication::activeModalWidget());
        int before = m_main->sessions().size();
        banner->findChild<QPushButton *>("always")->click();
        QTRY_COMPARE(m_main->sessions().size(), before + 1);
        SessionWidget *s = m_main->sessions().last();
        QTRY_COMPARE(s->state(), SSHLX_STATE_CONNECTED);
        QCOMPARE(s->serialByIdPath(), d.byId);
        // Template: detected speed 8N1 (the fake device doesn't answer the
        // probe: the 9600 fallback), stable by-id path as the serial line.
        QCOMPARE(sshlx_conf_get_int(s->conf(), sshlx_conf_key_find("serspeed")), 9600);
        QCOMPARE(QString::fromUtf8(sshlx_conf_get_str(s->conf(), sshlx_conf_key_find("serline"))), d.byId);
        pty.write("from-usb-device\r\n");
        QTRY_VERIFY(s->view()->screenText().contains("from-usb-device"));
        const SerialRule *rule = sm->rules().match(d.ruleKey(), QString());
        QVERIFY(rule && rule->action == SerialRule::AutoConnect);
        QVERIFY(QFile::exists(SerialRules::defaultPath()));

        // Unplug: the tab disconnects (kept, with its reconnect bar).
        unplugFake("ttyUSB7");
        ::close(pty.master);
        pty.master = -1;
        QTRY_COMPARE(sm->ports()->count(), 0);
        QTRY_COMPARE(s->state(), SSHLX_STATE_DISCONNECTED);

        // Re-plug the same adapter (now behind a different pty): the tab
        // reconnects by itself through the by-id path, no new tab.
        Pty pty2;
        QVERIFY(pty2.open());
        int tabs = m_main->sessions().size();
        plugFake("ttyUSB7", pty2.name, "A10KZ8QX");
        QTRY_COMPARE_WITH_TIMEOUT(s->state(), SSHLX_STATE_CONNECTED, 2000);
        QCOMPARE(m_main->sessions().size(), tabs);
        pty2.write("back-again\r\n");
        QTRY_VERIFY(s->view()->screenText().contains("back-again"));

        // A second, unknown adapter: "Never ask" stores an ignore rule.
        Pty pty3;
        QVERIFY(pty3.open());
        plugFake("ttyUSB8", pty3.name, "OTHER001");
        QTRY_VERIFY(banner->isVisible());
        banner->findChild<QPushButton *>("never")->click();
        QTRY_VERIFY(!banner->isVisible());
        QCOMPARE(m_main->sessions().size(), tabs);
        bool ignored = false;
        for (const SerialRule &r : sm->rules().rules())
            if (r.key.contains("OTHER001") && r.action == SerialRule::Ignore)
                ignored = true;
        QVERIFY(ignored);

        unplugFake("ttyUSB8");
        unplugFake("ttyUSB7");
        m_main->findChild<DocumentArea *>()->removeSession(s);
    }

    void dockingLayout()
    {
        DocumentArea *docs = m_main->documents();
        auto *mgr = m_main->findChild<ads::CDockManager *>();
        QVERIFY(mgr);
        for (SessionWidget *s : docs->sessions())
            docs->removeSession(s);
        QTest::qWait(50);

        // Three sessions backed by saved PuTTY sessions (restorable).
        QList<Pty *> ptys;
        QList<SessionWidget *> tabs;
        for (int i = 0; i < 3; i++) {
            auto *p = new Pty;
            QVERIFY(p->open());
            ptys << p;
            sshlx_conf *c = serialConf(p->name);
            QString name = QString("dock-%1").arg(i);
            QVERIFY(sshlx_conf_save(c, name.toUtf8().constData(), nullptr) == 0);
            sshlx_conf_free(c);
            m_main->openSavedSession(name);
            tabs << m_main->sessions().last();
        }
        QCOMPARE(docs->sessions().size(), 3);
        auto sessionAreas = [&] {
            QSet<ads::CDockAreaWidget *> areas;
            for (SessionWidget *s : docs->sessions())
                if (auto *dw = docs->dockFor(s); dw && !dw->isFloating())
                    areas.insert(dw->dockAreaWidget());
            return areas.size();
        };
        QCOMPARE(sessionAreas(), 1);   // all tabs in one area

        // Split the current tab to the right: two areas side by side.
        docs->activate(tabs[2]);
        docs->splitCurrent(Qt::Horizontal);
        QCOMPARE(sessionAreas(), 2);
        QVERIFY(docs->dockFor(tabs[2])->dockAreaWidget() !=
                docs->dockFor(tabs[0])->dockAreaWidget());

        // Ctrl+Tab style cycling visits every tab across areas.
        QSet<SessionWidget *> seen;
        docs->activate(tabs[0]);
        for (int i = 0; i < 3; i++) {
            seen.insert(docs->currentSession());
            docs->nextTab(+1);
        }
        QCOMPARE(seen.size(), 3);

        // Every session still works after being moved.
        for (int i = 0; i < 3; i++) {
            docs->activate(tabs[i]);
            ptys[i]->write(QString("dock %1 ok\r\n").arg(i).toUtf8());
            QTRY_VERIFY(tabs[i]->view()->screenText().contains(QString("dock %1 ok").arg(i)));
        }

        if (qEnvironmentVariableIsSet("SSHLX_SCREENSHOT_DIR")) {
            m_main->resize(1280, 760);
            QTest::qWait(300);
            m_main->grab().save(qEnvironmentVariable("SSHLX_SCREENSHOT_DIR") + "/sshlx-docking.png");

            // README picture: staged shell / serial output in split tabs.
            const char *G = "\x1b[1;32m", *B = "\x1b[1;34m", *C = "\x1b[36m", *Y = "\x1b[33m",
                       *R = "\x1b[31m", *D = "\x1b[2m", *N = "\x1b[0m";
            auto prompt = [&](const char *host) {
                return QString("%1admin@%2%3:%4~%5$ ").arg(G, host, N, B, N);
            };
            ptys[0]->write(QString("\x1b]0;admin@web-01: ~\x07\x1b[2J\x1b[H"
                "%1uptime\r\n 10:42:17 up 41 days, load average: 0.08, 0.11\r\n"
                "%1ls -l /srv/www\r\ntotal 12\r\n"
                "drwxr-xr-x 4 www-data 4096 Oct 27 %2api%3\r\n"
                "drwxr-xr-x 6 www-data 4096 Oct 28 %2app%3\r\n"
                "-rwxr-xr-x 1 admin     812 Oct 21 %4deploy.sh%3\r\n"
                "%1systemctl status nginx | head -3\r\n"
                "%5\xe2\x97\x8f%3 nginx.service - nginx web server\r\n"
                "   Loaded: loaded (enabled)\r\n"
                "   Active: %5active (running)%3 since Mon 07:30\r\n"
                "%1df -h /\r\nFilesystem      Size  Used Avail Use% Mounted on\r\n"
                "/dev/vda1        78G   31G   44G  42% /\r\n%1")
                .arg(prompt("web-01"), B, N, G, "\x1b[32m").toUtf8());
            ptys[1]->write("\x1b]0;admin@db-01: ~\x07");
            ptys[2]->write(QString("\x1b]0;/dev/ttyUSB0 @ 115200\x07\x1b[2J\x1b[H"
                "%1U-Boot 2024.01 (Jan 08 2026)%2\r\n\r\n"
                "DRAM:  512 MiB\r\nCore:  42 devices, 18 uclasses\r\n"
                "Net:   eth0: ethernet@30be0000\r\n"
                "%3Hit any key to stop autoboot:  0%2\r\n"
                "Starting kernel ...\r\n\r\n"
                "%4[    0.000000]%2 Booting Linux on CPU 0x0\r\n"
                "%4[    1.204311]%2 usb 1-1: new USB device\r\n"
                "%4[    2.981005]%2 %5EXT4-fs: mounted with errors%2\r\n"
                "%4[    3.412870]%2 Reached target %6Multi-User%2\r\n\r\n"
                "edge-gw login: ").arg(Y, N, C, D, R, "\x1b[1m").toUtf8());
            ConnectionStore *st = m_main->store();
            ConnectionNode *prod = st->newFolder(nullptr, "Production");
            prod->icon = "linux";
            st->newConnection(prod, "web-01");
            st->newConnection(prod, "db-01");
            st->newConnection(prod, "backup-01");
            prod->icon = "server";
            ConnectionNode *lab = st->newFolder(nullptr, "Lab");
            lab->icon = "lab";
            st->newConnection(lab, "edge-gw");
            st->newConnection(lab, "jump-host");
            m_main->connectionsPanel()->select(st->tree().findBySession("web-01"));
            m_main->resize(1500, 860);
            docs->activate(tabs[0]);
            QTest::qWait(500);
            m_main->grab().save(qEnvironmentVariable("SSHLX_SCREENSHOT_DIR") + "/sshlx-showcase.png");
            st->remove(prod);
            st->remove(lab);
        }

        // Layout + tab list round trip (what quitting and restarting does).
        QByteArray layout = docs->saveLayout();
        // The crash breadcrumb lists the open tabs while running.
        QFile crumb(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) +
                    "/open-tabs.crash");
        QVERIFY(crumb.open(QIODevice::ReadOnly));
        QStringList entries = QString::fromUtf8(crumb.readAll()).split('\n', Qt::SkipEmptyParts);
        crumb.close();
        QCOMPARE(entries.size(), 3);
        QVERIFY(entries[0].contains("\tsession\tdock-"));
        QString area2Dock = docs->dockName(tabs[2]);
        for (SessionWidget *s : docs->sessions())
            docs->removeSession(s);
        QTest::qWait(50);
        QCOMPARE(m_main->restoreTabs(entries), 3);
        QVERIFY(docs->restoreLayout(layout));
        QCOMPARE(docs->sessions().size(), 3);
        QCOMPARE(sessionAreas(), 2);   // the split came back
        SessionWidget *restored2 = nullptr;
        for (SessionWidget *s : docs->sessions())
            if (docs->dockName(s) == area2Dock)
                restored2 = s;
        QVERIFY(restored2);
        QTRY_COMPARE(restored2->state(), SSHLX_STATE_CONNECTED);

        // Float a tab into its own window.
        docs->activate(restored2);
        docs->floatCurrent();
        QVERIFY(docs->dockFor(restored2)->isFloating());

        // The tab close button routes through the normal close path. (A
        // connected session would first ask PuTTY's close confirmation.)
        QVERIFY(!restored2->closeWarning().isEmpty());
        restored2->disconnectSession();
        QTRY_VERIFY(restored2->closeWarning().isEmpty());
        int before = docs->sessions().size();
        docs->dockFor(restored2)->requestCloseDockWidget();   // what the close button and middle-click do
        QTRY_COMPARE(docs->sessions().size(), before - 1);

        // Named panels: connections with the same "Panel" share an area.
        ConnectionStore *store = m_main->store();
        store->syncSessions();   // pick up the sessions saved above
        QList<ConnectionNode *> conns;
        for (int i = 0; i < 2; i++) {
            ConnectionNode *n = store->tree().findBySession(QString("dock-%1").arg(i));
            QVERIFY(n);
            n->panel = "Lab Panel";
            conns << n;
        }
        m_main->openConnection(conns[0]);
        SessionWidget *p0 = m_main->sessions().last();
        m_main->openConnection(conns[1]);
        SessionWidget *p1 = nullptr;
        for (SessionWidget *s : m_main->sessions())
            if (s->connectionId() == conns[1]->id)
                p1 = s;
        QVERIFY(p1);
        QCOMPARE(docs->dockFor(p0)->dockAreaWidget(), docs->dockFor(p1)->dockAreaWidget());

        // Opening a connection that has a tab brings that tab forward;
        // "Open in New Tab" (openConnection) always adds one.
        int count = m_main->sessions().size();
        m_main->showConnection(conns[0]);
        QCOMPARE(m_main->sessions().size(), count);
        QCOMPARE(docs->currentSession(), p0);
        m_main->openConnection(conns[0]);
        QCOMPARE(m_main->sessions().size(), count + 1);
        QSettings().setValue("behaviour/reuseTab", false);
        m_main->showConnection(conns[1]);
        QCOMPARE(m_main->sessions().size(), count + 2);
        QSettings().remove("behaviour/reuseTab");

        for (SessionWidget *s : docs->sessions())
            docs->removeSession(s);
        qDeleteAll(ptys);
        Q_UNUSED(mgr);
    }

    void sshPromptInTab()
    {
        QString key = m_tmp.filePath("host.ppk");
        QVERIFY(QProcess::execute(PUTTYGEN_PATH, {"-t", "ed25519", "-o", key,
                                                  "--new-passphrase", "/dev/null"}) == 0);
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ::bind(fd, reinterpret_cast<sockaddr *>(&a), sizeof(a));
        socklen_t len = sizeof(a);
        getsockname(fd, reinterpret_cast<sockaddr *>(&a), &len);
        int port = ntohs(a.sin_port);
        ::close(fd);
        m_uppity = fork();
        if (m_uppity == 0) {
            int dn = ::open("/dev/null", O_RDWR);
            dup2(dn, 0); dup2(dn, 1); dup2(dn, 2);
            setenv("SHELL", "/bin/sh", 1);
            execl(UPPITY_PATH, "uppity", "--listen", QByteArray::number(port).constData(),
                  "--hostkey", key.toUtf8().constData(), (char *)nullptr);
            _exit(127);
        }
        QTest::qWait(500);

        sshlx_conf *c = sshlx_conf_new_default();
        sshlx_conf_set_int(c, sshlx_conf_key_protocol(), protoId("ssh"));
        sshlx_conf_set_str(c, sshlx_conf_key_find("HostName"), "127.0.0.1");
        sshlx_conf_set_int(c, sshlx_conf_key_find("PortNumber"), port);
        sshlx_conf_set_str(c, sshlx_conf_key_find("UserName"), qgetenv("USER").constData());
        sshlx_conf_set_bool(c, sshlx_conf_key_find("AuthKI"), false);
        SessionWidget *s = m_main->openSession(c, "ssh-test");
        sshlx_conf_free(c);

        // The host-key question appears inside the tab, non-modally.
        auto *bar = s->findChild<PromptBar *>();
        QVERIFY(bar);
        QTRY_VERIFY_WITH_TIMEOUT(bar->isVisible(), 15000);
        QVERIFY(!QApplication::activeModalWidget());
        QPushButton *once = nullptr;
        for (QPushButton *b : bar->findChildren<QPushButton *>())
            if (b->text() == "Connect Once")
                once = b;
        QVERIFY(once);
        once->click();
        QTRY_VERIFY_WITH_TIMEOUT(s->view()->screenText().contains("password:"), 15000);
        s->view()->setFocus();
        QTest::keyClicks(s->view(), "weasel");
        QTest::keyClick(s->view(), Qt::Key_Return);
        QTRY_COMPARE_WITH_TIMEOUT(s->state(), SSHLX_STATE_CONNECTED, 15000);
        QTest::qWait(300);
        QTest::keyClicks(s->view(), "echo gui-$((6*7))");
        QTest::keyClick(s->view(), Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(s->view()->screenText().contains("gui-42"), 15000);
        m_main->findChild<DocumentArea *>()->removeSession(s);
    }
};

QTEST_MAIN(AppGuiTest)
#include "app_gui_test.moc"
