#include "filetransferpanel.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMimeData>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include "dialogkit.h"
#include "icons.h"
#include "scpclient.h"
#include "sessionwidget.h"

static const char *kRemoteMime = "application/x-sshlx-remote-names";
static constexpr int kMaxDepth = 64;
static constexpr size_t kMaxBacklog = 256 * 1024;

namespace {
enum { ColName, ColSize, ColModified, ColPerms };
enum { RoleDir = Qt::UserRole, RoleLink, RoleSize, RoleTime };

// Folders first, then the clicked column; sizes and dates by value.
class RemoteItem : public QTreeWidgetItem {
public:
    using QTreeWidgetItem::QTreeWidgetItem;
    bool operator<(const QTreeWidgetItem &o) const override
    {
        const bool a = data(ColName, RoleDir).toBool(), b = o.data(ColName, RoleDir).toBool();
        const int col = treeWidget() ? treeWidget()->sortColumn() : ColName;
        const bool ascending = !treeWidget()
            || treeWidget()->header()->sortIndicatorOrder() == Qt::AscendingOrder;
        if (a != b)
            return ascending ? a : b;
        if (col == ColSize)
            return data(ColName, RoleSize).toLongLong() < o.data(ColName, RoleSize).toLongLong();
        if (col == ColModified)
            return data(ColName, RoleTime).toLongLong() < o.data(ColName, RoleTime).toLongLong();
        return text(col).compare(o.text(col), Qt::CaseInsensitive) < 0;
    }
};

class RemoteTree : public QTreeWidget {
public:
    using QTreeWidget::QTreeWidget;

protected:
    QStringList mimeTypes() const override { return {kRemoteMime}; }
    QMimeData *mimeData(const QList<QTreeWidgetItem *> &items) const override
    {
        QStringList names;
        for (QTreeWidgetItem *i : items)
            names << i->text(ColName);
        auto *m = new QMimeData;
        m->setData(kRemoteMime, names.join('\n').toUtf8());
        return m;
    }
};

QString permsText(uint p)
{
    QString s = (p & 0170000) == 0040000 ? "d" : (p & 0170000) == 0120000 ? "l" : "-";
    for (int i = 8; i >= 0; i--)
        s += p & (1u << i) ? "rwx"[(8 - i) % 3] : '-';
    return s;
}

QString sizeText(qint64 n) { return QLocale().formattedDataSize(n); }

QToolButton *toolButton(const QString &icon, const QString &tip)
{
    auto *b = new QToolButton;
    b->setIcon(Icons::get(icon));
    b->setToolTip(tip);
    b->setAutoRaise(true);
    return b;
}

QString parentPath(const QString &p)
{
    const int i = p.lastIndexOf('/');
    return i <= 0 ? QStringLiteral("/") : p.left(i);
}
} // namespace

