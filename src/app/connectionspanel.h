// ConnectionsPanel - the "Connections" dock: toolbar, connection tree with
// drag & drop, search/filter box and context menu.
#pragma once

#include <QWidget>

class ConnectionStore;
class ConnectionNode;
class ConnectionTreeModel;
class SerialPortList;
class QTreeView;
class QLineEdit;
class QAction;
class QSortFilterProxyModel;

class ConnectionsPanel : public QWidget {
    Q_OBJECT
public:
    ConnectionsPanel(ConnectionStore *store, QWidget *parent = nullptr);

    void setSerialPorts(SerialPortList *ports);
    ConnectionNode *currentNode() const;
    ConnectionNode *currentFolder() const;   // selected folder or parent
    void select(ConnectionNode *n);
    void editName(ConnectionNode *n);
    QTreeView *view() const { return m_view; }
    ConnectionTreeModel *model() const { return m_model; }

public slots:
    void newConnection();
    void newFolder();
    void deleteSelected();
    void duplicateSelected();
    void sortAll();

signals:
    void openRequested(ConnectionNode *n);
    void openWithOptionsRequested(ConnectionNode *n);
    void openSerialRequested(int deviceRow, bool withOptions);
    void serialRuleRequested(int deviceRow);
    void currentNodeChanged(ConnectionNode *n);

private:
    QModelIndex toSource(const QModelIndex &i) const;
    QModelIndex fromSource(const QModelIndex &i) const;
    void restoreExpansion();
    void contextMenu(const QPoint &pos);
    void activated(const QModelIndex &proxyIndex);

    ConnectionStore *m_store;
    ConnectionTreeModel *m_model;
    QSortFilterProxyModel *m_proxy;
    QTreeView *m_view;
    QLineEdit *m_filter;
    QAction *m_favouritesOnly;
    bool m_serialExpanded = true;
};
