// Unit tests: connection-tree serialisation, session sync and inheritance.
#include <QTemporaryDir>
#include <QtTest>

#include "connectiontree.h"
#include "serialrules.h"

class ModelTest : public QObject {
    Q_OBJECT
private slots:
    void jsonRoundTrip()
    {
        ConnectionTree t;
        ConnectionNode *linuxFolder = t.addFolder(nullptr, "Linux");
        linuxFolder->icon = "linux";
        linuxFolder->properties["username"] = "vabc";
        ConnectionNode *prod = t.addFolder(linuxFolder, "Production");
        ConnectionNode *c = t.addConnection(prod, "web1", "web1");
        c->description = "front end";
        c->favourite = true;
        c->panel = "Right";
        c->inherit = {"username", "port"};
        t.addConnection(nullptr, "router", "router");

        QTemporaryDir dir;
        QString path = dir.filePath("sub/connections.json");
        QString err;
        QVERIFY2(t.save(path, &err), qPrintable(err));

        ConnectionTree u;
        QVERIFY2(u.load(path, &err), qPrintable(err));
        QCOMPARE(u.root()->children.size(), 2);
        ConnectionNode *l2 = u.root()->children[0];
        QCOMPARE(l2->name, QString("Linux"));
        QCOMPARE(l2->icon, QString("linux"));
        QCOMPARE(l2->properties.value("username"), QString("vabc"));
        ConnectionNode *c2 = u.findById(c->id);
        QVERIFY(c2);
        QCOMPARE(c2->description, QString("front end"));
        QVERIFY(c2->favourite);
        QCOMPARE(c2->panel, QString("Right"));
        QCOMPARE(c2->inherit, QStringList({"username", "port"}));
        QCOMPARE(c2->path(), QString("Linux/Production"));
        QCOMPARE(u.toJson(), t.toJson());
    }

    void rejectsGarbage()
    {
        ConnectionTree t;
        QString err;
        QVERIFY(!t.fromJson(QJsonObject{{"format", "nope"}}, &err));
        QVERIFY(!err.isEmpty());
        QVERIFY(!t.fromJson(QJsonObject{{"format", "sshLX-connections"}, {"version", 99}}, &err));
        // Missing file = empty tree, not an error.
        QVERIFY(t.load("/nonexistent/sshlx/connections.json", &err));
    }

    void syncImportsPuttySessions()
    {
        ConnectionTree t;
        ConnectionNode *f = t.addFolder(nullptr, "Mine");
        t.addConnection(f, "existing", "existing");
        QList<ConnectionNode *> added = t.syncWithSessions(
            {"Default Settings", "existing", "new one", "other"});
        QCOMPARE(added.size(), 2);
        QCOMPARE(added[0]->session, QString("new one"));
        QCOMPARE(added[0]->parent, t.root());
        // Idempotent
        QVERIFY(t.syncWithSessions({"existing", "new one", "other"}).isEmpty());
        // Missing detection
        QList<ConnectionNode *> missing = t.missingSessions({"existing", "other"});
        QCOMPARE(missing.size(), 1);
        QCOMPARE(missing[0]->session, QString("new one"));
    }

    void moveAndSort()
    {
        ConnectionTree t;
        ConnectionNode *a = t.addFolder(nullptr, "A");
        ConnectionNode *b = t.addFolder(a, "B");
        ConnectionNode *x = t.addConnection(nullptr, "x", "x");
        QVERIFY(t.move(x, b, -1));
        QCOMPARE(x->parent, b);
        QVERIFY(!t.move(a, b, -1));   // folder into its own subtree
        QCOMPARE(a->parent, t.root());
        ConnectionNode *z = t.addConnection(nullptr, "zeta", "zeta");
        ConnectionNode *al = t.addConnection(nullptr, "alpha", "alpha");
        t.addFolder(nullptr, "Zfolder");
        t.sortAlphabetically(t.root());
        QStringList names;
        for (ConnectionNode *n : t.root()->children)
            names << n->name;
        QCOMPARE(names, QStringList({"A", "Zfolder", "alpha", "zeta"}));
        // Reorder within the same parent (index past the removed row).
        QVERIFY(t.move(al, t.root(), 4));
        QCOMPARE(t.root()->children.last(), al);
        Q_UNUSED(z);
    }

