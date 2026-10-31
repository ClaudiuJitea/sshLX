// ConfigPush against a scripted device.
#include <QSignalSpy>
#include <QtTest>

#include "configpush.h"

using Pause = ConfigPush::Pause;
using LineState = ConfigPush::LineState;

// Answers each line as an IOS box would: echo, then replies, then a prompt.
class Device : public QObject {
    Q_OBJECT
public:
    explicit Device(ConfigPush *p) : push(p)
    {
        connect(p, &ConfigPush::send, this, &Device::onLine);
        connect(p, &ConfigPush::sendKeys, this, [this](const QString &k) { keys << k; say(pending); });
    }
    QStringList received, keys;
    QString prompt = "R1(config)#";
    QHash<QString, QString> replies;
    QString pending;
    bool silent = false;

    void say(const QString &text)
    {
        QByteArray b = text.toUtf8();
        push->feed(b.constData(), size_t(b.size()));
    }
private slots:
    void onLine(const QString &line)
    {
        received << line;
        if (silent)
            return;
        QString out = line + "\r\n";
        if (replies.contains(line)) {
            QString r = replies[line];
            if (r.startsWith("@noprompt ")) {
                say(out + r.mid(10));
                return;
            }
            out += r;
        }
        say(out + prompt);
    }
private:
    ConfigPush *push;
};

class ConfigPushTest : public QObject {
    Q_OBJECT
    ConfigPush::Options fast()
    {
        ConfigPush::Options o;
        o.timeoutMs = 300;
        return o;
    }
private slots:
    void cleanPush()
    {
        ConfigPush p;
        Device d(&p);
        QSignalSpy done(&p, &ConfigPush::finished);
        p.start({"interface Gi0/1", "", "! comment", " no shutdown"}, fast());
        QVERIFY(done.wait(3000));
        QCOMPARE(done.first().first().toBool(), true);
        QCOMPARE(d.received, QStringList({"", "interface Gi0/1", " no shutdown"}));
    }

    void errorThenAmendThenContinue()
    {
        ConfigPush p;
        Device d(&p);
        d.replies["interfce Gi0/1"] = "              ^\r\n% Invalid input detected at '^' marker.\r\n\r\n";
        QSignalSpy paused(&p, &ConfigPush::paused);
        QSignalSpy done(&p, &ConfigPush::finished);
        p.start({"hostname R1", "interfce Gi0/1", "no shutdown"}, fast());
        QVERIFY(paused.wait(3000));
        QCOMPARE(paused.first().at(0).value<Pause>(), Pause::DeviceError);
        QVERIFY(paused.first().at(1).toString().contains("Invalid input"));
        QCOMPARE(p.current(), 1);
        QCOMPARE(d.received.size(), 3);       // nothing after the bad line
        p.retry("interface Gi0/1");
        QVERIFY(done.wait(3000));
        QCOMPARE(d.received, QStringList({"", "hostname R1", "interfce Gi0/1",
                                          "interface Gi0/1", "no shutdown"}));
        QCOMPARE(p.lines()[1].state, LineState::Ok);
        QVERIFY(p.report().contains("amended from: interfce Gi0/1"));
    }

    void skipContinuesFromNextLine()
    {
        ConfigPush p;
        Device d(&p);
        d.replies["bad"] = "% Incomplete command.\r\n";
        QSignalSpy paused(&p, &ConfigPush::paused);
        QSignalSpy done(&p, &ConfigPush::finished);
        p.start({"bad", "good"}, fast());
        QVERIFY(paused.wait(3000));
        p.skip();
        QVERIFY(done.wait(3000));
        QCOMPARE(d.received.last(), QString("good"));
        QCOMPARE(p.lines()[0].state, LineState::Skipped);
    }

    void echoAndNoticesAreNotErrors()
    {
        ConfigPush p;
        Device d(&p);
        d.replies["description Error: uplink"] = "% Warning: odd\r\n*Mar 1: %SYS-5-CONFIG_I: done\r\n";
        QSignalSpy paused(&p, &ConfigPush::paused);
        QSignalSpy done(&p, &ConfigPush::finished);
        p.start({"description Error: uplink"}, fast());
        QVERIFY(done.wait(3000));
        QCOMPARE(paused.size(), 0);
    }

