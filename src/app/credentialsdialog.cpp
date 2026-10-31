#include "credentialsdialog.h"

#include "connectionstore.h"
#include "dialogkit.h"
#include "keyring.h"

#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QTreeWidget>
#include <QUuid>
#include <QVBoxLayout>

#include <functional>

using namespace DialogKit;

static constexpr int kNodeId = Qt::UserRole;

CredentialsDialog::CredentialsDialog(ConnectionStore *store, QWidget *parent)
    : QDialog(parent), m_store(store), m_creds(store->tree().credentials())
{
    setWindowTitle(tr("Credentials Manager"));
    resize(900, 640);
    auto *v = new QVBoxLayout(this);
    frame(v);
    v->addWidget(header(tr("Credentials Manager"),
                        tr("Shared logins. Change one here and every linked connection uses "
                           "it at its next login.")));

    m_list = new QListWidget;
    m_list->setMaximumWidth(220);
    for (const Credential &c : m_creds)
        m_list->addItem(c.name);

    m_name = new QLineEdit;
    m_user = new QLineEdit;
    m_password = new QLineEdit;
    m_password->setEchoMode(QLineEdit::Password);
    m_password->setPlaceholderText(tr("Type to set or change"));
    m_key = new QLineEdit;
    m_passphrase = new QLineEdit;
    m_passphrase->setEchoMode(QLineEdit::Password);
    m_passphrase->setPlaceholderText(tr("Type to set or change"));
    m_browse = secondaryButton(tr("Browse..."));
    m_clearPassword = secondaryButton(tr("Remove"));
    m_clearPassphrase = secondaryButton(tr("Remove"));
    m_passwordState = new QLabel;
    m_passphraseState = new QLabel;

    auto rowOf = [](std::initializer_list<QWidget *> widgets) {
        auto *h = new QHBoxLayout;
        for (QWidget *w : widgets)
            h->addWidget(w, w->inherits("QLineEdit") ? 1 : 0);
        return h;
    };
    auto *form = new QFormLayout;
    form->addRow(tr("Name"), m_name);
    form->addRow(tr("Username"), m_user);
    form->addRow(tr("Password"), rowOf({m_password, m_passwordState, m_clearPassword}));
    form->addRow(tr("Private key file"), rowOf({m_key, m_browse}));
    form->addRow(tr("Key passphrase"), rowOf({m_passphrase, m_passphraseState, m_clearPassphrase}));

    m_tree = new QTreeWidget;
    m_tree->setHeaderHidden(true);
    m_count = new QLabel;

    auto *right = new QVBoxLayout;
    right->addLayout(form);
    if (!Keyring::available())
        right->addWidget(hint(tr("Passwords need the system keyring, and this build has none. "
                                 "User names and key files still work.")));
    else if (!Keyring::enabled())
        right->addWidget(hint(tr("Saving a password turns on keyring storage. Passwords are "
                                 "kept only in the keyring, never in a file.")));
    right->addWidget(m_count);
    right->addWidget(m_tree, 1);

    auto *row = new QHBoxLayout;
    row->addWidget(m_list);
    row->addLayout(right, 1);
    v->addLayout(row, 1);

    auto *add = secondaryButton(tr("Add"));
    m_delete = secondaryButton(tr("Delete"));
    auto *left = new QWidget;
    auto *lh = new QHBoxLayout(left);
    lh->setContentsMargins(0, 0, 0, 0);
    lh->addWidget(add);
    lh->addWidget(m_delete);
    auto *cancel = secondaryButton(tr("Cancel"));
    m_save = primaryButton(tr("Save"));
    v->addWidget(footer({cancel, m_save}, left));

    std::function<void(const ConnectionNode *)> gather = [&](const ConnectionNode *n) {
        if (n->parent && !n->credential.isEmpty())
            m_links.insert(n->id, n->credential);
        for (const ConnectionNode *c : n->children)
            gather(c);
    };
    gather(store->tree().root());
    rebuildLinks();

    for (QPushButton *b : {add, m_delete, cancel, m_browse, m_clearPassword, m_clearPassphrase})
        b->setAutoDefault(false);
    for (QLineEdit *e : {m_password, m_passphrase})
        e->setEnabled(Keyring::available());
    connect(add, &QPushButton::clicked, this, &CredentialsDialog::addCredential);
    connect(m_delete, &QPushButton::clicked, this, &CredentialsDialog::deleteCredential);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_save, &QPushButton::clicked, this, &CredentialsDialog::save);
    connect(m_list, &QListWidget::currentRowChanged, this, &CredentialsDialog::select);
    connect(m_name, &QLineEdit::textEdited, this, [this](const QString &t) {
        if (m_row >= 0)
            m_list->item(m_row)->setText(t);
    });
    connect(m_browse, &QPushButton::clicked, this, [this] {
        const QString p = QFileDialog::getOpenFileName(this, tr("Private Key File"), m_key->text());
        if (!p.isEmpty())
            m_key->setText(p);
    });
    connect(m_clearPassword, &QPushButton::clicked, this, [this] {
        m_password->clear();
        m_secrets[m_creds[m_row].id].clearPassword = true;
        m_passwordState->setText(tr("Removed on Save"));
    });
    connect(m_clearPassphrase, &QPushButton::clicked, this, [this] {
        m_passphrase->clear();
        m_secrets[m_creds[m_row].id].clearPassphrase = true;
        m_passphraseState->setText(tr("Removed on Save"));
    });
    connect(m_tree, &QTreeWidget::itemChanged, this, &CredentialsDialog::onLinkChanged);

    m_list->setCurrentRow(m_creds.isEmpty() ? -1 : 0);
    select(m_list->currentRow());
}

