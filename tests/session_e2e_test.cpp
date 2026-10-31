// A real SessionWidget and engine, driving tests/fake_ios.py through a pty:
// mouse copy and paste, the paste guard, and a config push with an amended
// line. Needs a Qt platform (offscreen will do); run from CTest.
#include <QApplication>
#include <QClipboard>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QMenu>
#include <QPrinter>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QTimer>
#include <QtTest>

#include "configpushbar.h"
#include "connectionstore.h"
#include "credentialsdialog.h"
#include "keyring.h"
#include "promptbar.h"
#include "configpushdialog.h"
#include "highlightruledialog.h"
#include "highlightrules.h"
#include "macros.h"
#include "qthostloop.h"
#include "findbar.h"
#include "sessionwidget.h"
#include "terminalview.h"

static QPushButton *button(QWidget *in, const QString &text)
{
    for (QPushButton *b : in->findChildren<QPushButton *>())
        if (b->isVisible() && b->text().remove('&') == text)
            return b;
    return nullptr;
}

class SessionE2E : public QObject {
    Q_OBJECT
    QTemporaryDir dir;
    QString logPath;
    SessionWidget *s = nullptr;

    QStringList received()
    {
        QFile f(logPath);
        if (!f.open(QIODevice::ReadOnly))
            return {};
        return QString::fromUtf8(f.readAll()).split('\n', Qt::SkipEmptyParts);
    }
    QString screen()
    {
        char *t = sshlx_session_all_text(s->sessionForTests());
        QString r = QString::fromUtf8(t ? t : "");
        sshlx_free(t);
        return r;
    }
    QPoint cellPos(int col, int row)
    {
        QSize c = s->view()->cellSize();
        return QPoint(1 + col * c.width() + c.width() / 2, 1 + row * c.height() + c.height() / 2);
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
        sshlx_conf *c = sshlx_conf_new_default();
        sshlx_conf_set_int(c, sshlx_conf_key_protocol(), SessionWidget::protocolId("localshell"));
        s = new SessionWidget(c, "fake");
        sshlx_conf_free(c);
        s->resize(900, 520);
        s->show();
        s->connectSession();
        QTRY_VERIFY(s->state() == SSHLX_STATE_CONNECTED);
        QTRY_VERIFY(screen().contains("R1#"));
    }

    void selectCopiesAndRightClickPastes()
    {
        QWidget *v = s->view();
        QTest::mousePress(v, Qt::LeftButton, {}, cellPos(0, 0));
        QTest::mouseMove(v, cellPos(1, 0));
        QMouseEvent move(QEvent::MouseMove, QPointF(cellPos(1, 0)), QPointF(v->mapToGlobal(cellPos(1, 0))),
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(v, &move);
        QTest::mouseRelease(v, Qt::LeftButton, {}, cellPos(1, 0));
        QTRY_COMPARE(QApplication::clipboard()->text(), QString("R1"));

        QApplication::clipboard()->setText("pasted-by-right-click\r");
        QTest::mouseClick(v, Qt::RightButton, {}, cellPos(5, 5));
        QTRY_VERIFY(received().contains("pasted-by-right-click"));
    }

    void guardCountsNewlines()
    {
        QApplication::clipboard()->setText("one\ntwo\n");
        bool asked = false;
        QTimer::singleShot(300, this, [&] {
            QWidget *w = QApplication::activeModalWidget();
            asked = w != nullptr;
            if (w)
                button(w, "Cancel")->click();
        });
        s->paste();
        QTRY_VERIFY(asked);
        QTest::qWait(200);
        QVERIFY(!received().contains("one"));
    }

    void ctrlRightClickShowsMenu()
    {
        QString seen;
        QTimer::singleShot(200, this, [&] {
            if (auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget())) {
                for (QAction *a : menu->actions())
                    seen += a->text() + '|';
                menu->close();
            }
        });
        QTest::mouseClick(s->view(), Qt::RightButton, Qt::ControlModifier, cellPos(5, 5));
        QTRY_VERIFY(seen.contains("Paste Con&figuration"));
    }

