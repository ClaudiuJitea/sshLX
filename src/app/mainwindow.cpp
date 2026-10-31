#include "mainwindow.h"
#include "credentialsdialog.h"
#include "filetransferpanel.h"
#include "highlightrules.h"
#include "dialogkit.h"
#include "icons.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFontDatabase>
#include <QInputDialog>
#include <QLineEdit>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLocale>
#include <QTimer>
#include <DockAreaWidget.h>
#include <DockManager.h>
#include <DockWidget.h>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QBoxLayout>
#include <QPlainTextEdit>
#include <QToolButton>
#include <QDesktopServices>
#include <QProcess>
#include <QUrl>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QToolBar>
#include <QUuid>
#include <QRegularExpression>
#include <functional>

#include "commandmanager.h"
#include "connecttarget.h"
#include "commandwindow.h"
#include "connectionspanel.h"
#include "connectionstore.h"
#include "connectiontreemodel.h"
#include "documentarea.h"
#include "keybindings.h"
#include "keyring.h"
#include "macros.h"
#include "optionsdialog.h"
#include "propertygrid.h"
#include "scripthost.h"
#include "startpage.h"
#include "theme.h"
#include "serialmanager.h"
#include "serialportlist.h"
#include "sessionsmanager.h"
#include "sessionwidget.h"
#include "terminallook.h"
#include "settingsdialog.h"
#include "shortcuts.h"
#include "tftpdialog.h"
#include "tftpserver.h"
#include "updatecheck.h"

// Resources live in the sshlx-app static library, so they must be
// registered explicitly.
static void initResources()
{
    Q_INIT_RESOURCE(sshlx);
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    initResources();
    setWindowTitle("sshLX");
    // ---- docking (Qt Advanced Docking System) ----
    ads::CDockManager::setConfigFlags(ads::CDockManager::DefaultOpaqueConfig);
    ads::CDockManager::setConfigFlag(ads::CDockManager::FocusHighlighting, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::MiddleMouseButtonClosesTab, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::DockAreaHasTabsMenuButton, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::AllTabsHaveCloseButton, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::EqualSplitOnInsertion, true);
    // Full tab titles; with many tabs the bar scrolls and the tab menu lists all.
    ads::CDockManager::setConfigFlag(ads::CDockManager::DisableTabTextEliding, true);
    m_dockManager = new ads::CDockManager(this);   // becomes the central widget
    m_dockManager->setStyleSheet(Theme::dockStyleSheet());
    m_docs = new DocumentArea(m_dockManager, this);

    // The Start page anchors the session area, so the side panels keep
    // their own column even with no session open.
    auto *start = new StartPage;
    m_startDock = new ads::CDockWidget(m_dockManager, tr("Start"));
    m_startDock->setObjectName("StartDock");
    m_startDock->setWidget(start, ads::CDockWidget::ForceNoScrollArea);
    m_startDock->setIcon(Icons::get("home"));
    m_startDock->setFeature(ads::CDockWidget::DockWidgetClosable, false);
    m_startDock->setFeature(ads::CDockWidget::DockWidgetFloatable, false);
    m_dockManager->addDockWidget(ads::CenterDockWidgetArea, m_startDock);
    m_docs->setHomeDock(m_startDock);
    // With sessions open, the Start tab shrinks to its icon.
    connect(m_docs, &DocumentArea::sessionCountChanged, this, [this](int n) {
        m_startDock->setWindowTitle(n ? QString() : tr("Start"));
        m_startDock->setToolTip(tr("Start"));
    });

    m_store = new ConnectionStore(ConnectionTree::defaultPath(), this);
    QString err;
    if (!m_store->load(&err))
        DialogKit::warning(this, tr("Connection tree not loaded"),
                             tr("Could not read %1:\n%2\n\nStarting with an empty tree; "
                                "the file was not changed.").arg(m_store->path(), err));
    m_connections = new ConnectionsPanel(m_store);
    m_connectionsDock = new ads::CDockWidget(m_dockManager, tr("Connections"));
    m_connectionsDock->setObjectName("ConnectionsDock");
    m_connectionsDock->setWidget(m_connections, ads::CDockWidget::ForceNoScrollArea);
    m_connectionsDock->setIcon(Icons::get("connections"));
    
    ads::CDockAreaWidget *left =
        m_dockManager->addDockWidget(ads::LeftDockWidgetArea, m_connectionsDock);
    m_connectionsDock->setMinimumSizeHintMode(ads::CDockWidget::MinimumSizeHintFromContent);
    connect(m_connections, &ConnectionsPanel::openRequested, this, &MainWindow::showConnection);
    connect(m_connections, &ConnectionsPanel::openInNewTabRequested, this,
            [this](ConnectionNode *n) { openConnection(n, false); });
    connect(m_connections, &ConnectionsPanel::openWithOptionsRequested, this,
            [this](ConnectionNode *n) { openConnection(n, true); });

    m_grid = new PropertyGrid(m_store);
    m_configDock = new ads::CDockWidget(m_dockManager, tr("Config"));
    m_configDock->setObjectName("ConfigDock");
    m_configDock->setWidget(m_grid, ads::CDockWidget::ForceNoScrollArea);
    m_configDock->setIcon(Icons::get("properties"));
    m_dockManager->addDockWidget(ads::BottomDockWidgetArea, m_configDock, left);
    connect(m_connections, &ConnectionsPanel::currentNodeChanged, m_grid, &PropertyGrid::setNode);
    connect(m_grid, &PropertyGrid::openRequested, this,
            [this](ConnectionNode *n) { openConnection(n, false); });
    connect(m_grid, &PropertyGrid::aboutRequested, this, &MainWindow::showAbout);
    m_docs->setSidePanels({m_connectionsDock, m_configDock});

    // Serial hot-plug: "Serial Ports" tree section + device banner.
    m_bannerBar = new QToolBar(tr("Device notifications"), this);
    m_bannerBar->setObjectName("DeviceBannerBar");
    m_bannerBar->setMovable(false);
    m_bannerBar->setFloatable(false);
    addToolBar(Qt::TopToolBarArea, m_bannerBar);
    QSettings st;
    m_serial = new SerialManager(this, m_bannerBar, st.value("serial/udev", true).toBool());
    m_connections->setSerialPorts(m_serial->ports());
    connect(m_connections, &ConnectionsPanel::fileTransferRequested, this, [this](ConnectionNode *n) {
        sshlx_conf *c = m_store->effectiveConf(n);
        openFileTransfer(c, n->name, n->id);
        sshlx_conf_free(c);
    });
    connect(m_connections, &ConnectionsPanel::openSerialRequested, this,
            [this](int row, bool withOptions) {
        if (row >= 0 && row < m_serial->ports()->count())
            m_serial->openDevice(m_serial->ports()->at(row), withOptions);
    });
    connect(m_connections, &ConnectionsPanel::serialRuleRequested, this, [this](int row) {
        if (row >= 0 && row < m_serial->ports()->count())
            m_serial->editRule(m_serial->ports()->at(row));
    });
    connect(m_connections, &ConnectionsPanel::serialHideRequested, this, [this](int row) {
        if (row >= 0 && row < m_serial->ports()->count())
            m_serial->hideDevice(m_serial->ports()->at(row));
    });
    connect(m_docs, &DocumentArea::closeRequested, this, &MainWindow::closeSession);
    connect(m_docs, &DocumentArea::currentSessionChanged, this, [this](SessionWidget *s) {
        setWindowTitle(s ? s->title() + " - sshLX" : QString("sshLX"));
    });
    connect(start, &StartPage::newConnection, this, [this] {
        m_connectionsDock->toggleView(true);
        m_connections->newConnection();
    });
    connect(start, &StartPage::quickConnect, this, &MainWindow::newSession);
    connect(start, &StartPage::localShell, this, [this] { openLocalShell(); });
    connect(start, &StartPage::options, this, &MainWindow::showOptions);
    connect(start, &StartPage::openRecent, this, [this](const QString &id) {
        if (ConnectionNode *n = m_store->tree().findById(id))
            showConnection(n);
    });
    // Names, hosts and icons can change, and connections can be deleted.
    for (auto sig : {&ConnectionStore::reset, &ConnectionStore::structureChanged})
        connect(m_store, sig, this, &MainWindow::refreshRecent);
    connect(m_store, &ConnectionStore::nodeChanged, this, &MainWindow::refreshRecent);
    refreshRecent();

    // Command Window and button bar: QMainWindow's own docks, outside the
    // session area, so that no tab can land in them and layouts keep working.
    m_command = new CommandWindow(m_docs);
    m_commandDock = new QDockWidget(tr("Command Window"), this);
    m_commandDock->setObjectName("CommandDock");
    m_commandDock->setFeatures(QDockWidget::DockWidgetClosable);
    m_commandDock->setAllowedAreas(Qt::BottomDockWidgetArea);
    m_commandDock->setWidget(m_command);
    addDockWidget(Qt::BottomDockWidgetArea, m_commandDock);
    m_commandDock->setVisible(st.value("window/commandWindow", false).toBool());
    m_buttonBar = new ButtonBar(this);
    addToolBar(st.value("window/buttonBarBottom", false).toBool() ? Qt::BottomToolBarArea
                                                                  : Qt::TopToolBarArea, m_buttonBar);
    m_buttonBar->setVisible(st.value("window/buttonBar", true).toBool());
    connect(m_buttonBar, &ButtonBar::macroTriggered, this, &MainWindow::runMacro);
    auto manageMacros = [this] {
        MacroDialog dlg(this);
        dlg.exec();
    };
    connect(m_buttonBar, &ButtonBar::manageRequested, this, manageMacros);
    m_sessionsPanel = new SessionsManager(m_docs, this);
    m_sessionsDock = new QDockWidget(tr("Active Sessions"), this);
    m_sessionsDock->setObjectName("SessionsDock");
    m_sessionsDock->setFeatures(QDockWidget::DockWidgetClosable);
    m_sessionsDock->setWidget(m_sessionsPanel);
    addDockWidget(Qt::RightDockWidgetArea, m_sessionsDock);
    m_sessionsDock->setVisible(st.value("window/sessionsPanel", false).toBool());
    m_commandManager = new CommandManager(this);
    m_commandsDock = new QDockWidget(tr("Command Manager"), this);
    m_commandsDock->setObjectName("CommandsDock");
    m_commandsDock->setFeatures(QDockWidget::DockWidgetClosable);
    m_commandsDock->setWidget(m_commandManager);
    addDockWidget(Qt::RightDockWidgetArea, m_commandsDock);
    m_commandsDock->setVisible(st.value("window/commandManager", false).toBool());
    connect(m_commandManager, &CommandManager::macroTriggered, this, &MainWindow::runMacro);
    connect(m_commandManager, &CommandManager::manageRequested, this, manageMacros);
    m_recordLabel = new QLabel;
    statusBar()->addPermanentWidget(m_recordLabel);
    m_statusLabel = new QLabel;
    statusBar()->addPermanentWidget(m_statusLabel);
    auto *statusTimer = new QTimer(this);
    connect(statusTimer, &QTimer::timeout, this, &MainWindow::updateStatus);
    statusTimer->start(1000);
    connect(m_docs, &DocumentArea::currentSessionChanged, this, &MainWindow::updateStatus);

    // The quick bar: connect to a host, and mark a word in every tab.
    m_quickBar = new QToolBar(tr("Connect Bar"), this);
    m_quickBar->setObjectName("QuickBar");
    m_quickBar->setFloatable(false);
    m_quickBar->setAllowedAreas(Qt::TopToolBarArea | Qt::BottomToolBarArea);
    auto *connectBox = new QLineEdit;
    connectBox->setObjectName("connectBox");
    connectBox->setPlaceholderText(tr("Connect: name, user@host:port or ssh://..."));
    connectBox->setMaximumWidth(340);
    connectBox->setAccessibleName(tr("Connect to"));
    auto *markBox = new QLineEdit;
    markBox->setObjectName("highlightBox");
    markBox->setPlaceholderText(tr("Highlight a word"));
    markBox->setClearButtonEnabled(true);
    markBox->setMaximumWidth(220);
    markBox->setAccessibleName(tr("Highlight a word"));
    m_quickBar->addWidget(connectBox);
    m_quickBar->addSeparator();
    m_quickBar->addWidget(markBox);
    addToolBar(Qt::TopToolBarArea, m_quickBar);
    m_quickBar->setVisible(st.value("window/quickBar", true).toBool());
    connect(connectBox, &QLineEdit::returnPressed, this, [this, connectBox] {
        QString error;
        if (connectTo(connectBox->text(), true, &error))
            connectBox->clear();
        else
            statusBar()->showMessage(error, 6000);
    });
    connect(markBox, &QLineEdit::textChanged, this, &SessionWidget::setAdHocHighlight);

    // The scratchpad is a tab beside the sessions, kept in a file of its own.
    auto *pad = new QPlainTextEdit;
    pad->setObjectName("scratchpad");
    pad->setAccessibleName(tr("Scratchpad"));
    pad->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    const QString padPath = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
        + "/scratchpad.txt";
    QFile padFile(padPath);
    if (padFile.open(QIODevice::ReadOnly))
        pad->setPlainText(QString::fromUtf8(padFile.readAll()));
    auto *padSave = new QTimer(this);
    padSave->setSingleShot(true);
    padSave->setInterval(500);
    connect(pad, &QPlainTextEdit::textChanged, padSave, qOverload<>(&QTimer::start));
    connect(padSave, &QTimer::timeout, this, [pad, padPath] {
        QDir().mkpath(QFileInfo(padPath).path());
        QSaveFile f(padPath);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(pad->toPlainText().toUtf8());
            f.commit();
        }
    });
    m_scratchDock = new ads::CDockWidget(m_dockManager, tr("Scratchpad"));
    m_scratchDock->setObjectName("ScratchpadDock");
    m_scratchDock->setWidget(pad, ads::CDockWidget::ForceNoScrollArea);
    m_scratchDock->setIcon(Icons::get("event-log"));
    m_dockManager->addDockWidgetTabToArea(m_scratchDock, m_startDock->dockAreaWidget());
    m_scratchDock->toggleView(false);
    m_startDock->raise();
    connect(m_docs, &DocumentArea::currentSessionChanged, this, &MainWindow::applyTabButtonBar);
    m_tftp = new TftpServer(this);
    m_scripts = new ScriptHost(this);
    auto *tftpLabel = new QLabel;
    statusBar()->addPermanentWidget(tftpLabel);
    auto updateTftp = [this, tftpLabel] {
        tftpLabel->setText(m_tftp->running() ? tr("TFTP on port %1").arg(m_tftp->port()) : QString());
    };
    connect(m_tftp, &TftpServer::logLine, this, updateTftp);

    buildMenus();
    m_sendNextLabel = new QLabel;
    statusBar()->addPermanentWidget(m_sendNextLabel);
    connect(&Shortcuts::instance(), &Shortcuts::sendNextChanged, this, [this](bool armed) {
        m_sendNextLabel->setText(armed ? tr("Next shortcut goes to the terminal") : QString());
    });
    resize(1100, 720);
}

