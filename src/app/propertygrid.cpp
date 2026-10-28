#include "propertygrid.h"

#include <QApplication>
#include <QColorDialog>
#include <QComboBox>
#include <QFileDialog>
#include <QFontDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QTextBrowser>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

#include "connectionstore.h"
#include "fontspec.h"
#include "icons.h"
#include "keyring.h"
#include "settingsdialog.h"

enum { DefRole = Qt::UserRole + 10 };
static const char *kIconNames[] = {"", "linux", "windows", "network"};

static int key(const PropertyDef &d)
{
    return sshlx_conf_key_find(d.key.toUtf8().constData());
}

static int keyOf(const QString &id)
{
    return sshlx_conf_key_find(id.toUtf8().constData());
}

// ---------------------------------------------------------------------------
// Editor delegate: one editor per property kind.
// ---------------------------------------------------------------------------
namespace {
class GridDelegate : public QStyledItemDelegate {
public:
    GridDelegate(PropertyGrid *grid) : QStyledItemDelegate(grid), m_grid(grid) {}

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &,
                          const QModelIndex &idx) const override
    {
        const PropertyDef *d = m_grid->defAt(idx);
        if (!d || idx.column() != 1)
            return nullptr;
        switch (d->kind) {
        case PropertyDef::Int: {
            auto *sb = new QSpinBox(parent);
            sb->setRange(d->min == 0 && d->key != "ping_interval" ? -2147483647 : d->min,
                         d->max);
            return sb;
        }
        case PropertyDef::Enum: {
            auto *cb = new QComboBox(parent);
            for (const auto &e : d->enumValues)
                cb->addItem(e.second, e.first);
            return cb;
        }
        case PropertyDef::Bool:
            return nullptr;   // the cell is checkable
        case PropertyDef::Colour:
        case PropertyDef::Font:
        case PropertyDef::File: {
            // Line edit with a "..." button opening the matching dialog.
            auto *w = new QWidget(parent);
            auto *h = new QHBoxLayout(w);
            h->setContentsMargins(0, 0, 0, 0);
            h->setSpacing(0);
            auto *le = new QLineEdit;
            le->setObjectName("value");
            auto *b = new QToolButton;
            b->setText("...");
            h->addWidget(le, 1);
            h->addWidget(b);
            w->setFocusProxy(le);
            PropertyDef::Kind kind = d->kind;
            QObject::connect(b, &QToolButton::clicked, w, [w, le, kind] {
                if (kind == PropertyDef::Colour) {
                    QColor c = QColorDialog::getColor(QColor(le->text()), w);
                    if (c.isValid())
                        le->setText(c.name());
                } else if (kind == PropertyDef::Font) {
                    bool ok = false;
                    QFont f = QFontDialog::getFont(&ok, FontSpec::fromPutty(le->text()).font,
                                                   w, QString(), QFontDialog::MonospacedFonts);
                    if (ok)
                        le->setText(FontSpec::toPutty(f));
                } else {
                    QString p = QFileDialog::getOpenFileName(w, QString(), le->text());
                    if (!p.isEmpty())
                        le->setText(p);
                }
            });
            return w;
        }
        case PropertyDef::Password: {
            auto *le = new QLineEdit(parent);
            le->setEchoMode(QLineEdit::Password);
            return le;
        }
        default:
            return new QLineEdit(parent);
        }
    }

    void setEditorData(QWidget *editor, const QModelIndex &idx) const override
    {
        const PropertyDef *d = m_grid->defAt(idx);
        if (!d)
            return;
        QVariant v = m_grid->editValue(*d);
        if (auto *sb = qobject_cast<QSpinBox *>(editor))
            sb->setValue(v.toInt());
        else if (auto *cb = qobject_cast<QComboBox *>(editor))
            cb->setCurrentIndex(std::max(0, cb->findData(v.toInt())));
        else if (auto *le = qobject_cast<QLineEdit *>(editor))
            le->setText(v.toString());
        else if (auto *le2 = editor->findChild<QLineEdit *>("value"))
            le2->setText(v.toString());
    }

