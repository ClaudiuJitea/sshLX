#include "connectiontree.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>
#include <functional>

static QString newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

// ---- ConnectionNode -------------------------------------------------------
ConnectionNode::ConnectionNode(Type t) : type(t), id(newId()) {}

ConnectionNode::~ConnectionNode()
{
    qDeleteAll(children);
}

int ConnectionNode::row() const
{
    return parent ? int(parent->children.indexOf(const_cast<ConnectionNode *>(this))) : 0;
}

QString ConnectionNode::path() const
{
    QStringList parts;
    for (const ConnectionNode *n = isFolder() ? this : parent; n && n->parent; n = n->parent)
        parts.prepend(n->name);
    return parts.join('/');
}

void ConnectionNode::insertChild(int index, ConnectionNode *child)
{
    child->parent = this;
    if (index < 0 || index > children.size())
        children.append(child);
    else
        children.insert(index, child);
}

ConnectionNode *ConnectionNode::takeChild(int index)
{
    ConnectionNode *c = children.takeAt(index);
    c->parent = nullptr;
    return c;
}

// ---- ConnectionTree ----------------------------------------------------------
ConnectionTree::ConnectionTree() : m_root(new ConnectionNode(ConnectionNode::Folder))
{
    m_root->name = QStringLiteral("Connections");
}

ConnectionTree::~ConnectionTree() = default;

ConnectionNode *ConnectionTree::addFolder(ConnectionNode *parent, const QString &name, int index)
{
    auto *n = new ConnectionNode(ConnectionNode::Folder);
    n->name = name;
    (parent ? parent : m_root.get())->insertChild(index, n);
    return n;
}

ConnectionNode *ConnectionTree::addConnection(ConnectionNode *parent, const QString &name,
                                              const QString &session, int index)
{
    auto *n = new ConnectionNode(ConnectionNode::Connection);
    n->name = name;
    n->session = session;
    (parent ? parent : m_root.get())->insertChild(index, n);
    return n;
}

void ConnectionTree::remove(ConnectionNode *n)
{
    if (!n || n == m_root.get() || !n->parent)
        return;
    delete n->parent->takeChild(n->row());
}

bool ConnectionTree::move(ConnectionNode *n, ConnectionNode *newParent, int index)
{
    if (!n || n == m_root.get() || !newParent || !newParent->isFolder())
        return false;
    for (ConnectionNode *p = newParent; p; p = p->parent)
        if (p == n)
            return false;   // would move a folder into itself
    ConnectionNode *old = n->parent;
    int oldRow = n->row();
    if (old == newParent && index > oldRow)
        index--;            // account for removal shifting later rows
    old->takeChild(oldRow);
    newParent->insertChild(index, n);
    return true;
}

static void walk(ConnectionNode *n, const std::function<void(ConnectionNode *)> &f)
{
    f(n);
    for (ConnectionNode *c : n->children)
        walk(c, f);
}

ConnectionNode *ConnectionTree::findById(const QString &id) const
{
    ConnectionNode *found = nullptr;
    walk(m_root.get(), [&](ConnectionNode *n) { if (!found && n->id == id) found = n; });
    return found;
}

ConnectionNode *ConnectionTree::findBySession(const QString &session) const
{
    ConnectionNode *found = nullptr;
    walk(m_root.get(), [&](ConnectionNode *n) {
        if (!found && !n->isFolder() && n->session == session)
            found = n;
    });
    return found;
}

QList<ConnectionNode *> ConnectionTree::connections() const
{
    QList<ConnectionNode *> r;
    walk(m_root.get(), [&](ConnectionNode *n) { if (!n->isFolder()) r << n; });
    return r;
}

QList<ConnectionNode *> ConnectionTree::favourites() const
{
    QList<ConnectionNode *> r;
    walk(m_root.get(), [&](ConnectionNode *n) { if (!n->isFolder() && n->favourite) r << n; });
    return r;
}

