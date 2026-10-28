// ConnectionTreeModel - QAbstractItemModel over the ConnectionTree, with
// drag & drop to reorder or move between folders, and an optional live
// "Serial Ports" section (Phase 4) appended after the user's tree.
#pragma once

#include <QAbstractItemModel>
#include <QIcon>
#include <QList>

class ConnectionStore;
class ConnectionNode;
class SerialPortList;

class ConnectionTreeModel : public QAbstractItemModel {
    Q_OBJECT
public:
    enum Roles {
        NodeIdRole = Qt::UserRole + 1,
        IsFolderRole,
        FavouriteRole,
        SerialDeviceRole,   // non-empty for entries in the Serial Ports section
        SearchTextRole      // name + description + host, for the filter
    };

    explicit ConnectionTreeModel(ConnectionStore *store, QObject *parent = nullptr);

    void setSerialPorts(SerialPortList *ports);

    ConnectionNode *node(const QModelIndex &idx) const;
    QModelIndex indexOf(ConnectionNode *n) const;
    bool isSerialSection(const QModelIndex &idx) const;
    bool isSerialDevice(const QModelIndex &idx) const;
    int serialDeviceRow(const QModelIndex &idx) const;

    QModelIndex index(int row, int column, const QModelIndex &parent = {}) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &idx, int role) const override;
    bool setData(const QModelIndex &idx, const QVariant &value, int role) override;
    Qt::ItemFlags flags(const QModelIndex &idx) const override;

    Qt::DropActions supportedDropActions() const override;
    QStringList mimeTypes() const override;
    QMimeData *mimeData(const QModelIndexList &indexes) const override;
    bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row,
                      int column, const QModelIndex &parent) override;

    static QIcon iconFor(const QString &iconName);

signals:
    void renameFailed(const QString &message);

private:
    ConnectionStore *m_store;
    SerialPortList *m_serial = nullptr;
};