CredentialsDialog::~CredentialsDialog() { wipeSecrets(); }

void CredentialsDialog::wipeSecrets()
{
    for (Secrets &s : m_secrets) {
        s.password.fill(QChar(0));
        s.passphrase.fill(QChar(0));
    }
    m_secrets.clear();
    m_password->clear();
    m_passphrase->clear();
}

void CredentialsDialog::addCredential()
{
    storeForm();
    Credential c;
    c.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    c.name = tr("New Credential");
    m_creds.append(c);
    m_new.append(c.id);
    m_list->addItem(c.name);
    m_list->setCurrentRow(m_creds.size() - 1);
    m_name->setFocus();
    m_name->selectAll();
}

void CredentialsDialog::deleteCredential()
{
    if (m_row < 0)
        return;
    const QString id = m_creds[m_row].id;
    const int used = linkedCount(id);
    if (used && !confirm(this, tr("Delete Credential"),
                         tr("%n connection(s) use this credential. They go back to their own "
                            "settings.", nullptr, used),
                         tr("Delete"), QString(), false, true))
        return;
    const int row = m_row;
    m_row = -1;     // what is in the form must not land on a neighbour
    m_creds.removeAt(row);
    m_secrets.remove(id);
    if (m_new.removeAll(id) == 0)
        m_deleted.append(id);
    for (auto it = m_links.begin(); it != m_links.end();)
        it = it.value() == id ? m_links.erase(it) : std::next(it);
    delete m_list->takeItem(row);
    select(m_list->currentRow());
}

void CredentialsDialog::storeForm()
{
    if (m_row < 0 || m_row >= m_creds.size())
        return;
    Credential &c = m_creds[m_row];
    c.name = m_name->text().trimmed();
    c.username = m_user->text().trimmed();
    c.keyFile = m_key->text().trimmed();
    Secrets &s = m_secrets[c.id];
    if (!m_password->text().isEmpty()) {
        s.password = m_password->text();
        s.clearPassword = false;
    }
    if (!m_passphrase->text().isEmpty()) {
        s.passphrase = m_passphrase->text();
        s.clearPassphrase = false;
    }
}