FileTransferPanel::FileTransferPanel(SessionWidget *host, const sshlx_conf *conf,
                                     Configure configure, QWidget *parent)
    : QWidget(parent), m_host(host), m_conf(sshlx_conf_copy(conf)),
      m_configure(std::move(configure)), m_sftp(new SftpClient(this)), m_scp(new ScpClient(this)),
      m_clock(new QElapsedTimer)
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(6, 6, 6, 6);
    buildToolBar(v);

    // ---- this computer ----
    auto *localPane = new QWidget;
    auto *lv = new QVBoxLayout(localPane);
    lv->setContentsMargins(0, 0, 0, 0);
    auto *lh = new QHBoxLayout;
    m_localLabel = new QLabel(tr("This computer"));
    auto *localUp = toolButton("chevron-up", tr("Parent folder"));
    m_localPath = new QLineEdit;
    m_localPath->setObjectName("localPath");
    lh->addWidget(m_localLabel);
    lh->addWidget(localUp);
    lh->addWidget(m_localPath, 1);
    lv->addLayout(lh);
    m_fsModel = new QFileSystemModel(this);
    m_fsModel->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
    m_fsModel->setRootPath(QDir::rootPath());
    m_localView = new QTreeView;
    m_localView->setObjectName("localView");
    m_localView->setModel(m_fsModel);
    m_localView->setRootIsDecorated(false);
    m_localView->setItemsExpandable(false);
    m_localView->setSortingEnabled(true);
    m_localView->sortByColumn(0, Qt::AscendingOrder);
    m_localView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_localView->setDragEnabled(true);
    m_localView->setDragDropMode(QAbstractItemView::DragOnly);
    m_localView->header()->setStretchLastSection(false);
    m_localView->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_localView->setColumnHidden(2, true);
    lv->addWidget(m_localView, 1);

    // ---- the server ----
    auto *remotePane = new QWidget;
    auto *rv = new QVBoxLayout(remotePane);
    rv->setContentsMargins(0, 0, 0, 0);
    m_remoteStack = new QStackedWidget;
    auto *browser = new QWidget;
    auto *bv = new QVBoxLayout(browser);
    bv->setContentsMargins(0, 0, 0, 0);
    auto *rh = new QHBoxLayout;
    auto *remoteUp = toolButton("chevron-up", tr("Parent folder"));
    m_remotePath = new QLineEdit;
    m_remotePath->setObjectName("remotePath");
    m_remoteLabel = new QLabel(tr("Server"));
    rh->addWidget(m_remoteLabel);
    rh->addWidget(remoteUp);
    rh->addWidget(m_remotePath, 1);
    bv->addLayout(rh);
    m_remote = new RemoteTree;
    m_remote->setObjectName("remoteView");
    m_remote->setHeaderLabels({tr("Name"), tr("Size"), tr("Modified"), tr("Permissions")});
    m_remote->setRootIsDecorated(false);
    m_remote->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_remote->setDragEnabled(true);
    m_remote->setDragDropMode(QAbstractItemView::DragOnly);
    m_remote->setSortingEnabled(true);
    m_remote->sortByColumn(ColName, Qt::AscendingOrder);
    m_remote->setContextMenuPolicy(Qt::CustomContextMenu);
    m_remote->header()->setStretchLastSection(false);
    m_remote->header()->setSectionResizeMode(ColName, QHeaderView::Stretch);
    bv->addWidget(m_remote, 1);
    m_remoteStack->addWidget(browser);

    auto *scpPage = new QWidget;
    auto *sv = new QVBoxLayout(scpPage);
    sv->setContentsMargins(0, 0, 0, 0);
    auto *scpTitle = new QLabel(tr("<b>SCP only</b>"));
    auto *scpHint = DialogKit::hint(
        tr("This device does not offer SFTP, so its files cannot be listed, and an upload "
           "replaces any file already at that path. Each file opens its own connection."));
    m_scpPath = new QLineEdit;
    m_scpPath->setObjectName("scpPath");
    m_scpPath->setPlaceholderText(tr("Remote path, for example flash:/image.bin or /tmp/file"));
    sv->addWidget(scpTitle);
    sv->addWidget(scpHint);
    sv->addWidget(m_scpPath);
    sv->addStretch(1);
    m_remoteStack->addWidget(scpPage);
    rv->addWidget(m_remoteStack, 1);

    auto *panes = new QSplitter(Qt::Horizontal);
    panes->addWidget(localPane);
    panes->addWidget(remotePane);
    panes->setStretchFactor(0, 1);
    panes->setStretchFactor(1, 1);

    // ---- the queue ----
    m_queue = new QTreeWidget;
    m_queue->setObjectName("transferQueue");
    m_queue->setHeaderLabels({tr("File"), tr("Direction"), tr("Size"), tr("Status")});
    m_queue->setRootIsDecorated(false);
    m_queue->setSelectionMode(QAbstractItemView::NoSelection);
    m_queue->header()->setStretchLastSection(false);
    m_queue->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_queue->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    auto *qb = new QHBoxLayout;
    m_summary = new QLabel;
    m_summary->setTextFormat(Qt::PlainText);
    m_cancel = DialogKit::secondaryButton(tr("Cancel Transfers"));
    auto *clear = DialogKit::secondaryButton(tr("Clear Finished"));
    qb->addWidget(m_summary, 1);
    qb->addWidget(clear);
    qb->addWidget(m_cancel);

    auto *split = new QSplitter(Qt::Vertical);
    auto *queuePane = new QWidget;
    auto *qv = new QVBoxLayout(queuePane);
    qv->setContentsMargins(0, 0, 0, 0);
    qv->addWidget(m_queue, 1);
    qv->addLayout(qb);
    split->addWidget(panes);
    split->addWidget(queuePane);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 1);
    v->addWidget(split, 1);

    m_scpHost = new QVBoxLayout;
    v->addLayout(m_scpHost);

    m_scpPump = new QTimer(this);
    m_scpPump->setInterval(2);
    connect(m_scpPump, &QTimer::timeout, this, [this] {
        if (m_scpSession && m_scpSession->sendBacklog() < kMaxBacklog)
            m_scp->pump();
    });

    // ---- wiring ----
    host->setRawReceiver([this](const char *d, size_t n) { m_sftp->feed(d, n); });
    connect(m_sftp, &SftpClient::send, host, &SessionWidget::sendRaw);
    connect(m_sftp, &SftpClient::ready, this, &FileTransferPanel::onReady);
    connect(m_sftp, &SftpClient::protocolError, this, [this](const QString &why) {
        note(why);
        // This arrives from inside the session's output, which must return first.
        QTimer::singleShot(0, this, [this] {
            if (m_host)
                m_host->disconnectSession();
        });
    });
    connect(host, &SessionWidget::stateChanged, this, &FileTransferPanel::onHostState);

    connect(m_scp, &ScpClient::progress, this, [this](qint64 done, qint64 total) {
        if (m_current < 0)
            return;
        m_jobs[m_current].size = total;
        m_jobs[m_current].item->setText(2, sizeText(total));
        const double secs = qMax<qint64>(m_clock->elapsed(), 1) / 1000.0;
        setJobText(m_jobs[m_current], tr("%1% at %2/s").arg(total ? done * 100 / total : 100)
                                          .arg(sizeText(qint64(done / secs))));
    });
    connect(m_scp, &ScpClient::finished, this, [this](bool ok, const QString &err) {
        m_scpPump->stop();
        if (m_scpSession) {
            m_scpSession->hide();
            m_scpSession->deleteLater();
            m_scpSession = nullptr;
        }
        finishJob(ok, err);
    });

    connect(localUp, &QToolButton::clicked, this, [this] {
        QDir d(m_localDir);
        if (d.cdUp())
            setLocalDir(d.path());
    });
    connect(m_localPath, &QLineEdit::returnPressed, this, [this] { setLocalDir(m_localPath->text()); });
    connect(m_localView, &QTreeView::doubleClicked, this, &FileTransferPanel::localActivated);
    connect(remoteUp, &QToolButton::clicked, this, [this] { loadRemote(parentPath(m_remoteDir)); });
    connect(m_remotePath, &QLineEdit::returnPressed, this, [this] { loadRemote(m_remotePath->text()); });
    connect(m_remote, &QTreeWidget::itemActivated, this, &FileTransferPanel::remoteActivated);
    connect(m_remote, &QWidget::customContextMenuRequested, this, &FileTransferPanel::showRemoteMenu);
    connect(m_remote, &QTreeWidget::itemSelectionChanged, this, &FileTransferPanel::updateActions);
    connect(m_localView->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            &FileTransferPanel::updateActions);
    connect(m_cancel, &QPushButton::clicked, this, &FileTransferPanel::cancelTransfers);
    connect(clear, &QPushButton::clicked, this, &FileTransferPanel::clearFinished);

    m_remote->viewport()->setAcceptDrops(true);
    m_remote->viewport()->installEventFilter(this);
    m_localView->viewport()->setAcceptDrops(true);
    m_localView->viewport()->installEventFilter(this);
    m_remote->installEventFilter(this);
    m_localView->installEventFilter(this);

    setLocalDir(QSettings().value("fileTransfer/localDir", QDir::homePath()).toString());
    applyFilter();
    setRemoteActive(true);
    updateSummary();
}