    void pushStopsAmendsAndContinues()
    {
        const QStringList config = {"configure terminal", "interfce Gi0/1", " no shutdown",
                                    "! a comment", "", "banner motd ^", "Authorised use only",
                                    "^", "end", "write memory", "show version"};
        const int before = received().size();
        QTimer::singleShot(200, this, [&] {
            auto *d = qobject_cast<ConfigPushDialog *>(QApplication::activeModalWidget());
            QVERIFY(d);
            button(d, "Start")->click();
        });
        s->pasteConfiguration(config.join('\n'));
        auto *bar = s->findChild<ConfigPushBar *>();
        QTRY_VERIFY_WITH_TIMEOUT(button(bar, "Send Again"), 8000);
        QStringList got = received().mid(before);
        QCOMPARE(got.last(), QString("interfce Gi0/1"));   // stopped on the bad line
        const QString shot = qEnvironmentVariable("SSHLX_E2E_SHOT");
        if (!shot.isEmpty())
            s->grab().save(shot);

        auto *edit = bar->findChild<QLineEdit *>();
        edit->setText("interface Gi0/1");
        QVERIFY(button(bar, "Send Amended"));
        button(bar, "Send Amended")->click();
        QTRY_VERIFY2_WITH_TIMEOUT(button(bar, "Close"), qPrintable(screen().right(400)), 15000);

        got = received().mid(before);
        QCOMPARE(got, QStringList({"configure terminal", "interfce Gi0/1", "interface Gi0/1",
                                   " no shutdown", "banner motd ^", "end", "write memory",
                                   "show version"}));
        QVERIFY(screen().contains("line 2"));    // the pager was paged
        button(bar, "Close")->click();
    }

    void linkUnderCursor()
    {
        int found = 0;
        for (int r = 0; r < s->view()->rows() && !found; r++)
            for (int c = 0; c < s->view()->cols(); c++)
                if (!s->view()->linkAt(QPoint(c, r)).isEmpty()) {
                    QCOMPARE(s->view()->linkAt(QPoint(c, r)), QString("https://example.com/ios"));
                    found = 1;
                    break;
                }
        QVERIFY(found);
    }

    void keywordsAreColouredOnScreen()
    {
        auto pixels = [this](QRgb rgb) {
            const QImage img = s->view()->grab().toImage().convertToFormat(QImage::Format_RGB32);
            int n = 0;
            for (int y = 0; y < img.height(); y++)
                for (int x = 0; x < img.width(); x++)
                    n += (img.pixel(x, y) & 0xffffff) == (rgb & 0xffffff);
            return n;
        };
        const QRgb green = qRgb(0, 255, 0), box = qRgb(90, 0, 120);
        QCOMPARE(pixels(box), 0);
        HighlightRule up;
        up.pattern = "line";
        up.fg = QColor(green).name();
        up.bg = QColor(box).name();
        HighlightRule prompt;
        prompt.pattern = "[^\\s#]+#";
        prompt.regex = true;
        prompt.wholeWord = false;
        prompt.fg = "#ffa500";
        HighlightSet::save({up, prompt});
        SessionWidget::reloadHighlights();
        QTRY_VERIFY(pixels(box) > 0);
        QVERIFY(pixels(qRgb(255, 165, 0)) > 0);
        const QString shot = qEnvironmentVariable("SSHLX_E2E_SHOT3");
        if (!shot.isEmpty())
            s->view()->grab().save(shot);

        HighlightSet::setEnabled(false);
        SessionWidget::reloadHighlights();
        QTRY_COMPARE(pixels(box), 0);
        HighlightSet::setEnabled(true);
        HighlightSet::save({});
        SessionWidget::reloadHighlights();
    }

    void addKeywordStartsFromSelection()
    {
        QWidget *v = s->view();
        QTest::mouseDClick(v, Qt::LeftButton, {}, cellPos(0, 1));
        QString seen;
        QTimer::singleShot(200, this, [&] {
            auto *d = qobject_cast<HighlightRuleDialog *>(QApplication::activeModalWidget());
            if (d) {
                seen = d->rule().pattern;
                d->reject();
            }
        });
        s->addKeyword();
        QVERIFY(!seen.isEmpty());
    }

    void terminalMenuFunctions()
    {
        QVERIFY(screen().contains("Invalid input"));
        s->selectAll();
        QTRY_VERIFY(QApplication::clipboard()->text().contains("Invalid input"));

#ifdef SSHLX_HAVE_PRINT
        QPrinter printer;
        printer.setOutputFormat(QPrinter::PdfFormat);
        printer.setOutputFileName(dir.filePath("t.pdf"));
        s->printTo(&printer);
        QVERIFY(QFile(dir.filePath("t.pdf")).size() > 500);
#endif

        const QString serverTitle = s->title();
        s->setCustomTitle("core switch");
        QCOMPARE(s->title(), QString("core switch"));
        s->setCustomTitle(QString());
        QCOMPARE(s->title(), serverTitle);
    }

