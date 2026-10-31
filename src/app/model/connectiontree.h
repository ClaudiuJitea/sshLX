// ConnectionTree - sshLX's connection tree (folders, connections, ordering,
// icons, descriptions, favourites and folder-level inheritance), persisted
// as ~/.config/sshLX/connections.json.
//
// Connection settings themselves are *not* stored here: a connection
// references a PuTTY saved session by name, and PuTTY's own storage code
// keeps those in PuTTY's format. This file only adds what PuTTY has no
// place for.
//
// Inheritance: a folder may carry values for any PuTTY Conf key (stored as
// opaque strings produced by the engine's sshlx_conf_key_export()). A
// connection lists which keys it inherits; for each, the nearest ancestor
// folder that defines the key supplies the value. This class is Qt Core
// only, so it can be unit-tested without the engine or widgets.
#pragma once

#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include <memory>

#include "logonrunner.h"

// A shared login. Connections and folders link to it by id, so changing it
// here changes every linked connection. The password and key passphrase are
// never in this struct or in any file: they live in the system keyring under
// this id (see keyring.h).
struct Credential {
    QString id;
    QString name;
    QString username;
    QString keyFile;
};

class ConnectionNode {
public:
    enum Type { Folder, Connection };

    explicit ConnectionNode(Type t);
    ~ConnectionNode();

    Type type;
    QString id;               // stable UUID
    QString name;             // display name (== PuTTY session for connections)
    QString description;
    QString icon;             // "", "linux", "windows", "network", ...
    QString panel;            // dock area to open in (connections)
    bool favourite = false;
    bool expanded = true;     // folders
    QString session;          // PuTTY saved-session name (connections)
    QStringList inherit;      // Conf key ids inherited from folders
    QMap<QString, QString> properties;   // folder values: key id -> value
    QString credential;       // Credential id; empty: use the nearest folder's
    QList<LogonStep> logon;   // Run after connecting; empty: the nearest folder's
    QString logonScript;      // A Python script run after them; same inheritance
    QString buttonBar;        // Button bar shown on this tab; same inheritance
    QString tabColour;        // "#rrggbb" for its tabs; same inheritance
    QString antiIdleText;     // Sent after antiIdleSeconds idle; same inheritance
    int antiIdleSeconds = 0;

    ConnectionNode *parent = nullptr;
    QList<ConnectionNode *> children;

    bool isFolder() const { return type == Folder; }
    int row() const;
    // Folder path, e.g. "Linux/Production"
    QString path() const;

    // Take ownership of child at index (or append with -1).
    void insertChild(int index, ConnectionNode *child);
    ConnectionNode *takeChild(int index);
};

class ConnectionTree {
public:
    ConnectionTree();
    ~ConnectionTree();

    ConnectionNode *root() const { return m_root.get(); }

    ConnectionNode *addFolder(ConnectionNode *parent, const QString &name, int index = -1);
    ConnectionNode *addConnection(ConnectionNode *parent, const QString &name,
                                  const QString &session, int index = -1);
    // Remove (and delete) a node and its subtree.
    void remove(ConnectionNode *n);
    // Move n under newParent at index (-1 = append). Refuses to move a
    // folder into its own subtree; returns false then.
    bool move(ConnectionNode *n, ConnectionNode *newParent, int index);

    ConnectionNode *findById(const QString &id) const;
    ConnectionNode *findBySession(const QString &session) const;
    QList<ConnectionNode *> connections() const;     // depth-first order
    QList<ConnectionNode *> favourites() const;

    // Sort a folder's children A->Z (folders first), recursively.
    void sortAlphabetically(ConnectionNode *folder);

    // Make sure every PuTTY saved session appears in the tree ("zero-step
    // import"). Sessions not yet referenced are added to the root folder
    // (or to `into` if given). Returns the nodes added.
    QList<ConnectionNode *> syncWithSessions(const QStringList &savedSessions,
                                             ConnectionNode *into = nullptr);
    // Connections whose PuTTY session no longer exists.
    QList<ConnectionNode *> missingSessions(const QStringList &savedSessions) const;

    // Inheritance: for a connection, the folder value for each inherited
    // key (nearest ancestor that defines it). Keys no ancestor defines are
    // absent from the result. Also usable for a folder (resolves the
    // folder's own value or its ancestors').
    QMap<QString, QString> resolveInherited(const ConnectionNode *n) const;
    // The folder that supplies key for n, or nullptr.
    const ConnectionNode *inheritSource(const ConnectionNode *n, const QString &key) const;

    QList<Credential> credentials() const { return m_credentials; }
    void setCredentials(const QList<Credential> &c) { m_credentials = c; }
    const Credential *credential(const QString &id) const;
    // The credential a node uses: its own link, or its nearest folder's. A
    // link to a credential that no longer exists counts as none.
    const Credential *resolveCredential(const ConnectionNode *n) const;
    // The logon actions a node uses: its own, or its nearest folder's.
    QList<LogonStep> resolveLogon(const ConnectionNode *n) const;
    QString resolveLogonScript(const ConnectionNode *n) const;
    QString resolveButtonBar(const ConnectionNode *n) const;
    QString resolveTabColour(const ConnectionNode *n) const;
    const ConnectionNode *resolveAntiIdle(const ConnectionNode *n) const;
    // Connections using the credential, directly or through a folder.
    QList<ConnectionNode *> linkedConnections(const QString &id) const;
    // Forget every direct link to the credential.
    void unlinkCredential(const QString &id);

    QJsonObject toJson() const;
    bool fromJson(const QJsonObject &o, QString *error = nullptr);
    bool load(const QString &path, QString *error = nullptr);
    bool save(const QString &path, QString *error = nullptr) const;

    static QString defaultPath();   // ~/.config/sshLX/connections.json

private:
    std::unique_ptr<ConnectionNode> m_root;
    QList<Credential> m_credentials;
};
