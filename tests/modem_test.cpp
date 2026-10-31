// Xmodem, Ymodem and Zmodem: against each other over a loop, with a damaged
// line, and, when SSHLX_LRZSZ names a directory with lrzsz's sz, rz, sx, rx,
// sb and rb, against those real programs through a pty.
#include <QProcessEnvironment>
#include <QRandomGenerator>
#include <QSocketNotifier>
#include <QTemporaryDir>
#include <QtTest>

#include <cerrno>
#include <fcntl.h>
#include <pty.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include "modemtransfer.h"

using P = ModemTransfer::Protocol;

static QByteArray allBytes(int size)
{
    QByteArray b(size, '\0');
    QRandomGenerator rng(42);
    for (int i = 0; i < size; i++)
        b[i] = i < 256 ? char(i) : char(rng.bounded(256));
    return b;
}

static void writeFile(const QString &path, const QByteArray &data)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
}

static QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

// One program on a pty, in raw mode, as it would be behind a session.
class Pty : public QObject {
    Q_OBJECT
public:
    int fd = -1;
    pid_t pid = -1;
    std::function<void(const QByteArray &)> onData;
    QSocketNotifier *reader = nullptr;
    QByteArray pending;

    bool start(const QString &cwd, const QStringList &argv)
    {
        // A real speed: lrzsz paces itself by it, and crawls without one.
        struct termios t {};
        cfmakeraw(&t);
        cfsetspeed(&t, B115200);
        pid = forkpty(&fd, nullptr, &t, nullptr);
        if (pid < 0)
            return false;
        if (pid == 0) {
            if (chdir(cwd.toLocal8Bit().constData()) != 0)
                _exit(126);
            // Their progress messages would share the line with the data.
            dup2(open("/dev/null", O_WRONLY), 2);
            QList<QByteArray> store;
            std::vector<char *> args;
            for (const QString &a : argv)
                store.append(a.toLocal8Bit());
            for (QByteArray &a : store)
                args.push_back(a.data());
            args.push_back(nullptr);
            execv(args[0], args.data());
            _exit(127);
        }
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
        reader = new QSocketNotifier(fd, QSocketNotifier::Read, this);
        connect(reader, &QSocketNotifier::activated, this, [this] {
            char buf[65536];
            const ssize_t n = read(fd, buf, sizeof buf);
            if (n > 0) {
                if (onData)
                    onData(QByteArray(buf, n));
            } else if (n == 0 || errno != EAGAIN) {
                reader->setEnabled(false);
            }
        });
        auto *writer = new QTimer(this);
        connect(writer, &QTimer::timeout, this, [this] { flush(); });
        writer->start(1);
        return true;
    }
    void write(const QByteArray &d)
    {
        pending += d;
        flush();
    }
    void flush()
    {
        while (!pending.isEmpty()) {
            const ssize_t n = ::write(fd, pending.constData(), size_t(pending.size()));
            if (n <= 0)
                return;
            pending.remove(0, n);
        }
    }
    ~Pty() override
    {
        if (pid > 0) {
            kill(pid, SIGKILL);
            waitpid(pid, nullptr, 0);
        }
        if (fd >= 0)
            close(fd);
    }
};

class ModemTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir;
    QString tool(const QString &name)
    {
        const QString d = qEnvironmentVariable("SSHLX_LRZSZ");
        return d.isEmpty() ? QString() : d + '/' + name;
    }

    // a sends, b receives; damage may change bytes on the way.
    void loop(P proto, int size, std::function<QByteArray(QByteArray)> damage = {})
    {
        QTemporaryDir src, dst;
        const QString in = src.filePath("image.bin");
        const QByteArray data = allBytes(size);
        writeFile(in, data);
        ModemTransfer a, b;
        connect(&a, &ModemTransfer::send, &b, [&](const QByteArray &d) {
            const QByteArray x = damage ? damage(d) : d;
            QTimer::singleShot(0, &b, [&b, x] { b.feed(x.constData(), size_t(x.size())); });
        });
        connect(&b, &ModemTransfer::send, &a, [&](const QByteArray &d) {
            QTimer::singleShot(0, &a, [&a, d] { a.feed(d.constData(), size_t(d.size())); });
        });
        a.setTimeoutMs(300);
        b.setTimeoutMs(300);
        bool aDone = false, bDone = false, aOk = false, bOk = false;
        QString aMsg, bMsg;
        connect(&a, &ModemTransfer::finished, this, [&](bool ok, const QString &m) {
            aDone = true; aOk = ok; aMsg = m;
        });
        connect(&b, &ModemTransfer::finished, this, [&](bool ok, const QString &m) {
            bDone = true; bOk = ok; bMsg = m;
        });
        const QString xname = "got.bin";
        b.startReceive(proto, dst.path(), proto == P::Xmodem || proto == P::Xmodem1k ? xname : QString());
        a.startSend(proto, {in});
        QTRY_VERIFY2_WITH_TIMEOUT(aDone && bDone, qPrintable(aMsg + " / " + bMsg), 30000);
        QVERIFY2(aOk, qPrintable(aMsg));
        QVERIFY2(bOk, qPrintable(bMsg));
        QCOMPARE(b.savedFiles().size(), 1);
        QByteArray got = readFile(b.savedFiles().first());
        if (proto == P::Xmodem || proto == P::Xmodem1k) {
            QVERIFY(got.size() >= data.size());
            QVERIFY(got.mid(data.size()).count(char(0x1a)) == got.size() - data.size());
            got.truncate(data.size());
        }
        QVERIFY(got == data);
        QCOMPARE(QDir(dst.path()).entryList(QDir::Files).size(), 1);   // no .part left
    }

