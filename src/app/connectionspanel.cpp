#include "connectionspanel.h"
#include "bulkeditdialog.h"
#include "logondialog.h"
#include "tabcolours.h"
#include "macros.h"
#include "dialogkit.h"
#include "icons.h"

#include <QAction>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QSortFilterProxyModel>
#include <QToolBar>
#include <QTreeView>
#include <QVBoxLayout>

#include "connectionstore.h"
#include "connectiontreemodel.h"

namespace {
// Text filter over name/description/host, plus "favourites only".
class TreeFilter : public QSortFilterProxyModel {
public:
    bool favouritesOnly = false;
    QString text;
    using QSortFilterProxyModel::QSortFilterProxyModel;
    void refilter()
    {
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
        beginFilterChange();
        endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
        invalidateFilter();
#endif
    }

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override
    {
        QModelIndex i = sourceModel()->index(row, 0, parent);
        bool folder = i.data(ConnectionTreeModel::IsFolderRole).toBool();
        if (favouritesOnly && !folder &&
            !i.data(ConnectionTreeModel::FavouriteRole).toBool())
            return false;
        if (favouritesOnly && folder)
            return false;   // folders show only via recursive acceptance
        if (text.isEmpty())
            return !favouritesOnly || !folder;
        return i.data(ConnectionTreeModel::SearchTextRole).toString()
            .contains(text, Qt::CaseInsensitive);
    }
};
} // namespace

ConnectionsPanel::ConnectionsPanel(ConnectionStore *store, QWidget *parent)
    : QWidget(parent), m_store(store)
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(2);

    auto *tb = new QToolBar;
    tb->setIconSize(QSize(18, 18));
    QAction *aNew = tb->addAction(Icons::get("connection-new"), tr("New Connection"),
                                  this, &ConnectionsPanel::newConnection);
    aNew->setToolTip(tr("New connection"));
    tb->addAction(Icons::get("folder-new"), tr("New Folder"), this,
                  &ConnectionsPanel::newFolder);
    tb->addSeparator();
    tb->addAction(Icons::get("expand-all"), tr("Expand All"), this,
                  [this] { m_view->expandAll(); });
    tb->addAction(Icons::get("collapse-all"), tr("Collapse All"), this,
                  [this] { m_view->collapseAll(); });
    tb->addAction(Icons::get("sort-az"), tr("Sort A→Z"),
                  this, &ConnectionsPanel::sortAll);
    auto *spacer = new QWidget;
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tb->addWidget(spacer);
    m_favouritesOnly = tb->addAction(Icons::get("star"), tr("Show favourites only"));
    m_favouritesOnly->setCheckable(true);
    v->addWidget(tb);

    m_model = new ConnectionTreeModel(store, this);
    auto *proxy = new TreeFilter(this);
    proxy->setSourceModel(m_model);
    proxy->setRecursiveFilteringEnabled(true);
    proxy->setAutoAcceptChildRows(true);
    m_proxy = proxy;

    m_view = new QTreeView;
    m_view->setModel(m_proxy);
    m_view->setHeaderHidden(true);
    m_view->setDragDropMode(QAbstractItemView::InternalMove);
    m_view->setDefaultDropAction(Qt::MoveAction);
    m_view->setDragEnabled(true);
    m_view->setAcceptDrops(true);
    m_view->setDropIndicatorShown(true);
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_view->setEditTriggers(QAbstractItemView::EditKeyPressed |
                            QAbstractItemView::SelectedClicked);
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    m_view->setExpandsOnDoubleClick(false);
    m_view->setIconSize(QSize(18, 18));
    m_view->setIndentation(16);
    m_view->setFrameShape(QFrame::NoFrame);
    v->addWidget(m_view, 1);

    m_filter = new QLineEdit;
    m_filter->setPlaceholderText(tr("Search connections..."));
    m_filter->setClearButtonEnabled(true);
    m_filter->addAction(Icons::get("search"), QLineEdit::LeadingPosition);
    auto *filterRow = new QHBoxLayout;
    filterRow->setContentsMargins(6, 4, 6, 6);
    filterRow->addWidget(m_filter);
    v->addLayout(filterRow);

    connect(m_filter, &QLineEdit::textChanged, this, [this, proxy](const QString &t) {
        proxy->text = t.trimmed();
        proxy->refilter();
        if (!t.isEmpty() || proxy->favouritesOnly)
            m_view->expandAll();
        else
            restoreExpansion();
    });
    connect(m_favouritesOnly, &QAction::toggled, this, [this, proxy](bool on) {
        proxy->favouritesOnly = on;
        proxy->refilter();
        if (on)
            m_view->expandAll();
        else
            restoreExpansion();
    });
    connect(m_view, &QTreeView::customContextMenuRequested, this, &ConnectionsPanel::contextMenu);
    connect(m_view, &QTreeView::doubleClicked, this, &ConnectionsPanel::activated);
    connect(m_view, &QTreeView::expanded, this, [this](const QModelIndex &i) {
        QModelIndex s = toSource(i);
        if (m_model->isSerialSection(s)) { m_serialExpanded = true; return; }
        if (ConnectionNode *n = m_model->node(s); n && n->isFolder() && !n->expanded) {
            n->expanded = true;
            if (m_filter->text().isEmpty() && !m_favouritesOnly->isChecked())
                m_store->save();
        }
    });
    connect(m_view, &QTreeView::collapsed, this, [this](const QModelIndex &i) {
        QModelIndex s = toSource(i);
        if (m_model->isSerialSection(s)) { m_serialExpanded = false; return; }
        if (ConnectionNode *n = m_model->node(s); n && n->isFolder() && n->expanded) {
            n->expanded = false;
            m_store->save();
        }
    });
    connect(m_view->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex &cur) {
        QModelIndex s = toSource(cur);
        emit currentNodeChanged(s.isValid() ? m_model->node(s) : nullptr);
    });
    connect(m_model, &QAbstractItemModel::modelReset, this, [this] {
        QMetaObject::invokeMethod(this, &ConnectionsPanel::restoreExpansion, Qt::QueuedConnection);
    });
    connect(m_model, &QAbstractItemModel::rowsMoved, this, [this] {
        QMetaObject::invokeMethod(this, &ConnectionsPanel::restoreExpansion, Qt::QueuedConnection);
    });
    connect(m_model, &ConnectionTreeModel::renameFailed, this, [this](const QString &m) {
        DialogKit::warning(this, tr("Can't rename"), m);
    });
    restoreExpansion();
}