    void macroRunsItsLine()
    {
        s->runMacro("macro-line\n");
        QTRY_VERIFY(received().contains("macro-line"));
    }

    void findJumpsInsideTheTerminal()
    {
        auto pixels = [this](QRgb rgb) {
            const QImage img = s->view()->grab().toImage().convertToFormat(QImage::Format_RGB32);
            int n = 0;
            for (int y = 0; y < img.height(); y++)
                for (int x = 0; x < img.width(); x++)
                    n += (img.pixel(x, y) & 0xffffff) == (rgb & 0xffffff);
            return n;
        };
        s->runMacro("flood");
        QTRY_VERIFY(screen().contains("needle-299"));
        QTest::qWait(100);
        auto *scroll = s->findChild<QScrollBar *>();
        const int bottom = scroll->value();
        QVERIFY(bottom > 250);

        s->findInTerminal();
        auto *bar = s->findChild<FindBar *>();
        auto *edit = bar->findChild<QLineEdit *>();
        edit->setText("needle-7 ");
        auto *result = bar->findChildren<QLabel *>().last();
        QCOMPARE(result->text(), QString("1 of 1"));
        QVERIFY(scroll->value() < 50);                // jumped into the scrollback
        QTRY_VERIFY(pixels(qRgb(0xff, 0x92, 0x2b)) > 0);   // the match is marked on screen
        bool shown = false;
        for (int r = 0; r < s->view()->rows(); r++)
            shown = shown || s->view()->rowText(r).startsWith("needle-7 filler");
        QVERIFY(shown);
        const QString shot = qEnvironmentVariable("SSHLX_E2E_SHOT4");
        if (!shot.isEmpty())
            s->grab().save(shot);

        edit->setText("needle-1");                    // 1, 10-19, 100-199
        QCOMPARE(result->text().section(" of ", 1), QString("111"));
        const QString first = result->text();
        button(bar, "Previous")->click();
        QVERIFY(result->text() != first);
        button(bar, "Next")->click();
        QCOMPARE(result->text(), first);
        edit->setText("not-in-the-buffer");
        QCOMPARE(result->text(), QString("No matches"));

        button(bar, "Close")->click();
        QVERIFY(bar->isHidden());
        QCOMPARE(scroll->value(), scroll->maximum());
    }

    void saveTerminalTextWritesTheFile()
    {
        QString path;
        QTimer::singleShot(300, this, [&] {
            if (auto *d = qobject_cast<QFileDialog *>(QApplication::activeModalWidget())) {
                d->setDirectory(dir.path());
                path = d->selectedFiles().value(0);
                QMetaObject::invokeMethod(d, "accept");
            }
        });
        s->saveTerminalText();
        QVERIFY(path.startsWith(dir.path()));
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QVERIFY(f.readAll().contains("needle-299"));
    }

    void manageMacrosSavesThem()
    {
        MacroDialog d(nullptr);
        d.show();
        button(&d, "Add")->click();
        d.findChild<QLineEdit *>()->setText("first");
        d.findChild<QPlainTextEdit *>()->setPlainText("one\ntwo");
        button(&d, "Add")->click();
        d.findChild<QLineEdit *>()->setText("second");
        d.findChild<QPlainTextEdit *>()->setPlainText("three");
        button(&d, "Remove")->click();
        button(&d, "Add")->click();
        d.findChild<QLineEdit *>()->setText("kept");
        d.findChild<QPlainTextEdit *>()->setPlainText("four");
        button(&d, "Save")->click();
        const QList<Macro> m = loadMacros();
        QCOMPARE(m.size(), 2);
        QCOMPARE(m[0].name, QString("first"));
        QCOMPARE(m[0].text, QString("one\ntwo"));
        QCOMPARE(m[1].name, QString("kept"));
        QCOMPARE(m[1].text, QString("four"));
        saveMacros({});
    }