void FileTransferPanel::buildToolBar(QLayout *into)
{
    auto *bar = new QToolBar;
    bar->setObjectName("transferToolBar");
    bar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    bar->setIconSize(QSize(16, 16));
    auto add = [&](const char *name, const QString &icon, const QString &text, const QString &tip,
                   const QKeySequence &key = {}) {
        QAction *a = bar->addAction(Icons::get(icon), text);
        a->setObjectName(name);
        a->setToolTip(tip);
        if (!key.isEmpty()) {
            // Only here: in the terminal tabs these keys belong to the device.
            a->setShortcut(key);
            a->setShortcutContext(Qt::WidgetWithChildrenShortcut);
            addAction(a);
        }
        return a;
    };
    m_upload = add("upload", "export", tr("Upload"), tr("Send the selection to the server"));
    m_download = add("download", "import", tr("Download"), tr("Fetch the selection to this computer"));
    bar->addSeparator();
    m_refresh = add("refresh", "reconnect", tr("Refresh"), tr("Read the server's folder again (F5)"),
                    Qt::Key_F5);
    m_newFolder = add("newFolder", "folder-new", tr("New Folder"), QString());
    m_rename = add("rename", "settings", tr("Rename"), QString(), Qt::Key_F2);
    m_delete = add("delete", "close", tr("Delete"), QString(), Qt::Key_Delete);
    bar->addSeparator();
    m_hidden = add("hiddenFiles", "search", tr("Hidden Files"), tr("Show files starting with a dot"));
    m_hidden->setCheckable(true);
    m_hidden->setChecked(QSettings().value("fileTransfer/showHidden", false).toBool());
    auto *spring = new QWidget;
    spring->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    bar->addWidget(spring);
    m_filter = new QLineEdit;
    m_filter->setObjectName("transferFilter");
    m_filter->setPlaceholderText(tr("Filter both sides"));
    m_filter->setClearButtonEnabled(true);
    m_filter->setMaximumWidth(220);
    bar->addWidget(m_filter);
    into->addWidget(bar);

    connect(m_upload, &QAction::triggered, this, &FileTransferPanel::uploadSelected);
    connect(m_download, &QAction::triggered, this, &FileTransferPanel::downloadSelected);
    connect(m_refresh, &QAction::triggered, this, [this] { loadRemote(m_remoteDir); });
    connect(m_newFolder, &QAction::triggered, this, &FileTransferPanel::newFolder);
    connect(m_rename, &QAction::triggered, this, &FileTransferPanel::renameSelected);
    connect(m_delete, &QAction::triggered, this, &FileTransferPanel::deleteSelected);
    connect(m_hidden, &QAction::toggled, this, [this](bool on) {
        QSettings().setValue("fileTransfer/showHidden", on);
        applyFilter();
    });
    connect(m_filter, &QLineEdit::textChanged, this, &FileTransferPanel::applyFilter);
}

void FileTransferPanel::setRemoteActive(bool remote)
{
    m_remoteActive = remote;
    QFont on = font(), off = font();
    on.setBold(true);
    m_remoteLabel->setFont(remote ? on : off);
    m_localLabel->setFont(remote ? off : on);
    updateActions();
}

