// MainWindow - menus, docks and the session document area.
#pragma once

#include <QMainWindow>
#include <QPointer>

#include "sshlx_engine.h"

class DocumentArea;
class SessionWidget;
class ConnectionStore;
class ConnectionsPanel;
class ConnectionNode;
class SerialPortList;
class PropertyGrid;
class SerialManager;
class QToolBar;
namespace ads { class CDockManager; class CDockWidget; }
class QMenu;
class QLabel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    // Opens a new tab running conf (copied) and connects it.
    SessionWidget *openSession(const sshlx_conf *conf, const QString &displayName,
                               const QString &connectionId = QString(),
                               bool connectNow = true, const QString &panel = QString(),
                               const QString &dockName = QString());
    void openSavedSession(const QString &name);
    void openConnection(ConnectionNode *n, bool withOptions = false,
                        const QString &dockName = QString());
    // Brings forward the connection's open tab (reconnecting it if it has
    // ended) when "behaviour/reuseTab" is on; otherwise opens a new one.
    void showConnection(ConnectionNode *n);
    DocumentArea *documents() const { return m_docs; }
    // Reopen tabs saved by a previous run; returns how many were opened.
    int restoreTabs(const QStringList &entries);
    void restoreStartupState();
    // Re-apply the theme (after a change in Options).
    void applyTheme();
    ConnectionStore *store() const { return m_store; }
    ConnectionsPanel *connectionsPanel() const { return m_connections; }
    PropertyGrid *propertyGrid() const { return m_grid; }
    SerialManager *serialManager() const { return m_serial; }
    void globalEvent(sshlx_global_event ev, const QString &text);
    QList<SessionWidget *> sessions() const;

public slots:
    void newSession();
    void showAbout();

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    void buildMenus();
    void rebuildSavedSessionsMenu();
    bool confirmClose(SessionWidget *s);
    void closeSession(SessionWidget *s);
    void sessionTitleChanged(SessionWidget *s);
    SessionWidget *current() const;
    void launchTool(const QString &exe);
    void exportTree();
    void importTree();
    void importPuttySessions();
    void writeOpenTabsBreadcrumb();
    QStringList openTabEntries() const;
    void saveWindowState();
    void showOptions();
    void applyDefaultSizes();

    DocumentArea *m_docs;
    ConnectionStore *m_store;
    ConnectionsPanel *m_connections;
    ads::CDockManager *m_dockManager;
    ads::CDockWidget *m_connectionsDock;
    PropertyGrid *m_grid;
    SerialManager *m_serial = nullptr;
    QToolBar *m_bannerBar = nullptr;
    ads::CDockWidget *m_configDock;
    ads::CDockWidget *m_startDock = nullptr;
    QByteArray m_defaultLayout;
    bool m_restoring = false;
    QMenu *m_savedMenu = nullptr;
    QMenu *m_sessionMenu = nullptr;
    QLabel *m_sendNextLabel = nullptr;
};
