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
class QDockWidget;
class ButtonBar;
class CommandWindow;
class ScriptHost;
class TftpDialog;
class TftpServer;
struct Macro;
namespace ads { class CDockManager; class CDockWidget; }
class QMenu;
class QLabel;
class QAction;

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
    // A tab with the user's shell on this machine.
    SessionWidget *openLocalShell(const QString &dockName = QString());
    SessionWidget *openFileTransfer(const sshlx_conf *conf, const QString &name,
                                    const QString &connectionId);
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
    CommandWindow *commandWindow() const { return m_command; }
    ButtonBar *buttonBar() const { return m_buttonBar; }
    ScriptHost *scripts() const { return m_scripts; }
    TftpServer *tftpServer() const { return m_tftp; }
    void runMacro(const Macro &macro, bool onTargets);
    // Closes the tab without asking.
    void closeTabNow(SessionWidget *s);
    // Opens a connection by name, Folder/Name or id; nullptr with *error set.
    SessionWidget *openByName(const QString &name, QString *error);

    // Named sets of tabs with their layout, kept as files. Tabs that have no
    // saved settings, such as a Quick Connect, can't be reopened and are left
    // out; save returns how many.
    QStringList workspaces() const;
    int saveWorkspace(const QString &name);
    bool removeWorkspace(const QString &name);
    // replace: close what is open and take the saved layout. Otherwise the
    // saved tabs join the open ones.
    bool openWorkspace(const QString &name, bool replace);

public slots:
    void newSession();
    void showAbout();
    void checkForUpdates();

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    void attachPasswords(SessionWidget *s, const QString &connectionId);
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
    // Keep a tab's colours, fonts and cursor for its connection, or as
    // PuTTY's Default Settings for every new session.
    void saveAppearance(SessionWidget *s, bool asDefault);
    void rememberRecent(const QString &connectionId);
    void refreshRecent();
    void rebuildWorkspaceMenu();
    void saveWorkspaceAs();
    void chooseWorkspace(const QString &name);
    void applySavedLayout(const QByteArray &layout);
    QString workspacePath(const QString &name) const;
    void runScriptFile();
    void rebuildScriptMenu();

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
    QAction *m_updateAction = nullptr;
    QMenu *m_workspaceMenu = nullptr;
    QMenu *m_scriptMenu = nullptr;
    QDockWidget *m_commandDock = nullptr;
    CommandWindow *m_command = nullptr;
    ButtonBar *m_buttonBar = nullptr;
    ScriptHost *m_scripts = nullptr;
    TftpServer *m_tftp = nullptr;
    QPointer<TftpDialog> m_tftpDialog;
};