    void multilineMacroOpensPasteConfiguration()
    {
        QString shown;
        QTimer::singleShot(200, this, [&] {
            if (auto *d = qobject_cast<ConfigPushDialog *>(QApplication::activeModalWidget())) {
                shown = d->lines().join('|');
                d->reject();
            }
        });
        s->runMacro("alpha\nbeta\n");
        QCOMPARE(shown, QString("alpha|beta"));
        QVERIFY(!received().contains("alpha"));
    }

    void clearKeepsThePromptLine()
    {
        s->clearTerminal();
        QTRY_VERIFY(!screen().contains("Invalid input"));
        QVERIFY(screen().startsWith("R1#"));
        s->runMacro("after-clear");
        QTRY_VERIFY(received().contains("after-clear"));
        QTRY_VERIFY(screen().contains("after-clear"));
        QCOMPARE(screen().split('\n').first().trimmed(), QString("R1#after-clear"));
    }

    void scrollbackHolds128000Lines()
    {
        sshlx_conf *c = sshlx_conf_new_default();
        QCOMPARE(sshlx_conf_get_int(c, sshlx_conf_key_find("ScrollbackLines")), 128000);
        sshlx_conf_free(c);
        if (qEnvironmentVariableIsEmpty("SSHLX_E2E_BIG"))
            return;      // slow: fills the whole buffer and times a search
        s->runMacro("flood 140000");
        auto *scroll = s->findChild<QScrollBar *>();
        QElapsedTimer fill;
        fill.start();
        QTRY_VERIFY_WITH_TIMEOUT(scroll->maximum() >= 127900, 120000);
        QTRY_VERIFY_WITH_TIMEOUT(s->view()->rowText(s->view()->rows() - 2).contains("needle-139999")
                                 || scroll->maximum() >= 128000, 120000);
        QTest::qWait(1000);
        qInfo() << "fill ms" << fill.elapsed() << "scroll max" << scroll->maximum();
        QElapsedTimer t;
        t.start();
        s->findInTerminal();     // the bar still holds the last text: reads the whole buffer
        qInfo() << "full scan ms" << t.elapsed();
        auto *bar = s->findChild<FindBar *>();
        t.restart();
        bar->findChild<QLineEdit *>()->setText("needle-13");
        qInfo() << "first search ms" << t.elapsed()
                << bar->findChildren<QLabel *>().last()->text();
        t.restart();
        bar->findChild<QLineEdit *>()->setText("needle-139");
        qInfo() << "second search ms" << t.elapsed()
                << bar->findChildren<QLabel *>().last()->text();
        button(bar, "Close")->click();
    }

    void savedPasswordAnswersPromptsInsideTheSession()
    {
        Keyring::useMemoryForTests();
        Keyring::write("credential:sudo", "hidden-pw-1");
        s->setSavedPasswords([] {
            return QList<SessionWidget::SavedPassword>{{"linux", "credential:sudo"}};
        });
        QMenu menu;
        s->buildContextMenu(&menu);
        QAction *entry = nullptr;
        for (QAction *a : menu.actions())
            if (a->text().remove('&') == "Send Saved Password")
                entry = a;
        QVERIFY(entry && entry->isEnabled());
        QCOMPARE(entry->menu()->actions().first()->text(), QString("linux"));

        // At a shell prompt it asks first, and Cancel sends nothing.
        const int before = received().size();
        bool asked = false;
        QTimer::singleShot(200, this, [&] {
            QWidget *w = QApplication::activeModalWidget();
            asked = w != nullptr;
            if (w)
                button(w, "Cancel")->click();
        });
        s->sendSavedPassword("linux", "credential:sudo");
        QTRY_VERIFY(asked);
        QTest::qWait(200);
        QCOMPARE(received().size(), before);

        // At a hidden prompt it goes straight in.
        s->runMacro("sudo-test");
        QTRY_VERIFY(s->view()->rowText(s->view()->cursorRow()).contains("Password:"));
        s->sendSavedPassword("linux", "credential:sudo");
        QTRY_VERIFY(received().contains("secret=hidden-pw-1"));
        QTRY_VERIFY(screen().contains("authenticated"));
        QVERIFY(!screen().contains("hidden-pw-1"));

        // Paused on the same question during a push: the password answers it,
        // stays out of the field and the report, and the push carries on.
        QTimer::singleShot(200, this, [&] {
            button(QApplication::activeModalWidget(), "Start")->click();
        });
        s->pasteConfiguration("sudo-test\nend");
        auto *bar = s->findChild<ConfigPushBar *>();
        QTRY_VERIFY_WITH_TIMEOUT(button(bar, "Saved Password"), 8000);
        QCOMPARE(bar->findChild<QLineEdit *>()->echoMode(), QLineEdit::Password);
        Keyring::write("credential:sudo", "hidden-pw-2");
        s->sendSavedPassword("linux", "credential:sudo");
        QTRY_VERIFY(received().contains("secret=hidden-pw-2"));
        QTRY_VERIFY_WITH_TIMEOUT(button(bar, "Close"), 8000);
        QCOMPARE(received().last(), QString("end"));
        QVERIFY(!screen().contains("hidden-pw-2"));
        button(bar, "Copy Report")->click();
        QVERIFY(!QApplication::clipboard()->text().contains("hidden-pw"));
        button(bar, "Close")->click();
    }