    void setModelData(QWidget *editor, QAbstractItemModel *, const QModelIndex &idx) const override
    {
        const PropertyDef *d = m_grid->defAt(idx);
        if (!d)
            return;
        QVariant v;
        if (auto *sb = qobject_cast<QSpinBox *>(editor))
            v = sb->value();
        else if (auto *cb = qobject_cast<QComboBox *>(editor))
            v = cb->currentData();
        else if (auto *le = qobject_cast<QLineEdit *>(editor))
            v = le->text();
        else if (auto *le2 = editor->findChild<QLineEdit *>("value"))
            v = le2->text();
        if (v.isValid() && v != m_grid->editValue(*d)) {
            // Apply after the editor has closed (applying rebuilds the rows).
            const PropertyDef *dd = d;
            QPointer<PropertyGrid> g(m_grid);
            QMetaObject::invokeMethod(m_grid, [g, dd, v] {
                if (g)
                    g->applyValue(*dd, v);
            }, Qt::QueuedConnection);
        }
    }

private:
    PropertyGrid *m_grid;
};
} // namespace

// ---------------------------------------------------------------------------
PropertyGrid::PropertyGrid(ConnectionStore *store, QWidget *parent)
    : QWidget(parent), m_store(store)
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(2);
    auto *bar = new QHBoxLayout;
    m_catButton = new QToolButton;
    m_catButton->setText(tr("Categorised"));
    m_catButton->setCheckable(true);
    m_catButton->setChecked(true);
    m_catButton->setIcon(Icons::get("properties"));
    m_catButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_alphaButton = new QToolButton;
    m_alphaButton->setText(tr("A→Z"));
    m_alphaButton->setCheckable(true);
    m_alphaButton->setIcon(Icons::get("sort-az"));
    m_alphaButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_fullButton = new QToolButton;
    m_fullButton->setText(tr("Full settings..."));
    m_fullButton->setIcon(Icons::get("settings"));
    m_fullButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_title = new QLabel;
    QFont tf = m_title->font();
    tf.setBold(true);
    m_title->setFont(tf);
    m_title->setContentsMargins(8, 6, 8, 2);
    m_title->setTextInteractionFlags(Qt::TextSelectableByMouse);
    v->addWidget(m_title);
    bar->setContentsMargins(6, 2, 6, 4);
    bar->setSpacing(4);
    bar->addWidget(m_catButton);
    bar->addWidget(m_alphaButton);
    bar->addStretch(1);
    bar->addWidget(m_fullButton);
    v->addLayout(bar);

    m_model = new QStandardItemModel(this);
    m_view = new QTreeView;
    m_view->setModel(m_model);
    m_view->setItemDelegate(new GridDelegate(this));
    m_view->setEditTriggers(QAbstractItemView::AllEditTriggers);
    m_view->setAlternatingRowColors(true);
    m_view->setUniformRowHeights(true);
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    m_view->setFrameShape(QFrame::NoFrame);
    m_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_view->setTextElideMode(Qt::ElideRight);
    v->addWidget(m_view, 1);

    m_help = new QTextBrowser;
    m_help->setMaximumHeight(96);
    m_help->setFrameShape(QFrame::NoFrame);
    m_help->document()->setDocumentMargin(8);
    m_help->setVisible(false);
    v->addWidget(m_help);

    connect(m_catButton, &QToolButton::clicked, this, [this] { setAlphabetical(false); });
    connect(m_alphaButton, &QToolButton::clicked, this, [this] { setAlphabetical(true); });
    connect(m_fullButton, &QToolButton::clicked, this, &PropertyGrid::fullSettings);
    connect(m_view->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex &cur) {
        const PropertyDef *d = defAt(cur);
        m_help->setVisible(d != nullptr);   // only takes space when useful
        if (!d) {
            m_help->clear();
            return;
        }
        QString extra;
        if (m_node && !m_node->isFolder() && !d->isSpecial()) {
            if (isInherited(*d)) {
                const ConnectionNode *src =
                    m_store->tree().inheritSource(m_node, d->inheritKey());
                extra = src ? tr("<p><i>Inherited from folder \"%1\".</i></p>").arg(src->path().toHtmlEscaped())
                            : tr("<p><i>Inherited, but no parent folder sets it: the "
                                 "connection's own value is used.</i></p>");
            }
        }
        m_help->setHtml(QStringLiteral("<b>%1</b><p>%2</p>%3")
                            .arg(d->name.toHtmlEscaped(), d->help.toHtmlEscaped(), extra));
    });
    connect(m_model, &QStandardItemModel::itemChanged, this, [this](QStandardItem *it) {
        if (m_updating)
            return;
        const PropertyDef *d = defAt(it->index());
        if (!d)
            return;
        bool on = it->checkState() == Qt::Checked;
        if (it->column() == 1 && d->kind == PropertyDef::Bool) {
            QMetaObject::invokeMethod(this, [this, d, on] { applyValue(*d, on); },
                                      Qt::QueuedConnection);
        } else if (it->column() == 2) {
            if (m_node && m_node->isFolder()) {
                if (!on)
                    QMetaObject::invokeMethod(this, [this, d] { clearFolderValue(*d); },
                                              Qt::QueuedConnection);
                else  // define it with the current (inherited/default) value
                    QMetaObject::invokeMethod(this, [this, d] { applyValue(*d, editValue(*d)); },
                                              Qt::QueuedConnection);
            } else {
                QMetaObject::invokeMethod(this, [this, d, on] { setInherited(*d, on); },
                                          Qt::QueuedConnection);
            }
        }
    });
    connect(m_view, &QTreeView::customContextMenuRequested, this, [this](const QPoint &p) {
        const PropertyDef *d = defAt(m_view->indexAt(p));
        if (!d || !m_node || d->isSpecial())
            return;
        QMenu menu;
        if (m_node->isFolder()) {
            QAction *a = menu.addAction(tr("Clear folder value"), this, [this, d] { clearFolderValue(*d); });
            a->setEnabled(folderDefines(*d));
        } else {
            QAction *a = menu.addAction(tr("Inherit from folder"));
            a->setCheckable(true);
            a->setChecked(isInherited(*d));
            connect(a, &QAction::toggled, this, [this, d](bool on) { setInherited(*d, on); });
        }
        menu.exec(m_view->viewport()->mapToGlobal(p));
    });
    connect(store, &ConnectionStore::nodeChanged, this, [this](ConnectionNode *n) {
        if (n == m_node && !m_updating)
            reload();
    });
    connect(store, &ConnectionStore::structureChanged, this, [this] {
        // The node may be gone.
        if (m_node && !m_store->tree().findById(m_node->id))
            setNode(nullptr);
        else
            reload();
    });
    setNode(nullptr);
}

