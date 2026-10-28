#include "connectiontreemodel.h"
#include "icons.h"

#include <QMimeData>

#include "connectionstore.h"
#include "serialportlist.h"
#include "sessionwidget.h"

// Internal pointers: tree nodes use their ConnectionNode*; the Serial Ports
// section header uses &kSerialSection; serial devices use &kSerialDevice
// (their row identifies the device).
static char kSerialSection;
static char kSerialDevice;

static const char *kMime = "application/x-sshlx-node-ids";

ConnectionTreeModel::ConnectionTreeModel(ConnectionStore *store, QObject *parent)
    : QAbstractItemModel(parent), m_store(store)
{
    connect(store, &ConnectionStore::aboutToReset, this, [this] { beginResetModel(); });
    connect(store, &ConnectionStore::reset, this, [this] { endResetModel(); });
    connect(store, &ConnectionStore::structureChanged, this, [this] {
        beginResetModel();
        endResetModel();
    });
    connect(store, &ConnectionStore::nodeChanged, this, [this](ConnectionNode *n) {
        QModelIndex i = indexOf(n);
        if (i.isValid())
            emit dataChanged(i, i);
    });
}

void ConnectionTreeModel::setSerialPorts(SerialPortList *ports)
{
    beginResetModel();
    m_serial = ports;
    endResetModel();
    if (!ports)
        return;
    // The device list changes between these two signals.
    connect(ports, &SerialPortList::aboutToChange, this, [this] { beginResetModel(); });
    connect(ports, &SerialPortList::changed, this, [this] { endResetModel(); });
}

ConnectionNode *ConnectionTreeModel::node(const QModelIndex &idx) const
{
    if (!idx.isValid())
        return m_store->tree().root();
    void *p = idx.internalPointer();
    if (p == &kSerialSection || p == &kSerialDevice)
        return nullptr;
    return static_cast<ConnectionNode *>(p);
}

bool ConnectionTreeModel::isSerialSection(const QModelIndex &idx) const
{
    return idx.isValid() && idx.internalPointer() == &kSerialSection;
}

bool ConnectionTreeModel::isSerialDevice(const QModelIndex &idx) const
{
    return idx.isValid() && idx.internalPointer() == &kSerialDevice;
}

int ConnectionTreeModel::serialDeviceRow(const QModelIndex &idx) const
{
    return isSerialDevice(idx) ? idx.row() : -1;
}

QModelIndex ConnectionTreeModel::indexOf(ConnectionNode *n) const
{
    if (!n || n == m_store->tree().root() || !n->parent)
        return {};
    return createIndex(n->row(), 0, n);
}

QModelIndex ConnectionTreeModel::index(int row, int column, const QModelIndex &parent) const
{
    if (column != 0 || row < 0)
        return {};
    if (isSerialSection(parent)) {
        if (m_serial && row < m_serial->count())
            return createIndex(row, 0, &kSerialDevice);
        return {};
    }
    if (isSerialDevice(parent))
        return {};
    ConnectionNode *p = node(parent);
    if (!p)
        return {};
    if (row < p->children.size())
        return createIndex(row, 0, p->children[row]);
    if (!parent.isValid() && m_serial && row == p->children.size())
        return createIndex(row, 0, &kSerialSection);
    return {};
}

QModelIndex ConnectionTreeModel::parent(const QModelIndex &child) const
{
    if (!child.isValid())
        return {};
    if (child.internalPointer() == &kSerialSection)
        return {};
    if (child.internalPointer() == &kSerialDevice)
        return createIndex(m_store->tree().root()->children.size(), 0, &kSerialSection);
    ConnectionNode *n = node(child);
    if (!n || !n->parent || n->parent == m_store->tree().root())
        return {};
    return createIndex(n->parent->row(), 0, n->parent);
}

int ConnectionTreeModel::rowCount(const QModelIndex &parent) const
{
    if (isSerialSection(parent))
        return m_serial ? m_serial->count() : 0;
    if (isSerialDevice(parent))
        return 0;
    ConnectionNode *p = node(parent);
    if (!p || !p->isFolder())
        return 0;
    int n = p->children.size();
    if (!parent.isValid() && m_serial)
        n++;
    return n;
}

int ConnectionTreeModel::columnCount(const QModelIndex &) const
{
    return 1;
}

QIcon ConnectionTreeModel::iconFor(const QString &iconName)
{
    return Icons::forNode(iconName);
}