    void credentialRotatesEveryLinkedConnection()
    {
        ConnectionStore store(dir.filePath("connections.json"));
        store.load();
        ConnectionNode *folder = store.newFolder(nullptr, "core");
        ConnectionNode *r1 = store.newConnection(folder, "r1");
        ConnectionNode *r2 = store.newConnection(folder, "r2");
        ConnectionNode *other = store.newConnection(nullptr, "other");
        auto field = [](CredentialsDialog &d, int i) { return d.findChildren<QLineEdit *>()[i]; };
        auto secret = [](const QString &account) {
            QString out;
            Keyring::read(account, [&](bool found, const QString &pw) {
                out = found ? pw : QStringLiteral("<none>");
            });
            return out;
        };
        auto user = [&](ConnectionNode *n) {
            sshlx_conf *c = store.effectiveConf(n);
            QString u = QString::fromUtf8(sshlx_conf_get_str(c, sshlx_conf_key_find("username")));
            sshlx_conf_free(c);
            return u;
        };
        auto keyFile = [&](ConnectionNode *n) {
            sshlx_conf *c = store.effectiveConf(n);
            QString k = QString::fromUtf8(sshlx_conf_get_str(c, sshlx_conf_key_find("keyfile")));
            sshlx_conf_free(c);
            return k;
        };

        QString id;
        {
            CredentialsDialog d(&store, nullptr);
            d.show();
            button(&d, "Add")->click();
            field(d, 0)->setText("netops");
            field(d, 1)->setText("admin");
            field(d, 2)->setText("first-secret");
            field(d, 3)->setText("/keys/ops.ppk");
            field(d, 4)->setText("key-phrase");
            auto *tree = d.findChild<QTreeWidget *>();
            QTreeWidgetItem *folderItem = tree->findItems("core", Qt::MatchExactly)[0];
            folderItem->setCheckState(0, Qt::Checked);
            button(&d, "Save")->click();
        }
        QCOMPARE(store.tree().credentials().size(), 1);
        id = store.tree().credentials()[0].id;
        QCOMPARE(store.tree().linkedConnections(id), QList<ConnectionNode *>({r1, r2}));
        QCOMPARE(user(r1), QString("admin"));
        QCOMPARE(user(r2), QString("admin"));
        QVERIFY(keyFile(r1).contains("/keys/ops.ppk"));
        QVERIFY(user(other).isEmpty());
        QCOMPARE(secret(Keyring::credentialAccount(id, false)), QString("first-secret"));
        QCOMPARE(secret(Keyring::credentialAccount(id, true)), QString("key-phrase"));

        // Nothing secret on disk, and nothing shown back in the form.
        QFile json(dir.filePath("connections.json"));
        QVERIFY(json.open(QIODevice::ReadOnly));
        const QByteArray onDisk = json.readAll();
        QVERIFY(onDisk.contains("admin"));
        QVERIFY(!onDisk.contains("first-secret"));
        QVERIFY(!onDisk.contains("key-phrase"));

        {   // Rotation: one field, and the links stay.
            CredentialsDialog d(&store, nullptr);
            d.show();
            QVERIFY(field(d, 2)->text().isEmpty());
            field(d, 2)->setText("rotated-secret");
            field(d, 1)->setText("admin2");
            button(&d, "Save")->click();
        }
        QCOMPARE(secret(Keyring::credentialAccount(id, false)), QString("rotated-secret"));
        QCOMPARE(secret(Keyring::credentialAccount(id, true)), QString("key-phrase"));
        QCOMPARE(user(r1), QString("admin2"));
        QCOMPARE(user(r2), QString("admin2"));

        {   // Cancel changes nothing.
            CredentialsDialog d(&store, nullptr);
            d.show();
            field(d, 2)->setText("never-saved");
            button(&d, "Cancel")->click();
        }
        QCOMPARE(secret(Keyring::credentialAccount(id, false)), QString("rotated-secret"));

        {   // Delete: the links go, and so do the secrets.
            CredentialsDialog d(&store, nullptr);
            d.show();
            QTimer::singleShot(200, this, [] {
                if (QWidget *w = QApplication::activeModalWidget())
                    if (w->windowTitle().contains("Delete"))
                        button(w, "Delete")->click();
            });
            button(&d, "Delete")->click();
            button(&d, "Save")->click();
        }
        QVERIFY(store.tree().credentials().isEmpty());
        QVERIFY(user(r1).isEmpty());
        QVERIFY(folder->credential.isEmpty());
        QCOMPARE(secret(Keyring::credentialAccount(id, false)), QString("<none>"));
        QCOMPARE(secret(Keyring::credentialAccount(id, true)), QString("<none>"));
    }