MainWindow::~MainWindow() = default;

SessionWidget *MainWindow::current() const
{
    return m_docs->currentSession();
}

QList<SessionWidget *> MainWindow::sessions() const
{
    return m_docs->sessions();
}

void MainWindow::buildMenus()
{
    Shortcuts &sc = Shortcuts::instance();

    QMenu *file = menuBar()->addMenu(tr("&File"));
    QAction *newConn = file->addAction(Icons::get("connection-new"), tr("New &Connection"), this, [this] {
        m_connectionsDock->toggleView(true);
        m_connections->newConnection();
    });
    sc.add("file.newConnection", newConn, QKeySequence());
    file->addAction(Icons::get("folder-new"), tr("New &Folder"), this, [this] {
        m_connectionsDock->toggleView(true);
        m_connections->newFolder();
    });
    QAction *newSess = file->addAction(Icons::get("quick-connect"), tr("&Quick Connect (PuTTY dialog)..."), this,
                                       &MainWindow::newSession);
    sc.add("file.newSession", newSess, QKeySequence("Ctrl+Shift+N"));
    QAction *local = file->addAction(Icons::get("proto-localshell"), tr("New &Local Shell"), this,
                                     [this] { openLocalShell(); });
    sc.add("file.localShell", local, QKeySequence("Ctrl+Shift+T"));
    file->addSeparator();
    file->addAction(Icons::get("import"), tr("&Import PuTTY Sessions"), this, &MainWindow::importPuttySessions);
    file->addAction(Icons::get("import"), tr("I&mport Connection Tree..."), this, &MainWindow::importTree);
    file->addAction(Icons::get("export"), tr("E&xport Connection Tree..."), this, &MainWindow::exportTree);
    QAction *saveAct = file->addAction(Icons::get("save"), tr("&Save"), this, [this] {
        m_store->save();
        statusBar()->showMessage(tr("Saved %1").arg(m_store->path()), 3000);
    });
    sc.add("file.save", saveAct, QKeySequence());
    m_savedMenu = file->addMenu(Icons::get("terminal"), tr("&Saved Sessions"));
    connect(m_savedMenu, &QMenu::aboutToShow, this, &MainWindow::rebuildSavedSessionsMenu);
    file->addSeparator();
    QAction *quit = file->addAction(Icons::get("quit"), tr("&Quit"), this, &QWidget::close);
    sc.add("file.quit", quit, QKeySequence("Ctrl+Shift+Q"));

    m_sessionMenu = menuBar()->addMenu(tr("&Session"));
    connect(m_sessionMenu, &QMenu::aboutToShow, this, [this] {
        m_sessionMenu->clear();
        if (SessionWidget *s = current())
            s->buildContextMenu(m_sessionMenu);
        else
            m_sessionMenu->addAction(tr("(no session)"))->setEnabled(false);
    });

    QMenu *view = menuBar()->addMenu(tr("&View"));
    view->addAction(m_connectionsDock->toggleViewAction());
    view->addAction(m_configDock->toggleViewAction());
    QAction *commandAct = m_commandDock->toggleViewAction();
    commandAct->setIcon(Icons::get("send"));
    connect(commandAct, &QAction::triggered, this, [this](bool on) {
        if (on)
            m_command->focusInput();
    });
    sc.add("view.commandWindow", commandAct, QKeySequence("Ctrl+Shift+B"));
    view->addAction(commandAct);
    view->addAction(m_buttonBar->toggleViewAction());
    view->addAction(m_sessionsDock->toggleViewAction());
    view->addAction(m_commandsDock->toggleViewAction());
    view->addAction(m_quickBar->toggleViewAction());
    view->addAction(m_scratchDock->toggleViewAction());
    m_workspaceMenu = view->addMenu(Icons::get("workspace"), tr("&Workspaces"));
    connect(m_workspaceMenu, &QMenu::aboutToShow, this, &MainWindow::rebuildWorkspaceMenu);
    QAction *panelsRight = view->addAction(tr("Side Panels on the R&ight"));
    panelsRight->setCheckable(true);
    panelsRight->setChecked(QSettings().value("window/panelsRight", false).toBool());
    connect(panelsRight, &QAction::toggled, this, [this](bool on) {
        m_docs->setSidePanelsRight(on);
        QSettings().setValue("window/panelsRight", on);
    });
    view->addAction(Icons::get("home"), tr("&Reset Layout"), this, [this] {
        if (m_dockManager->restoreState(m_defaultLayout)) {
            // The default layout has the panels on the left.
            if (m_docs->sidePanelsRight())
                m_docs->setSidePanelsRight(true);
        } else {
            applyDefaultSizes();
        }
        // Put session tabs back next to the panels.
        for (SessionWidget *s : m_docs->sessions())
            if (ads::CDockWidget *dw = m_docs->dockFor(s))
                dw->toggleView(true);
    });
    view->addSeparator();
    QAction *splitR = view->addAction(Icons::get("split-right"), tr("Split Tab &Right"), this,
                                      [this] { m_docs->splitCurrent(Qt::Horizontal); });
    sc.add("view.splitRight", splitR, QKeySequence());
    QAction *splitD = view->addAction(Icons::get("split-down"), tr("Split Tab &Down"), this,
                                      [this] { m_docs->splitCurrent(Qt::Vertical); });
    sc.add("view.splitDown", splitD, QKeySequence());
    QAction *flt = view->addAction(Icons::get("float"), tr("F&loat Tab"), this, [this] { m_docs->floatCurrent(); });
    sc.add("view.float", flt, QKeySequence());
    QAction *att = view->addAction(Icons::get("attach"), tr("Re-&attach Tab"), this, [this] { m_docs->attachCurrent(); });
    sc.add("view.attach", att, QKeySequence());
    view->addSeparator();
    QAction *full = view->addAction(Icons::get("fullscreen"), tr("&Full Screen"));
    full->setCheckable(true);
    connect(full, &QAction::toggled, this, [this](bool on) {
        on ? showFullScreen() : showNormal();
    });
    sc.add("view.fullScreen", full, QKeySequence("F11"));
    view->addSeparator();
    QAction *next = view->addAction(tr("Next Tab"), this, [this] { m_docs->nextTab(+1); });
    sc.add("tabs.next", next, QKeySequence("Ctrl+Tab"));
    QAction *prev = view->addAction(tr("Previous Tab"), this, [this] { m_docs->nextTab(-1); });
    sc.add("tabs.prev", prev, QKeySequence("Ctrl+Shift+Tab"));
    QAction *next2 = view->addAction(tr("Next Tab (Page Down)"), this, [this] { m_docs->nextTab(+1); });
    sc.add("tabs.nextPgDn", next2, QKeySequence("Ctrl+PgDown"));
    QAction *prev2 = view->addAction(tr("Previous Tab (Page Up)"), this, [this] { m_docs->nextTab(-1); });
    sc.add("tabs.prevPgUp", prev2, QKeySequence("Ctrl+PgUp"));
    QAction *closeTab = view->addAction(Icons::get("close"), tr("Close Tab"), this, [this] {
        if (current())
            closeSession(current());
    });
    sc.add("tabs.close", closeTab, QKeySequence("Ctrl+Shift+W"));

    // In the menu bar rather than the rebuilt Session menu, so their keys
    // work whether or not a menu has been opened.
    QMenu *edit = menuBar()->addMenu(tr("&Edit"));
    QAction *copyAct = edit->addAction(Icons::get("copy"), tr("&Copy"), this, [this] {
        if (current())
            current()->copy();
    });
    sc.add("edit.copy", copyAct, QKeySequence("Ctrl+Shift+C"));
    QAction *pasteAct = edit->addAction(Icons::get("paste"), tr("&Paste"), this, [this] {
        if (current())
            current()->paste();
    });
    sc.add("edit.paste", pasteAct, QKeySequence("Ctrl+Shift+V"));
    QAction *selectAllAct = edit->addAction(tr("Select &All"), this, [this] {
        if (current())
            current()->selectAll();
    });
    sc.add("edit.selectAll", selectAllAct, QKeySequence("Ctrl+Shift+A"));
    QAction *pushAct = edit->addAction(Icons::get("paste"), tr("Paste Con&figuration..."), this, [this] {
        if (current())
            current()->pasteConfiguration();
    });
    sc.add("edit.pasteConfiguration", pushAct, QKeySequence());
    QAction *searchAct = edit->addAction(Icons::get("search"), tr("&Find in Terminal..."), this, [this] {
        if (current())
            current()->findInTerminal();
    });
    sc.add("edit.findInTerminal", searchAct, QKeySequence("Ctrl+Shift+F"));
    edit->addSeparator();
    QAction *keywordAct = edit->addAction(Icons::get("palette"), tr("Add &Keyword..."), this, [this] {
        if (current())
            current()->addKeyword();
    });
    sc.add("edit.addKeyword", keywordAct, QKeySequence());
    QAction *toggleKeywords = edit->addAction(tr("Toggle Keyword &Highlighting"), this, [] {
        HighlightSet::setEnabled(!HighlightSet::enabled());
        SessionWidget::reloadHighlights();
    });
    sc.add("edit.toggleKeywords", toggleKeywords, QKeySequence("Ctrl+Shift+H"));

    QMenu *tools = menuBar()->addMenu(tr("&Tools"));
    QAction *opts = tools->addAction(Icons::get("settings"), tr("&Options..."), this, &MainWindow::showOptions);
    sc.add("tools.options", opts, QKeySequence());
    QAction *credentials = tools->addAction(Icons::get("lock"), tr("&Credentials Manager..."), this, [this] {
        CredentialsDialog dlg(m_store, this);
        dlg.exec();
    });
    sc.add("tools.credentials", credentials, QKeySequence());
    QAction *transfer = tools->addAction(Icons::get("export"), tr("&File Transfer"), this, [this] {
        if (SessionWidget *s = current())
            openFileTransfer(s->conf(), s->displayName(), s->connectionId());
        else
            DialogKit::warning(this, tr("File Transfer"),
                               tr("Open an SSH tab first, or right-click a connection and "
                                  "choose File Transfer."));
    });
    sc.add("tools.fileTransfer", transfer, QKeySequence());
    QAction *tftp = tools->addAction(Icons::get("usb"), tr("TFTP &Server..."), this, [this] {
        if (!m_tftpDialog) {
            m_tftpDialog = new TftpDialog(m_tftp, this);
            m_tftpDialog->setAttribute(Qt::WA_DeleteOnClose);
        }
        m_tftpDialog->show();
        m_tftpDialog->raise();
    });
    sc.add("tools.tftp", tftp, QKeySequence());
    tools->addAction(Icons::get("script"), tr("&Macros..."), this, [this] {
        MacroDialog dlg(this);
        dlg.exec();
    });
    tools->addAction(Icons::get("keyboard"), tr("&Key Mappings..."), this, [this] {
        KeyBindingsDialog dlg(this);
        dlg.exec();
    });
    tools->addAction(Icons::get("script"), tr("Record Scrip&t"), this, [this] {
        toggleRecording(current());
    });
    QAction *runScript = tools->addAction(Icons::get("script"), tr("&Run Script..."), this,
                                          &MainWindow::runScriptFile);
    sc.add("tools.runScript", runScript, QKeySequence());
    m_scriptMenu = tools->addMenu(tr("Scr&ipts"));
    connect(m_scriptMenu, &QMenu::aboutToShow, this, &MainWindow::rebuildScriptMenu);
    QAction *evlog = tools->addAction(Icons::get("event-log"), tr("&Event Log"), this, [this] {
        if (current())
            current()->showEventLog();
    });
    sc.add("tools.eventLog", evlog, QKeySequence());
    QAction *sendNext = tools->addAction(Icons::get("keyboard"), tr("Send Next Shortcut to Terminal"), this, [] {
        Shortcuts::instance().armSendNext();
    });
    sc.add("tools.sendNext", sendNext, QKeySequence("Ctrl+Shift+F12"));
    tools->addSeparator();
    for (const char *exe : {"pageant", "puttygen"}) {
        QString path = QStandardPaths::findExecutable(exe);
        QAction *a = tools->addAction(QString("%1...").arg(exe == QString("pageant")
                                                          ? tr("Pageant") : tr("PuTTYgen")),
                                      this, [this, path] { launchTool(path); });
        a->setEnabled(!path.isEmpty());
        if (path.isEmpty())
            a->setToolTip(tr("%1 is not installed").arg(exe));
    }

    QMenu *help = menuBar()->addMenu(tr("&Help"));
    m_updateAction = help->addAction(Icons::get("reconnect"), tr("Check for &Updates..."), this,
                                     &MainWindow::checkForUpdates);
    help->addSeparator();
    help->addAction(Icons::get("info"), tr("&About sshLX"), this, &MainWindow::showAbout);
}