void FileTransferPanel::updateActions()
{
    const int local = int(m_localView->selectionModel()->selectedRows().size());
    const int remote = int(m_remote->selectedItems().size());
    const QString side = m_remoteActive ? tr("server") : tr("this computer");
    const bool browsing = !m_scpOnly;
    const bool hereWorks = !m_remoteActive || (browsing && m_sftpReady);
    const int here = m_remoteActive ? remote : local;
    m_upload->setEnabled(local > 0);
    m_download->setEnabled(m_scpOnly || (m_sftpReady && remote > 0));
    m_refresh->setEnabled(browsing && m_sftpReady);
    m_newFolder->setEnabled(hereWorks);
    m_rename->setEnabled(hereWorks && here == 1);
    m_delete->setEnabled(hereWorks && here > 0);
    m_filter->setEnabled(browsing);
    m_hidden->setEnabled(browsing);
    m_newFolder->setToolTip(tr("New folder on %1").arg(side));
    m_rename->setToolTip(tr("Rename on %1 (F2)").arg(side));
    m_delete->setToolTip(m_remoteActive ? tr("Delete from the server (Del)")
                                        : tr("Move to the Trash (Del)"));
}

void FileTransferPanel::applyFilter()
{
    const QString text = m_filter->text().trimmed();
    const bool hidden = m_hidden->isChecked();
    QDir::Filters filters = QDir::AllEntries | QDir::NoDotAndDotDot;
    if (hidden)
        filters |= QDir::Hidden;
    m_fsModel->setFilter(filters);
    m_fsModel->setNameFilterDisables(false);
    m_fsModel->setNameFilters(text.isEmpty() ? QStringList()
                                             : QStringList{'*' + text + '*'});
    for (int i = 0; i < m_remote->topLevelItemCount(); i++) {
        QTreeWidgetItem *it = m_remote->topLevelItem(i);
        const QString name = it->text(ColName);
        const bool show = (hidden || !name.startsWith('.')) && name.contains(text, Qt::CaseInsensitive);
        it->setHidden(!show);
        if (!show)
            it->setSelected(false);
    }
}

void FileTransferPanel::newFolder()
{
    if (m_remoteActive) {
        newRemoteFolder();
        return;
    }
    bool ok = false;
    const QString name = QInputDialog::getText(window(), tr("New Folder"), tr("Name:"),
                                               QLineEdit::Normal, QString(), &ok).trimmed();
    if (ok && !name.isEmpty() && (!SftpClient::isSafeName(name) || !QDir(m_localDir).mkdir(name)))
        note(tr("Could not create %1.").arg(name));
}

void FileTransferPanel::renameSelected()
{
    if (m_remoteActive) {
        const QList<QTreeWidgetItem *> items = m_remote->selectedItems();
        if (items.size() == 1)
            renameRemote(items.first());
        return;
    }
    const QModelIndexList rows = m_localView->selectionModel()->selectedRows();
    if (rows.size() != 1)
        return;
    const QString old = m_fsModel->fileName(rows.first());
    bool ok = false;
    const QString name = QInputDialog::getText(window(), tr("Rename"), tr("New name:"),
                                               QLineEdit::Normal, old, &ok).trimmed();
    if (!ok || name.isEmpty() || name == old)
        return;
    // QDir::rename will not replace an existing file.
    if (!SftpClient::isSafeName(name) || !QDir(m_localDir).rename(old, name))
        note(tr("Could not rename %1.").arg(old));
}

void FileTransferPanel::deleteSelected()
{
    if (m_remoteActive)
        deleteRemote();
    else
        deleteLocal();
}

void FileTransferPanel::deleteLocal()
{
    QStringList paths;
    for (const QModelIndex &i : m_localView->selectionModel()->selectedRows())
        paths << m_fsModel->filePath(i);
    if (paths.isEmpty())
        return;
    const int choice = DialogKit::message(
        window(), DialogKit::Kind::Warning, tr("Move to the Trash?"),
        tr("You can restore these from the Trash."), {tr("Cancel"), tr("Move to Trash")}, 0,
        paths.join('\n'), true);
    if (choice != 1)
        return;
    for (const QString &p : paths)
        if (!QFile::moveToTrash(p))
            note(tr("Could not move %1 to the Trash.").arg(QFileInfo(p).fileName()));
}

FileTransferPanel::~FileTransferPanel()
{
    sshlx_conf_free(m_conf);
    delete m_clock;
}

bool FileTransferPanel::busy() const { return m_current >= 0 || m_walking > 0; }

void FileTransferPanel::note(const QString &text) { m_summary->setText(text); }

void FileTransferPanel::setLocalDir(const QString &dir)
{
    if (!QFileInfo(dir).isDir()) {
        m_localPath->setText(m_localDir);
        return;
    }
    m_localDir = QDir(dir).absolutePath();
    m_localPath->setText(m_localDir);
    m_localView->setRootIndex(m_fsModel->index(m_localDir));
    QSettings().setValue("fileTransfer/localDir", m_localDir);
}

void FileTransferPanel::onHostState(sshlx_state st)
{
    if (st == SSHLX_STATE_CONNECTED) {
        m_sftpReady = false;
        whenSendReady(m_host, [this] { m_sftp->start(); });
    } else if (st == SSHLX_STATE_DISCONNECTED) {
        const QString why = m_host ? m_host->lastError() : QString();
        const bool neverReady = !m_sftpReady;
        m_sftpReady = false;
        m_sftp->connectionLost(why.isEmpty() ? tr("Connection lost.") : why);
        // Logged in, then refused or closed the SFTP channel.
        if (neverReady && !m_scpOnly && m_host && m_host->loggedIn())
            enterScpMode();
        else if (!m_scpOnly && m_host)
            m_host->showFilePanel(false);
    }
}