PropertyGrid::~PropertyGrid()
{
    sshlx_conf_free(m_conf);
}

void PropertyGrid::setAlphabetical(bool on)
{
    m_alphabetical = on;
    m_catButton->setChecked(!on);
    m_alphaButton->setChecked(on);
    rebuild();
}

sshlx_conf *PropertyGrid::workingConf() const
{
    if (!m_node)
        return nullptr;
    if (!m_node->isFolder())
        return m_store->effectiveConf(m_node);
    // Folder view: PuTTY defaults overlaid with the values this folder and
    // its ancestors define.
    sshlx_conf *c = sshlx_conf_new_default();
    const QMap<QString, QString> vals = m_store->tree().resolveInherited(m_node);
    for (auto it = vals.begin(); it != vals.end(); ++it)
        if (int k = keyOf(it.key()); k >= 0)
            sshlx_conf_key_import(c, k, it.value().toUtf8().constData());
    return c;
}

void PropertyGrid::setNode(ConnectionNode *n)
{
    m_node = n;
    reload();
}

void PropertyGrid::reload()
{
    sshlx_conf_free(m_conf);
    m_conf = workingConf();
    rebuild();
}

const PropertyDef *PropertyGrid::defAt(const QModelIndex &idx) const
{
    if (!idx.isValid())
        return nullptr;
    QModelIndex first = idx.sibling(idx.row(), 0);
    QVariant v = m_model->data(first, DefRole);
    if (!v.isValid())
        return nullptr;
    int i = v.toInt();
    return (i >= 0 && i < m_rowDefs.size()) ? m_rowDefs[i] : nullptr;
}