void MainWindow::runMacro(const Macro &macro, bool onTargets)
{
    if (!macro.script.isEmpty()) {
        m_scripts->run(macro.script);
    } else if (onTargets) {
        m_command->send(macro.text);
    } else if (SessionWidget *s = current()) {
        if (s->canSendCommands())
            s->runMacro(macro.text);
        else
            statusBar()->showMessage(tr("This tab can't take commands right now."), 4000);
    } else {
        statusBar()->showMessage(tr("Open a terminal tab first."), 4000);
    }
}

void MainWindow::runMacroByName(const QString &name)
{
    for (const Macro &m : loadMacros()) {
        if (m.name == name) {
            runMacro(m, false);
            return;
        }
    }
    statusBar()->showMessage(tr("No macro is called \"%1\".").arg(name), 4000);
}

void MainWindow::applyTabButtonBar(SessionWidget *s)
{
    const ConnectionNode *n = s ? m_store->tree().findById(s->connectionId()) : nullptr;
    const QString bar = n ? m_store->tree().resolveButtonBar(n) : QString();
    m_buttonBar->setForcedBar(bar.isEmpty() ? QString() : bar);
}

void MainWindow::toggleRecording(SessionWidget *s)
{
    if (!s) {
        statusBar()->showMessage(tr("Open a terminal tab first."), 4000);
        return;
    }
    if (!s->recording()) {
        s->startRecording();
        m_recordLabel->setText(tr("Recording %1").arg(s->title()));
        return;
    }
    const QString script = s->stopRecording();
    m_recordLabel->clear();
    if (script.isEmpty()) {
        statusBar()->showMessage(tr("Nothing was recorded."), 4000);
        return;
    }
    QDir().mkpath(ScriptHost::scriptsFolder());
    const QString path = QFileDialog::getSaveFileName(this, tr("Save Recorded Script"),
                                                      ScriptHost::scriptsFolder() + "/recorded.py",
                                                      tr("Python scripts (*.py)"));
    if (path.isEmpty())
        return;
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(script.toUtf8()) < 0 || !f.commit())
        DialogKit::error(this, tr("Script not saved"), f.errorString());
}