void ConnectionTree::sortAlphabetically(ConnectionNode *folder)
{
    if (!folder || !folder->isFolder())
        return;
    std::stable_sort(folder->children.begin(), folder->children.end(),
                     [](const ConnectionNode *a, const ConnectionNode *b) {
        if (a->isFolder() != b->isFolder())
            return a->isFolder();
        return QString::localeAwareCompare(a->name, b->name) < 0;
    });
    for (ConnectionNode *c : folder->children)
        sortAlphabetically(c);
}

QList<ConnectionNode *> ConnectionTree::syncWithSessions(const QStringList &saved,
                                                         ConnectionNode *into)
{
    QSet<QString> known;
    for (ConnectionNode *c : connections())
        known.insert(c->session);
    QList<ConnectionNode *> added;
    for (const QString &s : saved) {
        if (s == QLatin1String("Default Settings") || known.contains(s))
            continue;
        added << addConnection(into ? into : m_root.get(), s, s);
        known.insert(s);
    }
    return added;
}

QList<ConnectionNode *> ConnectionTree::missingSessions(const QStringList &saved) const
{
    QSet<QString> have(saved.begin(), saved.end());
    QList<ConnectionNode *> r;
    for (ConnectionNode *c : connections())
        if (!have.contains(c->session))
            r << c;
    return r;
}

const ConnectionNode *ConnectionTree::inheritSource(const ConnectionNode *n,
                                                    const QString &key) const
{
    for (const ConnectionNode *p = n->isFolder() ? n : n->parent; p; p = p->parent)
        if (p->properties.contains(key))
            return p;
    return nullptr;
}

const Credential *ConnectionTree::credential(const QString &id) const
{
    for (const Credential &c : m_credentials)
        if (c.id == id)
            return &c;
    return nullptr;
}

const Credential *ConnectionTree::resolveCredential(const ConnectionNode *n) const
{
    for (; n; n = n->parent)
        if (!n->credential.isEmpty())
            return credential(n->credential);
    return nullptr;
}

QList<ConnectionNode *> ConnectionTree::linkedConnections(const QString &id) const
{
    QList<ConnectionNode *> linked;
    for (ConnectionNode *c : connections())
        if (const Credential *cred = resolveCredential(c); cred && cred->id == id)
            linked.append(c);
    return linked;
}

void ConnectionTree::unlinkCredential(const QString &id)
{
    std::function<void(ConnectionNode *)> walk = [&](ConnectionNode *n) {
        if (n->credential == id)
            n->credential.clear();
        for (ConnectionNode *c : n->children)
            walk(c);
    };
    walk(m_root.get());
}

QMap<QString, QString> ConnectionTree::resolveInherited(const ConnectionNode *n) const
{
    QMap<QString, QString> r;
    if (n->isFolder()) {
        // Everything defined on this folder or above (nearest wins).
        for (const ConnectionNode *p = n; p; p = p->parent)
            for (auto it = p->properties.begin(); it != p->properties.end(); ++it)
                if (!r.contains(it.key()))
                    r.insert(it.key(), it.value());
        return r;
    }
    for (const QString &key : n->inherit)
        if (const ConnectionNode *src = inheritSource(n, key))
            r.insert(key, src->properties.value(key));
    return r;
}

// ---- JSON --------------------------------------------------------------------
static QJsonObject nodeToJson(const ConnectionNode *n)
{
    QJsonObject o;
    o["type"] = n->isFolder() ? "folder" : "connection";
    o["id"] = n->id;
    o["name"] = n->name;
    if (!n->description.isEmpty()) o["description"] = n->description;
    if (!n->icon.isEmpty()) o["icon"] = n->icon;
    if (!n->credential.isEmpty()) o["credential"] = n->credential;
    if (n->isFolder()) {
        o["expanded"] = n->expanded;
        if (!n->properties.isEmpty()) {
            QJsonObject p;
            for (auto it = n->properties.begin(); it != n->properties.end(); ++it)
                p[it.key()] = it.value();
            o["properties"] = p;
        }
        QJsonArray kids;
        for (const ConnectionNode *c : n->children)
            kids.append(nodeToJson(c));
        o["children"] = kids;
    } else {
        o["session"] = n->session;
        if (!n->panel.isEmpty()) o["panel"] = n->panel;
        if (n->favourite) o["favourite"] = true;
        if (!n->inherit.isEmpty())
            o["inherit"] = QJsonArray::fromStringList(n->inherit);
    }
    return o;
}