bool PropertyGrid::isInherited(const PropertyDef &d) const
{
    return m_node && !m_node->isFolder() && !d.isSpecial() &&
        m_node->inherit.contains(d.inheritKey());
}

bool PropertyGrid::folderDefines(const PropertyDef &d) const
{
    return m_node && m_node->isFolder() && m_node->properties.contains(d.inheritKey());
}

QVariant PropertyGrid::editValue(const PropertyDef &d) const
{
    if (!m_node)
        return {};
    if (d.key == "@name") return m_node->name;
    if (d.key == "@description") return m_node->description;
    if (d.key == "@panel") return m_node->panel;
    if (d.key == "@favourite") return m_node->favourite;
    if (d.key == "@password") return QString();   // never read back into the UI
    if (d.key == "@icon") {
        for (int i = 0; i < 4; i++)
            if (m_node->icon == kIconNames[i])
                return i;
        return 0;
    }
    if (!m_conf)
        return {};
    if (d.key == "@sshtunnel") {
        // Shown when the proxy is "SSH to proxy and use port forwarding".
        if (sshlx_conf_get_int(m_conf, keyOf("proxy_type")) != 6)
            return QString();
        QString host = QString::fromUtf8(sshlx_conf_get_str(m_conf, keyOf("proxy_host")));
        QString user = QString::fromUtf8(sshlx_conf_get_str(m_conf, keyOf("proxy_username")));
        int port = sshlx_conf_get_int(m_conf, keyOf("proxy_port"));
        QString s = user.isEmpty() ? host : user + '@' + host;
        if (port && port != 22)
            s += ':' + QString::number(port);
        return s;
    }
    int k = key(d);
    const sshlx_conf_key_info *ki = sshlx_conf_key_get(k);
    switch (d.kind) {
    case PropertyDef::Bool:
        return sshlx_conf_get_bool(m_conf, k);
    case PropertyDef::Int:
        return sshlx_conf_get_int(m_conf, k);
    case PropertyDef::Enum:
        return ki->value_type == SSHLX_T_BOOL ? int(sshlx_conf_get_bool(m_conf, k))
                                              : sshlx_conf_get_int(m_conf, k);
    case PropertyDef::Colour: {
        int base = d.colourIndex * 3;
        QColor c(sshlx_conf_get_int_int(m_conf, k, base, 0),
                 sshlx_conf_get_int_int(m_conf, k, base + 1, 0),
                 sshlx_conf_get_int_int(m_conf, k, base + 2, 0));
        return c.name();
    }
    case PropertyDef::KeyedMap: {
        sshlx_strlist *subs = sshlx_conf_str_subkeys(m_conf, k);
        QStringList parts;
        for (size_t i = 0; i < subs->n; i++)
            parts << QString::fromUtf8(subs->items[i]) + '=' +
                     QString::fromUtf8(sshlx_conf_get_str_str(m_conf, k, subs->items[i]));
        sshlx_strlist_free(subs);
        return parts.join("; ");
    }
    default:
        return QString::fromUtf8(sshlx_conf_get_str(m_conf, k));
    }
}