SessionWidget *MainWindow::connectTo(const QString &text, bool typed, QString *error)
{
    const QString wanted = text.trimmed();
    if (typed) {
        for (ConnectionNode *c : m_store->tree().connections())
            if (c->name == wanted || c->path() + '/' + c->name == wanted) {
                const QList<SessionWidget *> before = sessions();
                openConnection(c);
                for (SessionWidget *s : sessions())
                    if (!before.contains(s))
                        return s;
                return nullptr;
            }
    }
    ConnectTarget t;
    if (!ConnectTarget::parse(wanted, &t)) {
        *error = tr("\"%1\" is not a connection or a host name.").arg(wanted);
        return nullptr;
    }
    sshlx_conf *c = sshlx_conf_new_default();
    sshlx_conf_set_int(c, sshlx_conf_key_protocol(), SessionWidget::protocolId(t.protocol.toUtf8().constData()));
    sshlx_conf_set_str(c, sshlx_conf_key_find("host"), t.host.toUtf8().constData());
    sshlx_conf_set_int(c, sshlx_conf_key_find("port"), t.port ? t.port : t.protocol == "telnet" ? 23 : 22);
    if (!t.user.isEmpty())
        sshlx_conf_set_str(c, sshlx_conf_key_find("username"), t.user.toUtf8().constData());
    SessionWidget *s = openSession(c, t.host);
    sshlx_conf_free(c);
    return s;
}

QString MainWindow::instanceSocket()
{
    return QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + "/sshlx-instance";
}

bool MainWindow::sendToRunningInstance(const QString &socketName, const QString &target)
{
    QLocalSocket s;
    s.connectToServer(socketName);
    if (!s.waitForConnected(500))
        return false;
    s.write(target.toUtf8() + '\n');
    return s.waitForBytesWritten(1000);
}

void MainWindow::listenForInstances(const QString &socketName)
{
    // Another window has it: that one takes the links.
    QLocalSocket probe;
    probe.connectToServer(socketName);
    if (probe.waitForConnected(200))
        return;
    QLocalServer::removeServer(socketName);    // left by a crash
    m_instanceServer = new QLocalServer(this);
    m_instanceServer->setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_instanceServer->listen(socketName))
        return;
    connect(m_instanceServer, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket *c = m_instanceServer->nextPendingConnection()) {
            connect(c, &QLocalSocket::disconnected, c, &QObject::deleteLater);
            connect(c, &QLocalSocket::readyRead, this, [this, c] {
                while (c->canReadLine()) {
                    const QString target = QString::fromUtf8(c->readLine()).trimmed();
                    QString error;
                    // Links have a scheme, and only "sshLX NAME" typed by the
                    // user names a saved connection.
                    if (connectTo(target, !target.contains("://"), &error)) {
                        raise();
                        activateWindow();
                    } else {
                        statusBar()->showMessage(error, 6000);
                    }
                }
            });
        }
    });
}

QString MainWindow::statusText() const
{
    SessionWidget *s = m_docs->currentSession();
    if (!s)
        return {};
    const SessionWidget::Status st = s->status();
    QStringList parts{st.protocol};
    if (!st.cipherIn.isEmpty())
        parts << (st.cipherIn == st.cipherOut || st.cipherOut.isEmpty()
                      ? st.cipherIn : st.cipherOut + " / " + st.cipherIn);
    parts << QStringLiteral("%1x%2").arg(st.cols).arg(st.rows);
    if (st.connectedSecs >= 0) {
        const qint64 t = st.connectedSecs;
        parts << QStringLiteral("%1:%2:%3").arg(t / 3600, 2, 10, QChar('0'))
                     .arg(t / 60 % 60, 2, 10, QChar('0')).arg(t % 60, 2, 10, QChar('0'));
    }
    parts << QLocale().formattedDataSize(st.received, 1, QLocale::DataSizeTraditionalFormat);
    return parts.join(QStringLiteral("  |  "));
}

QWidget *MainWindow::scratchpad() const
{
    return m_scratchDock->widget();
}

void MainWindow::updateStatus()
{
    m_statusLabel->setText(statusText());
}

void MainWindow::closeTabNow(SessionWidget *s)
{
    m_docs->removeSession(s);
    writeOpenTabsBreadcrumb();
}

SessionWidget *MainWindow::openByName(const QString &name, QString *error)
{
    ConnectionNode *found = m_store->tree().findById(name);
    int matches = found ? 1 : 0;
    if (!found) {
        for (ConnectionNode *c : m_store->tree().connections()) {
            const QString path = c->path();
            if (c->name == name || path == name || (path + '/' + c->name) == name) {
                found = c;
                matches++;
            }
        }
    }
    if (matches != 1) {
        *error = matches ? tr("Several connections are called \"%1\"; use Folder/Name.").arg(name)
                         : tr("No connection is called \"%1\".").arg(name);
        return nullptr;
    }
    const QList<SessionWidget *> before = sessions();
    openConnection(found);
    for (SessionWidget *s : sessions())
        if (!before.contains(s))
            return s;
    *error = tr("\"%1\" could not be opened.").arg(name);
    return nullptr;
}

void MainWindow::runScriptFile()
{
    QSettings st;
    const QString path = QFileDialog::getOpenFileName(this, tr("Run Script"),
                                                      st.value("scripts/last").toString(),
                                                      tr("Python scripts (*.py);;All files (*)"));
    if (path.isEmpty())
        return;
    st.setValue("scripts/last", path);
    m_scripts->run(path);
}

void MainWindow::rebuildScriptMenu()
{
    m_scriptMenu->clear();
    const QString dir = ScriptHost::scriptsFolder();
    const QStringList names = QDir(dir).entryList({"*.py"}, QDir::Files, QDir::Name);
    for (const QString &n : names)
        m_scriptMenu->addAction(n, this, [this, path = QDir(dir).filePath(n)] { m_scripts->run(path); });
    if (names.isEmpty())
        m_scriptMenu->addAction(tr("(no scripts in the folder)"))->setEnabled(false);
    m_scriptMenu->addSeparator();
    m_scriptMenu->addAction(tr("Open Scripts Folder"), this, [dir] {
        QDir().mkpath(dir);
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });
    m_scriptMenu->addAction(tr("Show Script Console"), m_scripts, &ScriptHost::showConsole);
}