    // Needs tests' throwaway SSH server: SSHLX_E2E_SSH_PORT, SSHLX_E2E_SSH_KEY.
    void realLoginGivesEachPromptItsOwnSecret()
    {
        const QByteArray port = qgetenv("SSHLX_E2E_SSH_PORT");
        if (port.isEmpty())
            QSKIP("no test SSH server");
        sshlx_conf *c = sshlx_conf_new_default();
        sshlx_conf_set_int(c, sshlx_conf_key_protocol(), SessionWidget::protocolId("ssh"));
        sshlx_conf_set_str(c, sshlx_conf_key_find("host"), "127.0.0.1");
        sshlx_conf_set_int(c, sshlx_conf_key_find("port"), port.toInt());
        sshlx_conf_set_str(c, sshlx_conf_key_find("username"), "admin");
        sshlx_conf_set_str(c, sshlx_conf_key_find("keyfile"), qgetenv("SSHLX_E2E_SSH_KEY").constData());
        SessionWidget w(c, "ssh");
        sshlx_conf_free(c);
        QStringList asked;
        QString password = "first-secret";
        w.setPasswordLookup([&](const QString &, const QString &, int, bool passphrase,
                                std::function<void(bool, const QString &)> done) {
            asked << (passphrase ? "passphrase" : "password");
            done(true, passphrase ? QStringLiteral("key-phrase") : password);
        });
        w.resize(800, 400);
        w.show();
        w.connectSession();
        auto *bar = w.findChild<PromptBar *>();
        auto screenText = [&] {
            char *t = sshlx_session_all_text(w.sessionForTests());
            QString r = QString::fromUtf8(t ? t : "");
            sshlx_free(t);
            return r;
        };
        QTRY_VERIFY_WITH_TIMEOUT(button(bar, "Connect Once"), 10000);
        button(bar, "Connect Once")->click();
        QTRY_VERIFY_WITH_TIMEOUT(screenText().contains("logged in with first-secret"), 10000);
        QCOMPARE(asked, QStringList({"passphrase", "password"}));

        // Rotated while the tab is open: the next login uses the new one.
        w.disconnectSession();
        QTRY_VERIFY_WITH_TIMEOUT(w.state() == SSHLX_STATE_DISCONNECTED, 5000);
        password = "rotated-secret";
        asked.clear();
        w.reconnect();
        QTRY_VERIFY_WITH_TIMEOUT(button(bar, "Connect Once") || screenText().contains("rotated-secret"), 10000);
        if (QPushButton *once = button(bar, "Connect Once"))
            once->click();
        QTRY_VERIFY_WITH_TIMEOUT(screenText().contains("logged in with rotated-secret"), 10000);
        QCOMPARE(asked, QStringList({"passphrase", "password"}));
    }

    void cleanupTestCase()
    {
        delete s;
        sshlx_engine_run_pending();
        QSettings().clear();
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
    SessionE2E t;
    int rc = QTest::qExec(&t, argc, argv);
    sshlx_engine_shutdown();
    return rc;
}
#include "session_e2e_test.moc"