void FileTransferPanel::whenSendReady(SessionWidget *s, std::function<void()> fn)
{
    auto *poll = new QTimer(this);
    poll->setInterval(10);
    QPointer<SessionWidget> session(s);
    connect(poll, &QTimer::timeout, this, [poll, session, fn] {
        if (!session || session->state() != SSHLX_STATE_CONNECTED) {
            poll->deleteLater();
        } else if (session->sendReady()) {
            poll->deleteLater();
            fn();
        }
    });
    poll->start();
}

void FileTransferPanel::onReady()
{
    m_sftpReady = true;
    m_sftp->realPath(m_remoteDir.isEmpty() ? QStringLiteral(".") : m_remoteDir,
                     [this](bool ok, const QString &err, const QString &path) {
        if (!ok) {
            note(err);
            return;
        }
        if (m_host)
            m_host->showFilePanel(true);
        loadRemote(path);
        if (m_current < 0)
            startNext();
    });
}

void FileTransferPanel::enterScpMode()
{
    m_scpOnly = true;
    m_remoteStack->setCurrentIndex(1);
    if (m_host) {
        m_host->setReconnectBarEnabled(false);
        m_host->showFilePanel(true);
    }
    m_download->setToolTip(tr("Fetch the file at the remote path"));
    updateActions();
}

void FileTransferPanel::loadRemote(const QString &dir)
{
    if (!m_sftpReady)
        return;
    m_sftp->list(dir, [this, dir](bool ok, const QString &err, const QList<SftpClient::Entry> &entries) {
        if (!ok) {
            note(tr("%1: %2").arg(dir, err));
            m_remotePath->setText(m_remoteDir);
            return;
        }
        // A refresh keeps the selection; a new folder starts without one.
        const QStringList keep = dir == m_remoteDir ? selectedRemoteNames() : QStringList();
        m_remoteDir = dir;
        m_remotePath->setText(dir);
        m_remote->setSortingEnabled(false);
        m_remote->clear();
        for (const SftpClient::Entry &e : entries) {
            auto *it = new RemoteItem(m_remote);
            it->setText(ColName, e.name);
            it->setIcon(ColName, Icons::get(e.isDir ? "folder" : "copy"));
            it->setText(ColSize, e.isDir ? QString() : sizeText(e.size));
            it->setText(ColModified, QLocale().toString(QDateTime::fromSecsSinceEpoch(e.mtime),
                                                        QLocale::ShortFormat));
            it->setText(ColPerms, permsText(e.perms));
            it->setData(ColName, RoleDir, e.isDir);
            it->setData(ColName, RoleLink, e.isLink);
            it->setData(ColName, RoleSize, e.size);
            it->setData(ColName, RoleTime, e.mtime);
            it->setSelected(keep.contains(e.name));
        }
        m_remote->setSortingEnabled(true);
        m_remote->resizeColumnToContents(ColSize);
        m_remote->resizeColumnToContents(ColModified);
        applyFilter();
        updateActions();
    });
}

void FileTransferPanel::remoteActivated(QTreeWidgetItem *item)
{
    const QString path = SftpClient::joinPath(m_remoteDir, item->text(ColName));
    if (item->data(ColName, RoleDir).toBool()) {
        loadRemote(path);
    } else if (item->data(ColName, RoleLink).toBool()) {
        m_sftp->stat(path, [this, path](bool ok, const QString &, const SftpClient::Entry &e) {
            if (ok && e.isDir)
                loadRemote(path);
            else if (ok)
                downloadSelected();
        });
    } else {
        downloadSelected();
    }
}

void FileTransferPanel::localActivated()
{
    const QModelIndex i = m_localView->currentIndex();
    if (m_fsModel->isDir(i))
        setLocalDir(m_fsModel->filePath(i));
    else
        uploadSelected();
}

QStringList FileTransferPanel::selectedRemoteNames() const
{
    QStringList names;
    for (QTreeWidgetItem *i : m_remote->selectedItems())
        names << i->text(ColName);
    return names;
}

void FileTransferPanel::uploadSelected()
{
    QStringList paths;
    for (const QModelIndex &i : m_localView->selectionModel()->selectedRows())
        paths << m_fsModel->filePath(i);
    if (paths.isEmpty())
        note(tr("Select something under This computer first."));
    uploadPaths(paths);
}