// ---- workspaces --------------------------------------------------------------
QString MainWindow::workspacePath(const QString &name) const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/workspaces/" +
           QString::fromLatin1(QUrl::toPercentEncoding(name)) + ".json";
}

QStringList MainWindow::workspaces() const
{
    const QDir dir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/workspaces");
    QStringList names;
    for (const QString &f : dir.entryList({"*.json"}, QDir::Files))
        names.append(QUrl::fromPercentEncoding(QFileInfo(f).completeBaseName().toLatin1()));
    names.sort(Qt::CaseInsensitive);
    return names;
}

int MainWindow::saveWorkspace(const QString &name)
{
    const QStringList tabs = openTabEntries();
    int skipped = 0;
    for (SessionWidget *s : sessions())
        if (!s->isFileTransfer())
            skipped++;
    skipped -= int(tabs.size());
    QJsonObject o;
    o["name"] = name;
    o["tabs"] = QJsonArray::fromStringList(tabs);
    o["layout"] = QString::fromLatin1(m_docs->saveLayout().toBase64());
    const QString path = workspacePath(name);
    QDir().mkpath(QFileInfo(path).path());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(QJsonDocument(o).toJson()) < 0 || !f.commit()) {
        DialogKit::error(this, tr("Workspace not saved"), f.errorString());
        return -1;
    }
    return skipped;
}

bool MainWindow::removeWorkspace(const QString &name)
{
    return QFile::remove(workspacePath(name));
}

bool MainWindow::openWorkspace(const QString &name, bool replace)
{
    QFile f(workspacePath(name));
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    QStringList tabs;
    for (const QJsonValue &v : o["tabs"].toArray())
        tabs.append(v.toString());
    if (replace) {
        for (SessionWidget *s : sessions())
            m_docs->removeSession(s);
    } else {
        // Same tabs can be open already: give them fresh dock names.
        for (QString &t : tabs)
            t = t.section('\t', 1).prepend('\t');
    }
    restoreTabs(tabs);
    if (replace)
        applySavedLayout(QByteArray::fromBase64(o["layout"].toString().toLatin1()));
    return true;
}

void MainWindow::applySavedLayout(const QByteArray &layout)
{
    m_docs->restoreLayout(layout);
    if (QSettings().value("window/panelsRight", false).toBool())
        m_docs->setSidePanelsRight(true);
    for (SessionWidget *s : m_docs->sessions())
        if (ads::CDockWidget *dw = m_docs->dockFor(s))
            if (dw->isClosed())
                dw->toggleView(true);
}

void MainWindow::saveWorkspaceAs()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Save Workspace"), tr("Workspace name:"),
                                               QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || name.isEmpty())
        return;
    if (workspaces().contains(name) &&
        !DialogKit::confirm(this, tr("Replace workspace?"),
                            tr("A workspace called \"%1\" exists already.").arg(name), tr("Replace"),
                            tr("Cancel"), false, true))
        return;
    const int skipped = saveWorkspace(name);
    if (skipped > 0)
        DialogKit::warning(this, tr("Workspace saved"),
                           tr("%n tab(s) were left out: they have no saved settings to reopen "
                              "from, as with a Quick Connect.", nullptr, skipped));
    else if (skipped == 0)
        statusBar()->showMessage(tr("Workspace \"%1\" saved.").arg(name), 4000);
}

void MainWindow::chooseWorkspace(const QString &name)
{
    int live = 0;
    for (SessionWidget *s : sessions())
        if (!s->closeWarning().isEmpty())
            live++;
    bool replace = true;
    if (!sessions().isEmpty()) {
        QString text = tr("Replace the open tabs, or add the workspace's tabs to them?");
        if (live)
            text += "\n\n" + tr("Replacing closes %n running session(s).", nullptr, live);
        const int choice = DialogKit::message(this, DialogKit::Kind::Question,
                                              tr("Open workspace \"%1\"").arg(name), text,
                                              {tr("Cancel"), tr("Add to Open Tabs"), tr("Replace Open Tabs")},
                                              1);
        if (choice != 1 && choice != 2)
            return;
        replace = choice == 2;
    }
    openWorkspace(name, replace);
}

void MainWindow::rebuildWorkspaceMenu()
{
    m_workspaceMenu->clear();
    m_workspaceMenu->addAction(Icons::get("save"), tr("&Save Current As..."), this,
                               &MainWindow::saveWorkspaceAs);
    const QStringList names = workspaces();
    if (names.isEmpty())
        return;
    m_workspaceMenu->addSeparator();
    for (const QString &n : names)
        m_workspaceMenu->addAction(n, this, [this, n] { chooseWorkspace(n); });
    m_workspaceMenu->addSeparator();
    QMenu *del = m_workspaceMenu->addMenu(Icons::get("close"), tr("&Delete"));
    for (const QString &n : names)
        del->addAction(n, this, [this, n] {
            if (DialogKit::confirm(this, tr("Delete workspace?"),
                                   tr("Delete the workspace \"%1\"? Its connections stay.").arg(n),
                                   tr("Delete"), tr("Cancel"), false, true))
                removeWorkspace(n);
        });
}

void MainWindow::launchTool(const QString &exe)
{
    if (!exe.isEmpty())
        QProcess::startDetached(exe, {});
}

void MainWindow::rebuildSavedSessionsMenu()
{
    m_savedMenu->clear();
    sshlx_strlist *l = sshlx_saved_sessions_list();
    for (size_t i = 0; i < l->n; i++) {
        QString name = QString::fromUtf8(l->items[i]);
        if (name == "Default Settings")
            continue;
        m_savedMenu->addAction(name, this, [this, name] { openSavedSession(name); });
    }
    sshlx_strlist_free(l);
    if (m_savedMenu->isEmpty())
        m_savedMenu->addAction(tr("(none)"))->setEnabled(false);
}

SessionWidget *MainWindow::openSession(const sshlx_conf *conf,
                                       const QString &displayName,
                                       const QString &connectionId,
                                       bool connectNow, const QString &panel,
                                       const QString &dockName)
{
    auto *s = new SessionWidget(conf, displayName);
    s->setConnectionId(connectionId);
    connect(s, &SessionWidget::titleChanged, this, [this, s] { sessionTitleChanged(s); });
    connect(s, &SessionWidget::stateChanged, this, [this, s] {
        m_docs->updateTab(s);
        writeOpenTabsBreadcrumb();
    });
    connect(s, &SessionWidget::closeRequested, this, [this, s] { closeSession(s); });
    connect(s, &SessionWidget::identityChanged, this, &MainWindow::writeOpenTabsBreadcrumb);
    connect(s, &SessionWidget::exitedWithClose, this, [this, s] {
        m_docs->removeSession(s);
    });
    connect(s, &SessionWidget::saveAppearanceRequested, this, [this, s] { saveAppearance(s, false); });
    connect(s, &SessionWidget::defaultAppearanceRequested, this, [this, s] { saveAppearance(s, true); });
    connect(s, &SessionWidget::duplicateRequested, this, [this, s] {
        if (s->isFileTransfer())
            openFileTransfer(s->conf(), s->displayName(), s->connectionId());
        else
            openSession(s->conf(), s->displayName(), s->connectionId());
    });
    connect(s, &SessionWidget::fileTransferRequested, this, [this, s] {
        openFileTransfer(s->conf(), s->displayName(), s->connectionId());
    });
    connect(s, &SessionWidget::scriptRequested, this, [this](const QString &path) {
        m_scripts->run(path);
    });
    connect(s, &SessionWidget::logonScriptRequested, this, [this, s](const QString &path) {
        m_scripts->run(path, {}, s);
    });
    connect(s, &SessionWidget::macroRequested, this, &MainWindow::runMacroByName);
    connect(s, &SessionWidget::sftpUploadRequested, this, [this, s](const QStringList &paths) {
        if (SessionWidget *t = openFileTransfer(s->conf(), s->displayName(), s->connectionId()))
            if (auto *panel = t->findChild<FileTransferPanel *>())
                panel->uploadWhenReady(paths);
    });
    s->setAntiIdle([this, connectionId]() -> SessionWidget::AntiIdle {
        const ConnectionNode *n = m_store->tree().findById(connectionId);
        const ConnectionNode *src = n ? m_store->tree().resolveAntiIdle(n) : nullptr;
        return src ? SessionWidget::AntiIdle{src->antiIdleText, src->antiIdleSeconds}
                   : SessionWidget::AntiIdle{};
    });
    connect(s, &SessionWidget::recordToggleRequested, this, [this, s] { toggleRecording(s); });
    connect(s, &SessionWidget::lockChanged, this, [this, s] {
        m_docs->updateTab(s);
        m_sessionsPanel->refreshSoon();
    });
    connect(s, &SessionWidget::identityChanged, this, [this, s] {
        m_docs->updateTab(s);
        m_command->refreshGroups();
        m_sessionsPanel->refreshSoon();
    });
    connect(s, &SessionWidget::stateChanged, m_sessionsPanel, &SessionsManager::refreshSoon);
    connect(s, &SessionWidget::titleChanged, m_sessionsPanel, &SessionsManager::refreshSoon);
    s->setLogonScript([this, connectionId] {
        const ConnectionNode *n = m_store->tree().findById(connectionId);
        return n ? m_store->tree().resolveLogonScript(n) : QString();
    });
    attachPasswords(s, connectionId);
    s->setLogonSteps([this, connectionId] {
        const ConnectionNode *n = m_store->tree().findById(connectionId);
        return n ? m_store->tree().resolveLogon(n) : QList<LogonStep>();
    });
    s->setCredentialAccount([this, connectionId](const QString &id) {
        const ConnectionNode *n = m_store->tree().findById(connectionId);
        const Credential *c = id.isEmpty() ? (n ? m_store->tree().resolveCredential(n) : nullptr)
                                           : m_store->tree().credential(id);
        return c ? Keyring::credentialAccount(c->id, false) : QString();
    });
    s->setSavedPasswords([this, connectionId] {
        const ConnectionNode *n = m_store->tree().findById(connectionId);
        const Credential *linked = n ? m_store->tree().resolveCredential(n) : nullptr;
        QList<SessionWidget::SavedPassword> list;
        for (const Credential &c : m_store->tree().credentials()) {
            const SessionWidget::SavedPassword p{c.name, Keyring::credentialAccount(c.id, false)};
            if (linked && c.id == linked->id)
                list.prepend(p);
            else
                list.append(p);
        }
        return list;
    });
    m_docs->addSession(s, panel, dockName);
    if (connectNow)
        s->connectSession();
    writeOpenTabsBreadcrumb();
    return s;
}