QString PropertyGrid::displayValue(const PropertyDef &d) const
{
    QVariant v = editValue(d);
    switch (d.kind) {
    case PropertyDef::Enum:
        for (const auto &e : d.enumValues)
            if (e.first == v.toInt())
                return e.second;
        return v.toString();
    case PropertyDef::Bool:
        return {};
    case PropertyDef::Password:
        return v.toString().isEmpty() ? QString() : QStringLiteral("••••••");
    default:
        return v.toString();
    }
}

void PropertyGrid::rebuild()
{
    m_updating = true;
    QString currentName;
    if (const PropertyDef *d = defAt(m_view->currentIndex()))
        currentName = d->category + '/' + d->name;
    m_model->clear();
    m_rowDefs.clear();
    const bool folder = m_node && m_node->isFolder();
    m_model->setHorizontalHeaderLabels({tr("Property"), tr("Value"),
                                        folder ? tr("Set here") : tr("Inherit")});
    m_title->setText(m_node ? (folder ? tr("Folder: %1") : tr("Connection: %1")).arg(m_node->name)
                            : tr("Select a connection or folder"));
    m_title->setEnabled(m_node != nullptr);   // muted placeholder when empty
    m_fullButton->setEnabled(m_node != nullptr);
    if (!m_node) {
        m_updating = false;
        return;
    }

    QHash<QString, QStandardItem *> cats;
    QList<const PropertyDef *> defs;
    for (const PropertyDef &d : PropertyDefs::all()) {
        // Folders have no name-as-session semantics but keep Name/Description/Icon.
        if (folder && (d.key == "@panel" || d.key == "@favourite" || d.key == "@sshtunnel" ||
                       d.key == "@password"))
            continue;
        if (d.key == "@password" && !Keyring::enabled())
            continue;
        defs << &d;
    }
    if (m_alphabetical)
        std::stable_sort(defs.begin(), defs.end(), [](const PropertyDef *a, const PropertyDef *b) {
            return QString::localeAwareCompare(a->name, b->name) < 0;
        });

    QModelIndex restore;
    for (const PropertyDef *d : defs) {
        QStandardItem *parent = m_model->invisibleRootItem();
        if (!m_alphabetical) {
            QStandardItem *&c = cats[d->category];
            if (!c) {
                c = new QStandardItem(d->category);
                QFont f = c->font();
                f.setBold(true);
                c->setFont(f);
                c->setEditable(false);
                auto *c1 = new QStandardItem;
                c1->setEditable(false);
                auto *c2 = new QStandardItem;
                c2->setEditable(false);
                m_model->appendRow({c, c1, c2});
            }
            parent = c;
        }
        int idx = m_rowDefs.size();
        m_rowDefs << d;
        auto *nameItem = new QStandardItem(d->name);
        nameItem->setEditable(false);
        nameItem->setData(idx, DefRole);
        nameItem->setToolTip(d->help);
        auto *valItem = new QStandardItem;
        valItem->setData(idx, DefRole);
        auto *inhItem = new QStandardItem;
        inhItem->setEditable(false);
        if (!d->isSpecial()) {
            inhItem->setCheckable(true);
            inhItem->setCheckState(folder ? (folderDefines(*d) ? Qt::Checked : Qt::Unchecked)
                                          : (isInherited(*d) ? Qt::Checked : Qt::Unchecked));
        }
        parent->appendRow({nameItem, valItem, inhItem});
        if (d->category + '/' + d->name == currentName)
            restore = nameItem->index();
    }
    m_updating = false;
    refreshValues();
    if (!m_alphabetical) {
        m_view->expandAll();
        // The long advanced list starts collapsed.
        if (QStandardItem *adv = cats.value(PropertyDefs::categoryOrder().last()))
            m_view->collapse(adv->index());
    }
    // Property gets ~45% (resizable), Value takes the rest, Inherit stays
    // compact; no horizontal scrolling.
    QHeaderView *h = m_view->header();
    h->setStretchLastSection(false);
    h->setSectionResizeMode(0, QHeaderView::Interactive);
    h->setSectionResizeMode(1, QHeaderView::Stretch);
    h->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    h->resizeSection(0, std::max(120, m_view->viewport()->width() * 45 / 100));
    if (restore.isValid())
        m_view->setCurrentIndex(restore);
}

