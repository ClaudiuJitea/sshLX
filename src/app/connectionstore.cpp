#include "connectionstore.h"

#include <QSet>

ConnectionStore::ConnectionStore(const QString &path, QObject *parent)
    : QObject(parent), m_path(path)
{
}

QStringList ConnectionStore::savedSessions() const
{
    QStringList r;
    sshlx_strlist *l = sshlx_saved_sessions_list();
    for (size_t i = 0; i < l->n; i++)
        r << QString::fromUtf8(l->items[i]);
    sshlx_strlist_free(l);
    return r;
}

bool ConnectionStore::load(QString *error)
{
    emit aboutToReset();
    bool ok = m_tree.load(m_path, error);
    m_tree.syncWithSessions(savedSessions());
    m_infoCache.clear();
    emit reset();
    if (ok)
        save();
    return ok;
}

void ConnectionStore::save()
{
    m_tree.save(m_path);
}

void ConnectionStore::syncSessions()
{
    QList<ConnectionNode *> added = m_tree.syncWithSessions(savedSessions());
    m_infoCache.clear();
    if (!added.isEmpty()) {
        save();
        emit structureChanged();
    }
}

ConnectionStore::Info ConnectionStore::info(const ConnectionNode *n) const
{
    if (!n || n->isFolder())
        return {-1, {}};
    auto it = m_infoCache.find(n->session);
    if (it != m_infoCache.end())
        return *it;
    sshlx_conf *c = effectiveConf(n);
    char *dest = sshlx_conf_dest(c);
    Info i{sshlx_conf_get_int(c, sshlx_conf_key_protocol()), QString::fromUtf8(dest)};
    sshlx_free(dest);
    sshlx_conf_free(c);
    m_infoCache.insert(n->session, i);
    return i;
}

int ConnectionStore::protocolOf(const ConnectionNode *n) const { return info(n).protocol; }
QString ConnectionStore::hostOf(const ConnectionNode *n) const { return info(n).host; }

sshlx_conf *ConnectionStore::effectiveConf(const ConnectionNode *n) const
{
    sshlx_conf *c = sshlx_conf_load(n->session.toUtf8().constData(), nullptr);
    const QMap<QString, QString> inh = m_tree.resolveInherited(n);
    for (auto it = inh.begin(); it != inh.end(); ++it) {
        int key = sshlx_conf_key_find(it.key().toUtf8().constData());
        if (key >= 0)
            sshlx_conf_key_import(c, key, it.value().toUtf8().constData());
    }
    // Last, so a credential beats both the session and folder values.
    if (const Credential *cred = m_tree.resolveCredential(n)) {
        if (!cred->username.isEmpty())
            sshlx_conf_set_str(c, sshlx_conf_key_find("username"),
                               cred->username.toUtf8().constData());
        if (!cred->keyFile.isEmpty())
            sshlx_conf_set_str(c, sshlx_conf_key_find("keyfile"),
                               cred->keyFile.toUtf8().constData());
    }
    return c;
}

bool ConnectionStore::saveConf(ConnectionNode *n, const sshlx_conf *conf, QString *error)
{
    char *err = nullptr;
    if (sshlx_conf_save(conf, n->session.toUtf8().constData(), &err) != 0) {
        if (error)
            *error = QString::fromUtf8(err ? err : "unknown error");
        sshlx_free(err);
        return false;
    }
    m_infoCache.remove(n->session);
    emit nodeChanged(n);
    return true;
}

QString ConnectionStore::uniqueSessionName(const QString &base) const
{
    QSet<QString> taken;
    for (const QString &s : savedSessions())
        taken.insert(s);
    for (ConnectionNode *c : const_cast<ConnectionTree &>(m_tree).connections())
        taken.insert(c->session);
    QString name = base.trimmed().isEmpty() ? QStringLiteral("New Connection") : base.trimmed();
    if (!taken.contains(name))
        return name;
    for (int i = 2;; i++) {
        QString cand = QStringLiteral("%1 (%2)").arg(name).arg(i);
        if (!taken.contains(cand))
            return cand;
    }
}

ConnectionNode *ConnectionStore::newFolder(ConnectionNode *parent, const QString &name)
{
    ConnectionNode *f = m_tree.addFolder(parent, name);
    save();
    emit structureChanged();
    return f;
}