void MainWindow::attachPasswords(SessionWidget *s, const QString &connectionId)
{
    if (!connectionId.isEmpty() && Keyring::enabled()) {
        // Resolved at each login, so a rotated password is used on reconnect.
        s->setPasswordLookup([this, connectionId](const QString &, const QString &, int,
                                                  bool passphrase,
                                                  std::function<void(bool, const QString &)> done) {
            const ConnectionNode *n = m_store->tree().findById(connectionId);
            if (const Credential *c = n ? m_store->tree().resolveCredential(n) : nullptr)
                Keyring::read(Keyring::credentialAccount(c->id, passphrase), done);
            else if (passphrase)
                done(false, QString());
            else
                Keyring::read(Keyring::accountFor(connectionId), done);
        });
    }
}

SessionWidget *MainWindow::openFileTransfer(const sshlx_conf *conf, const QString &name,
                                            const QString &connectionId)
{
    if (sshlx_conf_get_int(conf, sshlx_conf_key_protocol()) != SessionWidget::protocolId("ssh")) {
        DialogKit::warning(this, tr("File Transfer"),
                           tr("File transfer needs an SSH connection, and \"%1\" is not one.")
                               .arg(name));
        return nullptr;
    }
    sshlx_conf *c = sshlx_conf_for_transfer(conf, "sftp", true);
    SessionWidget *s = openSession(c, name, connectionId, false);
    sshlx_conf_free(c);
    s->setCustomTitle(tr("Files: %1").arg(name));
    auto *panel = new FileTransferPanel(s, s->conf(), [this, connectionId](SessionWidget *w) {
        attachPasswords(w, connectionId);
    });
    s->setFilePanel(panel, [panel] { return panel->busy(); });
    writeOpenTabsBreadcrumb();
    s->connectSession();
    return s;
}

void MainWindow::openSavedSession(const QString &name)
{
    bool exists = false;
    sshlx_conf *c = sshlx_conf_load(name.toUtf8().constData(), &exists);
    if (!exists) {
        statusBar()->showMessage(tr("Saved session \"%1\" not found").arg(name), 5000);
    } else if (!sshlx_conf_launchable(c)) {
        // Not enough to connect: let the user complete it, as PuTTY does.
        auto *dlg = new SettingsDialog(c, false, 0, tr("sshLX Configuration"),
                                       this, true);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        connect(dlg, &SettingsDialog::accepted, this, [this, dlg, name] {
            openSession(dlg->conf(), name);
        });
        connect(dlg, &SettingsDialog::aboutRequested, this, &MainWindow::showAbout);
        dlg->open();
        return;
    } else {
        openSession(c, name)->setSavedSessionName(name);
    }
    sshlx_conf_free(c);
}

void MainWindow::openConnection(ConnectionNode *n, bool withOptions, const QString &dockName)
{
    if (!n || n->isFolder())
        return;
    if (!m_store->savedSessions().contains(n->session)) {
        DialogKit::warning(this, tr("Session not found"),
                             tr("The PuTTY saved session \"%1\" no longer exists.")
                                 .arg(n->session));
        return;
    }
    if (!m_restoring)
        rememberRecent(n->id);
    sshlx_conf *c = m_store->effectiveConf(n);
    const QString name = n->name, id = n->id, panel = n->panel;
    if (withOptions || !sshlx_conf_launchable(c)) {
        auto *dlg = new SettingsDialog(c, false, 0, tr("sshLX Configuration - %1").arg(name),
                                       this, true);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        connect(dlg, &SettingsDialog::aboutRequested, this, &MainWindow::showAbout);
        connect(dlg, &SettingsDialog::accepted, this, [this, dlg, name, id, panel] {
            openSession(dlg->conf(), name, id, true, panel);
        });
        dlg->open();
        return;
    }
    openSession(c, name, id, true, panel, dockName);
    sshlx_conf_free(c);
}

// The Start page's "Recent connections": connection ids, most recent first.
static const int kRecentMax = 5;

void MainWindow::rememberRecent(const QString &id)
{
    QSettings st;
    QStringList ids = st.value("start/recent").toStringList();
    ids.removeAll(id);
    ids.prepend(id);
    while (ids.size() > kRecentMax)
        ids.removeLast();
    st.setValue("start/recent", ids);
    refreshRecent();
}

void MainWindow::refreshRecent()
{
    auto *start = qobject_cast<StartPage *>(m_startDock->widget());
    if (!start)
        return;
    QList<StartPage::Recent> list;
    for (const QString &id : QSettings().value("start/recent").toStringList()) {
        ConnectionNode *n = m_store->tree().findById(id);
        if (!n || n->isFolder())
            continue;   // deleted since
        QIcon icon = n->icon.isEmpty() ? SessionWidget::iconForProtocol(m_store->protocolOf(n))
                                       : ConnectionTreeModel::iconFor(n->icon);
        list.append({n->id, n->name, m_store->hostOf(n), icon});
    }
    start->setRecent(list);
}

void MainWindow::showConnection(ConnectionNode *n)
{
    if (n && QSettings().value("behaviour/reuseTab", true).toBool()) {
        // Prefer the current tab when several belong to this connection.
        QList<SessionWidget *> open;
        for (SessionWidget *s : m_docs->sessions())
            if (s->connectionId() == n->id)
                open.append(s);
        if (!open.isEmpty()) {
            SessionWidget *s = open.contains(current()) ? current() : open.first();
            m_docs->activate(s);
            if (s->state() == SSHLX_STATE_DISCONNECTED)
                s->reconnect();
            return;
        }
    }
    openConnection(n, false);
}

void MainWindow::importPuttySessions()
{
    int before = m_store->tree().connections().size();
    m_store->syncSessions();
    int added = m_store->tree().connections().size() - before;
    statusBar()->showMessage(added ? tr("Imported %n PuTTY session(s)", nullptr, added)
                                   : tr("All PuTTY sessions are already in the tree"),
                             5000);
}

// Export: the tree plus the full settings of every referenced PuTTY
// session, so the file can be imported on another machine.
void MainWindow::exportTree()
{
    QString path = QFileDialog::getSaveFileName(this, tr("Export Connection Tree"),
                                                "sshlx-connections.json",
                                                tr("sshLX connections (*.json)"));
    if (path.isEmpty())
        return;
    QJsonObject o = m_store->tree().toJson();
    QJsonObject sessions;
    for (ConnectionNode *c : m_store->tree().connections()) {
        bool exists = false;
        sshlx_conf *conf = sshlx_conf_load(c->session.toUtf8().constData(), &exists);
        if (exists) {
            QJsonObject keys;
            for (size_t k = 0; k < sshlx_conf_key_count(); k++) {
                char *v = sshlx_conf_key_export(conf, int(k));
                keys[QString::fromUtf8(sshlx_conf_key_get(k)->id)] = QString::fromUtf8(v);
                sshlx_free(v);
            }
            sessions[c->session] = keys;
        }
        sshlx_conf_free(conf);
    }
    o["sessions"] = sessions;
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        DialogKit::error(this, tr("Export failed"), f.errorString());
        return;
    }
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    if (!f.commit())
        DialogKit::error(this, tr("Export failed"), f.errorString());
}

