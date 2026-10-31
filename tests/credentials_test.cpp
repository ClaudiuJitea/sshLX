// Credentials in the connection tree: links, folders, and what reaches the file.
#include <QJsonDocument>
#include <QtTest>

#include "connectiontree.h"

class CredentialsTest : public QObject {
    Q_OBJECT
private slots:
    void folderLinkReachesConnections()
    {
        ConnectionTree t;
        t.setCredentials({{"a", "ops", "netops", ""}, {"b", "lab", "labuser", ""}});
        ConnectionNode *site = t.addFolder(nullptr, "site");
        ConnectionNode *rack = t.addFolder(site, "rack");
        ConnectionNode *r1 = t.addConnection(rack, "r1", "r1");
        ConnectionNode *r2 = t.addConnection(rack, "r2", "r2");
        ConnectionNode *other = t.addConnection(nullptr, "other", "other");
        site->credential = "a";
        r2->credential = "b";            // the nearest link wins
        QCOMPARE(t.resolveCredential(r1)->username, QString("netops"));
        QCOMPARE(t.resolveCredential(r2)->username, QString("labuser"));
        QVERIFY(!t.resolveCredential(other));
        QCOMPARE(t.linkedConnections("a"), QList<ConnectionNode *>({r1}));
        QCOMPARE(t.linkedConnections("b"), QList<ConnectionNode *>({r2}));
    }
    void changingACredentialChangesEveryLink()
    {
        ConnectionTree t;
        t.setCredentials({{"a", "ops", "old", ""}});
        ConnectionNode *r1 = t.addConnection(nullptr, "r1", "r1");
        ConnectionNode *r2 = t.addConnection(nullptr, "r2", "r2");
        r1->credential = r2->credential = "a";
        t.setCredentials({{"a", "ops", "new", ""}});
        QCOMPARE(t.resolveCredential(r1)->username, QString("new"));
        QCOMPARE(t.resolveCredential(r2)->username, QString("new"));
    }
    void danglingLinkIsNoCredential()
    {
        ConnectionTree t;
        ConnectionNode *r1 = t.addConnection(nullptr, "r1", "r1");
        r1->credential = "gone";
        QVERIFY(!t.resolveCredential(r1));
    }
    void unlinkClearsDirectLinks()
    {
        ConnectionTree t;
        t.setCredentials({{"a", "ops", "u", ""}});
        ConnectionNode *f = t.addFolder(nullptr, "f");
        ConnectionNode *r1 = t.addConnection(f, "r1", "r1");
        f->credential = r1->credential = "a";
        t.unlinkCredential("a");
        QVERIFY(f->credential.isEmpty() && r1->credential.isEmpty());
    }
    void jsonRoundTrip()
    {
        ConnectionTree t;
        t.setCredentials({{"a", "ops", "netops", "/keys/ops.ppk"}});
        ConnectionNode *f = t.addFolder(nullptr, "f");
        t.addConnection(f, "r1", "r1");
        f->credential = "a";
        ConnectionTree back;
        QVERIFY(back.fromJson(t.toJson()));
        QCOMPARE(back.credentials().size(), 1);
        QCOMPARE(back.credentials()[0].keyFile, QString("/keys/ops.ppk"));
        QCOMPARE(back.resolveCredential(back.connections()[0])->name, QString("ops"));
    }
    void aTreeWithoutCredentialsStaysAsItWas()
    {
        ConnectionTree t;
        t.addConnection(nullptr, "r1", "r1");
        const QByteArray json = QJsonDocument(t.toJson()).toJson();
        QVERIFY(!json.contains("credential"));
    }
};

QTEST_GUILESS_MAIN(CredentialsTest)
#include "credentials_test.moc"