    void pagerGetsSpace()
    {
        ConfigPush p;
        Device d(&p);
        d.replies["show run"] = "@noprompt line\r\n --More-- ";
        d.pending = "\r        \rmore\r\nR1#";
        QSignalSpy done(&p, &ConfigPush::finished);
        p.start({"show run"}, fast());
        QVERIFY(done.wait(3000));
        QCOMPARE(d.keys, QStringList({" "}));
    }

    void questionAsksUser()
    {
        ConfigPush p;
        Device d(&p);
        d.replies["reload"] = "@noprompt Proceed with reload? [confirm]";
        QSignalSpy paused(&p, &ConfigPush::paused);
        QSignalSpy done(&p, &ConfigPush::finished);
        p.start({"reload", "end"}, fast());
        QVERIFY(paused.wait(3000));
        QCOMPARE(paused.first().at(0).value<Pause>(), Pause::Question);
        d.silent = true;
        p.answer("y");
        d.say("\r\nR1#");
        d.silent = false;
        QVERIFY(done.wait(3000));
        QCOMPARE(d.received.last(), QString("end"));
    }

    void bannerBlockHasNoPrompts()
    {
        ConfigPush p;
        Device d(&p);
        d.replies["banner motd ^"] = "@noprompt Enter TEXT message.  End with the character '^'.\r\n";
        d.replies["Authorised use only"] = "@noprompt ";
        QSignalSpy paused(&p, &ConfigPush::paused);
        QSignalSpy done(&p, &ConfigPush::finished);
        p.start({"banner motd ^", "Authorised use only", "^", "end"}, fast());
        QVERIFY(done.wait(3000));
        QCOMPARE(paused.size(), 0);
        QCOMPARE(d.received.size(), 5);
    }

    void silenceTimesOutAndCanWait()
    {
        ConfigPush p;
        Device d(&p);
        d.replies["write mem"] = "@noprompt Building configuration...";
        QSignalSpy paused(&p, &ConfigPush::paused);
        QSignalSpy done(&p, &ConfigPush::finished);
        p.start({"write mem"}, fast());
        QVERIFY(paused.wait(3000));
        QCOMPARE(paused.first().at(0).value<Pause>(), Pause::Timeout);
        p.keepWaiting();
        d.say("\r\n[OK]\r\nR1#");
        QVERIFY(done.wait(3000));
        QVERIFY(done.first().first().toBool());
    }

    void noPromptStopsBeforeFirstLine()
    {
        ConfigPush p;
        Device d(&p);
        d.silent = true;
        QSignalSpy paused(&p, &ConfigPush::paused);
        p.start({"hostname R1"}, fast());
        QVERIFY(paused.wait(3000));
        QCOMPARE(paused.first().at(0).value<Pause>(), Pause::NoPrompt);
        QCOMPARE(d.received, QStringList({""}));
        p.stop();
        QVERIFY(!p.running());
    }

    void promptPattern_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<bool>("isPrompt");
        for (const char *s : {"Router#", "sw1(config-if)#", "sw1>", "user@host:~$",
                              "[user@host ~]$", "user@r1# ", "<Huawei>", "[Huawei-Vlan10]",
                              "RP/0/RSP0/CPU0:ios(config)#", "FortiGate (global) #",
                              "ASA/pri/act(config)# "})
            QTest::newRow(s) << QString(s) << true;
        for (const char *s : {"Building configuration...", "[OK]x", "interface Gi0/1",
                              "Enter configuration commands, one per line."})
            QTest::newRow(s) << QString(s) << false;
    }
    void passwordPrompts_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<bool>("asks");
        for (const char *s : {"Password: ", "[sudo] password for clau: ", "[sudo: authenticate] Password: ",
                              "Enter passphrase for key '/k': ", "banner\nPassword:", "Enter PIN:"})
            QTest::newRow(s) << QString(s) << true;
        for (const char *s : {"clau@laptop:~$ ", "clau@laptop:~$ cat password.txt", "Password:\nR1#",
                              "password changed", "Username: "})
            QTest::newRow(s) << QString(s) << false;
    }
    void passwordPrompts()
    {
        QFETCH(QString, text);
        QFETCH(bool, asks);
        QCOMPARE(ConfigPush::looksLikePasswordPrompt(text), asks);
    }
    void promptPattern()
    {
        QFETCH(QString, text);
        QFETCH(bool, isPrompt);
        QCOMPARE(QRegularExpression(ConfigPush::defaultPromptPattern()).match(text).hasMatch(),
                 isPrompt);
    }
};

QTEST_GUILESS_MAIN(ConfigPushTest)
#include "configpush_test.moc"