void MainWindow::importTree()
{
    QString path = QFileDialog::getOpenFileName(this, tr("Import Connection Tree"), {},
                                                tr("sshLX connections (*.json)"));
    if (path.isEmpty())
        return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        DialogKit::error(this, tr("Import failed"), f.errorString());
        return;
    }
    QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    ConnectionTree imported;
    QString err;
    if (!imported.fromJson(o, &err)) {
        DialogKit::error(this, tr("Import failed"), err);
        return;
    }
    // Recreate bundled sessions under names that don't clash.
    QJsonObject sessions = o["sessions"].toObject();
    for (ConnectionNode *c : imported.connections()) {
        if (!sessions.contains(c->session))
            continue;
        sshlx_conf *conf = sshlx_conf_new_default();
        QJsonObject keys = sessions[c->session].toObject();
        for (auto it = keys.begin(); it != keys.end(); ++it) {
            int k = sshlx_conf_key_find(it.key().toUtf8().constData());
            if (k >= 0)
                sshlx_conf_key_import(conf, k, it.value().toString().toUtf8().constData());
        }
        QString name = m_store->uniqueSessionName(c->session);
        char *e = nullptr;
        sshlx_conf_save(conf, name.toUtf8().constData(), &e);
        sshlx_free(e);
        sshlx_conf_free(conf);
        c->session = name;
        c->name = name;
    }
    // Credentials travel without their secrets: add the ones not known here.
    QList<Credential> credentials = m_store->tree().credentials();
    for (const Credential &c : imported.credentials())
        if (!m_store->tree().credential(c.id))
            credentials.append(c);
    m_store->tree().setCredentials(credentials);
    // Graft the imported tree under a new folder, with fresh ids.
    ConnectionNode *dest = m_store->tree().addFolder(
        nullptr, tr("Imported - %1").arg(QFileInfo(path).completeBaseName()));
    while (!imported.root()->children.isEmpty()) {
        ConnectionNode *n = imported.root()->takeChild(0);
        std::function<void(ConnectionNode *)> fresh = [&](ConnectionNode *x) {
            x->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            // A file from elsewhere must not get to run programs on connect.
            x->logonScript.clear();
            for (ConnectionNode *c : x->children)
                fresh(c);
        };
        fresh(n);
        dest->insertChild(-1, n);
    }
    m_store->save();
    emit m_store->structureChanged();
    m_connections->select(dest);
}

void MainWindow::newSession()
{
    sshlx_conf *c = sshlx_conf_new_default();
    auto *dlg = new SettingsDialog(c, false, 0, tr("sshLX Configuration"), this, true);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &SettingsDialog::aboutRequested, this, &MainWindow::showAbout);
    connect(dlg, &SettingsDialog::accepted, this, [this, dlg] {
        char *dest = sshlx_conf_dest(dlg->conf());
        openSession(dlg->conf(), QString::fromUtf8(dest));
        sshlx_free(dest);
    });
    // PuTTY's own dialog can save sessions; pick them up in the tree.
    connect(dlg, &QDialog::finished, m_store, &ConnectionStore::syncSessions);
    dlg->open();
}

void MainWindow::saveAppearance(SessionWidget *s, bool asDefault)
{
    const QStringList keys = TerminalLook::appearanceKeys();
    if (asDefault) {
        // PuTTY's "Default Settings": the starting point of every new
        // connection, quick connect and local shell.
        const char *name = "Default Settings";
        sshlx_conf *c = sshlx_conf_load(name, nullptr);
        for (const QString &k : keys)
            sshlx_conf_key_copy(c, s->conf(), sshlx_conf_key_find(k.toUtf8().constData()));
        char *err = nullptr;
        if (sshlx_conf_save(c, name, &err) != 0)
            DialogKit::warning(this, tr("Appearance not saved"), QString::fromUtf8(err ? err : ""));
        else
            statusBar()->showMessage(tr("New sessions will use this appearance."), 4000);
        sshlx_free(err);
        sshlx_conf_free(c);
        return;
    }
    ConnectionNode *n = m_store->tree().findById(s->connectionId());
    if (!n)
        return;
    sshlx_conf *c = m_store->effectiveConf(n);
    for (const QString &k : keys) {
        sshlx_conf_key_copy(c, s->conf(), sshlx_conf_key_find(k.toUtf8().constData()));
        // A value inherited from a folder would override the saved one.
        if (n->inherit.contains(k))
            m_store->setInherited(n, k, false);
    }
    QString err;
    if (!m_store->saveConf(n, c, &err))
        DialogKit::warning(this, tr("Appearance not saved"), err);
    else
        statusBar()->showMessage(tr("Appearance saved to %1.").arg(n->name), 4000);
    sshlx_conf_free(c);
}

SessionWidget *MainWindow::openLocalShell(const QString &dockName)
{
    sshlx_conf *c = sshlx_conf_new_default();
    sshlx_conf_set_int(c, sshlx_conf_key_protocol(), SessionWidget::protocolId("localshell"));
    SessionWidget *s = openSession(c, tr("Local shell"), {}, true, {}, dockName);
    sshlx_conf_free(c);
    return s;
}

void MainWindow::sessionTitleChanged(SessionWidget *s)
{
    m_docs->updateTab(s);
    if (s == current())
        setWindowTitle(s->title() + " - sshLX");
}

bool MainWindow::confirmClose(SessionWidget *s)
{
    QString warn = s->closeWarning();
    if (warn.isEmpty())
        return true;
    return DialogKit::confirm(this, tr("Close this session?"), warn, tr("Close"),
                              tr("Cancel"), false, true);
}

void MainWindow::closeSession(SessionWidget *s)
{
    if (!s || !confirmClose(s))
        return;
    m_docs->removeSession(s);
    writeOpenTabsBreadcrumb();
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    int live = 0;
    for (SessionWidget *s : sessions())
        if (!s->closeWarning().isEmpty())
            live++;
    if (live > 0 &&
        !DialogKit::confirm(this, tr("Quit sshLX?"),
                            tr("%n session(s) are still connected and will be closed.",
                               nullptr, live),
                            tr("Quit"), tr("Cancel"), false, true)) {
        e->ignore();
        return;
    }
    saveWindowState();
    m_scripts->stopAll();
    // A clean exit: no crash breadcrumb.
    QFile::remove(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) +
                  "/open-tabs.crash");
    e->accept();
}

// One line per tab: dock name, kind, reference. Kinds: "connection" (tree
// id), "session" (PuTTY saved session), "serial" (device path). Ad-hoc
// quick-connect tabs have no saved settings and are not listed.
QStringList MainWindow::openTabEntries() const
{
    QStringList r;
    for (SessionWidget *s : m_docs->sessions()) {
        if (s->isFileTransfer())
            continue;
        QString kind, ref;
        if (!s->connectionId().isEmpty()) {
            kind = "connection";
            ref = s->connectionId();
        } else if (!s->serialByIdPath().isEmpty()) {
            kind = "serial";
            ref = s->serialByIdPath();
        } else if (!s->savedSessionName().isEmpty()) {
            kind = "session";
            ref = s->savedSessionName();
        } else if (s->protocol() == SessionWidget::protocolId("localshell")) {
            kind = "local";
            ref = "shell";
        } else {
            continue;
        }
        r << m_docs->dockName(s) + '\t' + kind + '\t' + ref + '\t' + s->group();
    }
    return r;
}

void MainWindow::saveWindowState()
{
    QSettings st;
    st.setValue("window/geometry", saveGeometry());
    st.setValue("window/layout2", m_docs->saveLayout());
    st.setValue("session/openTabs", openTabEntries());
    st.setValue("window/buttonBarBottom", toolBarArea(m_buttonBar) == Qt::BottomToolBarArea);
    st.setValue("window/commandWindow", !m_commandDock->isHidden());
    st.setValue("window/buttonBar", !m_buttonBar->isHidden());
    st.setValue("window/sessionsPanel", !m_sessionsDock->isHidden());
    st.setValue("window/commandManager", !m_commandsDock->isHidden());
    st.setValue("window/quickBar", !m_quickBar->isHidden());
}

// Keeps the list of open tabs on disk while running, so that after an
// abnormal exit (a crash takes the whole process; see
// ARCHITECTURE-NOTES.md section 5) the next start can offer to reopen them.
void MainWindow::writeOpenTabsBreadcrumb()
{
    if (m_restoring)
        return;
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(dir);
    QSaveFile f(dir + "/open-tabs.crash");
    if (!f.open(QIODevice::WriteOnly))
        return;
    f.write(openTabEntries().join('\n').toUtf8() + "\n");
    f.commit();
}

