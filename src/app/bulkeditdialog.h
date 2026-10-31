// BulkEditDialog - changes chosen settings on several connections at once.
// Only rows that are ticked are written; everything else stays as it was on
// each connection. Open tabs pick the changes up when they next connect.
#pragma once

#include <QDialog>
#include <QVariant>

class ConnectionNode;
class ConnectionStore;
class QLineEdit;
class QTableWidget;

class BulkEditDialog : public QDialog {
    Q_OBJECT
public:
    BulkEditDialog(ConnectionStore *store, const QList<ConnectionNode *> &nodes, QWidget *parent);

    struct Change {
        QString key;      // a Conf key, or @credential, @buttonbar
        QVariant value;
    };
    // Returns how many connections took the changes; *errors lists the rest.
    static int apply(ConnectionStore *store, const QList<ConnectionNode *> &nodes,
                     const QList<Change> &changes, QStringList *errors);
    QTableWidget *table() const { return m_table; }
    QList<Change> changes() const;

private:
    ConnectionStore *m_store;
    QList<ConnectionNode *> m_nodes;
    QLineEdit *m_filter;
    QTableWidget *m_table;
};