void CredentialsDialog::select(int row)
{
    storeForm();
    m_row = row;
    const bool have = row >= 0 && row < m_creds.size();
    for (QWidget *w : QList<QWidget *>{m_name, m_user, m_key, m_browse, m_delete, m_tree})
        w->setEnabled(have);
    for (QWidget *w : QList<QWidget *>{m_password, m_passphrase, m_clearPassword, m_clearPassphrase})
        w->setEnabled(have && Keyring::available());
    m_name->setText(have ? m_creds[row].name : QString());
    m_user->setText(have ? m_creds[row].username : QString());
    m_key->setText(have ? m_creds[row].keyFile : QString());
    m_password->clear();
    m_passphrase->clear();
    showStoredState();
    refreshLinkMarks();
}

// Whether a secret exists, never what it is: it is not put in the form.
void CredentialsDialog::showStoredState()
{
    m_passwordState->clear();
    m_passphraseState->clear();
    if (m_row < 0)
        return;
    const QString id = m_creds[m_row].id;
    const Secrets s = m_secrets.value(id);
    auto show = [this, id](QLabel *label, bool passphrase, bool pending, bool clearing) {
        if (clearing) {
            label->setText(tr("Removed on Save"));
        } else if (pending) {
            label->setText(tr("Changed"));
        } else if (m_new.contains(id)) {
            label->setText(tr("Not set"));
        } else {
            QPointer<QLabel> safe(label);
            Keyring::read(Keyring::credentialAccount(id, passphrase),
                          [this, safe, id](bool found, const QString &) {
                if (safe && m_row >= 0 && m_row < m_creds.size() && m_creds[m_row].id == id)
                    safe->setText(found ? tr("Stored") : tr("Not set"));
            });
        }
    };
    show(m_passwordState, false, !s.password.isEmpty(), s.clearPassword);
    show(m_passphraseState, true, !s.passphrase.isEmpty(), s.clearPassphrase);
}

// ---- links --------------------------------------------------------------------
void CredentialsDialog::rebuildLinks()
{
    m_loading = true;
    m_tree->clear();
    std::function<void(const ConnectionNode *, QTreeWidgetItem *)> add =
        [&](const ConnectionNode *n, QTreeWidgetItem *parent) {
        for (const ConnectionNode *c : n->children) {
            auto *it = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
            it->setData(0, kNodeId, c->id);
            it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
            it->setCheckState(0, Qt::Unchecked);
            add(c, it);
        }
    };
    add(m_store->tree().root(), nullptr);
    m_tree->expandAll();
    m_loading = false;
}

// The credential a node would use given the links as they now stand.
QString CredentialsDialog::usedBy(const ConnectionNode *n) const
{
    for (; n; n = n->parent)
        if (m_links.contains(n->id))
            return m_links.value(n->id);
    return QString();
}

int CredentialsDialog::linkedCount(const QString &id) const
{
    int n = 0;
    for (const ConnectionNode *c : m_store->tree().connections())
        n += usedBy(c) == id;
    return n;
}

void CredentialsDialog::refreshLinkMarks()
{
    m_loading = true;
    const QString id = m_row >= 0 && m_row < m_creds.size() ? m_creds[m_row].id : QString();
    std::function<void(QTreeWidgetItem *)> mark = [&](QTreeWidgetItem *it) {
        const QString nodeId = it->data(0, kNodeId).toString();
        const ConnectionNode *n = m_store->tree().findById(nodeId);
        it->setCheckState(0, !id.isEmpty() && m_links.value(nodeId) == id ? Qt::Checked
                                                                          : Qt::Unchecked);
        QString label = n->name;
        const QString used = usedBy(n);
        if (!used.isEmpty() && used != id) {
            for (const Credential &c : m_creds)
                if (c.id == used)
                    label += tr("  (uses %1)").arg(c.name);
        } else if (!used.isEmpty() && !m_links.contains(nodeId)) {
            label += tr("  (through its folder)");
        }
        it->setText(0, label);
        for (int i = 0; i < it->childCount(); i++)
            mark(it->child(i));
    };
    for (int i = 0; i < m_tree->topLevelItemCount(); i++)
        mark(m_tree->topLevelItem(i));
    m_count->setText(id.isEmpty() ? QString() : tr("Used by %n connection(s):", nullptr, linkedCount(id)));
    m_loading = false;
}