int MainWindow::restoreTabs(const QStringList &entries)
{
    int n = 0;
    m_restoring = true;
    for (const QString &line : entries) {
        const QStringList p = line.split('\t');
        if (p.size() < 3 || p[2].isEmpty())
            continue;
        const QString &dock = p[0], &kind = p[1], &ref = p[2];
        const QList<SessionWidget *> before = sessions();
        if (kind == "connection") {
            if (ConnectionNode *node = m_store->tree().findById(ref)) {
                openConnection(node, false, dock);
                n++;
            }
        } else if (kind == "session") {
            bool exists = false;
            sshlx_conf *c = sshlx_conf_load(ref.toUtf8().constData(), &exists);
            if (exists && sshlx_conf_launchable(c)) {
                SessionWidget *s = openSession(c, ref, {}, true, {}, dock);
                s->setSavedSessionName(ref);
                n++;
            }
            sshlx_conf_free(c);
        } else if (kind == "serial") {
            SerialPortList::Device d;
            d.byId = ref.contains("/by-id/") ? ref : QString();
            d.node = ref;
            for (const SerialPortList::Device &x : m_serial->ports()->devices())
                if (x.stablePath() == ref)
                    d = x;
            sshlx_conf *c = m_serial->confFor(d);
            SessionWidget *s = openSession(c, d.product.isEmpty() ? ref : d.product,
                                           {}, true, {}, dock);
            s->setSerialByIdPath(ref);
            sshlx_conf_free(c);
            n++;
        } else if (kind == "local") {
            openLocalShell(dock);
            n++;
        }
        if (!p.value(3).isEmpty())
            for (SessionWidget *s : sessions())
                if (!before.contains(s))
                    s->setGroup(p[3]);
    }
    m_restoring = false;
    writeOpenTabsBreadcrumb();
    return n;
}

void MainWindow::restoreStartupState()
{
    QSettings st;
    restoreGeometry(st.value("window/geometry").toByteArray());

    QString crash = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) +
                    "/open-tabs.crash";
    QStringList entries;
    QFile cf(crash);
    if (cf.open(QIODevice::ReadOnly)) {
        entries = QString::fromUtf8(cf.readAll()).split('\n', Qt::SkipEmptyParts);
        cf.close();
        QStringList fatal = entries.filter(QRegularExpression("^fatal\t"));
        for (const QString &f : fatal)
            entries.removeAll(f);
        if (!entries.isEmpty() || !fatal.isEmpty()) {
            QString msg = tr("sshLX did not shut down cleanly last time.");
            if (!fatal.isEmpty())
                msg += "\n\n" + tr("Last error: %1").arg(fatal.last().section('\t', 1));
            if (!entries.isEmpty()) {
                msg += "\n\n" + tr("Reopen the %n tab(s) that were open?", nullptr,
                                     entries.size());
                if (!DialogKit::confirm(this, tr("Reopen tabs?"), msg, tr("Reopen"),
                                        tr("Not now"), true))
                    entries.clear();
            } else {
                DialogKit::info(this, tr("Unexpected exit"), msg);
            }
        }
        QFile::remove(crash);
    } else if (st.value("behaviour/restoreSessions", false).toBool()) {
        entries = st.value("session/openTabs").toStringList();
    }
    if (!entries.isEmpty())
        restoreTabs(entries);
    // Default proportions: a ~340 px side column, Connections over Config.
    applyDefaultSizes();
    m_defaultLayout = m_dockManager->saveState();
    // Saved dock layout last, so restored tabs return to their previous
    // areas. ("layout2": layouts from before the Start page are ignored.)
    m_docs->restoreLayout(st.value("window/layout2").toByteArray());
    // The default layout above has them on the left; a saved layout
    // normally has them on the right already, and then this keeps them.
    if (st.value("window/panelsRight", false).toBool())
        m_docs->setSidePanelsRight(true);
    for (SessionWidget *s : m_docs->sessions())
        if (ads::CDockWidget *dw = m_docs->dockFor(s))
            if (dw->isClosed())
                dw->toggleView(true);
}

void MainWindow::applyDefaultSizes()
{
    ads::CDockAreaWidget *startArea = m_startDock->dockAreaWidget();
    ads::CDockAreaWidget *connArea = m_connectionsDock->dockAreaWidget();
    const int side = qBound(280, fontMetrics().horizontalAdvance('M') * 26, 420);
    const int total = std::max(width(), side * 3);
    if (startArea)
        m_dockManager->setSplitterSizes(startArea, {side, total - side});
    if (connArea) {
        const int h = std::max(height(), 400);
        m_dockManager->setSplitterSizes(connArea, {h * 48 / 100, h * 52 / 100});
    }
}

void MainWindow::applyTheme()
{
    Theme::apply();
    m_dockManager->setStyleSheet(Theme::dockStyleSheet());
    m_connectionsDock->setIcon(Icons::get("connections"));
    m_configDock->setIcon(Icons::get("properties"));
    m_startDock->setIcon(Icons::get("home"));
    for (SessionWidget *s : sessions())
        m_docs->updateTab(s);
    emit m_store->structureChanged();   // tree icons follow the new palette
}

void MainWindow::showOptions()
{
    OptionsDialog dlg(this);
    dlg.exec();
}

void MainWindow::globalEvent(sshlx_global_event ev, const QString &text)
{
    switch (ev) {
    case SSHLX_GLOBAL_NONFATAL:
    case SSHLX_GLOBAL_OLD_KEYFILE: {
        DialogKit::notify(this, DialogKit::Kind::Warning, tr("sshLX"), text);
        break;
    }
    case SSHLX_GLOBAL_ABOUT_REQUESTED:
        showAbout();
        break;
    }
}

void MainWindow::checkForUpdates()
{
    m_updateAction->setEnabled(false);
    statusBar()->showMessage(tr("Checking for updates..."));
    auto *check = new UpdateCheck(this);
    auto done = [this, check] {
        m_updateAction->setEnabled(true);
        statusBar()->clearMessage();
        check->deleteLater();
    };
    connect(check, &UpdateCheck::finished, this,
            [this, done](const UpdateCheck::Release &r, bool newer) {
        done();
        if (!newer) {
            DialogKit::info(this, tr("sshLX is up to date"),
                            tr("You are running sshLX %1, the latest release.")
                                .arg(QApplication::applicationVersion()));
            return;
        }
        int b = DialogKit::message(this, DialogKit::Kind::Info, tr("sshLX %1 is available").arg(r.version),
                                   tr("You are running sshLX %1. Version %2 can be downloaded "
                                      "from the release page (.deb and AppImage).")
                                       .arg(QApplication::applicationVersion(), r.version),
                                   {tr("Open Release Page"), tr("Later")}, 0, r.notes);
        if (b == 0)
            QDesktopServices::openUrl(QUrl(r.pageUrl));
    });
    connect(check, &UpdateCheck::failed, this, [this, done](const QString &err) {
        done();
        int b = DialogKit::message(this, DialogKit::Kind::Warning, tr("Could not check for updates"),
                                   tr("GitHub could not be reached: %1").arg(err),
                                   {tr("Open Release Page"), tr("Close")}, 1);
        if (b == 0)
            QDesktopServices::openUrl(QUrl(UpdateCheck::releasesPage()));
    });
    check->start();
}

void MainWindow::showAbout()
{
    char *bi = sshlx_engine_buildinfo();
    const QString build = QString::fromUtf8(bi);
    sshlx_free(bi);
    QFile lic(":/LICENCE");
    QString licence;
    if (lic.open(QIODevice::ReadOnly))
        licence = QString::fromUtf8(lic.readAll());

    QDialog d(this);
    d.setWindowTitle(tr("About sshLX"));
    d.setMinimumWidth(520);
    auto *v = new QVBoxLayout(&d);
    DialogKit::frame(v);
    auto *top = new QHBoxLayout;
    top->setSpacing(18);
    auto *logo = new QLabel;
    logo->setPixmap(QApplication::windowIcon().pixmap(80, 80));
    top->addWidget(logo, 0, Qt::AlignTop);
    auto *col = new QVBoxLayout;
    col->setSpacing(6);
    col->addWidget(DialogKit::header(QStringLiteral("sshLX %1").arg(QApplication::applicationVersion()),
                                     tr("A tabbed, session-managed terminal client")));
    auto *text = new QLabel(tr(
        "<p>Built on the <b>PuTTY %1</b> core: the SSH, Telnet, Rlogin, SUPDUP, Raw "
        "and serial back ends, terminal emulator, cryptography, configuration and "
        "session storage are PuTTY's own code.</p>"
        "<p><b>PuTTY</b> is copyright 1997-2025 Simon Tatham and the PuTTY authors, "
        "distributed under the MIT licence.</p>"
        "<p>Qt, Qt Advanced Docking System and QtKeychain are used under their "
        "licences (LGPL / BSD), linked dynamically.</p>")
                               .arg(QString::fromUtf8(sshlx_engine_putty_version())));
    text->setWordWrap(true);
    text->setTextFormat(Qt::RichText);
    col->addWidget(text);
    top->addLayout(col, 1);
    v->addLayout(top);

    auto *details = new QPlainTextEdit(build + "\n\n" + licence);
    details->setReadOnly(true);
    details->setMinimumHeight(200);
    details->setVisible(false);
    v->addWidget(details);
    auto *toggle = new QToolButton;
    toggle->setText(tr("Build info and PuTTY licence"));
    toggle->setCheckable(true);
    connect(toggle, &QToolButton::toggled, &d, [details, &d](bool on) {
        details->setVisible(on);
        d.adjustSize();
    });
    QPushButton *close = DialogKit::primaryButton(tr("Close"));
    connect(close, &QPushButton::clicked, &d, &QDialog::accept);
    v->addWidget(DialogKit::footer({close}, toggle));
    d.exec();
}
