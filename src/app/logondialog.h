// LogonDialog - edits a connection's or folder's logon actions.
#pragma once

#include <QDialog>
#include <QList>

#include "connectiontree.h"

class QTableWidget;

class LogonDialog : public QDialog {
    Q_OBJECT
public:
    LogonDialog(const QList<LogonStep> &steps, const QList<Credential> &credentials, bool folder,
                QWidget *parent);
    QList<LogonStep> steps() const;
    QTableWidget *table() const { return m_table; }
    void addStep(const LogonStep &step);

private:
    void moveRow(int delta);

    QList<Credential> m_credentials;
    QTableWidget *m_table;
};