void CredentialsDialog::onLinkChanged(QTreeWidgetItem *item)
{
    if (m_loading || m_row < 0)
        return;
    const QString nodeId = item->data(0, kNodeId).toString();
    if (item->checkState(0) == Qt::Checked)
        m_links.insert(nodeId, m_creds[m_row].id);
    else
        m_links.remove(nodeId);
    refreshLinkMarks();
}

// ---- saving -------------------------------------------------------------------
void CredentialsDialog::save()
{
    storeForm();
    for (int i = 0; i < m_creds.size(); i++) {
        for (int j = 0; j < i; j++) {
            if (m_creds[i].name.compare(m_creds[j].name, Qt::CaseInsensitive) == 0) {
                error(this, tr("Credentials Manager"),
                      tr("Two credentials are called \"%1\".").arg(m_creds[i].name));
                return;
            }
        }
        if (m_creds[i].name.isEmpty()) {
            error(this, tr("Credentials Manager"), tr("Every credential needs a name."));
            return;
        }
    }
    QList<KeyringOp> ops;
    bool anySecret = false;
    for (auto it = m_secrets.cbegin(); it != m_secrets.cend(); ++it) {
        const Secrets &s = it.value();
        const QString pw = Keyring::credentialAccount(it.key(), false);
        const QString pp = Keyring::credentialAccount(it.key(), true);
        if (!s.password.isEmpty()) {
            ops.append({pw, s.password, false});
            anySecret = true;
        } else if (s.clearPassword) {
            ops.append({pw, QString(), true});
        }
        if (!s.passphrase.isEmpty()) {
            ops.append({pp, s.passphrase, false});
            anySecret = true;
        } else if (s.clearPassphrase) {
            ops.append({pp, QString(), true});
        }
    }
    for (const QString &id : m_deleted) {
        ops.append({Keyring::credentialAccount(id, false), QString(), true});
        ops.append({Keyring::credentialAccount(id, true), QString(), true});
    }
    if (anySecret)
        Keyring::setEnabled(true);
    m_save->setEnabled(false);
    runOps(ops, 0);
}

// Secrets first: if the keyring refuses, nothing else has changed and the
// dialog stays open with what was typed.
void CredentialsDialog::runOps(const QList<KeyringOp> &ops, int index)
{
    if (index >= ops.size()) {
        commit();
        return;
    }
    QPointer<CredentialsDialog> self(this);
    const KeyringOp op = ops[index];
    if (op.remove) {
        // Removing what was never stored is not a failure.
        Keyring::remove(op.account, [self, ops, index](bool) {
            if (self)
                self->runOps(ops, index + 1);
        });
        return;
    }
    Keyring::write(op.account, op.secret, [self, ops, index](bool ok, const QString &err) {
        if (!self)
            return;
        if (ok) {
            self->runOps(ops, index + 1);
            return;
        }
        self->m_save->setEnabled(true);
        error(self, tr("Credentials Manager"),
              tr("The keyring refused the password, so nothing was saved: %1").arg(err));
    });
}

void CredentialsDialog::commit()
{
    ConnectionTree &tree = m_store->tree();
    tree.setCredentials(m_creds);
    QList<ConnectionNode *> nodes;
    std::function<void(ConnectionNode *)> apply = [&](ConnectionNode *n) {
        n->credential = m_links.value(n->id);
        for (ConnectionNode *c : n->children) {
            nodes.append(c);
            apply(c);
        }
    };
    apply(tree.root());
    m_store->save();
    for (ConnectionNode *n : nodes)
        emit m_store->nodeChanged(n);
    wipeSecrets();
    accept();
}