static ConnectionNode *nodeFromJson(const QJsonObject &o, QString *error)
{
    QString type = o["type"].toString();
    if (type != "folder" && type != "connection") {
        if (error) *error = QStringLiteral("unknown node type '%1'").arg(type);
        return nullptr;
    }
    auto *n = new ConnectionNode(type == "folder" ? ConnectionNode::Folder
                                                  : ConnectionNode::Connection);
    if (o.contains("id")) n->id = o["id"].toString();
    n->name = o["name"].toString();
    n->description = o["description"].toString();
    n->icon = o["icon"].toString();
    n->credential = o["credential"].toString();
    if (n->isFolder()) {
        n->expanded = o["expanded"].toBool(true);
        const QJsonObject p = o["properties"].toObject();
        for (auto it = p.begin(); it != p.end(); ++it)
            n->properties.insert(it.key(), it.value().toString());
        for (const QJsonValue &v : o["children"].toArray()) {
            ConnectionNode *c = nodeFromJson(v.toObject(), error);
            if (!c) {
                delete n;
                return nullptr;
            }
            n->insertChild(-1, c);
        }
    } else {
        n->session = o["session"].toString(n->name);
        n->panel = o["panel"].toString();
        n->favourite = o["favourite"].toBool(false);
        for (const QJsonValue &v : o["inherit"].toArray())
            n->inherit << v.toString();
    }
    return n;
}

QJsonObject ConnectionTree::toJson() const
{
    QJsonObject o;
    o["format"] = "sshLX-connections";
    o["version"] = 1;
    o["root"] = nodeToJson(m_root.get());
    if (!m_credentials.isEmpty()) {
        QJsonArray list;
        for (const Credential &c : m_credentials) {
            QJsonObject e;
            e["id"] = c.id;
            e["name"] = c.name;
            if (!c.username.isEmpty()) e["username"] = c.username;
            if (!c.keyFile.isEmpty()) e["keyFile"] = c.keyFile;
            list.append(e);
        }
        o["credentials"] = list;
    }
    return o;
}

bool ConnectionTree::fromJson(const QJsonObject &o, QString *error)
{
    if (o["format"].toString() != "sshLX-connections") {
        if (error) *error = QStringLiteral("not an sshLX connections file");
        return false;
    }
    if (o["version"].toInt() > 1) {
        if (error) *error = QStringLiteral("connections file is from a newer sshLX");
        return false;
    }
    ConnectionNode *root = nodeFromJson(o["root"].toObject(), error);
    if (!root || !root->isFolder()) {
        delete root;
        if (error && error->isEmpty()) *error = QStringLiteral("bad root");
        return false;
    }
    m_root.reset(root);
    m_credentials.clear();
    for (const QJsonValue &v : o["credentials"].toArray()) {
        const QJsonObject e = v.toObject();
        m_credentials.append({e["id"].toString(), e["name"].toString(),
                              e["username"].toString(), e["keyFile"].toString()});
    }
    return true;
}

bool ConnectionTree::load(const QString &path, QString *error)
{
    QFile f(path);
    if (!f.exists())
        return true;   // empty tree
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    QJsonParseError pe;
    QJsonDocument d = QJsonDocument::fromJson(f.readAll(), &pe);
    if (d.isNull()) {
        if (error) *error = pe.errorString();
        return false;
    }
    return fromJson(d.object(), error);
}

bool ConnectionTree::save(const QString &path, QString *error) const
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    f.write(QJsonDocument(toJson()).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}

QString ConnectionTree::defaultPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
           QStringLiteral("/sshLX/connections.json");
}
