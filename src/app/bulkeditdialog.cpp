#include "bulkeditdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHeaderView>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include "connectionstore.h"
#include "dialogkit.h"
#include "macros.h"
#include "propertydefs.h"

using namespace DialogKit;

enum { ColApply, ColName, ColValue };

// Host and protocol identify a connection, so one value for many makes no
// sense; files, fonts and maps have their own editors in the Config panel.
static bool editable(const PropertyDef &d)
{
    if (d.isSpecial() || d.key == "host" || d.key == "protocol")
        return false;
    return d.kind == PropertyDef::Text || d.kind == PropertyDef::Int || d.kind == PropertyDef::Enum
        || d.kind == PropertyDef::Bool;
}

BulkEditDialog::BulkEditDialog(ConnectionStore *store, const QList<ConnectionNode *> &nodes,
                               QWidget *parent)
    : QDialog(parent), m_store(store), m_nodes(nodes)
{
    setWindowTitle(tr("Edit Connections"));
    resize(680, 560);
    auto *v = new QVBoxLayout(this);
    frame(v);
    v->addWidget(header(tr("Edit %n Connection(s)", nullptr, int(nodes.size())),
                        tr("Tick what to change. Everything else stays as it is.")));
    m_filter = new QLineEdit;
    m_filter->setPlaceholderText(tr("Filter settings..."));
    v->addWidget(m_filter);
    m_table = new QTableWidget(0, 3);
    m_table->setHorizontalHeaderLabels({QString(), tr("Setting"), tr("New value")});
    m_table->horizontalHeader()->setSectionResizeMode(ColName, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(ColValue, QHeaderView::Stretch);
    m_table->setColumnWidth(ColApply, 34);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    v->addWidget(m_table, 1);

    auto addRow = [this](const QString &key, const QString &name, QWidget *editor) {
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        auto *apply = new QCheckBox;
        auto *item = new QTableWidgetItem(name);
        item->setData(Qt::UserRole, key);
        item->setFlags(Qt::ItemIsEnabled);
        m_table->setCellWidget(row, ColApply, apply);
        m_table->setItem(row, ColName, item);
        m_table->setCellWidget(row, ColValue, editor);
        // Changing a value is what ticking is for.
        if (auto *c = qobject_cast<QComboBox *>(editor))
            connect(c, &QComboBox::activated, apply, [apply] { apply->setChecked(true); });
        else if (auto *e = qobject_cast<QLineEdit *>(editor))
            connect(e, &QLineEdit::textEdited, apply, [apply] { apply->setChecked(true); });
        else if (auto *s = qobject_cast<QSpinBox *>(editor))
            connect(s, &QSpinBox::valueChanged, apply, [apply] { apply->setChecked(true); });
        else if (auto *b = qobject_cast<QCheckBox *>(editor))
            connect(b, &QCheckBox::clicked, apply, [apply] { apply->setChecked(true); });
    };

    auto *cred = new QComboBox;
    cred->addItem(tr("(none)"), QString());
    for (const Credential &c : store->tree().credentials())
        cred->addItem(c.name, c.id);
    addRow("@credential", tr("Connection: Credential"), cred);
    auto *bar = new QComboBox;
    bar->addItem(tr("(as chosen, or the folder's)"), QString());
    for (const QString &n : ButtonBar::barNames())
        if (!n.isEmpty())
            bar->addItem(n, n);
    addRow("@buttonbar", tr("Connection: Button bar"), bar);
    for (const PropertyDef &d : PropertyDefs::curated()) {
        if (!editable(d))
            continue;
        QWidget *editor;
        if (d.kind == PropertyDef::Bool) {
            editor = new QCheckBox;
        } else if (d.kind == PropertyDef::Int) {
            auto *s = new QSpinBox;
            s->setRange(d.min == 0 ? 0 : d.min, d.max);
            editor = s;
        } else if (d.kind == PropertyDef::Enum) {
            auto *c = new QComboBox;
            for (const auto &e : d.enumValues)
                c->addItem(e.second, e.first);
            editor = c;
        } else {
            editor = new QLineEdit;
        }
        addRow(d.key, d.category + ": " + d.name, editor);
    }
    connect(m_filter, &QLineEdit::textChanged, this, [this](const QString &text) {
        for (int r = 0; r < m_table->rowCount(); r++)
            m_table->setRowHidden(r, !m_table->item(r, ColName)->text().contains(text, Qt::CaseInsensitive));
    });

    QPushButton *cancel = secondaryButton(tr("Cancel"));
    QPushButton *apply = primaryButton(tr("Apply"));
    v->addWidget(footer({cancel, apply}));
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(apply, &QPushButton::clicked, this, [this] {
        const QList<Change> list = changes();
        if (list.isEmpty()) {
            reject();
            return;
        }
        QStringList errors;
        BulkEditDialog::apply(m_store, m_nodes, list, &errors);
        if (!errors.isEmpty())
            warning(this, tr("Edit Connections"), errors.join('\n'));
        accept();
    });
}

QList<BulkEditDialog::Change> BulkEditDialog::changes() const
{
    QList<Change> out;
    for (int r = 0; r < m_table->rowCount(); r++) {
        if (!qobject_cast<QCheckBox *>(m_table->cellWidget(r, ColApply))->isChecked())
            continue;
        QVariant value;
        QWidget *w = m_table->cellWidget(r, ColValue);
        if (auto *c = qobject_cast<QComboBox *>(w))
            value = c->currentData();
        else if (auto *e = qobject_cast<QLineEdit *>(w))
            value = e->text();
        else if (auto *s = qobject_cast<QSpinBox *>(w))
            value = s->value();
        else if (auto *b = qobject_cast<QCheckBox *>(w))
            value = b->isChecked();
        out.append({m_table->item(r, ColName)->data(Qt::UserRole).toString(), value});
    }
    return out;
}

int BulkEditDialog::apply(ConnectionStore *store, const QList<ConnectionNode *> &nodes,
                          const QList<Change> &changes, QStringList *errors)
{
    int done = 0;
    for (ConnectionNode *n : nodes) {
        bool confChanged = false;
        sshlx_conf *c = store->effectiveConf(n);
        for (const Change &ch : changes) {
            if (ch.key == "@credential") {
                n->credential = ch.value.toString();
            } else if (ch.key == "@buttonbar") {
                n->buttonBar = ch.value.toString();
            } else {
                const int key = sshlx_conf_key_find(ch.key.toUtf8().constData());
                if (key < 0)
                    continue;
                switch (sshlx_conf_key_get(size_t(key))->value_type) {
                case SSHLX_T_BOOL: sshlx_conf_set_bool(c, key, ch.value.toBool()); break;
                case SSHLX_T_INT: sshlx_conf_set_int(c, key, ch.value.toInt()); break;
                default: sshlx_conf_set_str(c, key, ch.value.toString().toUtf8().constData());
                }
                // A value inherited from a folder would override the new one.
                if (n->inherit.contains(ch.key))
                    store->setInherited(n, ch.key, false);
                confChanged = true;
            }
        }
        QString err;
        if (confChanged && !store->saveConf(n, c, &err))
            errors->append(tr("%1: %2").arg(n->name, err));
        else
            done++;
        sshlx_conf_free(c);
        store->touch(n);
    }
    return done;
}