void FileTransferPanel::uploadPaths(const QStringList &paths)
{
    if (m_scpOnly) {
        QString target = m_scpPath->text().trimmed();
        if (target.isEmpty()) {
            note(tr("Type the remote path first."));
            return;
        }
        for (const QString &p : paths) {
            QFileInfo fi(p);
            if (fi.isDir()) {
                note(tr("SCP mode transfers single files, so %1 was left out.").arg(fi.fileName()));
                continue;
            }
            Job j;
            j.upload = true;
            j.local = p;
            // Several files, or a path ending in /, go into that folder.
            j.remote = target.endsWith('/') || paths.size() > 1
                ? SftpClient::joinPath(target, fi.fileName()) : target;
            j.size = fi.size();
            addJob(j);
        }
        return;
    }
    if (!m_sftpReady)
        return;
    for (const QString &p : paths) {
        QFileInfo fi(p);
        const QString remote = SftpClient::joinPath(m_remoteDir, fi.fileName());
        if (!fi.isDir()) {
            Job j;
            j.upload = true;
            j.local = p;
            j.remote = remote;
            j.size = fi.size();
            addJob(j);
            continue;
        }
        Job d;
        d.upload = d.makeDir = true;
        d.local = p;
        d.remote = remote;
        addJob(d);
        QDirIterator it(p, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::NoSymLinks,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            const QFileInfo f = it.fileInfo();
            Job j;
            j.upload = true;
            j.makeDir = f.isDir();
            j.local = f.filePath();
            j.remote = SftpClient::joinPath(remote, QDir(p).relativeFilePath(f.filePath()));
            j.size = f.isDir() ? 0 : f.size();
            addJob(j);
        }
    }
}

void FileTransferPanel::downloadSelected()
{
    if (m_scpOnly) {
        downloadPath();
        return;
    }
    const QList<QTreeWidgetItem *> items = m_remote->selectedItems();
    if (items.isEmpty())
        note(tr("Select something under Server first."));
    for (QTreeWidgetItem *i : items) {
        SftpClient::Entry e;
        e.name = i->text(ColName);
        e.size = i->data(ColName, RoleSize).toLongLong();
        e.isDir = i->data(ColName, RoleDir).toBool();
        e.isLink = i->data(ColName, RoleLink).toBool();
        queueDownload(e, SftpClient::joinPath(m_remoteDir, e.name), m_localDir, 0);
    }
}

void FileTransferPanel::downloadPath()
{
    const QString remote = m_scpPath->text().trimmed();
    QString name = remote.section('/', -1).section(':', -1);
    if (!SftpClient::isSafeName(name)) {
        note(tr("Type the path of a file to fetch first."));
        return;
    }
    Job j;
    j.local = QDir(m_localDir).filePath(name);
    j.remote = remote;
    addJob(j);
}

void FileTransferPanel::queueDownload(const SftpClient::Entry &e, const QString &remote,
                                      const QString &localDir, int depth)
{
    const QString local = QDir(localDir).filePath(e.name);
    if (e.isLink) {
        // Links are followed for files only: a link to a folder can loop.
        m_walking++;
        m_sftp->stat(remote, [=](bool ok, const QString &, const SftpClient::Entry &target) {
            m_walking--;
            if (ok && !target.isDir) {
                SftpClient::Entry f = target;
                f.name = e.name;
                queueDownload(f, remote, localDir, depth);
            } else {
                note(tr("%1 is a link to a folder and was left out.").arg(e.name));
            }
            updateSummary();
        });
        return;
    }
    if (!e.isDir) {
        Job j;
        j.local = local;
        j.remote = remote;
        j.size = e.size;
        addJob(j);
        return;
    }
    if (depth >= kMaxDepth || !QDir().mkpath(local)) {
        note(tr("Could not create %1.").arg(local));
        return;
    }
    m_walking++;
    m_sftp->list(remote, [=](bool ok, const QString &err, const QList<SftpClient::Entry> &entries) {
        m_walking--;
        if (!ok)
            note(tr("%1: %2").arg(remote, err));
        for (const SftpClient::Entry &c : entries)
            queueDownload(c, SftpClient::joinPath(remote, c.name), local, depth + 1);
        updateSummary();
    });
    updateSummary();
}

void FileTransferPanel::addJob(Job job)
{
    job.item = new QTreeWidgetItem(m_queue);
    job.item->setText(0, QFileInfo(job.upload ? job.local : job.remote).fileName());
    job.item->setToolTip(0, job.local + QStringLiteral(" : ") + job.remote);
    job.item->setText(1, job.upload ? tr("Upload") : tr("Download"));
    job.item->setText(2, job.makeDir ? QString() : sizeText(job.size));
    job.item->setText(3, tr("Queued"));
    m_jobs.append(job);
    m_overwriteAll = m_current >= 0 && m_overwriteAll;
    updateSummary();
    if (m_current < 0)
        startNext();
}

void FileTransferPanel::setJobText(const Job &job, const QString &status)
{
    job.item->setText(3, status);
}

bool FileTransferPanel::mayOverwrite(const QString &name)
{
    if (m_overwriteAll)
        return true;
    const int choice = DialogKit::message(
        window(), DialogKit::Kind::Question, tr("Replace the file?"),
        tr("%1 already exists.").arg(name), {tr("Skip"), tr("Replace"), tr("Replace All")}, 0);
    m_overwriteAll = choice == 2;
    return choice == 1 || choice == 2;
}

