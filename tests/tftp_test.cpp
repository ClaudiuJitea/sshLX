// The TFTP server, driven by a small client in this file; SSHLX_TFTP names
// the tftp-hpa client for a run against a real one.
#include <QNetworkDatagram>
#include <QProcess>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QUdpSocket>
#include <QtTest>

#include "tftpserver.h"

static QByteArray u16(int v)
{
    QByteArray b(2, 0);
    b[0] = char(v >> 8);
    b[1] = char(v);
    return b;
}
static int rd(const QByteArray &b, int at) { return (uchar(b[at]) << 8) | uchar(b[at + 1]); }

static QByteArray request(int op, const QByteArray &name, const QList<QByteArray> &options = {})
{
    QByteArray p = u16(op) + name + QByteArray(1, 0) + "octet" + QByteArray(1, 0);
    for (const QByteArray &o : options)
        p += o + QByteArray(1, 0);
    return p;
}

static QNetworkDatagram waitFor(QUdpSocket &s, int ms = 3000)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        if (s.hasPendingDatagrams())
            return s.receiveDatagram();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
    }
    return {};
}

static QByteArray pattern(int size)
{
    QByteArray b(size, 0);
    QRandomGenerator rng(7);
    for (int i = 0; i < size; i++)
        b[i] = char(rng.bounded(256));
    return b;
}

class TftpTest : public QObject {
    Q_OBJECT
    QTemporaryDir root, outside;
    TftpServer srv;
    TftpServer::Options opts;