void ConnectionsPanel::setSerialPorts(SerialPortList *ports)
{
    m_model->setSerialPorts(ports);
    restoreExpansion();
}

QModelIndex ConnectionsPanel::toSource(const QModelIndex &i) const
{
    return m_proxy->mapToSource(i);
}

QModelIndex ConnectionsPanel::fromSource(const QModelIndex &i) const
{
    return m_proxy->mapFromSource(i);
}

void ConnectionsPanel::restoreExpansion()
{
    if (!m_filter->text().isEmpty() || m_favouritesOnly->isChecked())
        return;
    std::function<void(const QModelIndex &)> rec = [&](const QModelIndex &parent) {
        for (int r = 0; r < m_proxy->rowCount(parent); r++) {
            QModelIndex pi = m_proxy->index(r, 0, parent);
            QModelIndex si = toSource(pi);
            if (m_model->isSerialSection(si)) {
                m_view->setExpanded(pi, m_serialExpanded);
                continue;
            }
            ConnectionNode *n = m_model->node(si);
            if (n && n->isFolder()) {
                m_view->setExpanded(pi, n->expanded);
                rec(pi);
            }
        }
    };
    rec(QModelIndex());
}

ConnectionNode *ConnectionsPanel::currentNode() const
{
    QModelIndex s = toSource(m_view->currentIndex());
    return s.isValid() ? m_model->node(s) : nullptr;
}

ConnectionNode *ConnectionsPanel::currentFolder() const
{
    ConnectionNode *n = currentNode();
    if (!n)
        return m_store->tree().root();
    return n->isFolder() ? n : n->parent;
}

void ConnectionsPanel::select(ConnectionNode *n)
{
    QModelIndex p = fromSource(m_model->indexOf(n));
    if (!p.isValid())
        return;
    for (QModelIndex a = p.parent(); a.isValid(); a = a.parent())
        m_view->expand(a);
    m_view->setCurrentIndex(p);
    m_view->scrollTo(p);
}

