#include "keybindings.h"

#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSet>
#include <QSettings>
#include <QTableWidget>
#include <QVBoxLayout>

#include "dialogkit.h"
#include "macros.h"
#include "shortcuts.h"

using namespace DialogKit;

static QList<KeyBinding> &cached()
{
    static QList<KeyBinding> list = KeyBindings::load();
    return list;
}

QList<KeyBinding> KeyBindings::load()
{
    QSettings st;
    QList<KeyBinding> list;
    const int n = st.beginReadArray("keybindings");
    for (int i = 0; i < n; i++) {
        st.setArrayIndex(i);
        list.append({QKeySequence(st.value("key").toString(), QKeySequence::PortableText),
                     KeyBinding::Kind(st.value("kind").toInt()), st.value("value").toString()});
    }
    st.endArray();
    return list;
}

void KeyBindings::save(const QList<KeyBinding> &list)
{
    QSettings st;
    st.remove("keybindings");
    st.beginWriteArray("keybindings", int(list.size()));
    for (int i = 0; i < list.size(); i++) {
        st.setArrayIndex(i);
        st.setValue("key", list[i].key.toString(QKeySequence::PortableText));
        st.setValue("kind", int(list[i].kind));
        st.setValue("value", list[i].value);
    }
    st.endArray();
    cached() = list;
}

const KeyBinding *KeyBindings::find(const QKeyEvent *e)
{
    if (cached().isEmpty() || Shortcuts::instance().sendNextArmed())
        return nullptr;
    const QKeySequence seq(QKeyCombination(e->modifiers() & ~Qt::KeypadModifier, Qt::Key(e->key())));
    for (const KeyBinding &b : cached())
        if (b.key == seq)
            return &b;
    return nullptr;
}

QString KeyBindings::decodeText(const QString &text)
{
    QString out;
    for (int i = 0; i < text.size(); i++) {
        if (text[i] != '\\' || i + 1 == text.size()) {
            out += text[i];
            continue;
        }
        const QChar c = text[++i];
        if (c == 'r') out += '\r';
        else if (c == 'n') out += '\n';
        else if (c == 't') out += '\t';
        else if (c == 'e') out += QChar(0x1b);
        else if (c == '\\') out += '\\';
        else if (c == 'x' && i + 2 < text.size()) {
            bool ok = false;
            const int v = text.mid(i + 1, 2).toInt(&ok, 16);
            if (ok) {
                out += QChar(v);
                i += 2;
            } else {
                out += "\\x";
            }
        } else {
            out += '\\';
            out += c;
        }
    }
    return out;
}

enum { ColKey, ColKind, ColValue };

KeyBindingsDialog::KeyBindingsDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Key Mappings"));
    resize(720, 420);
    auto *v = new QVBoxLayout(this);
    frame(v);
    v->addWidget(header(tr("Key Mappings"), tr("Keys that send text, or run a macro or script, "
                                               "in any terminal tab")));
    m_table = new QTableWidget(0, 3);
    m_table->setHorizontalHeaderLabels({tr("Key"), tr("Action"), tr("Text, macro or script")});
    m_table->horizontalHeader()->setSectionResizeMode(ColValue, QHeaderView::Stretch);
    m_table->setColumnWidth(ColKey, 170);
    m_table->setColumnWidth(ColKind, 130);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    v->addWidget(m_table, 1);
    v->addWidget(hint(tr("In text, \\r is Enter, \\t Tab, \\e Escape and \\x03 any byte. Text is "
                         "stored in sshLX's settings file, so don't put passwords in it. A mapped "
                         "key takes priority over a shortcut of sshLX's.")));
    auto *buttons = new QWidget;
    auto *bh = new QHBoxLayout(buttons);
    bh->setContentsMargins(0, 0, 0, 0);
    auto *add = secondaryButton(tr("Add"));
    auto *remove = secondaryButton(tr("Remove"));
    auto *browse = secondaryButton(tr("Choose Script..."));
    for (QPushButton *b : {add, remove, browse})
        bh->addWidget(b);
    QPushButton *cancel = secondaryButton(tr("Cancel"));
    QPushButton *save = primaryButton(tr("Save"));
    v->addWidget(footer({cancel, save}, buttons));
    connect(add, &QPushButton::clicked, this, [this] { addRow({}); });
    connect(remove, &QPushButton::clicked, this, [this] {
        if (m_table->currentRow() >= 0)
            m_table->removeRow(m_table->currentRow());
    });
    connect(browse, &QPushButton::clicked, this, [this] {
        const int row = m_table->currentRow();
        if (row < 0)
            return;
        const QString path = QFileDialog::getOpenFileName(this, tr("Python Script"), QString(),
                                                          tr("Python scripts (*.py);;All files (*)"));
        if (path.isEmpty())
            return;
        qobject_cast<QComboBox *>(m_table->cellWidget(row, ColKind))->setCurrentIndex(KeyBinding::Script);
        qobject_cast<QComboBox *>(m_table->cellWidget(row, ColValue))->setCurrentText(path);
    });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(save, &QPushButton::clicked, this, [this] {
        const QList<KeyBinding> list = bindings();
        QSet<QString> seen;
        for (const KeyBinding &b : list) {
            const QString k = b.key.toString();
            if (k.isEmpty()) {
                error(this, tr("Key Mappings"), tr("Every row needs a key."));
                return;
            }
            if (seen.contains(k)) {
                error(this, tr("Key Mappings"), tr("%1 is used twice.").arg(k));
                return;
            }
            seen.insert(k);
        }
        KeyBindings::save(list);
        accept();
    });
    for (const KeyBinding &b : KeyBindings::load())
        addRow(b);
}

void KeyBindingsDialog::fillValues(int row)
{
    auto *kind = qobject_cast<QComboBox *>(m_table->cellWidget(row, ColKind));
    auto *value = qobject_cast<QComboBox *>(m_table->cellWidget(row, ColValue));
    const QString text = value->currentText();
    value->clear();
    if (kind->currentIndex() == KeyBinding::Macro)
        for (const Macro &m : loadMacros())
            value->addItem(m.name);
    value->setCurrentText(text);
}

void KeyBindingsDialog::addRow(const KeyBinding &b)
{
    const int row = m_table->rowCount();
    m_table->insertRow(row);
    auto *key = new QKeySequenceEdit(b.key);
    key->setMaximumSequenceLength(1);
    auto *kind = new QComboBox;
    kind->addItems({tr("Send text"), tr("Run macro"), tr("Run script")});
    kind->setCurrentIndex(b.kind);
    auto *value = new QComboBox;
    value->setEditable(true);
    value->setCurrentText(b.value);
    m_table->setCellWidget(row, ColKey, key);
    m_table->setCellWidget(row, ColKind, kind);
    m_table->setCellWidget(row, ColValue, value);
    connect(kind, &QComboBox::currentIndexChanged, this, [this, kind] {
        for (int r = 0; r < m_table->rowCount(); r++)
            if (m_table->cellWidget(r, ColKind) == kind)
                fillValues(r);
    });
    fillValues(row);
    m_table->setCurrentCell(row, ColKey);
}

QList<KeyBinding> KeyBindingsDialog::bindings() const
{
    QList<KeyBinding> list;
    for (int r = 0; r < m_table->rowCount(); r++)
        list.append({qobject_cast<QKeySequenceEdit *>(m_table->cellWidget(r, ColKey))->keySequence(),
                     KeyBinding::Kind(qobject_cast<QComboBox *>(m_table->cellWidget(r, ColKind))->currentIndex()),
                     qobject_cast<QComboBox *>(m_table->cellWidget(r, ColValue))->currentText()});
    return list;
}