    void inheritanceResolution()
    {
        ConnectionTree t;
        ConnectionNode *top = t.addFolder(nullptr, "Top");
        top->properties["username"] = "top-user";
        top->properties["port"] = "port-top";
        top->properties["proxy_host"] = "proxy-top";
        ConnectionNode *mid = t.addFolder(top, "Mid");
        mid->properties["username"] = "mid-user";
        ConnectionNode *c = t.addConnection(mid, "c", "c");
        c->inherit = {"username", "port", "font"};

        QMap<QString, QString> r = t.resolveInherited(c);
        QCOMPARE(r.value("username"), QString("mid-user"));   // nearest wins
        QCOMPARE(r.value("port"), QString("port-top"));       // from grandparent
        QVERIFY(!r.contains("font"));                          // nobody defines it
        QVERIFY(!r.contains("proxy_host"));                    // not inherited
        QCOMPARE(t.inheritSource(c, "username"), mid);
        QCOMPARE(t.inheritSource(c, "port"), top);
        QCOMPARE(t.inheritSource(c, "font"), nullptr);

        // Folder view: its own and ancestors' values, nearest first.
        QMap<QString, QString> rf = t.resolveInherited(mid);
        QCOMPARE(rf.value("username"), QString("mid-user"));
        QCOMPARE(rf.value("proxy_host"), QString("proxy-top"));

        // Moving the connection changes where values come from.
        QVERIFY(t.move(c, top, -1));
        QCOMPARE(t.resolveInherited(c).value("username"), QString("top-user"));
    }

    void serialRules()
    {
        SerialRules rules;
        SerialRule r;
        r.key = "by-id:/dev/serial/by-id/usb-FTDI_FT232R_A10-if00-port0";
        r.usbKey = "usb:0403:6001:A10";
        r.label = "FT232R";
        r.action = SerialRule::AutoConnect;
        r.speed = 9600;
        r.parity = 2;
        rules.set(r);
        // Matched by by-id path, or by USB identity if the path differs.
        QVERIFY(rules.match(r.key, QString()));
        QVERIFY(rules.match("by-id:/other", "usb:0403:6001:A10"));
        QVERIFY(!rules.match("by-id:/other", "usb:0403:6001:B20"));
        // Defaults: 115200 8N1, no flow control, ask.
        SerialRule d;
        QCOMPARE(d.speed, 115200);
        QCOMPARE(d.dataBits, 8);
        QCOMPARE(d.parity, 0);
        QCOMPARE(d.stopHalfBits, 2);
        QCOMPARE(d.action, SerialRule::Ask);

        QTemporaryDir dir;
        QString path = dir.filePath("serial-rules.json");
        QVERIFY(rules.save(path));
        SerialRules back;
        QVERIFY(back.load(path));
        const SerialRule *m = back.match(r.key, QString());
        QVERIFY(m);
        QCOMPARE(m->action, SerialRule::AutoConnect);
        QCOMPARE(m->speed, 9600);
        QCOMPARE(m->parity, 2);
        QCOMPARE(m->label, QString("FT232R"));
        // Replace by key, then remove.
        r.action = SerialRule::Ignore;
        back.set(r);
        QCOMPARE(back.rules().size(), 1);
        QCOMPARE(back.match(r.key, QString())->action, SerialRule::Ignore);
        back.remove(r.key);
        QVERIFY(back.rules().isEmpty());
    }
};

QTEST_GUILESS_MAIN(ModelTest)
#include "model_test.moc"
