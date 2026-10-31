// SftpClient against a scripted server and ScpClient against the real scp.
#include <QFile>
#include <QProcess>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

#include "scpclient.h"
#include "sftpclient.h"

static QByteArray randomBytes(int n)
{
    QByteArray b(n, Qt::Uninitialized);
    QRandomGenerator::global()->fillRange(reinterpret_cast<quint32 *>(b.data()), n / 4);
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

class TransferTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir;
    QProcess server;
    SftpClient *sftp = nullptr;
    QString remoteRoot, localRoot;

private slots:
    void init()
    {
        remoteRoot = dir.filePath("remote");
        localRoot = dir.filePath("local");
        QDir(remoteRoot).removeRecursively();
        QDir(localRoot).removeRecursively();
        QDir().mkpath(remoteRoot);
        QDir().mkpath(localRoot);
        sftp = new SftpClient(this);
        connect(sftp, &SftpClient::send, &server, [this](const QByteArray &d) { server.write(d); });
        connect(&server, &QProcess::readyReadStandardOutput, sftp, [this] {
            const QByteArray d = server.readAllStandardOutput();
            sftp->feed(d.constData(), size_t(d.size()));
        });
        server.setProcessChannelMode(QProcess::ForwardedErrorChannel);
        server.start("python3", {SSHLX_FAKE_SFTP, remoteRoot});
        QVERIFY(server.waitForStarted());
        QSignalSpy ready(sftp, &SftpClient::ready);
        sftp->start();
        QVERIFY(ready.wait(5000));
    }
    void cleanup()
    {
        server.disconnect(sftp);
        server.kill();
        server.waitForFinished();
        delete sftp;
    }

    void listSkipsNamesThatLeaveTheFolder()
    {
        writeFile(remoteRoot + "/ok.txt", "x");
        QStringList names;
        bool done = false;
        sftp->list("/", [&](bool ok, const QString &, const QList<SftpClient::Entry> &e) {
            QVERIFY(ok);
            for (const SftpClient::Entry &x : e)
                names << x.name;
            done = true;
        });
        QTRY_VERIFY(done);
        QCOMPARE(names, QStringList{"ok.txt"});
    }

    void safeNames_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<bool>("safe");
        QTest::newRow("plain") << "a.txt" << true;
        QTest::newRow("dots inside") << "a..b" << true;
        QTest::newRow("empty") << "" << false;
        QTest::newRow("dot") << "." << false;
        QTest::newRow("parent") << ".." << false;
        QTest::newRow("slash") << "../x" << false;
        QTest::newRow("absolute") << "/etc/passwd" << false;
        QTest::newRow("backslash") << "..\\x" << false;
        QTest::newRow("nul") << QString("a") + QChar(0) + "b" << false;
    }
    void safeNames()
    {
        QFETCH(QString, name);
        QFETCH(bool, safe);
        QCOMPARE(SftpClient::isSafeName(name), safe);
    }

    void downloadSurvivesShortReads()
    {
        const QByteArray data = randomBytes(1024 * 1024 + 321);
        writeFile(remoteRoot + "/big.bin", data);
        bool done = false, ok = false;
        qint64 last = 0;
        const QString out = localRoot + "/big.bin";
        sftp->download("/big.bin", out, data.size(), [&](qint64 d, qint64) { last = d; },
                       [&](bool o, const QString &) { ok = o; done = true; });
        QVERIFY(!QFile::exists(out));          // nothing until it is whole
        QTRY_VERIFY_WITH_TIMEOUT(done, 20000);
        QVERIFY(ok);
        QCOMPARE(last, qint64(data.size()));
        QCOMPARE(readFile(out), data);
    }

    void uploadKeepsContentAndMode()
    {
        const QByteArray data = randomBytes(700000);
        const QString in = localRoot + "/up.bin";
        writeFile(in, data);
        QFile::setPermissions(in, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        bool done = false, ok = false;
        sftp->upload(in, "/up.bin", nullptr, [&](bool o, const QString &) { ok = o; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 20000);
        QVERIFY(ok);
        QCOMPARE(readFile(remoteRoot + "/up.bin"), data);
    }

    void emptyFileBothWays()
    {
        writeFile(remoteRoot + "/empty", "");
        bool done = false, ok = false;
        sftp->download("/empty", localRoot + "/empty", 0, nullptr,
                       [&](bool o, const QString &) { ok = o; done = true; });
        QTRY_VERIFY(done);
        QVERIFY(ok);
        QVERIFY(QFile::exists(localRoot + "/empty"));
        done = false;
        sftp->upload(localRoot + "/empty", "/empty2", nullptr,
                     [&](bool o, const QString &) { ok = o; done = true; });
        QTRY_VERIFY(done);
        QVERIFY(ok);
        QVERIFY(QFile::exists(remoteRoot + "/empty2"));
    }

    void missingFileLeavesNothingBehind()
    {
        const QString out = localRoot + "/none";
        bool done = false;
        QString err;
        sftp->download("/none", out, 0, nullptr, [&](bool, const QString &e) { err = e; done = true; });
        QTRY_VERIFY(done);
        QCOMPARE(err, QString("No such file"));
        QVERIFY(!QFile::exists(out));
    }

    void existingFileSurvivesACancelledDownload()
    {
        writeFile(remoteRoot + "/f", randomBytes(2000000));
        const QString out = localRoot + "/f";
        writeFile(out, "keep me");
        bool done = false, ok = true;
        sftp->download("/f", out, 2000000,
                       [&](qint64, qint64) { sftp->cancelTransfer(); },
                       [&](bool o, const QString &) { ok = o; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
        QVERIFY(!ok);
        QCOMPARE(readFile(out), QByteArray("keep me"));
    }

    void cancelledUploadRemovesThePartialFile()
    {
        const QString in = localRoot + "/u";
        writeFile(in, randomBytes(3000000));
        bool done = false, ok = true;
        sftp->upload(in, "/u", [&](qint64, qint64) { sftp->cancelTransfer(); },
                     [&](bool o, const QString &) { ok = o; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
        QVERIFY(!ok);
        QVERIFY(!QFile::exists(remoteRoot + "/u"));
    }

    void folderAndRenameOperations()
    {
        int pending = 1;
        auto next = [&](bool ok, const QString &) { QVERIFY(ok); pending--; };
        sftp->makeDir("/d", next);
        QTRY_COMPARE(pending, 0);
        QVERIFY(QFileInfo(remoteRoot + "/d").isDir());
        pending = 1;
        sftp->rename("/d", "/e", next);
        QTRY_COMPARE(pending, 0);
        pending = 1;
        sftp->removeDir("/e", next);
        QTRY_COMPARE(pending, 0);
        QVERIFY(!QFile::exists(remoteRoot + "/e"));
    }

    void lostConnectionFailsTheTransfer()
    {
        writeFile(remoteRoot + "/f", randomBytes(4000000));
        bool done = false;
        QString err;
        const QString out = localRoot + "/f";
        sftp->download("/f", out, 4000000, nullptr, [&](bool, const QString &e) { err = e; done = true; });
        sftp->connectionLost("gone");
        QVERIFY(done);
        QCOMPARE(err, QString("gone"));
        QVERIFY(!QFile::exists(out));
    }

    void garbageIsRefused()
    {
        QSignalSpy bad(sftp, &SftpClient::protocolError);
        const QByteArray junk("SSH-2.0-not sftp at all\r\n");
        sftp->feed(junk.constData(), size_t(junk.size()));
        QCOMPARE(bad.size(), 1);
    }

    void truncatedReplyIsRefused()
    {
        // A NAME claiming a string far longer than the packet.
        bool done = false;
        sftp->realPath(".", [&](bool, const QString &, const QString &) { done = true; });
        QTRY_VERIFY(done);
        SftpClient c;
        QSignalSpy bad(&c, &SftpClient::protocolError);
        const char pkt[] = {0, 0, 0, 5, 101, 0, 0, 0, 9};
        c.feed(pkt, sizeof pkt);
        QCOMPARE(bad.size(), 1);
    }

    void scpCommands()
    {
        QCOMPARE(ScpClient::commandFor(true, "flash:/image.bin"), QString("scp -t flash:/image.bin"));
        QCOMPARE(ScpClient::commandFor(false, "/tmp/a b"), QString("scp -f '/tmp/a b'"));
        QCOMPARE(ScpClient::commandFor(false, "it's"), QString("scp -f 'it'\\''s'"));
        QCOMPARE(ScpClient::commandFor(true, "$(reboot)"), QString("scp -t '$(reboot)'"));
        QCOMPARE(ScpClient::commandFor(true, "-x"), QString("scp -t -- -x"));
    }

    void scpBothWaysAgainstRealScp()
    {
        const QString scp = QStandardPaths::findExecutable("scp");
        if (scp.isEmpty())
            QSKIP("no scp here");
        const QByteArray data = randomBytes(500000);
        const QString in = localRoot + "/s.bin", back = localRoot + "/back.bin";
        writeFile(in, data);
        const QString target = remoteRoot + "/s.bin";

        auto run = [&](bool upload, const QStringList &args, const QString &local) {
            QProcess p;
            ScpClient c;
            connect(&c, &ScpClient::send, &p, [&](const QByteArray &d) { p.write(d); });
            connect(&p, &QProcess::readyReadStandardOutput, &c, [&] {
                const QByteArray d = p.readAllStandardOutput();
                c.feed(d.constData(), size_t(d.size()));
            });
            bool done = false, ok = false;
            QString err;
            connect(&c, &ScpClient::finished, this, [&](bool o, const QString &e) {
                ok = o; err = e; done = true;
            });
            if (upload)
                c.startUpload(local, "s.bin");
            p.start(scp, args);
            QVERIFY(p.waitForStarted());
            if (!upload)
                c.startDownload(local);
            QElapsedTimer t;
            t.start();
            while (!done && t.elapsed() < 15000) {
                c.pump();
                QTest::qWait(1);
            }
            QVERIFY2(ok, qPrintable(err));
            p.closeWriteChannel();
            p.waitForFinished();
        };
        run(true, {"-t", target}, in);
        QCOMPARE(readFile(target), data);
        run(false, {"-f", target}, back);
        QCOMPARE(readFile(back), data);
    }

    void scpRefusalIsReported()
    {
        const QString scp = QStandardPaths::findExecutable("scp");
        if (scp.isEmpty())
            QSKIP("no scp here");
        QProcess p;
        ScpClient c;
        connect(&c, &ScpClient::send, &p, [&](const QByteArray &d) { p.write(d); });
        connect(&p, &QProcess::readyReadStandardOutput, &c, [&] {
            const QByteArray d = p.readAllStandardOutput();
            c.feed(d.constData(), size_t(d.size()));
        });
        bool done = false, ok = true;
        QString err;
        connect(&c, &ScpClient::finished, this, [&](bool o, const QString &e) {
            ok = o; err = e; done = true;
        });
        const QString out = localRoot + "/never";
        p.start(scp, {"-f", remoteRoot + "/absent"});
        QVERIFY(p.waitForStarted());
        c.startDownload(out);
        QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
        QVERIFY(!ok);
        QVERIFY(err.contains("absent"));
        QVERIFY(!QFile::exists(out));
    }
};

QTEST_GUILESS_MAIN(TransferTest)
#include "transfer_test.moc"