QVariant ConnectionTreeModel::data(const QModelIndex &idx, int role) const
{
    if (!idx.isValid())
        return {};
    if (isSerialSection(idx)) {
        switch (role) {
        case Qt::DisplayRole: return tr("Serial Ports");
        case Qt::DecorationRole: return Icons::get("usb");
        case IsFolderRole: return true;
        case SearchTextRole: return tr("Serial Ports");
        default: return {};
        }
    }
    if (isSerialDevice(idx)) {
        if (!m_serial)
            return {};
        const SerialPortList::Device &d = m_serial->at(idx.row());
        switch (role) {
        case Qt::DisplayRole: return d.label();
        case Qt::ToolTipRole: return d.tooltip();
        case Qt::DecorationRole: return Icons::get("proto-serial");
        case SerialDeviceRole: return d.node;
        case SearchTextRole: return d.label() + ' ' + d.tooltip();
        default: return {};
        }
    }
    ConnectionNode *n = node(idx);
    switch (role) {
    case Qt::DisplayRole:
    case Qt::EditRole:
        return n->name;
    case Qt::ToolTipRole: {
        if (n->isFolder())
            return n->description.isEmpty() ? n->name : n->description;
        QString t = QStringLiteral("<b>%1</b><br>%2").arg(n->name.toHtmlEscaped(),
                                                          m_store->hostOf(n).toHtmlEscaped());
        if (!n->description.isEmpty())
            t += "<br>" + n->description.toHtmlEscaped();
        return t;
    }
    case Qt::DecorationRole:
        if (!n->icon.isEmpty())
            return iconFor(n->icon);
        if (n->isFolder())
            return Icons::get("folder");
        return SessionWidget::iconForProtocol(m_store->protocolOf(n));
    case Qt::FontRole:
        if (n->favourite) {
            QFont f;
            f.setBold(true);
            return f;
        }
        return {};
    case NodeIdRole:
        return n->id;
    case IsFolderRole:
        return n->isFolder();
    case FavouriteRole:
        return n->favourite;
    case SearchTextRole:
        return n->name + ' ' + n->description + ' ' +
               (n->isFolder() ? QString() : m_store->hostOf(n));
    default:
        return {};
    }
}

bool ConnectionTreeModel::setData(const QModelIndex &idx, const QVariant &value, int role)
{
    ConnectionNode *n = node(idx);
    if (!idx.isValid() || !n || role != Qt::EditRole)
        return false;
    QString err;
    if (!m_store->rename(n, value.toString(), &err)) {
        if (!err.isEmpty())
            emit renameFailed(err);
        return false;
    }
    emit dataChanged(idx, idx);
    return true;
}

Qt::ItemFlags ConnectionTreeModel::flags(const QModelIndex &idx) const
{
    if (!idx.isValid())
        return Qt::ItemIsDropEnabled;
    if (isSerialSection(idx) || isSerialDevice(idx))
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    ConnectionNode *n = node(idx);
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable |
                      Qt::ItemIsEditable | Qt::ItemIsDragEnabled;
    if (n->isFolder())
        f |= Qt::ItemIsDropEnabled;
    return f;
}

Qt::DropActions ConnectionTreeModel::supportedDropActions() const
{
    return Qt::MoveAction;
}

QStringList ConnectionTreeModel::mimeTypes() const
{
    return {QString::fromLatin1(kMime)};
}

QMimeData *ConnectionTreeModel::mimeData(const QModelIndexList &indexes) const
{
    QStringList ids;
    for (const QModelIndex &i : indexes)
        if (ConnectionNode *n = node(i); n && i.isValid())
            ids << n->id;
    auto *m = new QMimeData;
    m->setData(kMime, ids.join('\n').toUtf8());
    return m;
}

bool ConnectionTreeModel::dropMimeData(const QMimeData *data, Qt::DropAction action,
                                       int row, int, const QModelIndex &parent)
{
    if (action != Qt::MoveAction || !data->hasFormat(kMime))
        return false;
    if (isSerialSection(parent) || isSerialDevice(parent))
        return false;
    ConnectionNode *dst = node(parent);
    if (!dst || !dst->isFolder())
        return false;
    const QStringList ids = QString::fromUtf8(data->data(kMime)).split('\n', Qt::SkipEmptyParts);
    int insertAt = row < 0 ? dst->children.size() : row;
    for (const QString &id : ids) {
        ConnectionNode *n = m_store->tree().findById(id);
        if (!n)
            continue;
        bool intoSelf = false;
        for (ConnectionNode *p = dst; p; p = p->parent)
            if (p == n)
                intoSelf = true;
        if (intoSelf)
            continue;
        QModelIndex srcParent = indexOf(n->parent);
        int srcRow = n->row();
        if (!beginMoveRows(srcParent, srcRow, srcRow, parent, insertAt))
            continue;   // no-op move
        m_store->move(n, dst, insertAt);
        endMoveRows();
        insertAt = n->row() + 1;
    }
    // We moved the rows ourselves; tell the view not to delete the source.
    return false;
}
