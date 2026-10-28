// EventLogDialog - PuTTY's Event Log window for one session (non-modal).
#pragma once

#include <QDialog>

class QListWidget;

class EventLogDialog : public QDialog {
    Q_OBJECT
public:
    EventLogDialog(const QString &title, QWidget *parent = nullptr);
    void appendLine(const QString &line);

private:
    QListWidget *m_list;
};