private slots:
    void names()
    {
        QCOMPARE(ModemTransfer::safeFileName("../../etc/passwd"), QString("passwd"));
        QCOMPARE(ModemTransfer::safeFileName("..\\..\\x.ini"), QString("x.ini"));
        QCOMPARE(ModemTransfer::safeFileName("/abs/path/image.bin"), QString("image.bin"));
        QCOMPARE(ModemTransfer::safeFileName(".bashrc"), QString("bashrc"));
        QCOMPARE(ModemTransfer::safeFileName(".."), QString());
        QCOMPARE(ModemTransfer::safeFileName("bad\x01name\n"), QString("badname"));
    }
    void uniqueNames()
    {
        QTemporaryDir d;
        writeFile(d.filePath("a.bin"), "1");
        QCOMPARE(QFileInfo(ModemTransfer::uniquePath(d.path(), "a.bin")).fileName(), QString("a (1).bin"));
        QCOMPARE(QFileInfo(ModemTransfer::uniquePath(d.path(), "b")).fileName(), QString("b"));
    }
    void findsZmodemStart()
    {
        bool rx = false;
        QByteArray sz("rz\r**\x18" "B00000000000000\r\x8a\x11", 24);
        QCOMPARE(ModemTransfer::findZmodemStart(sz, &rx), 3);
        QVERIFY(rx);
        QByteArray rz("**\x18" "B0100000023be50\r\x8a\x11", 21);
        QCOMPARE(ModemTransfer::findZmodemStart(rz, &rx), 0);
        QVERIFY(!rx);
        QCOMPARE(ModemTransfer::findZmodemStart("hello **B00 world", &rx), -1);
    }

    void loopback_data()
    {
        QTest::addColumn<int>("proto");
        QTest::addColumn<int>("size");
        for (auto [name, p] : {std::pair("x", P::Xmodem), {"x1k", P::Xmodem1k}, {"y", P::Ymodem},
                               {"z", P::Zmodem}}) {
            for (int size : {1, 128, 1024, 1025, 70000})
                QTest::addRow("%s-%d", name, size) << int(p) << size;
        }
    }
    void loopback()
    {
        QFETCH(int, proto);
        QFETCH(int, size);
        loop(P(proto), size);
    }

    void damagedLine_data()
    {
        QTest::addColumn<int>("proto");
        QTest::addRow("x") << int(P::Xmodem);
        QTest::addRow("x1k") << int(P::Xmodem1k);
        QTest::addRow("y") << int(P::Ymodem);
        QTest::addRow("z") << int(P::Zmodem);
    }
    void damagedLine()
    {
        QFETCH(int, proto);
        int packets = 0;
        loop(P(proto), 20000, [&packets](QByteArray d) {
            // Every so often a byte goes bad, after the opening exchange.
            if (++packets % 7 == 0 && d.size() > 100)
                d[d.size() / 2] = char(d[d.size() / 2] ^ 0x55);
            return d;
        });
    }

    void refusesRemoteCommand()
    {
        // ZCOMMAND asks the receiver to run a program. It must never happen.
        QTemporaryDir d;
        ModemTransfer b;
        b.setTimeoutMs(500);
        QSignalSpy done(&b, &ModemTransfer::finished);
        b.startReceive(P::Zmodem, d.path());
        // A hex ZCOMMAND header with a correct CRC.
        QByteArray hdr("**\x18" "B120000000040d9\r\x8a\x11", 21);
        b.feed(hdr.constData(), size_t(hdr.size()));
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(done[0][0].toBool(), false);
    }

    void cancelRemovesPartial()
    {
        QTemporaryDir d;
        ModemTransfer b;
        b.startReceive(P::Xmodem, d.path(), "x.bin");
        QCOMPARE(QDir(d.path()).entryList(QDir::Files).size(), 1);
        b.cancel();
        QCOMPARE(QDir(d.path()).entryList(QDir::Files).size(), 0);
    }

    void realLrzsz_data()
    {
        QTest::addColumn<int>("proto");
        QTest::addColumn<QString>("sendTool");
        QTest::addColumn<QStringList>("sendArgs");
        QTest::addColumn<QString>("recvTool");
        QTest::addColumn<QStringList>("recvArgs");
        QTest::addRow("zmodem") << int(P::Zmodem) << "sz" << QStringList{"-b"}
                                << "rz" << QStringList{"-b"};
        QTest::addRow("ymodem") << int(P::Ymodem) << "sb" << QStringList{"--ymodem"}
                                << "rb" << QStringList{"--ymodem"};
        QTest::addRow("xmodem") << int(P::Xmodem) << "sx" << QStringList{"--xmodem"}
                                << "rx" << QStringList{"--xmodem", "-c"};
        QTest::addRow("xmodem1k") << int(P::Xmodem1k) << "sx" << QStringList{"--xmodem", "-k"}
                                  << "rx" << QStringList{"--xmodem", "-c"};
    }
    // Their sender to our receiver, and ours to their receiver.
    void realLrzsz()
    {
        if (tool("sz").isEmpty())
            QSKIP("SSHLX_LRZSZ is not set");
        QFETCH(int, proto);
        QFETCH(QString, sendTool);
        QFETCH(QStringList, sendArgs);
        QFETCH(QString, recvTool);
        QFETCH(QStringList, recvArgs);
        const P p = P(proto);
        const bool x = p == P::Xmodem || p == P::Xmodem1k;
        const QByteArray data = allBytes(80000);

        for (bool theySend : {true, false}) {
            QTemporaryDir src, dst;
            const QString file = src.filePath("image.bin");
            writeFile(file, data);
            ModemTransfer t;
            Pty pty;
            pty.onData = [&t](const QByteArray &d) { t.feed(d.constData(), size_t(d.size())); };
            connect(&t, &ModemTransfer::send, &pty, &Pty::write);
            t.setBacklog([&pty] { return size_t(pty.pending.size()); });
            bool done = false, ok = false;
            QString msg;
            connect(&t, &ModemTransfer::finished, this, [&](bool o, const QString &m) {
                done = true; ok = o; msg = m;
            });
            QStringList argv;
            if (theySend) {
                argv << tool(sendTool) << sendArgs << "image.bin";
                QVERIFY(pty.start(src.path(), argv));
                t.startReceive(p, dst.path(), x ? "got.bin" : QString());
            } else {
                argv << tool(recvTool) << recvArgs;
                if (x)
                    argv << "got.bin";
                t.startSend(p, {file});
                QVERIFY(pty.start(dst.path(), argv));
            }
            // A real event loop: QTest's waits sleep on each pass, and these
            // protocols wait for an answer to every block.
            QEventLoop loop;
            connect(&t, &ModemTransfer::finished, &loop, &QEventLoop::quit);
            QTimer::singleShot(60000, &loop, &QEventLoop::quit);
            if (!done)
                loop.exec();
            QVERIFY2(done, "the transfer did not end");
            QVERIFY2(ok, qPrintable(msg));
            QString out = theySend ? t.savedFiles().value(0)
                                   : dst.filePath(x ? "got.bin" : "image.bin");
            QTRY_VERIFY_WITH_TIMEOUT(readFile(out).size() >= data.size(), 5000);
            QByteArray got = readFile(out);
            if (x)
                got.truncate(data.size());
            QVERIFY2(got == data, theySend ? "their send differs" : "our send differs");
        }
    }
};

QTEST_GUILESS_MAIN(ModemTest)
#include "modem_test.moc"