void PropertyGrid::refreshValues()
{
    m_updating = true;
    std::function<void(QStandardItem *)> walk = [&](QStandardItem *parent) {
        for (int r = 0; r < parent->rowCount(); r++) {
            QStandardItem *name = parent->child(r, 0);
            QStandardItem *val = parent->child(r, 1);
            if (!name->data(DefRole).isValid()) {
                walk(name);
                continue;
            }
            const PropertyDef *d = m_rowDefs[name->data(DefRole).toInt()];
            bool inherited = isInherited(*d);
            bool setHere = folderDefines(*d);
            if (d->kind == PropertyDef::Bool) {
                val->setCheckable(true);
                val->setEditable(false);
                val->setCheckState(editValue(*d).toBool() ? Qt::Checked : Qt::Unchecked);
                val->setText(QString());
            } else {
                val->setText(displayValue(*d));
            }
            if (d->kind == PropertyDef::Colour) {
                QPixmap px(12, 12);
                px.fill(QColor(editValue(*d).toString()));
                val->setIcon(QIcon(px));
            }
            QFont f = val->font();
            f.setItalic(inherited);
            f.setBold(setHere);
            val->setFont(f);
            val->setForeground(inherited ? QApplication::palette().brush(QPalette::Disabled,
                                                                        QPalette::Text)
                                         : QApplication::palette().brush(QPalette::Text));
            if (inherited) {
                const ConnectionNode *src = m_store->tree().inheritSource(m_node, d->inheritKey());
                val->setToolTip(src ? tr("Inherited from \"%1\"").arg(src->path())
                                    : tr("Inherited (no folder sets it)"));
            } else {
                val->setToolTip(QString());
            }
            if (QStandardItem *inh = parent->child(r, 2); inh && inh->isCheckable())
                inh->setCheckState(m_node->isFolder() ? (setHere ? Qt::Checked : Qt::Unchecked)
                                                      : (inherited ? Qt::Checked : Qt::Unchecked));
        }
    };
    walk(m_model->invisibleRootItem());
    m_updating = false;
}

