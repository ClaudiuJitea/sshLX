// PropertyGrid - the "Config" dock: edits the selected connection or
// folder. Changes are saved immediately: connection settings to its PuTTY
// saved session, folder values to connections.json. Connections can
// inherit any Conf key from their folders (third column).
#pragma once

#include <QPointer>
#include <QWidget>

#include "propertydefs.h"
#include "sshlx_engine.h"

class ConnectionStore;
class ConnectionNode;
class QTreeView;
class QStandardItemModel;
class QStandardItem;
class QTextBrowser;
class QToolButton;
class QLabel;

class PropertyGrid : public QWidget {
    Q_OBJECT
public:
    explicit PropertyGrid(ConnectionStore *store, QWidget *parent = nullptr);
    ~PropertyGrid() override;

    void setNode(ConnectionNode *n);
    ConnectionNode *node() const { return m_node; }
    void setAlphabetical(bool on);

    // Used by the editor delegate and tests.
    QString displayValue(const PropertyDef &d) const;
    QVariant editValue(const PropertyDef &d) const;
    void applyValue(const PropertyDef &d, const QVariant &v);
    bool isInherited(const PropertyDef &d) const;
    void setInherited(const PropertyDef &d, bool on);
    // For folders: does this folder itself define the key?
    bool folderDefines(const PropertyDef &d) const;
    void clearFolderValue(const PropertyDef &d);
    const PropertyDef *defAt(const QModelIndex &idx) const;
    QTreeView *view() const { return m_view; }

signals:
    void openRequested(ConnectionNode *n);
    void aboutRequested();

public slots:
    void reload();
    void fullSettings();

private:
    void rebuild();
    void refreshValues();
    sshlx_conf *workingConf() const;

    ConnectionStore *m_store;
    ConnectionNode *m_node = nullptr;
    sshlx_conf *m_conf = nullptr;   // effective conf (connection) or folder view
    bool m_alphabetical = false;
    bool m_updating = false;
    QTreeView *m_view;
    QStandardItemModel *m_model;
    QTextBrowser *m_help;
    QLabel *m_title;
    QToolButton *m_catButton, *m_alphaButton, *m_fullButton;
    QList<const PropertyDef *> m_rowDefs;
};