    void write(const QString &name, const QByteArray &d)
    {
        QFile f(root.filePath(name));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(d);
    }
    QByteArray fetch(const QByteArray &name, int blockSize, QList<QByteArray> options, int *errorCode)
    {
        QUdpSocket c;
        c.bind(QHostAddress::LocalHost, 0);
        c.writeDatagram(request(1, name, options), QHostAddress::LocalHost, srv.port());
        QByteArray out;
        int expect = 1;
        *errorCode = -1;
        while (true) {
            QNetworkDatagram d = waitFor(c);
            if (d.isNull())
                return "TIMEOUT";
            const QByteArray p = d.data();
            const int op = rd(p, 0);
            if (op == 5) {
                *errorCode = rd(p, 2);
                return {};
            }
            if (op == 6) {
                c.writeDatagram(u16(4) + u16(0), d.senderAddress(), quint16(d.senderPort()));
                continue;
            }
            if (rd(p, 2) == (expect & 0xffff)) {
                out += p.mid(4);
                expect++;
            }
            c.writeDatagram(u16(4) + p.mid(2, 2), d.senderAddress(), quint16(d.senderPort()));
            if (p.size() - 4 < blockSize)
                return out;
        }
    }
    int upload(const QByteArray &name, const QByteArray &data)
    {
        QUdpSocket c;
        c.bind(QHostAddress::LocalHost, 0);
        c.writeDatagram(request(2, name), QHostAddress::LocalHost, srv.port());
        QNetworkDatagram d = waitFor(c);
        if (d.isNull())
            return -2;
        if (rd(d.data(), 0) == 5)
            return rd(d.data(), 2);
        const quint16 port = quint16(d.senderPort());
        int block = 1;
        for (int at = 0;; at += 512, block++) {
            const QByteArray chunk = data.mid(at, 512);
            c.writeDatagram(u16(3) + u16(block) + chunk, QHostAddress::LocalHost, port);
            if (waitFor(c).isNull())
                return -2;
            if (chunk.size() < 512)
                return -1;
        }
    }

private slots:
    void initTestCase()
    {
        opts.root = root.path();
        opts.address = QHostAddress::LocalHost;
        opts.port = 0;
        opts.retransmitMs = 100;
        opts.maxRetries = 2;
        QString err;
        QVERIFY2(srv.start(opts, &err), qPrintable(err));
        QVERIFY(srv.port() != 0);
    }
    void resolveStaysInside()
    {
        const QString r = QFileInfo(root.path()).canonicalFilePath();
        QCOMPARE(TftpServer::resolve(root.path(), "a.bin"), r + "/a.bin");
        QCOMPARE(TftpServer::resolve(root.path(), "/a.bin"), r + "/a.bin");
        QVERIFY(TftpServer::resolve(root.path(), "../a.bin").isEmpty());
        QVERIFY(TftpServer::resolve(root.path(), "x/../../a.bin").isEmpty());
        QVERIFY(TftpServer::resolve(root.path(), "..\\a.bin").isEmpty());
        QVERIFY(TftpServer::resolve(root.path(), "").isEmpty());
        QVERIFY(QFile::link(outside.path(), root.filePath("escape")));
        QVERIFY(TftpServer::resolve(root.path(), "escape/file").isEmpty());
        QVERIFY(QFile::link(outside.filePath("nothing"), root.filePath("dangling")));
        QVERIFY(TftpServer::resolve(root.path(), "dangling").isEmpty());
    }
    void download()
    {
        const QByteArray d = pattern(5000);
        write("image.bin", d);
        int err;
        QCOMPARE(fetch("image.bin", 512, {}, &err), d);
    }
    void downloadExactBlockBoundary()
    {
        const QByteArray d = pattern(1024);
        write("even.bin", d);
        int err;
        QCOMPARE(fetch("even.bin", 512, {}, &err), d);
    }
    void downloadWithOptions()
    {
        const QByteArray d = pattern(9000);
        write("opt.bin", d);
        int err;
        QCOMPARE(fetch("opt.bin", 1428, {"blksize", "1428", "tsize", "0"}, &err), d);
    }
    void blockNumberWraps()
    {
        // 65536 blocks of 8 bytes: the block number passes zero.
        const QByteArray d = pattern(8 * 66000);
        write("wrap.bin", d);
        int err;
        QCOMPARE(fetch("wrap.bin", 8, {"blksize", "8"}, &err), d);
    }
    void refusals()
    {
        int err;
        fetch("missing.bin", 512, {}, &err);
        QCOMPARE(err, 1);
        fetch("../outside", 512, {}, &err);
        QCOMPARE(err, 2);
        QCOMPARE(upload("new.bin", "x"), 2);              // uploads are off
        QVERIFY(!QFile::exists(root.filePath("new.bin")));
    }
    void uploads()
    {
        opts.allowUploads = true;
        QVERIFY(srv.start(opts));
        const QByteArray d = pattern(3000);
        QCOMPARE(upload("new.bin", d), -1);
        QFile f(root.filePath("new.bin"));
        QTRY_VERIFY(f.exists());
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), d);
        QCOMPARE(upload("new.bin", "again"), 6);          // exists
        opts.allowOverwrite = true;
        QVERIFY(srv.start(opts));
        QCOMPARE(upload("new.bin", "again"), -1);
        f.close();
        QTRY_COMPARE(QFileInfo(f.fileName()).size(), 5);
    }
    void abandonedUploadLeavesNothing()
    {
        QUdpSocket c;
        c.bind(QHostAddress::LocalHost, 0);
        c.writeDatagram(request(2, "partial.bin"), QHostAddress::LocalHost, srv.port());
        QNetworkDatagram d = waitFor(c);
        c.writeDatagram(u16(3) + u16(1) + QByteArray(512, 'x'), QHostAddress::LocalHost,
                        quint16(d.senderPort()));
        QTRY_COMPARE_WITH_TIMEOUT(srv.activeTransfers(), 0, 3000);
        QVERIFY(!QFile::exists(root.filePath("partial.bin")));
        QVERIFY(!QFile::exists(root.filePath("partial.bin.tftp-part")));
    }
    void repeatedRequestIsOneTransfer()
    {
        write("r.bin", pattern(2000));
        QUdpSocket c;
        c.bind(QHostAddress::LocalHost, 0);
        const QByteArray rrq = request(1, "r.bin");
        c.writeDatagram(rrq, QHostAddress::LocalHost, srv.port());
        QVERIFY(!waitFor(c).isNull());
        c.writeDatagram(rrq, QHostAddress::LocalHost, srv.port());
        QTest::qWait(30);
        QCOMPARE(srv.activeTransfers(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(srv.activeTransfers(), 0, 3000);
    }
    void strangerIsTurnedAway()
    {
        const QByteArray d = pattern(2000);
        write("s.bin", d);
        QUdpSocket c, intruder;
        c.bind(QHostAddress::LocalHost, 0);
        intruder.bind(QHostAddress::LocalHost, 0);
        c.writeDatagram(request(1, "s.bin"), QHostAddress::LocalHost, srv.port());
        QNetworkDatagram first = waitFor(c);
        intruder.writeDatagram(u16(4) + u16(1), QHostAddress::LocalHost, quint16(first.senderPort()));
        QNetworkDatagram r = waitFor(intruder);
        QCOMPARE(rd(r.data(), 0), 5);
        QCOMPARE(srv.activeTransfers(), 1);
    }
    void realClient()
    {
        const QString client = qEnvironmentVariable("SSHLX_TFTP");
        if (client.isEmpty())
            QSKIP("SSHLX_TFTP is not set");
        QTemporaryDir work;
        const QByteArray d = pattern(200000);
        write("real.bin", d);
        QProcess p;
        p.setWorkingDirectory(work.path());
        p.start(client, {"-m", "binary", "127.0.0.1", QString::number(srv.port()),
                         "-c", "get", "real.bin"});
        QTRY_VERIFY_WITH_TIMEOUT(p.state() == QProcess::NotRunning, 20000);
        QFile f(work.filePath("real.bin"));
        QVERIFY2(f.open(QIODevice::ReadOnly), p.readAllStandardError().constData());
        QCOMPARE(f.readAll(), d);
        // And back up.
        QFile::rename(work.filePath("real.bin"), work.filePath("up.bin"));
        p.start(client, {"-m", "binary", "127.0.0.1", QString::number(srv.port()),
                         "-c", "put", "up.bin"});
        QTRY_VERIFY_WITH_TIMEOUT(p.state() == QProcess::NotRunning, 20000);
        QFile g(root.filePath("up.bin"));
        QTRY_VERIFY(g.exists());
        QVERIFY(g.open(QIODevice::ReadOnly));
        QCOMPARE(g.readAll(), d);
    }
};

QTEST_GUILESS_MAIN(TftpTest)
#include "tftp_test.moc"
