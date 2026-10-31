#include "macros.h"

#include "dialogkit.h"

#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

using namespace DialogKit;

QList<Macro> loadMacros()
{
    QSettings st;
    QList<Macro> macros;
    const int n = st.beginReadArray("macros");
    for (int i = 0; i < n; i++) {
        st.setArrayIndex(i);
        macros.append({st.value("name").toString(), st.value("text").toString()});
    }
    st.endArray();
    return macros;
}

void saveMacros(const QList<Macro> &macros)
{
    QSettings st;
    st.remove("macros");
    st.beginWriteArray("macros", macros.size());
    for (int i = 0; i < macros.size(); i++) {
        st.setArrayIndex(i);
        st.setValue("name", macros[i].name);
        st.setValue("text", macros[i].text);
    }
    st.endArray();
}

MacroDialog::MacroDialog(QWidget *parent) : QDialog(parent), m_macros(loadMacros())
{
    setWindowTitle(tr("Macros"));
    resize(720, 480);
    auto *v = new QVBoxLayout(this);
    frame(v);
    v->addWidget(header(tr("Macros"), tr("Commands to run from the terminal menu, one per line.")));

    m_list = new QListWidget;
    m_list->setMaximumWidth(220);
    for (const Macro &m : m_macros)
        m_list->addItem(m.name);
    m_name = new QLineEdit;
    m_name->setPlaceholderText(tr("Name"));
    m_text = new QPlainTextEdit;
    m_text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_text->setLineWrapMode(QPlainTextEdit::NoWrap);
    auto *right = new QVBoxLayout;
    right->addWidget(m_name);
    right->addWidget(m_text, 1);
    auto *row = new QHBoxLayout;
    row->addWidget(m_list);
    row->addLayout(right, 1);
    v->addLayout(row, 1);

    auto *buttons = new QWidget;
    auto *bh = new QHBoxLayout(buttons);
    bh->setContentsMargins(0, 0, 0, 0);
    auto *add = secondaryButton(tr("Add"));
    auto *remove = secondaryButton(tr("Remove"));
    bh->addWidget(add);
    bh->addWidget(remove);
    QPushButton *cancel = secondaryButton(tr("Cancel"));
    QPushButton *save = primaryButton(tr("Save"));
    v->addWidget(footer({cancel, save}, buttons));

    connect(m_list, &QListWidget::currentRowChanged, this, &MacroDialog::select);
    connect(m_name, &QLineEdit::textEdited, this, [this](const QString &t) {
        if (m_row >= 0)
            m_list->item(m_row)->setText(t);
    });
    connect(add, &QPushButton::clicked, this, [this] {
        store();
        m_macros.append({tr("New Macro"), QString()});
        m_list->addItem(m_macros.last().name);
        m_list->setCurrentRow(m_macros.size() - 1);
        m_name->setFocus();
        m_name->selectAll();
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        const int row = m_row;
        if (row < 0)
            return;
        m_row = -1;     // what is in the editors must not land on a neighbour
        m_macros.removeAt(row);
        delete m_list->takeItem(row);
        select(m_list->currentRow());
    });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(save, &QPushButton::clicked, this, [this] {
        store();
        for (const Macro &m : m_macros) {
            if (m.name.trimmed().isEmpty()) {
                error(this, tr("Macros"), tr("Every macro needs a name."));
                return;
            }
        }
        saveMacros(m_macros);
        accept();
    });
    m_list->setCurrentRow(m_macros.isEmpty() ? -1 : 0);
    select(m_list->currentRow());
}

void MacroDialog::store()
{
    if (m_row >= 0 && m_row < m_macros.size())
        m_macros[m_row] = {m_name->text(), m_text->toPlainText()};
}

void MacroDialog::select(int row)
{
    store();
    m_row = row;
    const bool have = row >= 0 && row < m_macros.size();
    m_name->setEnabled(have);
    m_text->setEnabled(have);
    m_name->setText(have ? m_macros[row].name : QString());
    m_text->setPlainText(have ? m_macros[row].text : QString());
}