void PropertyGrid::applyValue(const PropertyDef &d, const QVariant &v)
{
    if (!m_node)
        return;
    m_updating = true;
    // ---- sshLX metadata ----
    if (d.key == "@password") {
        const QString account = Keyring::accountFor(m_node->id);
        QPointer<QTextBrowser> help(m_help);
        if (v.toString().isEmpty()) {
            Keyring::remove(account, [help](bool) {
                if (help) help->setHtml(tr("Password removed from the keyring."));
            });
        } else {
            Keyring::write(account, v.toString(), [help](bool ok, const QString &err) {
                if (help)
                    help->setHtml(ok ? tr("Password stored in the system keyring.")
                                     : "<font color=\"#c00000\">" +
                                       tr("Keyring error: %1").arg(err.toHtmlEscaped()) + "</font>");
            });
        }
        m_updating = false;
        return;
    }
    if (d.isSpecial() && d.key != "@sshtunnel") {
        if (d.key == "@name") {
            QString err;
            if (!m_store->rename(m_node, v.toString(), &err) && !err.isEmpty())
                m_help->setHtml("<font color=\"#c00000\">" + err.toHtmlEscaped() + "</font>");
        } else {
            if (d.key == "@description") m_node->description = v.toString();
            else if (d.key == "@panel") m_node->panel = v.toString();
            else if (d.key == "@favourite") m_node->favourite = v.toBool();
            else if (d.key == "@icon") m_node->icon = kIconNames[qBound(0, v.toInt(), 3)];
            m_store->touch(m_node);
        }
        m_updating = false;
        rebuild();
        return;
    }

    // ---- Conf keys: change the working conf ----
    QStringList changedKeys;
    if (d.key == "@sshtunnel") {
        QString s = v.toString().trimmed();
        int ptype = keyOf("proxy_type");
        if (s.isEmpty()) {
            if (sshlx_conf_get_int(m_conf, ptype) == 6)
                sshlx_conf_set_int(m_conf, ptype, 0);
        } else {
            QString user, host = s;
            int port = 22;
            if (int at = host.lastIndexOf('@'); at >= 0) {
                user = host.left(at);
                host = host.mid(at + 1);
            }
            if (int colon = host.lastIndexOf(':'); colon >= 0 && !host.contains(']')) {
                port = host.mid(colon + 1).toInt();
                host = host.left(colon);
            }
            sshlx_conf_set_int(m_conf, ptype, 6);
            sshlx_conf_set_str(m_conf, keyOf("proxy_host"), host.toUtf8().constData());
            sshlx_conf_set_int(m_conf, keyOf("proxy_port"), port);
            sshlx_conf_set_str(m_conf, keyOf("proxy_username"), user.toUtf8().constData());
        }
        changedKeys << "proxy_type" << "proxy_host" << "proxy_port" << "proxy_username";
    } else {
        int k = key(d);
        const sshlx_conf_key_info *ki = sshlx_conf_key_get(k);
        switch (d.kind) {
        case PropertyDef::Bool:
            sshlx_conf_set_bool(m_conf, k, v.toBool());
            break;
        case PropertyDef::Int:
            sshlx_conf_set_int(m_conf, k, v.toInt());
            break;
        case PropertyDef::Enum:
            if (ki->value_type == SSHLX_T_BOOL) {
                sshlx_conf_set_bool(m_conf, k, v.toInt() != 0);
            } else {
                if (d.key == "protocol") {
                    // As PuTTY's dialog: switching protocol moves the port to
                    // the new protocol's default if it was on the old default.
                    int oldProto = sshlx_conf_get_int(m_conf, k);
                    int portKey = keyOf("port");
                    int oldDef = -1, newDef = -1;
                    for (size_t i = 0; i < sshlx_protocol_count(); i++) {
                        const sshlx_protocol *p = sshlx_protocol_get(i);
                        if (p->id == oldProto) oldDef = p->default_port;
                        if (p->id == v.toInt()) newDef = p->default_port;
                    }
                    if (newDef > 0 && sshlx_conf_get_int(m_conf, portKey) == oldDef) {
                        sshlx_conf_set_int(m_conf, portKey, newDef);
                        changedKeys << "port";
                    }
                }
                sshlx_conf_set_int(m_conf, k, v.toInt());
            }
            break;
        case PropertyDef::Colour: {
            QColor c(v.toString());
            if (c.isValid()) {
                int base = d.colourIndex * 3;
                sshlx_conf_set_int_int(m_conf, k, base, c.red());
                sshlx_conf_set_int_int(m_conf, k, base + 1, c.green());
                sshlx_conf_set_int_int(m_conf, k, base + 2, c.blue());
            }
            break;
        }
        case PropertyDef::KeyedMap: {
            sshlx_strlist *subs = sshlx_conf_str_subkeys(m_conf, k);
            for (size_t i = 0; i < subs->n; i++)
                sshlx_conf_del_str_str(m_conf, k, subs->items[i]);
            sshlx_strlist_free(subs);
            for (const QString &part : v.toString().split(';', Qt::SkipEmptyParts)) {
                int eq = part.indexOf('=');
                QString sk = part.left(eq).trimmed();
                QString sv = eq >= 0 ? part.mid(eq + 1).trimmed() : QString();
                if (!sk.isEmpty())
                    sshlx_conf_set_str_str(m_conf, k, sk.toUtf8().constData(),
                                           sv.toUtf8().constData());
            }
            break;
        }
        default:
            sshlx_conf_set_str(m_conf, k, v.toString().toUtf8().constData());
            break;
        }
        changedKeys << d.inheritKey();
    }

    if (m_node->isFolder()) {
        for (const QString &id : changedKeys) {
            char *exp = sshlx_conf_key_export(m_conf, keyOf(id));
            m_store->setFolderProperty(m_node, id, QString::fromUtf8(exp));
            sshlx_free(exp);
        }
    } else {
        // Editing an inherited value makes it the connection's own.
        for (const QString &id : changedKeys)
            if (m_node->inherit.contains(id))
                m_store->setInherited(m_node, id, false);
        QString err;
        if (!m_store->saveConf(m_node, m_conf, &err))
            m_help->setHtml("<font color=\"#c00000\">" + err.toHtmlEscaped() + "</font>");
    }
    m_updating = false;
    reload();
}