void FileTransferPanel::startNext()
{
    if (m_current >= 0)
        return;
    int next = -1;
    for (int i = 0; i < m_jobs.size() && next < 0; i++)
        if (m_jobs[i].state == State::Queued)
            next = i;
    if (next < 0) {
        m_overwriteAll = false;
        updateSummary();
        if (!m_jobs.isEmpty() && m_sftpReady)
            loadRemote(m_remoteDir);
        return;
    }
    if (!m_scpOnly && !m_sftpReady)
        return;               // carries on after a reconnect
    m_current = next;
    Job &job = m_jobs[next];
    job.state = State::Running;
    setJobText(job, tr("Starting"));
    m_clock->restart();
    updateSummary();
    if (m_scpOnly)
        runScp(job);
    else
        runSftp(job);
}

void FileTransferPanel::runSftp(Job &job)
{
    auto progress = [this](qint64 done, qint64 total) {
        if (m_current < 0)
            return;
        const double secs = qMax<qint64>(m_clock->elapsed(), 1) / 1000.0;
        setJobText(m_jobs[m_current], tr("%1% at %2/s").arg(total ? done * 100 / total : 100)
                                          .arg(sizeText(qint64(done / secs))));
    };
    auto done = [this](bool ok, const QString &err) { finishJob(ok, err); };
    if (job.makeDir) {
        // Already there is fine: the files still go in.
        const QString remote = job.remote;
        m_sftp->makeDir(remote, [this, remote](bool ok, const QString &err) {
            if (ok) {
                finishJob(true, QString());
                return;
            }
            m_sftp->stat(remote, [this, err](bool found, const QString &, const SftpClient::Entry &e) {
                finishJob(found && e.isDir, err);
            });
        });
    } else if (job.upload) {
        const QString local = job.local, remote = job.remote;
        m_sftp->stat(remote, [=](bool exists, const QString &, const SftpClient::Entry &) {
            if (exists && !mayOverwrite(remote)) {
                finishJob(false, QStringLiteral("skip"));
                return;
            }
            m_sftp->upload(local, remote, progress, done);
        });
    } else {
        if (QFileInfo::exists(job.local) && !mayOverwrite(job.local)) {
            finishJob(false, QStringLiteral("skip"));
            return;
        }
        m_sftp->download(job.remote, job.local, job.size, progress, done);
    }
}

void FileTransferPanel::runScp(Job &job)
{
    if (!job.upload && QFileInfo::exists(job.local) && !mayOverwrite(job.local)) {
        finishJob(false, QStringLiteral("skip"));
        return;
    }
    const QByteArray command = ScpClient::commandFor(job.upload, job.remote).toUtf8();
    sshlx_conf *c = sshlx_conf_for_transfer(m_conf, command.constData(), false);
    auto *s = new SessionWidget(c, tr("SCP"));
    sshlx_conf_free(c);
    m_scpSession = s;
    s->setReconnectBarEnabled(false);
    s->setFixedHeight(160);        // shows the login until the channel opens
    if (m_configure)
        m_configure(s);
    s->setRawReceiver([this](const char *d, size_t n) { m_scp->feed(d, n); });
    disconnect(m_scp, &ScpClient::send, nullptr, nullptr);
    connect(m_scp, &ScpClient::send, s, &SessionWidget::sendRaw);
    m_scpHost->addWidget(s);
    const QString name = job.remote.section('/', -1).section(':', -1);
    const bool upload = job.upload;
    const QString local = job.local;
    if (upload)
        m_scp->startUpload(local, name);
    connect(s, &SessionWidget::stateChanged, this, [this, s, upload, local](sshlx_state st) {
        if (st == SSHLX_STATE_CONNECTED) {
            s->hide();
            whenSendReady(s, [this, upload, local] {
                if (upload)
                    m_scpPump->start();
                else
                    m_scp->startDownload(local);
            });
        } else if (st == SSHLX_STATE_DISCONNECTED && s == m_scpSession) {
            const QString why = s->lastError();
            m_scp->connectionLost(why.isEmpty() ? tr("The connection closed early.") : why);
        }
    });
    s->connectSession();
}

void FileTransferPanel::finishJob(bool ok, const QString &error)
{
    if (m_current < 0)
        return;
    Job &job = m_jobs[m_current];
    if (job.state == State::Cancelled) {
        setJobText(job, tr("Cancelled"));
    } else if (ok) {
        job.state = State::Done;
        setJobText(job, tr("Done"));
    } else if (error == QLatin1String("skip")) {
        job.state = State::Skipped;
        setJobText(job, tr("Skipped"));
    } else if (error.isEmpty()) {
        job.state = State::Cancelled;
        setJobText(job, tr("Cancelled"));
    } else {
        job.state = State::Failed;
        setJobText(job, tr("Failed: %1").arg(error));
        job.item->setToolTip(3, error);
    }
    m_current = -1;
    updateSummary();
    startNext();
}

void FileTransferPanel::cancelTransfers()
{
    for (Job &j : m_jobs) {
        if (j.state == State::Queued) {
            j.state = State::Cancelled;
            setJobText(j, tr("Cancelled"));
        }
    }
    if (m_current >= 0) {
        m_jobs[m_current].state = State::Cancelled;
        if (m_scpOnly)
            m_scp->cancel();
        else if (m_sftp->transferring())
            m_sftp->cancelTransfer();
    }
    updateSummary();
}

void FileTransferPanel::clearFinished()
{
    for (int i = int(m_jobs.size()) - 1; i >= 0; i--) {
        if (i != m_current && m_jobs[i].state != State::Queued) {
            delete m_jobs[i].item;
            m_jobs.removeAt(i);
            if (m_current > i)
                m_current--;
        }
    }
    updateSummary();
}