ConnectionNode *ConnectionStore::newConnection(ConnectionNode *parent, const QString &name,
                                               const sshlx_conf *conf)
{
    QString session = uniqueSessionName(name);
    sshlx_conf *c = conf ? sshlx_conf_copy(conf) : sshlx_conf_new_default();
    char *err = nullptr;
    sshlx_conf_save(c, session.toUtf8().constData(), &err);
    sshlx_free(err);
    sshlx_conf_free(c);
    ConnectionNode *n = m_tree.addConnection(parent, session, session);
    save();
    emit structureChanged();
    return n;
}

ConnectionNode *ConnectionStore::duplicate(ConnectionNode *n)
{
    if (!n || n->isFolder())
        return nullptr;
    sshlx_conf *c = sshlx_conf_load(n->session.toUtf8().constData(), nullptr);
    QString session = uniqueSessionName(n->name);
    char *err = nullptr;
    sshlx_conf_save(c, session.toUtf8().constData(), &err);
    sshlx_free(err);
    sshlx_conf_free(c);
    ConnectionNode *d = m_tree.addConnection(n->parent, session, session, n->row() + 1);
    d->description = n->description;
    d->icon = n->icon;
    d->panel = n->panel;
    d->inherit = n->inherit;
    d->credential = n->credential;
    save();
    emit structureChanged();
    return d;
}

bool ConnectionStore::rename(ConnectionNode *n, const QString &newNameIn, QString *error)
{
    const QString newName = newNameIn.trimmed();
    if (newName.isEmpty() || newName == n->name)
        return newName == n->name;
    if (n->isFolder()) {
        n->name = newName;
        touch(n);
        return true;
    }
    // A connection's name is its PuTTY session name: rename the session.
    if (savedSessions().contains(newName) || m_tree.findBySession(newName)) {
        if (error)
            *error = tr("A saved session called \"%1\" already exists.").arg(newName);
        return false;
    }
    sshlx_conf *c = sshlx_conf_load(n->session.toUtf8().constData(), nullptr);
    char *err = nullptr;
    if (sshlx_conf_save(c, newName.toUtf8().constData(), &err) != 0) {
        if (error)
            *error = QString::fromUtf8(err ? err : "");
        sshlx_free(err);
        sshlx_conf_free(c);
        return false;
    }
    sshlx_conf_free(c);
    sshlx_saved_session_delete(n->session.toUtf8().constData());
    m_infoCache.remove(n->session);
    n->session = newName;
    n->name = newName;
    touch(n);
    return true;
}

void ConnectionStore::remove(ConnectionNode *n)
{
    if (!n || n == m_tree.root())
        return;
    QList<ConnectionNode *> stack{n};
    while (!stack.isEmpty()) {
        ConnectionNode *x = stack.takeLast();
        if (!x->isFolder()) {
            sshlx_saved_session_delete(x->session.toUtf8().constData());
            m_infoCache.remove(x->session);
        }
        stack << x->children;
    }
    m_tree.remove(n);
    save();
    emit structureChanged();
}

bool ConnectionStore::move(ConnectionNode *n, ConnectionNode *newParent, int index)
{
    if (!m_tree.move(n, newParent, index))
        return false;
    m_infoCache.clear();   // inherited values may differ now
    save();
    return true;
}

void ConnectionStore::setFolderProperty(ConnectionNode *folder, const QString &key,
                                        const QString &value)
{
    folder->properties[key] = value;
    m_infoCache.clear();
    touch(folder);
}

void ConnectionStore::clearFolderProperty(ConnectionNode *folder, const QString &key)
{
    folder->properties.remove(key);
    m_infoCache.clear();
    touch(folder);
}

void ConnectionStore::setInherited(ConnectionNode *n, const QString &key, bool inherit)
{
    if (inherit && !n->inherit.contains(key))
        n->inherit << key;
    else if (!inherit)
        n->inherit.removeAll(key);
    m_infoCache.remove(n->session);
    touch(n);
}

void ConnectionStore::touch(ConnectionNode *n)
{
    save();
    emit nodeChanged(n);
}