void ConnectionsPanel::editName(ConnectionNode *n)
{
    select(n);
    m_view->edit(m_view->currentIndex());
}

QList<ConnectionNode *> ConnectionsPanel::selectedConnections() const
{
    QList<ConnectionNode *> out;
    const std::function<void(ConnectionNode *)> add = [&](ConnectionNode *n) {
        if (n->isFolder()) {
            for (ConnectionNode *c : n->children)
                add(c);
        } else if (!out.contains(n)) {
            out.append(n);
        }
    };
    for (const QModelIndex &pi : m_view->selectionModel()->selectedRows()) {
        const QModelIndex s = toSource(pi);
        if (ConnectionNode *n = m_model->node(s))
            add(n);
    }
    return out;
}

void ConnectionsPanel::newConnection()
{
    ConnectionNode *parent = currentFolder();
    ConnectionNode *n = m_store->newConnection(parent, tr("New Connection"));
    QMetaObject::invokeMethod(this, [this, n] { editName(n); }, Qt::QueuedConnection);
}

void ConnectionsPanel::newFolder()
{
    ConnectionNode *parent = currentFolder();
    ConnectionNode *f = m_store->newFolder(parent, tr("New Folder"));
    QMetaObject::invokeMethod(this, [this, f] { editName(f); }, Qt::QueuedConnection);
}

void ConnectionsPanel::duplicateSelected()
{
    if (ConnectionNode *n = currentNode(); n && !n->isFolder()) {
        ConnectionNode *d = m_store->duplicate(n);
        QMetaObject::invokeMethod(this, [this, d] { select(d); }, Qt::QueuedConnection);
    }
}

void ConnectionsPanel::deleteSelected()
{
    QList<ConnectionNode *> nodes;
    for (const QModelIndex &pi : m_view->selectionModel()->selectedRows()) {
        ConnectionNode *n = m_model->node(toSource(pi));
        if (n && n != m_store->tree().root())
            nodes << n;
    }
    if (nodes.isEmpty())
        return;
    // Drop nodes whose ancestor is also selected.
    QList<ConnectionNode *> top;
    for (ConnectionNode *n : nodes) {
        bool covered = false;
        for (ConnectionNode *p = n->parent; p; p = p->parent)
            if (nodes.contains(p))
                covered = true;
        if (!covered)
            top << n;
    }
    int sessions = 0;
    for (ConnectionNode *n : top) {
        QList<ConnectionNode *> st{n};
        while (!st.isEmpty()) {
            ConnectionNode *x = st.takeLast();
            if (!x->isFolder())
                sessions++;
            st << x->children;
        }
    }
    QString msg = top.size() == 1
        ? tr("Delete \"%1\"?").arg(top.first()->name)
        : tr("Delete %1 items?").arg(top.size());
    if (sessions)
        msg += "\n\n" + tr("This also deletes %n PuTTY saved session(s).", nullptr, sessions);
    if (!DialogKit::confirm(this, tr("Delete"), msg, tr("Delete"), tr("Cancel"), false, true))
        return;
    for (ConnectionNode *n : top)
        m_store->remove(n);
}

void ConnectionsPanel::sortAll()
{
    m_store->tree().sortAlphabetically(m_store->tree().root());
    m_store->save();
    emit m_store->structureChanged();
}

void ConnectionsPanel::activated(const QModelIndex &proxyIndex)
{
    QModelIndex s = toSource(proxyIndex);
    if (m_model->isSerialDevice(s)) {
        emit openSerialRequested(m_model->serialDeviceRow(s), false);
        return;
    }
    ConnectionNode *n = m_model->node(s);
    if (!n)
        return;
    if (n->isFolder())
        m_view->setExpanded(proxyIndex, !m_view->isExpanded(proxyIndex));
    else
        emit openRequested(n);
}