void FileTransferPanel::updateSummary()
{
    int waiting = 0, failed = 0, done = 0;
    for (const Job &j : m_jobs) {
        waiting += j.state == State::Queued || j.state == State::Running;
        failed += j.state == State::Failed;
        done += j.state == State::Done;
    }
    m_cancel->setEnabled(waiting > 0);
    if (m_jobs.isEmpty())
        m_summary->setText(tr("Drag files between the two sides, or use the buttons."));
    else if (waiting || m_walking)
        m_summary->setText(tr("%n file(s) left", nullptr, waiting));
    else
        m_summary->setText(failed ? tr("%1 done, %2 failed").arg(done).arg(failed)
                                  : tr("%1 done").arg(done));
}

void FileTransferPanel::showRemoteMenu(const QPoint &pos)
{
    QTreeWidgetItem *item = m_remote->itemAt(pos);
    QMenu menu;
    if (item) {
        menu.addAction(Icons::get("import"), tr("&Download"), this, &FileTransferPanel::downloadSelected);
        menu.addAction(tr("&Rename..."), this, [this, item] { renameRemote(item); });
        menu.addAction(Icons::get("close"), tr("De&lete..."), this, &FileTransferPanel::deleteRemote);
        menu.addAction(Icons::get("copy"), tr("Copy &Path"), this, [this, item] {
            QApplication::clipboard()->setText(SftpClient::joinPath(m_remoteDir, item->text(ColName)));
        });
        menu.addSeparator();
    }
    menu.addAction(Icons::get("folder-new"), tr("&New Folder..."), this, &FileTransferPanel::newRemoteFolder);
    menu.addAction(Icons::get("reconnect"), tr("Re&fresh"), this, [this] { loadRemote(m_remoteDir); });
    menu.exec(m_remote->viewport()->mapToGlobal(pos));
}

void FileTransferPanel::newRemoteFolder()
{
    bool ok = false;
    const QString name = QInputDialog::getText(window(), tr("New Folder"), tr("Name:"),
                                               QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || name.isEmpty())
        return;
    m_sftp->makeDir(SftpClient::joinPath(m_remoteDir, name), [this](bool done, const QString &err) {
        if (!done)
            note(err);
        loadRemote(m_remoteDir);
    });
}

void FileTransferPanel::renameRemote(QTreeWidgetItem *item)
{
    const QString old = item->text(ColName);
    bool ok = false;
    const QString name = QInputDialog::getText(window(), tr("Rename"), tr("New name:"),
                                               QLineEdit::Normal, old, &ok).trimmed();
    if (!ok || name.isEmpty() || name == old)
        return;
    m_sftp->rename(SftpClient::joinPath(m_remoteDir, old), SftpClient::joinPath(m_remoteDir, name),
                   [this](bool done, const QString &err) {
        if (!done)
            note(err);
        loadRemote(m_remoteDir);
    });
}

void FileTransferPanel::deleteRemote()
{
    const QList<QTreeWidgetItem *> items = m_remote->selectedItems();
    if (items.isEmpty())
        return;
    const QStringList names = selectedRemoteNames();
    const int choice = DialogKit::message(
        window(), DialogKit::Kind::Warning, tr("Delete from the server?"),
        tr("This cannot be undone. A folder must be empty first."),
        {tr("Cancel"), tr("Delete")}, 0, names.join('\n'), true);
    if (choice != 1)
        return;
    for (QTreeWidgetItem *i : items) {
        const QString path = SftpClient::joinPath(m_remoteDir, i->text(ColName));
        auto done = [this](bool ok, const QString &err) {
            if (!ok)
                note(err);
            loadRemote(m_remoteDir);
        };
        if (i->data(ColName, RoleDir).toBool())
            m_sftp->removeDir(path, done);
        else
            m_sftp->removeFile(path, done);
    }
}

bool FileTransferPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::FocusIn && (watched == m_remote || watched == m_localView))
        setRemoteActive(watched == m_remote);
    const bool toRemote = watched == m_remote->viewport();
    const bool toLocal = watched == m_localView->viewport();
    if (!toRemote && !toLocal)
        return QWidget::eventFilter(watched, event);
    if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove) {
        auto *e = static_cast<QDragMoveEvent *>(event);
        const bool fromServer = e->mimeData()->hasFormat(kRemoteMime);
        if ((toRemote && e->mimeData()->hasUrls() && !fromServer) || (toLocal && fromServer)) {
            e->setDropAction(Qt::CopyAction);
            e->accept();
        } else {
            e->ignore();
        }
        return true;
    }
    if (event->type() == QEvent::Drop) {
        auto *e = static_cast<QDropEvent *>(event);
        if (toRemote) {
            QStringList paths;
            for (const QUrl &u : e->mimeData()->urls())
                if (u.isLocalFile())
                    paths << u.toLocalFile();
            uploadPaths(paths);
        } else {
            downloadSelected();
        }
        e->setDropAction(Qt::CopyAction);
        e->accept();
        return true;
    }
    return QWidget::eventFilter(watched, event);
}
