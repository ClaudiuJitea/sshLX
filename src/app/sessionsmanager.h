// SessionsManager - the Active Sessions panel: every open tab in a filterable
// list, for acting on several at once.
#pragma once

#include <QList>
#include <QWidget>

class DocumentArea;
class MainWindow;
class QLabel;
class QLineEdit;
class QTimer;
class QTreeWidget;
class SessionWidget;

class SessionsManager : public QWidget {
    Q_OBJECT
public:
    SessionsManager(DocumentArea *docs, MainWindow *window);
    QTreeWidget *list() const { return m_list; }
    QLineEdit *filter() const { return m_filter; }
    QList<SessionWidget *> selected() const;

    void disconnectSelected();
    void reconnectSelected();
    void setSelectedLocked(bool locked);
    void closeSelected(bool ask = true);

public slots:
    void refreshSoon();
    void refresh();

private:
    DocumentArea *m_docs;
    MainWindow *m_window;
    QLineEdit *m_filter;
    QTreeWidget *m_list;
    QLabel *m_count;
    QTimer *m_refresh;
};
