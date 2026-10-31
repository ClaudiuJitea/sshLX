#include <QSignalSpy>
#include <QtTest>

#include "logonrunner.h"

class LogonTest : public QObject {
    Q_OBJECT
    static void feed(LogonRunner &r, const QByteArray &d) { r.feed(d.constData(), size_t(d.size())); }
private slots:
    void outputDropsEscapes()
    {
        OutputText t;
        const QByteArray in = "\x1b[1;32mgreen\x1b[0m \x1b]0;title\x07text\r\nabc\b\bX";
        t.append(in.constData(), size_t(in.size()));
        QCOMPARE(t.text(), QString("green text\naX"));
        QCOMPARE(t.lastLine(), QString("aX"));
    }
    void splitAcrossChunks()
    {
        OutputText t;
        t.append("\x1b", 1);
        t.append("[1m", 3);
        t.append("\xc3", 1);
        t.append("\xa9", 1);
        QCOMPARE(t.text(), QString::fromUtf8("é"));
    }
    void stepsRunInOrder()
    {
        LogonRunner r;
        QStringList sent;
        connect(&r, &LogonRunner::send, this, [&](const QString &s) { sent << s; });
        QSignalSpy done(&r, &LogonRunner::finished);
        r.start({{"Router>", false, "enable", false, {}},
                 {"Router#", false, "terminal length 0", false, {}}});
        feed(r, "banner\r\nRouter");
        QTest::qWait(20);
        QVERIFY(sent.isEmpty());          // half a prompt is no prompt
        feed(r, ">");
        QTRY_COMPARE(sent, QStringList{"enable"});
        feed(r, "enable\r\nRouter#");
        QTRY_COMPARE(sent.size(), 2);
        QCOMPARE(sent.last(), QString("terminal length 0"));
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(done[0][0].toBool(), true);
    }
    void matchedTextIsUsedUp()
    {
        // A second wait for the same text needs a new one.
        LogonRunner r;
        QStringList sent;
        connect(&r, &LogonRunner::send, this, [&](const QString &s) { sent << s; });
        r.start({{"$ ", false, "one", false, {}}, {"$ ", false, "two", false, {}}});
        feed(r, "$ ");
        QTRY_COMPARE(sent.size(), 1);
        QTest::qWait(30);
        QCOMPARE(sent.size(), 1);
        feed(r, "output\n$ ");
        QTRY_COMPARE(sent.size(), 2);
    }
    void regexAndEmptyWait()
    {
        LogonRunner r;
        QStringList sent;
        connect(&r, &LogonRunner::send, this, [&](const QString &s) { sent << s; });
        r.start({{"", false, "", false, {}}, {"sw[0-9]+#", true, "show ver", false, {}}});
        QTRY_COMPARE(sent, QStringList{""});   // Enter, to wake a serial console
        feed(r, "sw12#");
        QTRY_COMPARE(sent.size(), 2);
    }
    void passwordWaitsForOwner()
    {
        LogonRunner r;
        QStringList asked, sent;
        connect(&r, &LogonRunner::sendPassword, this, [&](const QString &c) { asked << c; });
        connect(&r, &LogonRunner::send, this, [&](const QString &s) { sent << s; });
        r.start({{"Password:", false, {}, true, "cred1"}, {"#", false, "next", false, {}}});
        feed(r, "Password: ");
        QTRY_COMPARE(asked, QStringList{"cred1"});
        QCOMPARE(r.lastLine(), QString("Password: "));
        feed(r, "\r\nR1#");
        QTest::qWait(20);
        QVERIFY(sent.isEmpty());
        r.stepDone();
        feed(r, "\r\nR1#");
        QTRY_COMPARE(sent.size(), 1);
    }
    void timeoutSaysWhat()
    {
        LogonRunner r;
        r.setTimeoutMs(50);
        QSignalSpy done(&r, &LogonRunner::finished);
        r.start({{"never", false, "x", false, {}}});
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(done[0][0].toBool(), false);
        QVERIFY(done[0][1].toString().contains("never"));
    }
};

QTEST_GUILESS_MAIN(LogonTest)
#include "logon_test.moc"