void PropertyGrid::setInherited(const PropertyDef &d, bool on)
{
    if (!m_node || m_node->isFolder() || d.isSpecial())
        return;
    m_updating = true;
    m_store->setInherited(m_node, d.inheritKey(), on);
    if (!on) {
        // Stop inheriting: keep the value currently in effect as the
        // connection's own, so nothing changes until it is edited.
        QString err;
        m_store->saveConf(m_node, m_conf, &err);
    }
    m_updating = false;
    reload();
}

void PropertyGrid::clearFolderValue(const PropertyDef &d)
{
    if (!m_node || !m_node->isFolder())
        return;
    m_updating = true;
    m_store->clearFolderProperty(m_node, d.inheritKey());
    m_updating = false;
    reload();
}

// PuTTY's complete dialog for the selected node. For a connection, OK saves
// to its session ("Open" also connects); for a folder, every key the user
// changed becomes a folder value.
void PropertyGrid::fullSettings()
{
    if (!m_node)
        return;
    sshlx_conf *before = sshlx_conf_copy(m_conf);
    sshlx_conf *edit = sshlx_conf_copy(m_conf);
    QString title = m_node->isFolder() ? tr("sshLX folder settings - %1").arg(m_node->name)
                                       : tr("sshLX Configuration - %1").arg(m_node->name);
    auto *dlg = new SettingsDialog(edit, false, 0, title, window(), true);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    QPushButton *save = dlg->addActionButton(tr("Save"), 2);
    save->setDefault(true);
    connect(dlg, &SettingsDialog::aboutRequested, this, &PropertyGrid::aboutRequested);
    QPointer<PropertyGrid> self(this);
    ConnectionNode *node = m_node;
    QString nodeId = node->id;
    connect(dlg, &QDialog::finished, this, [self, dlg, before, node, nodeId](int result) {
        if (self && result > 0 && self->m_store->tree().findById(nodeId) == node) {
            if (node->isFolder()) {
                for (size_t k = 0; k < sshlx_conf_key_count(); k++) {
                    if (sshlx_conf_key_equal(before, dlg->conf(), int(k)))
                        continue;
                    char *exp = sshlx_conf_key_export(dlg->conf(), int(k));
                    self->m_store->setFolderProperty(
                        node, QString::fromUtf8(sshlx_conf_key_get(k)->id), QString::fromUtf8(exp));
                    sshlx_free(exp);
                }
            } else {
                for (size_t k = 0; k < sshlx_conf_key_count(); k++) {
                    QString id = QString::fromUtf8(sshlx_conf_key_get(k)->id);
                    if (node->inherit.contains(id) &&
                        !sshlx_conf_key_equal(before, dlg->conf(), int(k)))
                        self->m_store->setInherited(node, id, false);
                }
                self->m_store->saveConf(node, dlg->conf());
                if (result == QDialog::Accepted)   // PuTTY's "Open"
                    emit self->openRequested(node);
            }
            self->reload();
        }
        sshlx_conf_free(before);
    });
    dlg->open();
}
