// CredentialsDialog - Tools > Credentials Manager. Shared logins, and which
// connections and folders use each. Nothing changes until Save. Passwords
// and passphrases go straight to the keyring and are never shown again.
#pragma once

#include <QDialog>
#include <QHash>
#include <QList>
#include <QStringList>

#include "connectiontree.h"

class ConnectionStore;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

class CredentialsDialog : public QDialog {
    Q_OBJECT
public:
    CredentialsDialog(ConnectionStore *store, QWidget *parent);
    ~CredentialsDialog() override;

private:
    // Typed but not saved yet. Empty leaves what the keyring holds.
    struct Secrets {
        QString password, passphrase;
        bool clearPassword = false, clearPassphrase = false;
    };
    struct KeyringOp {
        QString account, secret;
        bool remove;
    };
    void addCredential();
    void deleteCredential();
    void select(int row);
    void storeForm();
    void showStoredState();
    void rebuildLinks();
    void refreshLinkMarks();
    void onLinkChanged(QTreeWidgetItem *item);
    void save();
    void runOps(const QList<KeyringOp> &ops, int index);
    void commit();
    void wipeSecrets();
    int linkedCount(const QString &id) const;
    QString usedBy(const ConnectionNode *n) const;

    ConnectionStore *m_store;
    QList<Credential> m_creds;
    QHash<QString, QString> m_links;    // node id -> credential id
    QHash<QString, Secrets> m_secrets;  // credential id -> edits
    QStringList m_deleted;
    QStringList m_new;                  // nothing in the keyring to look up yet
    int m_row = -1;
    bool m_loading = false;

    QListWidget *m_list;
    QLineEdit *m_name, *m_user, *m_password, *m_key, *m_passphrase;
    QLabel *m_passwordState, *m_passphraseState, *m_count;
    QPushButton *m_clearPassword, *m_clearPassphrase, *m_browse, *m_delete, *m_save;
    QTreeWidget *m_tree;
};