void ConnectionsPanel::contextMenu(const QPoint &pos)
{
    QModelIndex pi = m_view->indexAt(pos);
    QModelIndex s = toSource(pi);
    QMenu menu;
    if (m_model->isSerialDevice(s)) {
        int row = m_model->serialDeviceRow(s);
        menu.addAction(Icons::get("terminal"), tr("&Connect"), this, [this, row] { emit openSerialRequested(row, false); });
        menu.addAction(Icons::get("settings"), tr("Connect with &Options..."), this,
                       [this, row] { emit openSerialRequested(row, true); });
        menu.addAction(Icons::get("usb"), tr("Device &Rule..."), this, [this, row] { emit serialRuleRequested(row); });
        menu.addSeparator();
        menu.addAction(tr("&Hide Port"), this, [this, row] { emit serialHideRequested(row); });
        menu.exec(m_view->viewport()->mapToGlobal(pos));
        return;
    }
    ConnectionNode *n = pi.isValid() ? m_model->node(s) : nullptr;
    if (n && !n->isFolder()) {
        menu.addAction(Icons::get("terminal"), tr("&Connect"), this, [this, n] { emit openRequested(n); });
        menu.addAction(tr("Open in &New Tab"), this, [this, n] { emit openInNewTabRequested(n); });
        menu.addAction(Icons::get("settings"), tr("Connect with &Options..."), this,
                       [this, n] { emit openWithOptionsRequested(n); });
        menu.addSeparator();
        menu.addAction(Icons::get("export"), tr("File &Transfer"), this,
                       [this, n] { emit fileTransferRequested(n); });
        menu.addAction(Icons::get("copy"), tr("D&uplicate"), this, &ConnectionsPanel::duplicateSelected);
        QAction *fav = menu.addAction(tr("&Favourite"));
        fav->setCheckable(true);
        fav->setChecked(n->favourite);
        connect(fav, &QAction::toggled, this, [this, n](bool on) {
            n->favourite = on;
            m_store->touch(n);
        });
    }
    if (n) {
        menu.addAction(Icons::get("script"), tr("&Logon Actions..."), this, [this, n] {
            LogonDialog dlg(n->logon, m_store->tree().credentials(), n->isFolder(), this);
            dlg.setScript(n->logonScript);
            dlg.setAntiIdle(n->antiIdleText, n->antiIdleSeconds);
            if (dlg.exec() == QDialog::Accepted) {
                n->logon = dlg.steps();
                n->logonScript = dlg.script();
                n->antiIdleText = dlg.antiIdleText();
                n->antiIdleSeconds = dlg.antiIdleSeconds();
                m_store->touch(n);
            }
        });
        TabColours::fillMenu(menu.addMenu(Icons::get("palette"), tr("Tab Co&lour")), n->tabColour, this,
                             [this, n](const QString &colour) {
            n->tabColour = colour;
            m_store->touch(n);
        });
        QMenu *bars = menu.addMenu(tr("&Button Bar"));
        const auto setBar = [this, n](const QString &name) {
            n->buttonBar = name;
            m_store->touch(n);
        };
        QAction *none = bars->addAction(tr("(as chosen, or the folder's)"), this, [setBar] { setBar(QString()); });
        none->setCheckable(true);
        none->setChecked(n->buttonBar.isEmpty());
        const QStringList names = ButtonBar::barNames();
        for (const QString &b : names) {
            if (b.isEmpty())
                continue;
            QAction *a = bars->addAction(b, this, [setBar, b] { setBar(b); });
            a->setCheckable(true);
            a->setChecked(n->buttonBar == b);
        }
        const QList<ConnectionNode *> many = selectedConnections();
        if (many.size() > 1)
            menu.addAction(Icons::get("settings"), tr("&Edit %n Connection(s)...", nullptr, int(many.size())),
                           this, [this, many] {
                BulkEditDialog dlg(m_store, many, this);
                dlg.exec();
            });
        menu.addAction(tr("&Rename"), this, [this, n] { editName(n); });
        menu.addAction(Icons::get("close"), tr("&Delete"), this, &ConnectionsPanel::deleteSelected);
        menu.addSeparator();
    }
    menu.addAction(Icons::get("connection-new"), tr("New Co&nnection"), this, &ConnectionsPanel::newConnection);
    menu.addAction(Icons::get("folder-new"), tr("New F&older"), this, &ConnectionsPanel::newFolder);
    menu.exec(m_view->viewport()->mapToGlobal(pos));
}
