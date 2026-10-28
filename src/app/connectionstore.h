// ConnectionStore - owns the ConnectionTree, keeps it in sync with PuTTY's
// saved sessions and performs operations that touch both (create, rename,
// duplicate, delete), plus effective-Conf construction with inheritance.
#pragma once

#include <QHash>
#include <QObject>

#include "connectiontree.h"
#include "sshlx_engine.h"

class ConnectionStore : public QObject {
    Q_OBJECT
public:
    explicit ConnectionStore(const QString &path = ConnectionTree::defaultPath(),
                             QObject *parent = nullptr);

    ConnectionTree &tree() { return m_tree; }
    QString path() const { return m_path; }

    // Load the JSON file and import any PuTTY sessions not yet in the tree.
    bool load(QString *error = nullptr);
    void save();
    // Re-scan PuTTY's saved sessions (e.g. after "Save" in PuTTY's dialog).
    void syncSessions();
    QStringList savedSessions() const;

    // The Conf to launch a connection with: its PuTTY saved session plus
    // values inherited from folders. Caller frees.
    sshlx_conf *effectiveConf(const ConnectionNode *n) const;
    // Save conf as the connection's PuTTY session. Inherited keys are
    // written with their effective values, so PuTTY itself sees the same
    // settings sshLX uses.
    bool saveConf(ConnectionNode *n, const sshlx_conf *conf, QString *error = nullptr);

    ConnectionNode *newFolder(ConnectionNode *parent, const QString &name);
    // Creates a PuTTY session (from Default Settings, or from conf).
    ConnectionNode *newConnection(ConnectionNode *parent, const QString &name,
                                  const sshlx_conf *conf = nullptr);
    ConnectionNode *duplicate(ConnectionNode *n);
    bool rename(ConnectionNode *n, const QString &newName, QString *error = nullptr);
    // Deletes the node; for connections also their PuTTY saved sessions.
    void remove(ConnectionNode *n);
    bool move(ConnectionNode *n, ConnectionNode *newParent, int index);
    void setFolderProperty(ConnectionNode *folder, const QString &key, const QString &value);
    void clearFolderProperty(ConnectionNode *folder, const QString &key);
    void setInherited(ConnectionNode *n, const QString &key, bool inherit);
    void touch(ConnectionNode *n);   // metadata changed: save + notify

    // Cached protocol of a connection's session (for icons).
    int protocolOf(const ConnectionNode *n) const;
    QString hostOf(const ConnectionNode *n) const;
    QString uniqueSessionName(const QString &base) const;

signals:
    void aboutToReset();
    void reset();
    void nodeChanged(ConnectionNode *n);
    void structureChanged();

private:
    struct Info { int protocol; QString host; };
    Info info(const ConnectionNode *n) const;

    ConnectionTree m_tree;
    QString m_path;
    mutable QHash<QString, Info> m_infoCache;   // by session name
};
